#include "platform/shell.hpp"

#include "platform/win.hpp"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

namespace instaseek::platform {

namespace {

template <typename Interface>
class ComReference {
public:
    ComReference() = default;
    ~ComReference() {
        if (pointer_ != nullptr) pointer_->Release();
    }
    ComReference(const ComReference&) = delete;
    ComReference& operator=(const ComReference&) = delete;

    Interface** put() { return &pointer_; }
    Interface* operator->() const { return pointer_; }
    Interface* get() const { return pointer_; }
    explicit operator bool() const { return pointer_ != nullptr; }

private:
    Interface* pointer_ = nullptr;
};

class ComApartment {
public:
    ComApartment() : initialized_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {}
    ~ComApartment() {
        if (initialized_) CoUninitialize();
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

private:
    bool initialized_;
};

std::wstring startFolder(const std::wstring& location) {
    if (location.empty()) return {};
    const std::wstring absolute = withoutExtendedPrefix(absolutePath(location));
    switch (pathKind(absolute)) {
        case PathKind::Directory: return absolute;
        case PathKind::File: {
            const auto slash = absolute.find_last_of(L'\\');
            return slash == std::wstring::npos ? std::wstring() : absolute.substr(0, slash + 1);
        }
        case PathKind::Missing: break;
    }
    return {};
}

}

std::optional<std::wstring> pickPath(PickKind kind, const std::wstring& start_location) {
    const ComApartment apartment;
    ComReference<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(dialog.put()))))
        return std::nullopt;

    FILEOPENDIALOGOPTIONS options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | FOS_PATHMUSTEXIST;
        options |= kind == PickKind::Folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
        dialog->SetOptions(options);
    }
    dialog->SetTitle(kind == PickKind::Folder ? L"InstaSeek: choose a folder" : L"InstaSeek: choose a file");

    const std::wstring folder = startFolder(start_location);
    if (!folder.empty()) {
        ComReference<IShellItem> folder_item;
        if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_IShellItem,
                                                  reinterpret_cast<void**>(folder_item.put()))))
            dialog->SetFolder(folder_item.get());
    }

    if (FAILED(dialog->Show(GetConsoleWindow()))) return std::nullopt;
    ComReference<IShellItem> result;
    if (FAILED(dialog->GetResult(result.put())) || !result) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) return std::nullopt;
    std::wstring chosen(path);
    CoTaskMemFree(path);
    return chosen;
}

bool revealInExplorer(const std::wstring& path) {
    const std::wstring clean = withoutExtendedPrefix(path);
    if (clean.empty()) return false;
    std::wstring parameters;
    if (pathKind(clean) == PathKind::Directory) parameters = quoteCommandLineArgument(clean);
    else parameters = L"/select," + quoteCommandLineArgument(clean);
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL));
    return result > 32;
}

}
