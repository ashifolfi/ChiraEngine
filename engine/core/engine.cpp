#include "engine.h"

#define IMGUI_USER_CONFIG <config/imguiConfig.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <i18n/translationManager.h>
#include <config/glVersion.h>
#include <loader/settings/jsonSettingsLoader.h>
#include <loader/image/image.h>
#include <sound/alSoundManager.h>
#include <hook/discordRPC.h>
#include <resource/provider/filesystemResourceProvider.h>
#include <resource/provider/internetResourceProvider.h>
#include <resource/shaderResource.h>
#include <loader/mesh/objMeshLoader.h>
#include <loader/mesh/primitiveMeshLoader.h>
#include <render/material/materialTypes.h>
#include <physics/bulletPhysicsProvider.h>
#include <render/ubo.h>
#include <event/events.h>
#include <entity/imgui/console/console.h>
#include <entity/imgui/profiler/profiler.h>
#include <utility/assertions.h>
#if __has_include(<windows.h>) && !defined(DEBUG)
#include <windows.h>
#endif

using namespace chira;

GLFWwindow* Engine::window = nullptr;
std::vector<std::function<void()>> Engine::initFunctions;
std::vector<std::function<void()>> Engine::renderFunctions;
std::vector<std::function<void()>> Engine::stopFunctions;
std::unique_ptr<AngelscriptProvider> Engine::angelscript = nullptr;
std::unique_ptr<AbstractSoundManager> Engine::soundManager = nullptr;
std::vector<Keybind> Engine::keybinds;
std::vector<Mousebind> Engine::mousebinds;
std::unique_ptr<AbstractSettingsLoader> Engine::settingsLoader = nullptr;
std::unique_ptr<AbstractPhysicsProvider> Engine::physicsProvider = nullptr;
Root* Engine::root = nullptr;
Console* Engine::console = nullptr;
#ifdef DEBUG
Profiler* Engine::profiler = nullptr;
#endif
bool Engine::mouseCaptured = false;
bool Engine::started = false;
bool Engine::iconified = false;
double Engine::lastTime, Engine::currentTime, Engine::lastMouseX, Engine::lastMouseY;

void Engine::framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    glViewport(0, 0, width, height);
    if (Engine::root) {
        Engine::root->getMainCamera()->createProjection(width, height);
    }
}

void Engine::keyboardCallback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    if (action == GLFW_REPEAT) return;
    for (Keybind& k : *Engine::getKeybinds()) {
        if (k.getButton() == key && k.getAction() == action) {
            k.run();
        }
    }
}

void Engine::keyboardRepeatingCallback() {
    for (Keybind& k : Engine::keybinds) {
        if (glfwGetKey(Engine::window, k.getButton()) && k.getAction() == GLFW_REPEAT) {
            k.run();
        }
    }
}

void Engine::mouseButtonCallback(GLFWwindow* w, int button, int action, int mods) {
    for (Keybind& k : *Engine::getKeybinds()) {
        if (k.getButton() == button && k.getAction() == action) {
            k.run();
        }
    }
}

void Engine::mouseButtonRepeatingCallback() {
    for (Keybind& k : Engine::keybinds) {
        if (k.isMouse() && (glfwGetMouseButton(Engine::window, k.getButton()) && k.getAction() == GLFW_REPEAT)) {
            k.run();
        }
    }
}

void Engine::mouseMovementCallback(GLFWwindow* w, double xPos, double yPos) {
    if (Engine::lastMouseX == -1) Engine::lastMouseX = xPos;
    if (Engine::lastMouseY == -1) Engine::lastMouseY = yPos;

    int width, height;
    glfwGetWindowSize(Engine::window, &width, &height);
    double xOffset = xPos - Engine::lastMouseX;
    double yOffset = yPos - Engine::lastMouseY;

    for (Mousebind& bind : *Engine::getMousebinds()) {
        if (bind.getType() == MouseActions::MOVE) {
            bind.run(xOffset, yOffset);
        }
    }

    Engine::lastMouseX = xPos;
    Engine::lastMouseY = yPos;
}

