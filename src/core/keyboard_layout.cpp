#include "core/keyboard_layout.hpp"

#include "core/utf8.hpp"

#include <array>
#include <utility>

namespace instaseek::core {

namespace {

constexpr std::array<std::pair<char32_t, char>, 33> kRussianJcukenToLatin = {{
    {U'й', 'q'}, {U'ц', 'w'}, {U'у', 'e'}, {U'к', 'r'}, {U'е', 't'}, {U'н', 'y'}, {U'г', 'u'},
    {U'ш', 'i'}, {U'щ', 'o'}, {U'з', 'p'}, {U'х', '['}, {U'ъ', ']'}, {U'ф', 'a'}, {U'ы', 's'},
    {U'в', 'd'}, {U'а', 'f'}, {U'п', 'g'}, {U'р', 'h'}, {U'о', 'j'}, {U'л', 'k'}, {U'д', 'l'},
    {U'ж', ';'}, {U'э', '\''}, {U'я', 'z'}, {U'ч', 'x'}, {U'с', 'c'}, {U'м', 'v'}, {U'и', 'b'},
    {U'т', 'n'}, {U'ь', 'm'}, {U'б', ','}, {U'ю', '.'}, {U'ё', '`'},
}};

bool isAsciiUpper(char32_t c) { return c >= U'A' && c <= U'Z'; }
bool isAsciiLower(char32_t c) { return c >= U'a' && c <= U'z'; }

}

std::optional<char> latinKeyForCyrillic(char32_t cyrillic) {
    const char32_t lower = utf8::foldCase(cyrillic);
    for (const auto& [letter, latin] : kRussianJcukenToLatin)
        if (letter == lower) return latin;
    return std::nullopt;
}

std::optional<HotkeyLetter> hotkeyFromTypedCharacter(std::string_view utf8_character) {
    if (utf8_character.empty()) return std::nullopt;
    const auto decoded = utf8::decode(utf8_character, 0);
    if (decoded.length != utf8_character.size()) return std::nullopt;
    const char32_t c = decoded.value;

    if (isAsciiLower(c)) return HotkeyLetter{static_cast<char>(c), false};
    if (isAsciiUpper(c)) return HotkeyLetter{static_cast<char>(c - U'A' + U'a'), true};
    if (c < 0x80) return HotkeyLetter{static_cast<char>(c), false};

    const auto latin = latinKeyForCyrillic(c);
    if (!latin) return std::nullopt;
    const bool shifted = utf8::foldCase(c) != c;
    return HotkeyLetter{*latin, shifted};
}

}
