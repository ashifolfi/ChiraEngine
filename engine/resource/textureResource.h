#pragma once

#include "resource.h"
#include <loader/image/abstractImage.h>

namespace chira {
    class TextureResource : public Resource {
    public:
        explicit TextureResource(const std::string& identifier_, bool vFlip_ = true);
        void compile(const unsigned char buffer[], std::size_t bufferLen) override;
        [[nodiscard]] AbstractImage* getFile() const {
            return this->file.get();
        }
        [[nodiscard]] unsigned char* getData() const {
            return this->file->getData();
        }
        [[nodiscard]] int getWidth() const {
            return this->width;
        }
        [[nodiscard]] int getHeight() const {
            return this->height;
        }
        [[nodiscard]] int getBitDepth() const {
            return this->bitDepth;
        }
        [[nodiscard]] bool isVerticallyFlipped() const {
            return this->vFlip;
        }
    protected:
        std::unique_ptr<AbstractImage> file = nullptr;
        int width = -1;
        int height = -1;
        int bitDepth = -1;
        bool vFlip = true;
    };
}
