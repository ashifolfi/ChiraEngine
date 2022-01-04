#pragma once

#include <imgui.h>
#include <imgui_markdown.h>
#if _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <string>
#include <memory>

namespace chira {
    class Markdown {
    public:
        /*
        void LoadFonts(float fontSize_ = 12.0f) {
            // todo: get H1, H2, and H3 fonts
            ImGuiIO &io = ImGui::GetIO();
            io.Fonts->Clear();
            // Base font
            io.Fonts->AddFontFromFileTTF("myfont.ttf", fontSize_);
            // Bold headings H2 and H3
            H2 = io.Fonts->AddFontFromFileTTF("myfont-bold.ttf", fontSize_);
            H3 = mdConfig.headingFormats[1].font;
            // bold heading H1
            float fontSizeH1 = fontSize_ * 1.1f;
            H1 = io.Fonts->AddFontFromFileTTF("myfont-bold.ttf", fontSizeH1);
        }
        */
        static void create(const std::string& markdown_) {
            Markdown::mdConfig->linkCallback = Markdown::linkCallback;
            //Markdown::mdConfig.headingFormats[0] = {H1, true};
            //Markdown::mdConfig.headingFormats[1] = {H2, true};
            //Markdown::mdConfig.headingFormats[2] = {H3, false};
            Markdown::mdConfig->userData = nullptr;
            ImGui::Markdown(markdown_.c_str(), markdown_.length(), *mdConfig);
        }
    private:
        static std::unique_ptr<ImGui::MarkdownConfig> mdConfig;

        static void linkCallback(ImGui::MarkdownLinkCallbackData data_) {
            std::string url(data_.link, data_.linkLength);
            if (!data_.isImage) {
    #if _WIN32
                ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    #else
                system(("open " + url).c_str());
    #endif
            }
        }
    };
}
