#include "DeviceDX11.h"
#include "../DXShared.h"

#include <vector>

#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <config/Config.h>
#include <config/ConEntry.h>
#include <i18n/TranslationManager.h>
#include <input/InputManager.h>
#include <loader/image/Image.h>
#include <resource/provider/FilesystemResourceProvider.h>
#include <render/material/MaterialFrameBuffer.h>
#include <render/material/MaterialTextured.h>
#include <render/mesh/MeshDataBuilder.h>
#include <ui/Font.h>
#include <ui/IPanel.h>
#include <SDL_syswm.h>

#include <dxgi.h>

using namespace chira;

CHIRA_CREATE_LOG(WINDOW);

static int g_d3dSwapInterval = 0;

static void setVSync(bool enable) {
	// TODO: adaptive vsync?
	if (!enable) {
		g_d3dSwapInterval = 0;
	} else {
		g_d3dSwapInterval = 1;
	}
}

ConVar win_vsync{"win_vsync", true, "Limit the FPS to your monitor's resolution.", CON_FLAG_CACHE, [](ConVar::CallbackArg newValue) {
    setVSync(static_cast<bool>(std::stoi(newValue.data())));
}};

[[maybe_unused]]
ConVar input_raw_mouse_motion{"input_raw_mouse_motion", true, "Get more accurate mouse motion.", CON_FLAG_CACHE};

static void setImGuiConfigPath() {
    static std::string configPath = Config::getConfigFile("imgui.ini");
    ImGui::GetIO().IniFilename = configPath.c_str();
}

Renderer::FrameBufferHandle g_WindowFramebufferHandle{};
SDL_Window* g_Splashscreen = nullptr;
// splashscreen stuff. we use SDL to render the splashscreen in DX (no reason not to lmao)
SDL_Renderer* g_SplashRenderer = nullptr;

bool Device::initBackendAndCreateSplashscreen(bool splashScreenVisible) {
    static bool alreadyRan = false;
    if (alreadyRan)
        return false;
    alreadyRan = true;

    SDL_SetMainReady();

#ifdef CHIRA_PLATFORM_WINDOWS
    // Force enable DPI awareness because the manifest method didn't work
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_DPI_SCALING, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2", SDL_HINT_OVERRIDE);
#endif

    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER)) {
        LOG_WINDOW.error("SDL2 failed to initialize: {}", SDL_GetError());
        return false;
    }

    int windowFlags =
            SDL_WINDOW_ALLOW_HIGHDPI |
            SDL_WINDOW_ALWAYS_ON_TOP |
            SDL_WINDOW_BORDERLESS |
            (splashScreenVisible ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
    int width = 640, height = 480;

    g_Splashscreen = SDL_CreateWindow("Loading...", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, windowFlags);
    if (!g_Splashscreen) {
        LOG_WINDOW.error("Splashscreen window creation failed! Error: {}", SDL_GetError());
        return false;
    }
	
	// setup the renderer and display the splash texture
    g_SplashRenderer = SDL_CreateRenderer(g_Splashscreen, -1, 0);

    SDL_SetRenderDrawColor(g_SplashRenderer, 255, 255, 255, 255);
    SDL_RenderClear(g_SplashRenderer);

    int splashWidth, splashHeight, bitsPerPixel;
    SDL_Surface* sdlSplash{};
    SDL_Texture* splashTex{};
    auto* splash = Image::getUncompressedImage(FilesystemResourceProvider::getResourceAbsolutePath("file://textures/ui/splashscreen.png"), &splashWidth, &splashHeight, &bitsPerPixel, 4, false);
    if (splash) {
        sdlSplash = SDL_CreateRGBSurfaceWithFormatFrom(splash, splashWidth, splashHeight, bitsPerPixel, 8, SDL_PIXELFORMAT_RGBA8888);
        splashTex = SDL_CreateTextureFromSurface(g_SplashRenderer, sdlSplash);
        if (SDL_RenderCopy(g_SplashRenderer, splashTex, NULL, NULL) != 0) {
            LOG_WINDOW.error("Failed to display splash screen texture! Error: {}", SDL_GetError());
        }
    }
    SDL_RenderPresent(g_SplashRenderer);

    SDL_DestroyTexture(splashTex);
    SDL_FreeSurface(sdlSplash);
    Image::deleteUncompressedImage(splash);

	// set the vsync value
	setVSync(win_vsync.getValue<bool>());

	// Create D3D11 device
	UINT createDeviceFlags = 0;
#ifdef DEBUG // Enable the debug layer on a debug build (of course)
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
	
	HRESULT hr = D3D11CreateDevice(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
		featureLevelArray, 2, D3D11_SDK_VERSION,
		&g_d3dDevice, &featureLevel, &g_d3dDeviceContext);
	
	if (hr != S_OK) {
		// yeah this kinda sucks but we can't just call DX_GetError
		std::string error;
		switch(hr) {
			case DXGI_ERROR_SDK_COMPONENT_MISSING:
				error = "An SDK component is missing or mismatched. Usually this is the debug dll.";
				break;
			case E_INVALIDARG: // if you somehow get here...
				error = "pAdapter is not null... Why are you initing in software mode?";
				break;
			default:
				error = "unknown error";
				break;
		}
		LOG_WINDOW.error("D3DDevice creation failed! Error: {}", error);
        
#ifndef DEBUG
		// make this visible to the user when it happens outside of debug
		// if you're in debug you're probably (should be) monitoring the log
		Device::popupError(TRC("error.directx.device_init_failed"));
#endif
		return false;
	}

    // HACK: change rasterizer to cull front faces by default
    ID3D11RasterizerState* rasterizerState;
    dxCreate(D3D11_RASTERIZER_DESC, rasterizerDesc)
    g_d3dDeviceContext->RSGetState(&rasterizerState);
    rasterizerState->GetDesc(&rasterizerDesc); // build off our default state
    rasterizerDesc.CullMode = D3D11_CULL_FRONT;
    g_d3dDevice->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
    g_d3dDeviceContext->RSSetState(rasterizerState);

    return true;
}

