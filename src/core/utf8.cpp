#include "core/utf8.hpp"

namespace instaseek::core::utf8 {

namespace {

bool isInRange(char32_t value, char32_t first, char32_t last) { return value >= first && value <= last; }

}

DecodedCodepoint decode(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) return {kReplacementCharacter, 0};
    const auto lead = static_cast<unsigned char>(text[offset]);
    if (lead < 0x80) return {lead, 1};

    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        value = lead & 0x1F;
        minimum = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        value = lead & 0x0F;
        minimum = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        value = lead & 0x07;
        minimum = 0x10000;
    } else {
        return {kReplacementCharacter, 1};
    }

    if (offset + length > text.size()) return {kReplacementCharacter, 1};
    for (std::size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[offset + i]);
        if ((next & 0xC0) != 0x80) return {kReplacementCharacter, 1};
        value = (value << 6) | (next & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF || isInRange(value, 0xD800, 0xDFFF)) return {kReplacementCharacter, 1};
    return {value, length};
}

void append(std::string& out, char32_t codepoint) {
    if (codepoint > 0x10FFFF || isInRange(codepoint, 0xD800, 0xDFFF)) codepoint = kReplacementCharacter;
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

char32_t foldCase(char32_t c) {
    if (isInRange(c, U'A', U'Z')) return c + 0x20;
    if (c < 0x80) return c;
    if (isInRange(c, 0xC0, 0xDE) && c != 0xD7) return c + 0x20;
    if (isInRange(c, 0x100, 0x137) || isInRange(c, 0x14A, 0x177)) return c | 1;
    if (isInRange(c, 0x139, 0x148) || isInRange(c, 0x179, 0x17E)) return (c & 1) ? c + 1 : c;
    if (c == 0x178) return 0xFF;
    if (isInRange(c, 0x391, 0x3AB) && c != 0x3A2) return c + 0x20;
    if (isInRange(c, 0x400, 0x40F)) return c + 0x50;
    if (isInRange(c, 0x410, 0x42F)) return c + 0x20;
    if (isInRange(c, 0x460, 0x481) || isInRange(c, 0x48A, 0x4BF) || isInRange(c, 0x4D0, 0x52F)) return c | 1;
    if (isInRange(c, 0x1E00, 0x1E95) || isInRange(c, 0x1EA0, 0x1EFF)) return c | 1;
    if (isInRange(c, 0xFF21, 0xFF3A)) return c + 0x20;
    return c;
}

std::string foldCase(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto decoded = decode(text, offset);
        append(out, foldCase(decoded.value));
        offset += decoded.length;
    }
    return out;
}

std::size_t codepointCount(std::string_view text) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        offset += decode(text, offset).length;
        ++count;
    }
    return count;
}

std::string takeCodepoints(std::string_view text, std::size_t count) {
    std::size_t offset = 0;
    while (offset < text.size() && count > 0) {
        offset += decode(text, offset).length;
        --count;
    }
    return std::string(text.substr(0, offset));
}

std::string fitToWidth(std::string_view text, std::size_t width) {
    const std::size_t length = codepointCount(text);
    if (length <= width) return std::string(text) + std::string(width - length, ' ');
    if (width == 0) return {};
    std::string out = takeCodepoints(text, width - 1);
    append(out, 0x2026);
    return out;
}

std::string fitToWidthKeepingEnd(std::string_view text, std::size_t width) {
    const std::size_t length = codepointCount(text);
    if (length <= width) return std::string(text) + std::string(width - length, ' ');
    if (width == 0) return {};
    std::size_t skip = length - (width - 1);
    std::size_t offset = 0;
    while (offset < text.size() && skip > 0) {
        offset += decode(text, offset).length;
        --skip;
    }
    std::string out;
    append(out, 0x2026);
    out.append(text.substr(offset));
    return out;
}

bool isContinuationByte(char byte) { return (static_cast<unsigned char>(byte) & 0xC0) == 0x80; }

std::size_t alignToCodepointStart(std::string_view text, std::size_t position) {
    if (position >= text.size()) return text.size();
    while (position > 0 && isContinuationByte(text[position])) --position;
    return position;
}

bool isWordCodepoint(char32_t c) {
    if (isInRange(c, U'0', U'9') || isInRange(c, U'a', U'z') || isInRange(c, U'A', U'Z') || c == U'_') return true;
    if (c < 0xC0) return false;
    if (c == 0xD7 || c == 0xF7) return false;
    if (isInRange(c, 0x2000, 0x2BFF) || isInRange(c, 0x3000, 0x303F) || isInRange(c, 0xFE30, 0xFE4F)) return false;
    if (isInRange(c, 0xFF00, 0xFF0F) || isInRange(c, 0xFF1A, 0xFF20) || c == kReplacementCharacter) return false;
    return true;
}

bool isWordBoundary(std::string_view text, std::size_t start, std::size_t end) {
    if (start > 0) {
        const std::size_t previous = alignToCodepointStart(text, start - 1);
        if (isWordCodepoint(decode(text, previous).value)) return false;
    }
    if (end < text.size() && isWordCodepoint(decode(text, end).value)) return false;
    return true;
}

std::size_t columnWidth(char32_t c) {
    if (c == 0) return 0;
    if (isInRange(c, 0x0300, 0x036F) || isInRange(c, 0x200B, 0x200F) || c == 0xFE0F) return 0;
    if (isInRange(c, 0x1100, 0x115F) || isInRange(c, 0x2E80, 0x303E) || isInRange(c, 0x3041, 0x33FF) ||
        isInRange(c, 0x3400, 0x4DBF) || isInRange(c, 0x4E00, 0x9FFF) || isInRange(c, 0xA000, 0xA4CF) ||
        isInRange(c, 0xAC00, 0xD7A3) || isInRange(c, 0xF900, 0xFAFF) || isInRange(c, 0xFE30, 0xFE4F) ||
        isInRange(c, 0xFF00, 0xFF60) || isInRange(c, 0xFFE0, 0xFFE6) || isInRange(c, 0x1F300, 0x1F64F) ||
        isInRange(c, 0x1F900, 0x1F9FF) || isInRange(c, 0x20000, 0x3FFFD))
        return 2;
    return 1;
}

std::size_t displayWidth(std::string_view text) {
    std::size_t width = 0;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto decoded = decode(text, offset);
        width += columnWidth(decoded.value);
        offset += decoded.length;
    }
    return width;
}

}
