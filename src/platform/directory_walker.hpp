#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace instaseek::platform {

struct DirectoryEntry {
    std::wstring name;
    std::uint32_t attributes = 0;
    std::uint64_t size = 0;
    std::int64_t modified = 0;

    bool isDirectory() const;
    bool isReparsePoint() const;
};

unsigned long listDirectory(const std::wstring& extended_directory, std::vector<DirectoryEntry>& entries);

}