void Device::destroySplashscreen() {
    if (g_Splashscreen) {
        SDL_DestroyWindow(g_Splashscreen);
        SDL_DestroyRenderer(g_SplashRenderer);
        g_Splashscreen = nullptr;
        g_SplashRenderer = nullptr;
    }
}

void Device::destroyBackend() {
    Renderer::destroyImGui();
    Device::destroyAllWindows();
	dxRelease(g_d3dDevice)
	dxRelease(g_d3dDeviceContext)
    SDL_Quit();
}

std::uint64_t Device::getTicks() {
    return SDL_GetTicks64();
}

std::array<Device::WindowHandle, 256> g_Windows{};

[[nodiscard]] static int findFreeWindow() {
    for (unsigned int i = 0; i < g_Windows.size(); i++) {
        if (!g_Windows.at(i))
            return static_cast<int>(i);
    }
    return -1;
}

Device::WindowHandle* Device::createWindow(int width, int height, std::string_view title, Viewport* viewport) {
    int freeWindow = findFreeWindow();
    if (freeWindow == -1)
        return nullptr;

    g_Windows[freeWindow] = WindowHandle{};
    WindowHandle& handle = g_Windows.at(freeWindow);

    handle.width = width;
    handle.height = height;

    int windowFlags =
            SDL_WINDOW_ALLOW_HIGHDPI |
            SDL_WINDOW_RESIZABLE |
            SDL_WINDOW_SHOWN;
    handle.window = SDL_CreateWindow(title.data(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, windowFlags);
    if (!handle.window) {
        LOG_WINDOW.error("Window creation failed! Error: {}", SDL_GetError());
        return nullptr;
    }
    SDL_SetWindowData(handle.window, "handle", &handle);

	// Get window information for d3d11
	SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(handle.window, &wmInfo);
    HWND hwnd = (HWND)wmInfo.info.win.window;

	// create device resources
	// Setup swap chain
	dxCreate(DXGI_SWAP_CHAIN_DESC, sd)
    sd.BufferCount = 2;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    // oh jesus
    HRESULT hr;
    IDXGIDevice* pDXGIDevice;
    hr = g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
    IDXGIAdapter* pDXGIAdapter;
    hr = pDXGIDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&pDXGIAdapter);
    IDXGIFactory* pIDXGIFactory;
    pDXGIAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&pIDXGIFactory);

	if (pIDXGIFactory->CreateSwapChain(g_d3dDevice, &sd, &handle.d3dSwapChain) != S_OK) {
		LOG_WINDOW.error("Window creation failed! Error: Failed to create Swap Chain");
        return nullptr;
	}

    ID3D11Texture2D* pBackBuffer;
    handle.d3dSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (pBackBuffer) {
        g_d3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &handle.renderTargetView);
        pBackBuffer->Release();
    } else {
        LOG_WINDOW.error("Failed to get back buffer!");
        return nullptr;
    }

    if (viewport) {
        handle.viewport = viewport;
        handle.viewportIsSelfOwned = false;
    } else {
        handle.viewport = new Viewport{{width, height}};
        handle.viewportIsSelfOwned = true;
    }

    int iconWidth, iconHeight, bitsPerPixel;
    auto* icon = Image::getUncompressedImage(FilesystemResourceProvider::getResourceAbsolutePath("file://textures/ui/icon.png"), &iconWidth, &iconHeight, &bitsPerPixel, 4, false);
    if (icon) {
        auto* sdlIcon = SDL_CreateRGBSurfaceWithFormatFrom(icon, iconWidth, iconHeight, bitsPerPixel, 8, SDL_PIXELFORMAT_RGBA8888);
        SDL_SetWindowIcon(handle.window, sdlIcon);
        SDL_FreeSurface(sdlIcon);
        Image::deleteUncompressedImage(icon);
    }

    handle.imguiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(handle.imguiContext);
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
    setImGuiConfigPath();

    Renderer::initImGui(handle.window);

    static bool bakedFonts = false;
    if (!bakedFonts) {
        bakedFonts = true;

        auto defaultFont = Resource::getUniqueUncachedResource<Font>(TR("resource.font.default"));
        io.FontDefault = defaultFont->getFont();
        io.Fonts->Build();
    }

    return &handle;
}

