#include "binaryResource.h"

using namespace chira;

void BinaryResource::compile(const unsigned char buffer[], std::size_t bufferLength) {
    this->bufferLength_ = bufferLength - 1;
    this->buffer_ = new unsigned char[this->bufferLength_];
    memcpy(this->buffer_, buffer, this->bufferLength_);
}

BinaryResource::~BinaryResource() {
    delete[] this->buffer_;
}

const unsigned char* BinaryResource::getBuffer() const noexcept {
    return this->buffer_;
}

std::size_t BinaryResource::getBufferLength() const noexcept {
    return this->bufferLength_;
}