void Engine::mouseScrollCallback(GLFWwindow* w, double xPos, double yPos) {
    for (Mousebind& bind : *Engine::getMousebinds()) {
        if (bind.getType() == MouseActions::SCROLL) {
            bind.run(xPos, yPos);
        }
    }
}

void Engine::windowIconifyCallback(GLFWwindow* w, int isIconified) {
    Engine::iconified = (isIconified == GLFW_TRUE);
}

void Engine::preInit(const std::string& configPath) {
#ifdef _WIN32
    // #define CP_UTF8 65001 in windows.h
    system(("chcp " + std::to_string(65001) + " > nul").c_str());
#ifndef DEBUG
    FreeConsole();
#endif
#endif
    Resource::addResourceProvider(new FilesystemResourceProvider{ENGINE_FILESYSTEM_PATH});
    Resource::addResourceProvider(new InternetResourceProvider{"http", 80});
    Resource::addResourceProvider(new InternetResourceProvider{"https", 443});
    Engine::setSettingsLoader(new JSONSettingsLoader(configPath));
    std::string defaultLang;
    Engine::getSettingsLoader()->getValue("ui", "language", &defaultLang);
    TranslationManager::setLanguage(defaultLang);
    TranslationManager::addTranslationFile("file://i18n/engine");
    Engine::lastTime = 0;
    Engine::currentTime = 0;
    Engine::lastMouseX = -1;
    Engine::lastMouseY = -1;
}

