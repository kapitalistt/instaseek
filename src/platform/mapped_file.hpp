#pragma once

#include "platform/unique_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace instaseek::platform {

class MappedView {
public:
    MappedView() = default;
    MappedView(void* base, const std::uint8_t* data, std::uint64_t offset, std::size_t size)
        : base_(base), data_(data), offset_(offset), size_(size) {}
    ~MappedView();

    MappedView(const MappedView&) = delete;
    MappedView& operator=(const MappedView&) = delete;
    MappedView(MappedView&& other) noexcept;
    MappedView& operator=(MappedView&& other) noexcept;

    const std::uint8_t* data() const { return data_; }
    std::uint64_t offset() const { return offset_; }
    std::size_t size() const { return size_; }
    bool valid() const { return data_ != nullptr || size_ == 0; }
    bool covers(std::uint64_t offset, std::uint64_t length) const {
        return offset >= offset_ && offset + length <= offset_ + size_;
    }

private:
    void release();

    void* base_ = nullptr;
    const std::uint8_t* data_ = nullptr;
    std::uint64_t offset_ = 0;
    std::size_t size_ = 0;
};

class MappedFile {
public:
    static std::unique_ptr<MappedFile> open(const std::wstring& path, unsigned long& error);

    std::uint64_t size() const { return size_; }
    const std::wstring& path() const { return path_; }

    bool map(std::uint64_t offset, std::size_t length, MappedView& view, unsigned long& error) const;

private:
    MappedFile() = default;

    UniqueHandle file_;
    UniqueHandle mapping_;
    std::uint64_t size_ = 0;
    std::wstring path_;
};

using GuardedBody = void (*)(void* context);
bool runGuardedAgainstPageErrors(GuardedBody body, void* context);

}
