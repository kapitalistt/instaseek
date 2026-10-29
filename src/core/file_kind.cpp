#include "core/file_kind.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace instaseek::core {

namespace {

struct ExtensionGroup {
    FileKind kind;
    std::string_view extensions;
};

constexpr std::array<ExtensionGroup, 6> kExtensionGroups = {{
    {FileKind::Document, " txt md pdf doc docx odt rtf xls xlsx ods csv ppt pptx odp epub log ini cfg conf json xml yaml yml toml "},
    {FileKind::Code, " c cc cpp cxx h hh hpp hxx cs java kt go rs py rb php js mjs ts tsx jsx lua sql sh ps1 bat cmd cmake html css scss vue swift "},
    {FileKind::Image, " png jpg jpeg gif bmp webp tif tiff svg ico heic raw psd "},
    {FileKind::Archive, " zip 7z rar tar gz tgz bz2 xz zst cab iso img vhd vhdx wim "},
    {FileKind::Media, " mp3 wav flac ogg opus m4a aac wma mp4 mkv avi mov wmv webm m4v "},
    {FileKind::Binary, " exe dll sys msi lib obj pdb bin dat db sqlite so dylib class jar pyc "},
}};

bool containsWord(std::string_view haystack, std::string_view word) {
    if (word.empty()) return false;
    std::string needle;
    needle.reserve(word.size() + 2);
    needle.push_back(' ');
    needle.append(word);
    needle.push_back(' ');
    return haystack.find(needle) != std::string_view::npos;
}

}

std::string_view extensionOf(std::string_view utf8_name) {
    const auto dot = utf8_name.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= utf8_name.size()) return {};
    return utf8_name.substr(dot + 1);
}

FileKind classifyFileName(std::string_view utf8_name, bool is_directory) {
    if (is_directory) return FileKind::Folder;
    const auto extension = extensionOf(utf8_name);
    if (extension.empty() || extension.size() > 8) return FileKind::Other;

    std::string lowered(extension);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    for (const auto& group : kExtensionGroups)
        if (containsWord(group.extensions, lowered)) return group.kind;
    return FileKind::Other;
}

}
