#include "core/encoding.hpp"

#include "core/utf8.hpp"

#include <algorithm>
#include <cstring>

namespace instaseek::core {

namespace {

constexpr std::array<char16_t, 128> kWindows1251High = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C,
    0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A,
    0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9,
    0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415,
    0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423,
    0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431,
    0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D,
    0x044E, 0x044F,
};

char16_t readUnit16(const std::uint8_t* unit, TextEncoding encoding) {
    if (encoding == TextEncoding::Utf16Be) return static_cast<char16_t>((unit[0] << 8) | unit[1]);
    return static_cast<char16_t>(unit[0] | (unit[1] << 8));
}

bool isHighSurrogate(char16_t unit) { return unit >= 0xD800 && unit <= 0xDBFF; }
bool isLowSurrogate(char16_t unit) { return unit >= 0xDC00 && unit <= 0xDFFF; }

struct Utf8Validation {
    std::size_t invalid_sequences = 0;
    std::size_t multibyte_sequences = 0;
};

Utf8Validation validateUtf8(const std::uint8_t* data, std::size_t size) {
    Utf8Validation result;
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    std::size_t offset = 0;
    while (offset < size) {
        const auto lead = data[offset];
        if (lead < 0x80) {
            ++offset;
            continue;
        }
        std::size_t expected = 0;
        if ((lead & 0xE0) == 0xC0) expected = 2;
        else if ((lead & 0xF0) == 0xE0) expected = 3;
        else if ((lead & 0xF8) == 0xF0) expected = 4;
        if (expected != 0 && offset + expected > size) break;
        const auto decoded = utf8::decode(text, offset);
        if (decoded.value == utf8::kReplacementCharacter && decoded.length == 1) {
            ++result.invalid_sequences;
        } else {
            ++result.multibyte_sequences;
        }
        offset += decoded.length;
    }
    return result;
}

}

std::string_view encodingName(TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf16Le: return "UTF-16 LE";
        case TextEncoding::Utf16Be: return "UTF-16 BE";
        case TextEncoding::Windows1251: return "Windows-1251";
        case TextEncoding::Utf8: break;
    }
    return "UTF-8";
}

std::size_t encodingUnitSize(TextEncoding encoding) {
    return encoding == TextEncoding::Utf16Le || encoding == TextEncoding::Utf16Be ? 2 : 1;
}

std::size_t bomLength(const std::uint8_t* data, std::size_t size, TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf8:
            return size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF ? 3 : 0;
        case TextEncoding::Utf16Le: return size >= 2 && data[0] == 0xFF && data[1] == 0xFE ? 2 : 0;
        case TextEncoding::Utf16Be: return size >= 2 && data[0] == 0xFE && data[1] == 0xFF ? 2 : 0;
        case TextEncoding::Windows1251: break;
    }
    return 0;
}

EncodingGuess detectEncoding(const std::uint8_t* data, std::size_t size) {
    EncodingGuess guess;
    for (const TextEncoding candidate : {TextEncoding::Utf8, TextEncoding::Utf16Le, TextEncoding::Utf16Be}) {
        const std::size_t bom = bomLength(data, size, candidate);
        if (bom > 0) {
            guess.encoding = candidate;
            guess.bom_length = bom;
            guess.from_bom = true;
            return guess;
        }
    }

    const std::size_t sample = std::min<std::size_t>(size, 64 * 1024);
    if (sample == 0) return guess;

    std::size_t zeros_even = 0;
    std::size_t zeros_odd = 0;
    for (std::size_t i = 0; i < sample; ++i)
        if (data[i] == 0) ++((i % 2 == 0) ? zeros_even : zeros_odd);

    const std::size_t pairs = sample / 2;
    if (pairs >= 2) {
        const double odd_ratio = static_cast<double>(zeros_odd) / static_cast<double>(pairs);
        const double even_ratio = static_cast<double>(zeros_even) / static_cast<double>(pairs);
        if (odd_ratio > 0.3 && even_ratio < 0.05) {
            guess.encoding = TextEncoding::Utf16Le;
            return guess;
        }
        if (even_ratio > 0.3 && odd_ratio < 0.05) {
            guess.encoding = TextEncoding::Utf16Be;
            return guess;
        }
    }
    if (zeros_even + zeros_odd > 0) {
        guess.looks_binary = true;
        return guess;
    }

    const Utf8Validation validation = validateUtf8(data, sample);
    if (validation.invalid_sequences == 0) return guess;
    if (validation.invalid_sequences * 4 < validation.multibyte_sequences) return guess;
    guess.encoding = TextEncoding::Windows1251;
    return guess;
}

bool isNewlineUnit(const std::uint8_t* unit, TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf16Le: return unit[0] == 0x0A && unit[1] == 0x00;
        case TextEncoding::Utf16Be: return unit[0] == 0x00 && unit[1] == 0x0A;
        case TextEncoding::Utf8:
        case TextEncoding::Windows1251: break;
    }
    return unit[0] == 0x0A;
}

