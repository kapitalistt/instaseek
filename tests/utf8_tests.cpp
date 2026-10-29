#include "test_harness.hpp"

#include "core/utf8.hpp"

using namespace instaseek::core;

INSTASEEK_TEST(utf8_decodes_multibyte_sequences) {
    const std::string text = "aЖ€😀";
    auto first = utf8::decode(text, 0);
    CHECK(first.value == U'a' && first.length == 1);
    auto second = utf8::decode(text, 1);
    CHECK(second.value == U'Ж' && second.length == 2);
    auto third = utf8::decode(text, 3);
    CHECK(third.value == U'€' && third.length == 3);
    auto fourth = utf8::decode(text, 6);
    CHECK(fourth.value == U'😀' && fourth.length == 4);
}

INSTASEEK_TEST(utf8_rejects_malformed_input) {
    const std::string overlong = "\xC0\xAF";
    CHECK(utf8::decode(overlong, 0).value == utf8::kReplacementCharacter);
    const std::string truncated = "\xE2\x82";
    CHECK(utf8::decode(truncated, 0).value == utf8::kReplacementCharacter);
    CHECK(utf8::decode(truncated, 0).length == 1);
    const std::string surrogate = "\xED\xA0\x80";
    CHECK(utf8::decode(surrogate, 0).value == utf8::kReplacementCharacter);
}

INSTASEEK_TEST(utf8_append_round_trips) {
    std::string out;
    for (const char32_t c : {U'z', U'ё', U'中', U'🙂'}) utf8::append(out, c);
    CHECK(out == "zё中🙂");
}

INSTASEEK_TEST(fold_case_handles_latin_and_cyrillic) {
    CHECK(utf8::foldCase("ReadMe.TXT") == "readme.txt");
    CHECK(utf8::foldCase("ОТЧЁТ Январь") == "отчёт январь");
    CHECK(utf8::foldCase("ÀÉÎÕÜ") == "àéîõü");
    CHECK(utf8::foldCase("ΑΒΓ") == "αβγ");
    CHECK(utf8::foldCase("ЀЉЏ") == "ѐљџ");
}

INSTASEEK_TEST(codepoint_count_and_fit_to_width) {
    CHECK(utf8::codepointCount("Файл.txt") == 8);
    CHECK(utf8::takeCodepoints("Привет", 3) == "При");
    CHECK(utf8::fitToWidth("abc", 5) == "abc  ");
    CHECK(utf8::fitToWidth("Документы", 5) == "Доку…");
    CHECK(utf8::fitToWidth("abc", 0).empty());
}