void Engine::init() {
    Engine::started = true;

    Engine::console = new Console{};
#ifdef DEBUG
    Engine::profiler = new Profiler{};
#endif

    if (!glfwInit()) {
        Logger::log(LogType::ERROR, "GLFW", TR("error.glfw.undefined"));
        exit(EXIT_FAILURE);
    }
    glfwSetErrorCallback([](int error, const char* description) {
        Logger::log(LogType::ERROR, "GLFW", fmt::format(TR("error.glfw.generic"), error, description));
    });
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, GL_VERSION_MAJOR);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, GL_VERSION_MINOR);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
#endif

    int windowWidth = 1600;
    Engine::getSettingsLoader()->getValue("graphics", "windowWidth", &windowWidth);
    int windowHeight = 900;
    Engine::getSettingsLoader()->getValue("graphics", "windowHeight", &windowHeight);
    bool fullscreen = false;
    Engine::getSettingsLoader()->getValue("graphics", "fullscreen", &fullscreen);
    Engine::window = glfwCreateWindow(windowWidth, windowHeight, TR("ui.window.title").c_str(), nullptr, nullptr);
    if (fullscreen) {
        const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        glfwSetWindowMonitor(Engine::window, glfwGetPrimaryMonitor(), 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    if (!Engine::window) {
        Logger::log(LogType::ERROR, "GLFW", TR("error.glfw.window"));
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(Engine::window);

    if (!fullscreen) {
        bool startMaximized = true;
        Engine::getSettingsLoader()->getValue("graphics", "startMaximized", &startMaximized);
        if (startMaximized) {
            glfwMaximizeWindow(Engine::window);
        }
    }

    if (Engine::getSettingsLoader()->hasValue("engine", "iconPath")) {
        std::string path;
        Engine::getSettingsLoader()->getValue("engine", "iconPath", &path);
        Engine::setIcon(path);
    } else {
        Logger::log(LogType::WARNING, "Engine", TR("error.engine.unset_icon_path"));
    }

    int major, minor, rev;
    glfwGetVersion(&major, &minor, &rev);
    Logger::log(LogType::INFO, "GLFW", fmt::format(TR("debug.glfw.version"), major, minor, rev));

    if (!gladLoadGL(glfwGetProcAddress)) {
        Logger::log(LogType::ERROR, "OpenGL", fmt::format("error.opengl.version", GL_VERSION_STRING_PRETTY));
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

#ifdef DEBUG
    int flags;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback([](GLenum source, GLenum type, unsigned int id, GLenum severity, GLsizei length, const char* message, const void* userParam) {
            // Leaving OpenGL error reports unlocalized is probably best

            if (id == 8 || id == 131169 || id == 131185 || id == 131218 || id == 131204) {
                // Ignore 8 because the Steam overlay tries to bind to an already bound framebuffer, very low overhead, don't worry about it
                // Others are ignored because learnopengl.com said they were duplicates
                return;
            }
            std::string output = "---------------\nDebug message (" + std::to_string(id) + "): " +  message;

            output += "\nSource: ";
            switch (source) {
                case GL_DEBUG_SOURCE_API:             output += "API"; break;
                case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   output += "Window System"; break;
                case GL_DEBUG_SOURCE_SHADER_COMPILER: output += "Shader Compiler"; break;
                case GL_DEBUG_SOURCE_THIRD_PARTY:     output += "Third Party"; break;
                case GL_DEBUG_SOURCE_APPLICATION:     output += "Application"; break;
                default:                              output += "Other";
            }
            output += "\nType: ";
            switch (type) {
                case GL_DEBUG_TYPE_ERROR:               output += "Error"; break;
                case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: output += "Deprecated Behaviour"; break;
                case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  output += "Undefined Behaviour"; break;
                case GL_DEBUG_TYPE_PORTABILITY:         output += "Portability"; break;
                case GL_DEBUG_TYPE_PERFORMANCE:         output += "Performance"; break;
                case GL_DEBUG_TYPE_MARKER:              output += "Marker"; break;
                case GL_DEBUG_TYPE_PUSH_GROUP:          output += "Push Group"; break;
                case GL_DEBUG_TYPE_POP_GROUP:           output += "Pop Group"; break;
                default:                                output += "Other";
            }
            output += "\nSeverity: ";
            switch (severity) {
                case GL_DEBUG_SEVERITY_HIGH:         output += "High"; break;
                case GL_DEBUG_SEVERITY_MEDIUM:       output += "Medium"; break;
                case GL_DEBUG_SEVERITY_LOW:          output += "Low"; break;
                case GL_DEBUG_SEVERITY_NOTIFICATION: output += "Notification"; break;
                default:                             output += "Other";
            }

            if (type == GL_DEBUG_TYPE_ERROR)
                Logger::log(LogType::ERROR, "OpenGL", output);
            else if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
                Logger::log(LogType::INFO, "OpenGL", output);
            else
                // Logging as a warning because most of the time the program runs perfectly fine
                Logger::log(LogType::WARNING, "OpenGL", output);
        }, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }

    int vertexAttributes, textureUnits;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &vertexAttributes);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &textureUnits);
    Logger::log(LogType::INFO, "OpenGL", fmt::format(TR("debug.opengl.vertex_attributes"), vertexAttributes));
    Logger::log(LogType::INFO, "OpenGL", fmt::format(TR("debug.opengl.texture_units"), textureUnits));
#endif

    int width, height;
    glfwGetFramebufferSize(Engine::window, &width, &height);
    glViewport(0, 0, width, height);
    glfwSetFramebufferSizeCallback(Engine::window, Engine::framebufferSizeCallback);
    Engine::setBackgroundColor(ColorRGB{});

    MeshResource::addMeshLoader("primitive", new PrimitiveMeshLoader{});
    MeshResource::addMeshLoader("obj", new OBJMeshLoader{});

    Engine::displaySplashScreen();
    Resource::cleanup();

    glfwSwapInterval(1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS); // Wiki says modern hardware is fine with this and it looks better

    glfwSetInputMode(Engine::window, GLFW_STICKY_KEYS, GLFW_TRUE);
    glfwSetInputMode(Engine::window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
    bool rawMouseMotion = false;
    Engine::getSettingsLoader()->getValue("input", "rawMouseMotion", &rawMouseMotion);
    if (glfwRawMouseMotionSupported() && rawMouseMotion) {
        glfwSetInputMode(Engine::window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    glfwSetKeyCallback(Engine::window, Engine::keyboardCallback);
    glfwSetMouseButtonCallback(Engine::window, Engine::mouseButtonCallback);
    glfwSetCursorPosCallback(Engine::window, Engine::mouseMovementCallback);
    glfwSetScrollCallback(Engine::window, Engine::mouseScrollCallback);
    glfwSetWindowIconifyCallback(Engine::window, Engine::windowIconifyCallback);

#ifdef DEBUG
    IMGUI_CHECKVERSION();
#endif
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(Engine::window, true);
    io.Fonts->Clear();
    ImGui_ImplOpenGL3_Init(GL_VERSION_STRING.data());
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    Logger::log(LogType::INFO, "ImGui", TR("debug.imgui.success"));

    bool openalEnabled = true;
    Engine::getSettingsLoader()->getValue("audio", "openal", &openalEnabled);
    if (openalEnabled) {
        Engine::setSoundManager(new ALSoundManager{});
    }
    Engine::soundManager->init();

    // todo: move this to a general lighting manager
    if (Engine::getSettingsLoader()->hasValue("engine", "maxPointLights")) {
        int maxLights;
        Engine::getSettingsLoader()->getValue("engine", "maxPointLights", &maxLights);
        ShaderResource::addPreprocessorSymbol("MAX_POINT_LIGHTS", std::to_string(maxLights));
    }
    if (Engine::getSettingsLoader()->hasValue("engine", "maxPointLights")) {
        int maxLights;
        Engine::getSettingsLoader()->getValue("engine", "maxDirectionalLights", &maxLights);
        ShaderResource::addPreprocessorSymbol("MAX_DIRECTIONAL_LIGHTS", std::to_string(maxLights));
    }
    if (Engine::getSettingsLoader()->hasValue("engine", "maxSpotLights")) {
        int maxLights;
        Engine::getSettingsLoader()->getValue("engine", "maxSpotLights", &maxLights);
        ShaderResource::addPreprocessorSymbol("MAX_SPOT_LIGHTS", std::to_string(maxLights));
    }

    bool bulletEnabled = true;
    Engine::getSettingsLoader()->getValue("physics", "bullet", &bulletEnabled);
    if (bulletEnabled) {
        Engine::setPhysicsProvider(new BulletPhysicsProvider{});
    }

    Engine::angelscript = std::make_unique<AngelscriptProvider>();
    Engine::angelscript->initProvider();
    // Static function:
    //Engine::angelscript->registerGlobalFunction(Engine::setBackgroundColor, "setBackgroundColor");
    // Method:
    //Engine::angelscript->asEngine->RegisterGlobalFunction("void showConsole(bool)", asMETHOD(Engine, showConsole), asCALL_THISCALL_ASGLOBAL, this);

    io.Fonts->AddFontDefault();
    Engine::console->precacheResource();
    auto defaultFont = Resource::getResource<FontResource>("file://fonts/default.json");
    ImGui::GetIO().FontDefault = defaultFont->getFont();

    Engine::root = new Root{"root"};
    Engine::root->addChild(Engine::console);
#if DEBUG
    Engine::root->addChild(Engine::profiler);
#endif
    Engine::callRegisteredFunctions(&Engine::initFunctions);
    Engine::angelscript->initScripts();

    io.Fonts->Build();
}

void Engine::displaySplashScreen() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    auto mat = Resource::getResource<MaterialTextured>(TR("resource.material.splashscreen_material_path"));
    auto plane = Resource::getResource<MeshResource>("file://meshes/plane.json");
    plane->setMaterial(mat.castAssert<MaterialBase>());
    plane->render(glm::identity<glm::mat4>());
    glfwSwapBuffers(Engine::window);
}

void Engine::run() {
    Engine::lastTime = Engine::currentTime;
    Engine::currentTime = glfwGetTime();

    while (!glfwWindowShouldClose(Engine::window)) {
        Engine::physicsProvider->updatePhysics(Engine::getDeltaTime());
        Engine::render();
        Engine::soundManager->setListenerPosition(Engine::getRoot()->getAudioListeningPosition());
        Engine::soundManager->setListenerRotation(Engine::getRoot()->getAudioListeningRotation(), Engine::getRoot()->getAudioListeningUpVector());
        Engine::soundManager->update();
        glfwSwapBuffers(Engine::window);
        glfwPollEvents();
        Engine::keyboardRepeatingCallback();
        Engine::mouseButtonRepeatingCallback();
        if (DiscordRPC::initialized())
            DiscordRPC::updatePresence();
        Events::update();
        Resource::cleanup();
    }
    Engine::stop();
}

void Engine::render() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Engine::lastTime = Engine::currentTime;
    Engine::currentTime = glfwGetTime();

    UBO_PV::get()->update(Engine::getRoot()->getMainCamera()->getProjection(), Engine::getRoot()->getMainCamera()->getView());

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    Engine::callRegisteredFunctions(&(Engine::renderFunctions));
    Engine::angelscript->render(Engine::getDeltaTime());
    Engine::root->render();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Engine::stop() {
    Logger::log(LogType::INFO_IMPORTANT, "Engine", TR("debug.engine.exit"));

    Engine::callRegisteredFunctions(&(Engine::stopFunctions));
    Engine::angelscript->stop();

    if (DiscordRPC::initialized()) {
        DiscordRPC::shutdown();
    }

    Engine::soundManager->stop();
    delete Engine::root;
    Engine::physicsProvider->stop();
    Resource::discardAll();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(Engine::window);
    glfwTerminate();
    exit(EXIT_SUCCESS);
}

void Engine::addInitFunction(const std::function<void()>& init) {
    Engine::initFunctions.push_back(init);
}

void Engine::addRenderFunction(const std::function<void()>& render) {
    Engine::renderFunctions.push_back(render);
}

void Engine::addStopFunction(const std::function<void()>& stop) {
    Engine::stopFunctions.push_back(stop);
}

void Engine::setBackgroundColor(ColorRGB color) {
    glClearColor(color.r, color.g, color.b, 1.f);
}

glm::vec2 Engine::getWindowSize() {
    int w, h;
    glfwGetWindowSize(Engine::window, &w, &h);
    return {w, h};
}

int Engine::getWindowWidth() {
    int w, h;
    glfwGetWindowSize(Engine::window, &w, &h);
    return w;
}

int Engine::getWindowHeight() {
    int w, h;
    glfwGetWindowSize(Engine::window, &w, &h);
    return h;
}

void Engine::setWindowSize(int width, int height) {
    glfwSetWindowSize(Engine::window, width, height);
}

void Engine::addKeybind(const Keybind& keybind) {
    Engine::keybinds.push_back(keybind);
}

std::vector<Keybind>* Engine::getKeybinds() {
    return &(Engine::keybinds);
}

void Engine::addMousebind(const Mousebind& mousebind) {
    Engine::mousebinds.push_back(mousebind);
}

std::vector<Mousebind>* Engine::getMousebinds() {
    return &(Engine::mousebinds);
}

AngelscriptProvider* Engine::getAngelscriptProvider() {
    if (!Engine::angelscript) {
        Logger::log(LogType::ERROR, "Engine::getAngelscriptProvider", fmt::format(TR("error.engine.script_provider_missing"), "AngelScript"));
    }
    return Engine::angelscript.get();
}

void Engine::setSoundManager(AbstractSoundManager* newSoundManager) {
    Engine::soundManager.reset(newSoundManager);
}

AbstractSoundManager* Engine::getSoundManager() {
    if (!Engine::soundManager) {
        Logger::log(LogType::WARNING, "Engine::getSoundManager", fmt::format(TR("error.engine.invalid_access"), "sound manager", "Engine::setSoundManager"));
        return nullptr;
    }
    return Engine::soundManager.get();
}

AbstractSettingsLoader* Engine::getSettingsLoader() {
    if (!Engine::settingsLoader) {
        Logger::log(LogType::WARNING, "Engine::getSettingsLoader", fmt::format(TR("error.engine.invalid_access"), "settings loader", "Engine::setSettingsLoader"));
        return nullptr;
    }
    return Engine::settingsLoader.get();
}

void Engine::setSettingsLoader(AbstractSettingsLoader* newSettingsLoader) {
    Engine::settingsLoader.reset(newSettingsLoader);
    Engine::setSettingsLoaderDefaults();
}

AbstractPhysicsProvider* Engine::getPhysicsProvider() {
    if (!Engine::physicsProvider) {
        Logger::log(LogType::WARNING, "Engine::getPhysicsProvider", fmt::format(TR("error.engine.invalid_access"), "physics provider", "Engine::setPhysicsProvider"));
        return nullptr;
    }
    return Engine::physicsProvider.get();
}

void Engine::setPhysicsProvider(AbstractPhysicsProvider* newPhysicsProvider) {
    Engine::physicsProvider.reset(newPhysicsProvider);
}

Root* Engine::getRoot() {
    return Engine::root;
}

void Engine::setSettingsLoaderDefaults() {
    Engine::settingsLoader->load();
    Engine::settingsLoader->addCategory("engine");
    Engine::settingsLoader->setValue("engine", "iconPath", std::string("textures/ui/icon.png"), false, false);
    Engine::settingsLoader->setValue("engine", "consoleColoredText", true, false, false);
    Engine::settingsLoader->setValue("engine", "maxPointLights", 64, false, false);
    Engine::settingsLoader->setValue("engine", "maxDirectionalLights", 4, false, false);
    Engine::settingsLoader->setValue("engine", "maxSpotLights", 4, false, false);
    Engine::settingsLoader->addCategory("audio");
    Engine::settingsLoader->setValue("audio", "openal", true, false, false);
    Engine::settingsLoader->addCategory("physics");
    Engine::settingsLoader->setValue("physics", "bullet", true, false, false);
    Engine::getSettingsLoader()->setValue("physics", "subStep", 4, false, false);
    Engine::settingsLoader->addCategory("graphics");
    Engine::settingsLoader->setValue("graphics", "windowWidth", 1600, false, false);
    Engine::settingsLoader->setValue("graphics", "windowHeight", 900, false, false);
    Engine::settingsLoader->setValue("graphics", "startMaximized", false, false, false);
    Engine::settingsLoader->setValue("graphics", "fullscreen", false, false, false);
    Engine::settingsLoader->addCategory("input");
    Engine::settingsLoader->setValue("input", "rawMouseMotion", true, false, false);
    Engine::settingsLoader->setValue("input", "invertYAxis", false, false, false);
    Engine::settingsLoader->addCategory("ui");
    // todo: use computer language as default
    Engine::settingsLoader->setValue("ui", "language", std::string("en"), false, false);
    Engine::settingsLoader->save();
}

void Engine::callRegisteredFunctions(const std::vector<std::function<void()>>* list) {
    for (const auto& func : *list) {
        func();
    }
}

GLFWwindow* Engine::getWindow() {
    return Engine::window;
}

bool Engine::isStarted() {
    return Engine::started;
}

double Engine::getDeltaTime() {
    return Engine::currentTime - Engine::lastTime;
}

void Engine::setIcon(const std::string& iconPath) {
    // todo(i18n)
    chira_assert(Engine::isStarted(), "Engine is not started: have you called Engine::preInit() and Engine::init()?");
    GLFWimage images[1];
    int width, height, bitsPerPixel;
    Image icon(
            assert_cast<FilesystemResourceProvider*>(Resource::getResourceProviderWithResource("file://" + iconPath))->getPath() + "/" + iconPath,
            &width, &height, &bitsPerPixel, 4, false);
    // todo(i18n)
    chira_assert(icon.getData(), "Icon has no data");
    images[0].width = width;
    images[0].height = height;
    images[0].pixels = icon.getData();
    glfwSetWindowIcon(Engine::window, 1, images);
}

void Engine::captureMouse(bool capture) {
    Engine::mouseCaptured = capture;
    if (capture) {
        glfwSetInputMode(Engine::window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        glfwSetInputMode(Engine::window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
}

bool Engine::isMouseCaptured() {
    return Engine::mouseCaptured;
}

Console* Engine::getConsole() {
    return Engine::console;
}

Profiler* Engine::getProfiler() {
#if DEBUG
    return Engine::profiler;
#else
    logger::log(ERR, "Engine::getProfiler", "Profiler window is not present in release build!");
    return nullptr;
#endif
}

bool Engine::isIconified() {
    return Engine::iconified;
}