void Device::refreshWindows() {
    // Render each window
    for (auto& handle : g_Windows) {
        if (!handle)
            continue;

        if (Device::isWindowAboutToBeDestroyed(&handle)) {
            Device::destroyWindow(&handle);
            continue;
        }

        if (!Device::isWindowVisible(&handle)) {
            handle.viewport->update();
            continue;
        }

        ImGui::SetCurrentContext(handle.imguiContext);
        setImGuiConfigPath();

        Renderer::pushFrameBuffer(*handle.viewport->getRawHandle());
        Renderer::startImGuiFrame();

        handle.viewport->update();
        handle.viewport->render();

        for (auto& [uuid, panel] : handle.panels) {
            panel->render();
        }

		// unbind depth stencil buffer
		g_d3dDeviceContext->OMSetRenderTargets(1, &handle.viewport->getRawHandle()->renderTargetView, nullptr);

        // HACK: set cull to back faces before we render imgui just in case.
        D3D11_RASTERIZER_DESC rasterizerDesc;
        ID3D11RasterizerState* rasterizerState;
        g_d3dDeviceContext->RSGetState(&rasterizerState);
        rasterizerState->GetDesc(&rasterizerDesc); // build off our default state
        rasterizerDesc.CullMode = D3D11_CULL_BACK;
        g_d3dDevice->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
        g_d3dDeviceContext->RSSetState(rasterizerState);

        Renderer::endImGuiFrame();
        Renderer::popFrameBuffer();

        rasterizerDesc.CullMode = D3D11_CULL_FRONT;
        g_d3dDevice->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
        g_d3dDeviceContext->RSSetState(rasterizerState);

        MeshDataBuilder surface;
        surface.addSquare({}, {2, -2}, SignedAxis::ZN, 0);
        surface.setMaterial(Resource::getUniqueUncachedResource<MaterialFrameBuffer>("file://materials/window.json", handle.viewport->getRawHandle()).cast<IMaterial>());
        surface.render(glm::identity<glm::mat4>());

		// rebind depth stencil buffer
		g_d3dDeviceContext->OMSetRenderTargets(1,
            &handle.viewport->getRawHandle()->renderTargetView,
            handle.viewport->getRawHandle()->depthStencilView);

        handle.d3dSwapChain->Present(g_d3dSwapInterval, 0);
    }

    // Process input
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        auto* handle = reinterpret_cast<Device::WindowHandle*>(SDL_GetWindowData(SDL_GetWindowFromID(event.window.windowID), "handle"));
        if (!handle)
            continue;

        ImGui::SetCurrentContext(handle->imguiContext);
        ImGui_ImplSDL2_ProcessEvent(&event);

        switch (event.type) {
            case SDL_QUIT:
                for (auto& windowHandle : g_Windows) {
                    if (windowHandle) {
                        Device::queueDestroyWindow(&windowHandle, true);
                    }
                }
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                    case SDL_WINDOWEVENT_CLOSE:
                        Device::queueDestroyWindow(handle, true);
                        break;
                    case SDL_WINDOWEVENT_RESIZED:
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                    case SDL_WINDOWEVENT_MAXIMIZED:
                        SDL_GetWindowSizeInPixels(handle->window, &handle->width, &handle->height);
                        handle->viewport->setSize({handle->width, handle->height});

						// recreate our render target
						dxRelease(handle->renderTargetView)
						ID3D11Texture2D* pBackBuffer;
						handle->d3dSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
						handle->d3dSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
                        if (pBackBuffer) {
                            g_d3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &handle->renderTargetView);
                            pBackBuffer->Release();
                        } else {
                            LOG_WINDOW.error("Failed to get back buffer!");
                        }
                        
                        break;
                    default:
                        // There's quite a few events we don't care about
                        break;
                }
                break;
            case SDL_KEYDOWN:
                for (const auto& keyEvent : Input::KeyEvent::getEvents()) {
                    if (keyEvent.getEvent() == event.key.keysym.sym && keyEvent.getEventType() == Input::KeyEventType::PRESSED) {
                        keyEvent();
                    }
                }
                break;
            case SDL_KEYUP:
                for (const auto& keyEvent : Input::KeyEvent::getEvents()) {
                    if (keyEvent.getEvent() == event.key.keysym.sym && keyEvent.getEventType() == Input::KeyEventType::RELEASED) {
                        keyEvent();
                    }
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                for (const auto& mouseEvent : Input::MouseEvent::getEvents()) {
                    if (static_cast<uint8_t>(mouseEvent.getEvent()) == event.button.button && mouseEvent.getEventType() == Input::MouseEventType::CLICKED) {
                        mouseEvent(event.button.x, event.button.y, event.button.clicks);
                    }
                }
                break;
            case SDL_MOUSEBUTTONUP:
                for (const auto& mouseEvent : Input::MouseEvent::getEvents()) {
                    if (static_cast<uint8_t>(mouseEvent.getEvent()) == event.button.button && mouseEvent.getEventType() == Input::MouseEventType::RELEASED) {
                        mouseEvent(event.button.x, event.button.y, event.button.clicks);
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                for (const auto& mouseMotionEvent : Input::MouseMotionEvent::getEvents()) {
                    if (mouseMotionEvent.getEvent() == Input::MouseMotion::MOVEMENT) {
                        mouseMotionEvent(event.motion.x, event.motion.y, event.motion.xrel, event.motion.yrel);
                    }
                }
                break;
            case SDL_MOUSEWHEEL:
                for (const auto& mouseMotionEvent : Input::MouseMotionEvent::getEvents()) {
                    if (mouseMotionEvent.getEvent() == Input::MouseMotion::SCROLL) {
                        mouseMotionEvent(event.wheel.x, event.wheel.y, event.wheel.x, event.wheel.y);
                    }
                }
                break;
            default:
                // todo(input): handle joystick / game controller inputs!
                break;
        }
    }

    // Handle repeating events
    // This is a pointer to a static variable in SDL so this is safe
    static const auto* keyStates = SDL_GetKeyboardState(nullptr);
    for (const auto& keyEvent : Input::KeyEvent::getEvents()) {
        if (keyStates[SDL_GetScancodeFromKey(keyEvent.getEvent())] && keyEvent.getEventType() == Input::KeyEventType::REPEATED) {
            keyEvent();
        }
    }
}