std::uint64_t countNewlines(const std::uint8_t* data, std::size_t size, TextEncoding encoding) {
    if (encodingUnitSize(encoding) == 1)
        return static_cast<std::uint64_t>(std::count(data, data + size, static_cast<std::uint8_t>(0x0A)));
    std::uint64_t count = 0;
    for (std::size_t i = 0; i + 1 < size; i += 2)
        if (isNewlineUnit(data + i, encoding)) ++count;
    return count;
}

std::optional<std::size_t> findNewline(const std::uint8_t* data, std::size_t size, std::size_t from,
                                       TextEncoding encoding) {
    if (encodingUnitSize(encoding) == 1) {
        if (from >= size) return std::nullopt;
        const void* found = std::memchr(data + from, 0x0A, size - from);
        if (found == nullptr) return std::nullopt;
        return static_cast<std::size_t>(static_cast<const std::uint8_t*>(found) - data);
    }
    for (std::size_t i = from; i + 1 < size; i += 2)
        if (isNewlineUnit(data + i, encoding)) return i;
    return std::nullopt;
}

char32_t windows1251ToCodepoint(std::uint8_t byte) {
    if (byte < 0x80) return byte;
    return static_cast<char32_t>(kWindows1251High[byte - 0x80]);
}

std::optional<std::uint8_t> codepointToWindows1251(char32_t codepoint) {
    if (codepoint < 0x80) return static_cast<std::uint8_t>(codepoint);
    for (std::size_t i = 0; i < kWindows1251High.size(); ++i)
        if (static_cast<char32_t>(kWindows1251High[i]) == codepoint && codepoint != 0xFFFD) return static_cast<std::uint8_t>(0x80 + i);
    return std::nullopt;
}

void appendAsUtf8(std::string& out, const std::uint8_t* data, std::size_t size, TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf8: {
            const std::string_view text(reinterpret_cast<const char*>(data), size);
            std::size_t offset = 0;
            while (offset < size) {
                const std::size_t start = offset;
                while (offset < size && data[offset] < 0x80) ++offset;
                out.append(text.substr(start, offset - start));
                if (offset >= size) break;
                const auto decoded = utf8::decode(text, offset);
                if (decoded.value == utf8::kReplacementCharacter && decoded.length == 1)
                    utf8::append(out, utf8::kReplacementCharacter);
                else
                    out.append(text.substr(offset, decoded.length));
                offset += decoded.length;
            }
            return;
        }
        case TextEncoding::Windows1251:
            for (std::size_t i = 0; i < size; ++i) {
                if (data[i] < 0x80) out.push_back(static_cast<char>(data[i]));
                else utf8::append(out, windows1251ToCodepoint(data[i]));
            }
            return;
        case TextEncoding::Utf16Le:
        case TextEncoding::Utf16Be: {
            std::size_t i = 0;
            while (i + 1 < size) {
                const char16_t unit = readUnit16(data + i, encoding);
                i += 2;
                if (unit < 0x80) {
                    out.push_back(static_cast<char>(unit));
                } else if (isHighSurrogate(unit) && i + 1 < size && isLowSurrogate(readUnit16(data + i, encoding))) {
                    const char16_t low = readUnit16(data + i, encoding);
                    i += 2;
                    utf8::append(out, 0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) +
                                          (static_cast<char32_t>(low) - 0xDC00));
                } else if (isHighSurrogate(unit) || isLowSurrogate(unit)) {
                    utf8::append(out, utf8::kReplacementCharacter);
                } else {
                    utf8::append(out, static_cast<char32_t>(unit));
                }
            }
            return;
        }
    }
}

std::string decodeToUtf8(const std::uint8_t* data, std::size_t size, TextEncoding encoding) {
    std::string out;
    out.reserve(encoding == TextEncoding::Utf8 ? size : size * 2);
    appendAsUtf8(out, data, size, encoding);
    return out;
}

std::optional<std::string> encodeFromUtf8(std::string_view utf8_text, TextEncoding encoding) {
    if (encoding == TextEncoding::Utf8) return std::string(utf8_text);
    std::string out;
    std::size_t offset = 0;
    while (offset < utf8_text.size()) {
        const auto decoded = utf8::decode(utf8_text, offset);
        offset += decoded.length;
        const char32_t c = decoded.value;
        if (encoding == TextEncoding::Windows1251) {
            const auto byte = codepointToWindows1251(c);
            if (!byte) return std::nullopt;
            out.push_back(static_cast<char>(*byte));
            continue;
        }
        auto pushUnit = [&](char32_t unit) {
            const auto high = static_cast<char>((unit >> 8) & 0xFF);
            const auto low = static_cast<char>(unit & 0xFF);
            if (encoding == TextEncoding::Utf16Be) {
                out.push_back(high);
                out.push_back(low);
            } else {
                out.push_back(low);
                out.push_back(high);
            }
        };
        if (c >= 0x10000) {
            const char32_t shifted = c - 0x10000;
            pushUnit(0xD800 + (shifted >> 10));
            pushUnit(0xDC00 + (shifted & 0x3FF));
        } else {
            pushUnit(c);
        }
    }
    return out;
}

}
