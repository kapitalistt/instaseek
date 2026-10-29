#pragma once

#include <utility>

namespace instaseek::platform {

using NativeHandle = void*;

bool isUsableHandle(NativeHandle handle);
void closeNativeHandle(NativeHandle handle);

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(NativeHandle handle) : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    NativeHandle get() const { return handle_; }
    bool valid() const { return isUsableHandle(handle_); }
    explicit operator bool() const { return valid(); }

    void reset(NativeHandle replacement = nullptr) {
        if (valid()) closeNativeHandle(handle_);
        handle_ = replacement;
    }

    NativeHandle release() { return std::exchange(handle_, nullptr); }

private:
    NativeHandle handle_ = nullptr;
};

}
