#include "platform/clipboard.hpp"

#include "platform/win.hpp"

#include <windows.h>

#include <chrono>
#include <cstring>
#include <thread>

namespace instaseek::platform {

namespace {

constexpr std::size_t kMaxClipboardChars = 1u << 20;

class ClipboardLock {
public:
    ClipboardLock() {
        for (int attempt = 0; attempt < 10 && !open_; ++attempt) {
            open_ = OpenClipboard(nullptr) != FALSE;
            if (!open_) std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }
    ~ClipboardLock() {
        if (open_) CloseClipboard();
    }
    ClipboardLock(const ClipboardLock&) = delete;
    ClipboardLock& operator=(const ClipboardLock&) = delete;

    bool open() const { return open_; }

private:
    bool open_ = false;
};

}

bool copyToClipboard(std::string_view utf8) {
    const std::wstring wide = toWide(utf8);
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == nullptr) return false;
    void* target = GlobalLock(memory);
    if (target == nullptr) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(target, wide.c_str(), bytes);
    GlobalUnlock(memory);

    const ClipboardLock lock;
    if (!lock.open() || !EmptyClipboard() || SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

std::optional<std::string> readClipboard() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return std::nullopt;
    const ClipboardLock lock;
    if (!lock.open()) return std::nullopt;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data == nullptr) return std::nullopt;
    const auto* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (text == nullptr) return std::nullopt;
    const std::size_t capacity = GlobalSize(data) / sizeof(wchar_t);
    std::size_t length = 0;
    while (length < capacity && length < kMaxClipboardChars && text[length] != L'\0') ++length;
    std::string utf8 = toUtf8(std::wstring_view(text, length));
    GlobalUnlock(data);
    return utf8;
}

}
