#pragma once

#include <optional>
#include <string_view>

namespace instaseek::core {

struct HotkeyLetter {
    char latin = 0;
    bool shifted = false;

    friend constexpr bool operator==(const HotkeyLetter&, const HotkeyLetter&) = default;
};

std::optional<char> latinKeyForCyrillic(char32_t cyrillic);

std::optional<HotkeyLetter> hotkeyFromTypedCharacter(std::string_view utf8_character);

}