int Device::getWindowCount() {
    int count = 0;
    for (const auto& handle : g_Windows) {
        count += static_cast<bool>(handle);
    }
    return count;
}

Viewport* Device::getWindowViewport(WindowHandle* handle) {
    return handle->viewport;
}

void Device::setWindowTitle(WindowHandle* handle, std::string_view title) {
    SDL_SetWindowTitle(handle->window, title.data());
}

std::string_view Device::getWindowTitle(WindowHandle* handle) {
    return SDL_GetWindowTitle(handle->window);
}

void Device::setWindowMaximized(WindowHandle* handle, bool maximize) {
    if (Device::isWindowFullscreen(handle))
        return;
    if (maximize) {
        SDL_MaximizeWindow(handle->window);
    } else {
        SDL_RestoreWindow(handle->window);
    }
    SDL_GetWindowSize(handle->window, &handle->width, &handle->height);
    handle->viewport->setSize({handle->width, handle->height});
}

bool Device::isWindowMaximized(WindowHandle* handle) {
    return SDL_GetWindowFlags(handle->window) & SDL_WINDOW_MAXIMIZED;
}

void Device::minimizeWindow(WindowHandle* handle, bool minimize) {
    if (minimize) {
        SDL_MinimizeWindow(handle->window);
    } else {
        SDL_RestoreWindow(handle->window);
    }
}

