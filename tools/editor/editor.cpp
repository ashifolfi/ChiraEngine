#include <config/ConEntry.h>
#include <core/Engine.h>
#include <i18n/TranslationManager.h>
#include <resource/provider/FilesystemResourceProvider.h>

#ifdef CHIRA_USE_DISCORD
    #include <module/discord/Discord.h>
#endif
#ifdef CHIRA_USE_STEAMWORKS
    #include <module/steam/Steam.h>
#endif

// Need to register phong material!
#include <render/material/MaterialPhong.h>

#include "ui/ControlsPanel.h"
#include "ui/EntitySelectPanel.h"
#include "ui/InspectorPanel.h"
#include "ui/ScriptEditorPanel.h"

#include "../ToolHelpers.h"

using namespace chira;

CHIRA_SETUP_GUI_TOOL(EDITOR);

#ifdef CHIRA_USE_DISCORD
CHIRA_GET_MODULE(Discord);
#endif

int main(int argc, const char* argv[]) {
    Engine::preinit(argc, argv);
    Resource::addResourceProvider(new FilesystemResourceProvider{"editor"});
    TranslationManager::addTranslationFile("file://i18n/editor");
    TranslationManager::addUniversalFile("file://i18n/editor");

#ifdef CHIRA_USE_DISCORD
    g_Discord->init(TR("editor.discord.application_id"));
    g_Discord->setLargeImage("main_logo");
    g_Discord->setTopButton({"View on GitHub", "https://github.com/craftablescience/ChiraEngine"});
    g_Discord->setBottomButton({"Join Discord", "https://discord.gg/ASgHFkX"});
#endif

#if defined(CHIRA_USE_STEAMWORKS) && defined(DEBUG)
    if (auto* steam_enabled = ConEntryRegistry::getConVar("steam_enabled"); steam_enabled && steam_enabled->getValue<bool>()) {
        // Steam API docs say this is bad practice, I say I don't care
        Steam::generateAppIDFile(1728950);
    }
#endif

    Engine::init();

    auto* window = Engine::getMainWindow();
    auto* viewport = Device::getWindowViewport(window);
    viewport->setBackgroundColor({0.15f});

    auto controls = new ControlsPanel{viewport};
    Device::addPanelToWindow(window, controls);

    auto inspector = new InspectorPanel{};
    Device::addPanelToWindow(window, inspector);

    auto scriptEditor = new ScriptEditorPanel{};
    Device::addPanelToWindow(window, scriptEditor);

    Device::addPanelToWindow(window, new EntitySelectPanel{viewport, controls, inspector});

    Engine::run();
}
