#include "test_harness.hpp"

#include "core/keyboard_layout.hpp"

using namespace instaseek::core;

INSTASEEK_TEST(cyrillic_letters_map_to_jcuken_positions) {
    CHECK(latinKeyForCyrillic(U'й') == 'q');
    CHECK(latinKeyForCyrillic(U'т') == 'n');
    CHECK(latinKeyForCyrillic(U'п') == 'g');
    CHECK(latinKeyForCyrillic(U'Т') == 'n');
    CHECK(latinKeyForCyrillic(U'ё') == '`');
    CHECK(!latinKeyForCyrillic(U'ї').has_value());
}

INSTASEEK_TEST(typed_characters_become_hotkeys) {
    CHECK((hotkeyFromTypedCharacter("n") == HotkeyLetter{'n', false}));
    CHECK((hotkeyFromTypedCharacter("N") == HotkeyLetter{'n', true}));
    CHECK((hotkeyFromTypedCharacter("т") == HotkeyLetter{'n', false}));
    CHECK((hotkeyFromTypedCharacter("Т") == HotkeyLetter{'n', true}));
    CHECK((hotkeyFromTypedCharacter("й") == HotkeyLetter{'q', false}));
    CHECK(!hotkeyFromTypedCharacter("").has_value());
    CHECK(!hotkeyFromTypedCharacter("ab").has_value());
    CHECK(!hotkeyFromTypedCharacter("中").has_value());
}