bool Device::isWindowMinimized(WindowHandle* handle) {
    return SDL_GetWindowFlags(handle->window) & SDL_WINDOW_MINIMIZED;
}

void Device::setWindowFullscreen(WindowHandle* handle, bool fullscreen) {
    SDL_SetWindowFullscreen(handle->window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

bool Device::isWindowFullscreen(WindowHandle* handle) {
    return SDL_GetWindowFlags(handle->window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void Device::setWindowVisibility(WindowHandle* handle, bool visible) {
    if (visible) {
        SDL_ShowWindow(handle->window);
    } else {
        SDL_HideWindow(handle->window);
    }
    handle->hidden = visible;
}

bool Device::isWindowVisible(WindowHandle* handle) {
    return !handle->hidden;
}

void Device::setWindowSize(WindowHandle* handle, int width, int height) {
    handle->width = width;
    handle->height = height;
    handle->viewport->setSize({width, height});
    SDL_SetWindowSize(handle->window, width, height);
}

glm::vec2i Device::getWindowSize(WindowHandle* handle) {
    glm::vec2i dims;
    SDL_GetWindowSize(handle->window, &dims.x, &dims.y);
    return dims;
}

void Device::setWindowPosition(WindowHandle* handle, int width, int height) {
    if (Device::isWindowFullscreen(handle))
        return;
    SDL_SetWindowPosition(handle->window, width, height);
}

void Device::setWindowPositionFromCenter(WindowHandle* handle, int width, int height) {
    if (Device::isWindowFullscreen(handle))
        return;
    SDL_Rect rect;
    SDL_GetDisplayBounds(0, &rect);
    SDL_SetWindowPosition(handle->window, (rect.w / 2) + width, (rect.h / 2) + height);
}

glm::vec2i Device::getWindowPosition(WindowHandle* handle) {
    glm::vec2i pos;
    SDL_GetWindowPosition(handle->window, &pos.x, &pos.y);
    return pos;
}

void Device::setMousePositionGlobal(int x, int y) {
    SDL_WarpMouseGlobal(x, y);
}

void Device::setMousePositionInWindow(WindowHandle* handle, int x, int y) {
    SDL_WarpMouseInWindow(handle->window, x, y);
}

glm::vec2i Device::getMousePositionGlobal() {
    glm::vec2i pos{-1, -1};
    SDL_GetGlobalMouseState(&pos.x, &pos.y);
    return pos;
}

glm::vec2i Device::getMousePositionInFocusedWindow() {
    glm::vec2i pos{-1, -1};
    SDL_GetMouseState(&pos.x, &pos.y);
    return pos;
}

void Device::setMouseCapturedWindow(WindowHandle* handle, bool captured) {
    SDL_RaiseWindow(handle->window);
    ImGui::SetCurrentContext(handle->imguiContext);
    setImGuiConfigPath();

    if (captured) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        SDL_SetRelativeMouseMode(SDL_FALSE);
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
    handle->mouseCaptured = captured;
}

bool Device::isMouseCapturedWindow(WindowHandle* handle) {
    return handle->mouseCaptured;
}

/// Destroys windows the next time refreshWindows() is called
void Device::queueDestroyWindow(WindowHandle* handle, bool free) {
    handle->shouldClose = free;
}

bool Device::isWindowAboutToBeDestroyed(WindowHandle* handle) {
    return handle->shouldClose;
}

void Device::destroyWindow(WindowHandle* handle) {
    if (handle->viewportIsSelfOwned) {
        delete handle->viewport;
    }
    Device::removeAllPanelsFromWindow(handle);
    ImGui::DestroyContext(handle->imguiContext);
    handle->imguiContext = nullptr;

	dxRelease(handle->renderTargetView)
	dxRelease(handle->d3dSwapChain)

    SDL_DestroyWindow(handle->window);
    handle->window = nullptr;
}

void Device::destroyAllWindows() {
    for (auto& handle : g_Windows) {
        if (handle) {
            Device::destroyWindow(&handle);
        }
    }
}

uuids::uuid Device::addPanelToWindow(WindowHandle* handle, IPanel* panel) {
    const auto uuid = UUIDGenerator::getNewUUID();
    handle->panels[uuid] = panel;
    return uuid;
}

[[nodiscard]] IPanel* Device::getPanelOnWindow(WindowHandle* handle, const uuids::uuid& panelID) {
    if (handle->panels.contains(panelID))
        return handle->panels[panelID];
    return nullptr;
}

void Device::removePanelFromWindow(WindowHandle* handle, const uuids::uuid& panelID) {
    if (handle->panels.contains(panelID)) {
        delete handle->panels[panelID];
        handle->panels.erase(panelID);
    }
}

void Device::removeAllPanelsFromWindow(WindowHandle* handle) {
    for (const auto& [panelID, panel] : handle->panels) {
        delete panel;
    }
    handle->panels.clear();
}

void Device::popup(std::string_view message, std::string_view title, unsigned int popupFlags, std::string_view ok) {
    SDL_MessageBoxButtonData buttons[] {
            {
                    .flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
                    .buttonid = 0,
                    .text = ok.data(),
            },
    };
    SDL_MessageBoxData data{
            .flags = popupFlags,
            .window = nullptr,
            .title = title.data(),
            .message = message.data(),
            .numbuttons = 1,
            .buttons = buttons,
            .colorScheme = nullptr,
    };
    int buttonID;
    SDL_ShowMessageBox(&data, &buttonID);
}

void Device::popupInfo(std::string_view message, std::string_view title) {
    Device::popup(message, title, POPUP_INFO);
}

void Device::popupWarning(std::string_view message, std::string_view title) {
    Device::popup(message, title, POPUP_WARNING);
}

void Device::popupError(std::string_view message, std::string_view title) {
    Device::popup(message, title, POPUP_ERROR);
}

bool Device::popupChoice(std::string_view message, std::string_view title, unsigned int popupFlags, std::string_view ok, std::string_view cancel) {
    SDL_MessageBoxButtonData buttons[] {
            {
                    .flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
                    .buttonid = 1,
                    .text = ok.data(),
            },
            {
                    .flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                    .buttonid = 0,
                    .text = cancel.data(),
            }
    };
    SDL_MessageBoxData data{
            .flags = popupFlags,
            .window = nullptr,
            .title = title.data(),
            .message = message.data(),
            .numbuttons = 2,
            .buttons = buttons,
            .colorScheme = nullptr,
    };
    int buttonID;
    SDL_ShowMessageBox(&data, &buttonID);
    return buttonID;
}

bool Device::popupInfoChoice(std::string_view message, std::string_view title) {
    return Device::popupChoice(message, title, POPUP_INFO);
}

bool Device::popupWarningChoice(std::string_view message, std::string_view title) {
    return Device::popupChoice(message, title, POPUP_WARNING);
}

bool Device::popupErrorChoice(std::string_view message, std::string_view title) {
    return Device::popupChoice(message, title, POPUP_ERROR);
}
