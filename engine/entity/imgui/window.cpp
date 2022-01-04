#include "window.h"

using namespace chira;

Window::Window(const std::string& title_, bool startVisible, const ImVec2& windowSize, bool enforceSize) {
    this->title = title_;
    this->isVisible_ = startVisible;
    this->nextWindowSize = windowSize;
    this->windowSizeCondition = enforceSize? ImGuiCond_Always : ImGuiCond_FirstUseEver;
    this->flags = 0;
}

void Window::render(const glm::mat4& parentTransform) {
    if (this->isVisible_) {
        ImGui::SetNextWindowSize(this->nextWindowSize, this->windowSizeCondition);
        this->preRenderContents();
        if (ImGui::Begin(this->title.c_str(), &this->isVisible_, this->flags)) {
            this->renderContents();
        }
        ImGui::End();
        this->postRenderContents();
    }
    Entity::render(parentTransform);
}

void Window::setVisible(bool visible) {
    this->isVisible_ = visible;
}

bool Window::isVisible() const {
    return this->isVisible_;
}
