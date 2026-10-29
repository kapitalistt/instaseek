#include "platform/mapped_file.hpp"

#include "platform/win.hpp"

#include <windows.h>

#include <algorithm>
#include <utility>

namespace instaseek::platform {

namespace {

std::uint64_t allocationGranularity() {
    static const std::uint64_t granularity = [] {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        return static_cast<std::uint64_t>(info.dwAllocationGranularity == 0 ? 65536 : info.dwAllocationGranularity);
    }();
    return granularity;
}

int pageErrorFilter(unsigned long code) {
    return code == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

}

MappedView::~MappedView() { release(); }

MappedView::MappedView(MappedView&& other) noexcept
    : base_(std::exchange(other.base_, nullptr)),
      data_(std::exchange(other.data_, nullptr)),
      offset_(std::exchange(other.offset_, 0)),
      size_(std::exchange(other.size_, 0)) {}

MappedView& MappedView::operator=(MappedView&& other) noexcept {
    if (this != &other) {
        release();
        base_ = std::exchange(other.base_, nullptr);
        data_ = std::exchange(other.data_, nullptr);
        offset_ = std::exchange(other.offset_, 0);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

void MappedView::release() {
    if (base_ != nullptr) UnmapViewOfFile(base_);
    base_ = nullptr;
    data_ = nullptr;
    offset_ = 0;
    size_ = 0;
}

std::unique_ptr<MappedFile> MappedFile::open(const std::wstring& path, unsigned long& error) {
    error = 0;
    const std::wstring extended = toExtendedPath(path);
    UniqueHandle file(CreateFileW(extended.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        error = GetLastError();
        return nullptr;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size)) {
        error = GetLastError();
        return nullptr;
    }

    std::unique_ptr<MappedFile> mapped(new MappedFile());
    mapped->size_ = static_cast<std::uint64_t>(size.QuadPart);
    mapped->path_ = withoutExtendedPrefix(extended);
    if (mapped->size_ > 0) {
        mapped->mapping_.reset(CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
        if (!mapped->mapping_) {
            error = GetLastError();
            return nullptr;
        }
    }
    mapped->file_ = std::move(file);
    return mapped;
}

bool MappedFile::map(std::uint64_t offset, std::size_t length, MappedView& view, unsigned long& error) const {
    error = 0;
    view = MappedView();
    if (offset >= size_ || length == 0) return true;
    length = static_cast<std::size_t>(std::min<std::uint64_t>(length, size_ - offset));

    const std::uint64_t granularity = allocationGranularity();
    const std::uint64_t aligned = offset - offset % granularity;
    const auto mapped_length = static_cast<SIZE_T>(length + (offset - aligned));
    void* base = MapViewOfFile(mapping_.get(), FILE_MAP_READ, static_cast<DWORD>(aligned >> 32),
                               static_cast<DWORD>(aligned & 0xFFFFFFFFu), mapped_length);
    if (base == nullptr) {
        error = GetLastError();
        return false;
    }
    const auto* data = static_cast<const std::uint8_t*>(base) + (offset - aligned);
    view = MappedView(base, data, offset, length);
    return true;
}

bool runGuardedAgainstPageErrors(GuardedBody body, void* context) {
    __try {
        body(context);
    } __except (pageErrorFilter(GetExceptionCode())) {
        return false;
    }
    return true;
}

}
