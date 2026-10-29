#include "platform/directory_walker.hpp"

#include "platform/win.hpp"

#include <windows.h>

namespace instaseek::platform {

bool DirectoryEntry::isDirectory() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }

bool DirectoryEntry::isReparsePoint() const { return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0; }

unsigned long listDirectory(const std::wstring& extended_directory, std::vector<DirectoryEntry>& entries) {
    entries.clear();
    const std::wstring pattern = joinPath(extended_directory, L"*");
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                   FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_NO_MORE_FILES ? 0 : error;
    }
    do {
        const std::wstring_view name(data.cFileName);
        if (name == L"." || name == L"..") continue;
        DirectoryEntry entry;
        entry.name.assign(name);
        entry.attributes = data.dwFileAttributes;
        entry.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        entry.modified = fileTimeTicks(data.ftLastWriteTime.dwLowDateTime, data.ftLastWriteTime.dwHighDateTime);
        entries.push_back(std::move(entry));
    } while (FindNextFileW(find, &data));
    const DWORD error = GetLastError();
    FindClose(find);
    return error == ERROR_NO_MORE_FILES ? 0 : error;
}

}
