#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace instaseek::core {

enum class TextEncoding { Utf8, Utf16Le, Utf16Be, Windows1251 };

inline constexpr std::array<TextEncoding, 4> kAllEncodings = {
    TextEncoding::Utf8, TextEncoding::Utf16Le, TextEncoding::Utf16Be, TextEncoding::Windows1251};

struct EncodingGuess {
    TextEncoding encoding = TextEncoding::Utf8;
    std::size_t bom_length = 0;
    bool from_bom = false;
    bool looks_binary = false;
};

std::string_view encodingName(TextEncoding encoding);
std::size_t encodingUnitSize(TextEncoding encoding);
std::size_t bomLength(const std::uint8_t* data, std::size_t size, TextEncoding encoding);

EncodingGuess detectEncoding(const std::uint8_t* data, std::size_t size);

bool isNewlineUnit(const std::uint8_t* unit, TextEncoding encoding);
std::uint64_t countNewlines(const std::uint8_t* data, std::size_t size, TextEncoding encoding);
std::optional<std::size_t> findNewline(const std::uint8_t* data, std::size_t size, std::size_t from,
                                       TextEncoding encoding);

char32_t windows1251ToCodepoint(std::uint8_t byte);
std::optional<std::uint8_t> codepointToWindows1251(char32_t codepoint);

void appendAsUtf8(std::string& out, const std::uint8_t* data, std::size_t size, TextEncoding encoding);
std::string decodeToUtf8(const std::uint8_t* data, std::size_t size, TextEncoding encoding);
std::optional<std::string> encodeFromUtf8(std::string_view utf8, TextEncoding encoding);

}
