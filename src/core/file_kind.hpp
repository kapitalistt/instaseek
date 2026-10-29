#pragma once

#include <cstddef>
#include <string_view>

namespace instaseek::core {

enum class FileKind { Folder, Document, Code, Image, Archive, Media, Binary, Other };

inline constexpr std::size_t kFileKindCount = 8;

FileKind classifyFileName(std::string_view utf8_name, bool is_directory);

std::string_view extensionOf(std::string_view utf8_name);

}
