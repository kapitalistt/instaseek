#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace instaseek::core::utf8 {

inline constexpr char32_t kReplacementCharacter = 0xFFFD;

struct DecodedCodepoint {
    char32_t value = kReplacementCharacter;
    std::size_t length = 1;
};

DecodedCodepoint decode(std::string_view text, std::size_t offset);
void append(std::string& out, char32_t codepoint);

char32_t foldCase(char32_t codepoint);
std::string foldCase(std::string_view text);

std::size_t codepointCount(std::string_view text);
std::string takeCodepoints(std::string_view text, std::size_t count);
std::string fitToWidth(std::string_view text, std::size_t width);
std::string fitToWidthKeepingEnd(std::string_view text, std::size_t width);

bool isContinuationByte(char byte);
std::size_t alignToCodepointStart(std::string_view text, std::size_t position);
bool isWordCodepoint(char32_t codepoint);
bool isWordBoundary(std::string_view text, std::size_t start, std::size_t end);

std::size_t columnWidth(char32_t codepoint);
std::size_t displayWidth(std::string_view text);

}
