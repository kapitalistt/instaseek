#include "test_harness.hpp"

#include "core/encoding.hpp"

#include <string>
#include <vector>

using namespace instaseek::core;

namespace {

const std::uint8_t* bytes(const std::string& text) { return reinterpret_cast<const std::uint8_t*>(text.data()); }

std::string encoded(const std::string& utf8, TextEncoding encoding) {
    const auto result = encodeFromUtf8(utf8, encoding);
    return result ? *result : std::string();
}

}

INSTASEEK_TEST(byte_order_marks_are_detected) {
    const std::string utf8 = "\xEF\xBB\xBFhello";
    const EncodingGuess a = detectEncoding(bytes(utf8), utf8.size());
    CHECK(a.encoding == TextEncoding::Utf8 && a.bom_length == 3 && a.from_bom);

    const std::string le = std::string("\xFF\xFE", 2) + encoded("hi", TextEncoding::Utf16Le);
    const EncodingGuess b = detectEncoding(bytes(le), le.size());
    CHECK(b.encoding == TextEncoding::Utf16Le && b.bom_length == 2);

    const std::string be = std::string("\xFE\xFF", 2) + encoded("hi", TextEncoding::Utf16Be);
    const EncodingGuess c = detectEncoding(bytes(be), be.size());
    CHECK(c.encoding == TextEncoding::Utf16Be && c.bom_length == 2);
}

INSTASEEK_TEST(encodings_are_guessed_without_bom) {
    const std::string le = encoded("plain ascii text in utf-16\nsecond line", TextEncoding::Utf16Le);
    CHECK(detectEncoding(bytes(le), le.size()).encoding == TextEncoding::Utf16Le);

    const std::string be = encoded("plain ascii text in utf-16\nsecond line", TextEncoding::Utf16Be);
    CHECK(detectEncoding(bytes(be), be.size()).encoding == TextEncoding::Utf16Be);

    const std::string cyrillic_utf8 = "Привет, мир! Это обычный текст.";
    CHECK(detectEncoding(bytes(cyrillic_utf8), cyrillic_utf8.size()).encoding == TextEncoding::Utf8);

    const std::string cp1251 = encoded("Привет, мир! Это текст в кодировке Windows-1251.", TextEncoding::Windows1251);
    CHECK(detectEncoding(bytes(cp1251), cp1251.size()).encoding == TextEncoding::Windows1251);

    const std::string binary("\x7F" "ELF\x01\x00\x00\x00\x13\x37\x00\xFF\x10\x00\x00\x01", 16);
    CHECK(detectEncoding(bytes(binary), binary.size()).looks_binary);
}

INSTASEEK_TEST(transcoding_round_trips) {
    const std::string sample = "Сводка: 42 €, naïve, 中文 и 😀";
    for (const TextEncoding encoding : {TextEncoding::Utf8, TextEncoding::Utf16Le, TextEncoding::Utf16Be}) {
        const std::string raw = encoded(sample, encoding);
        CHECK(decodeToUtf8(bytes(raw), raw.size(), encoding) == sample);
    }
    const std::string russian = "Съешь же ещё этих мягких французских булок, да выпей чаю. Ёж!";
    const std::string raw1251 = encoded(russian, TextEncoding::Windows1251);
    CHECK(raw1251.size() == 61);
    CHECK(decodeToUtf8(bytes(raw1251), raw1251.size(), TextEncoding::Windows1251) == russian);
    CHECK(!encodeFromUtf8("中", TextEncoding::Windows1251).has_value());
    CHECK(codepointToWindows1251(U'€') == std::uint8_t{0x88});
    CHECK(windows1251ToCodepoint(0xC0) == U'А');
}

INSTASEEK_TEST(invalid_utf8_is_replaced) {
    const std::string broken = "ok\xC3(end";
    CHECK(decodeToUtf8(bytes(broken), broken.size(), TextEncoding::Utf8) == "ok\xEF\xBF\xBD(end");
    const std::string lone_surrogate("\x00\xD8\x41\x00", 4);
    CHECK(decodeToUtf8(bytes(lone_surrogate), lone_surrogate.size(), TextEncoding::Utf16Le) == "\xEF\xBF\xBD" "A");
}

INSTASEEK_TEST(newlines_are_found_per_code_unit) {
    const std::string le = encoded("a\nbc\n\nd", TextEncoding::Utf16Le);
    CHECK(countNewlines(bytes(le), le.size(), TextEncoding::Utf16Le) == 3);
    CHECK(findNewline(bytes(le), le.size(), 0, TextEncoding::Utf16Le) == std::size_t{2});
    const std::string tricky = encoded("਀x", TextEncoding::Utf16Le);
    CHECK(countNewlines(bytes(tricky), tricky.size(), TextEncoding::Utf16Le) == 0);
    const std::string plain = "one\ntwo\nthree";
    CHECK(countNewlines(bytes(plain), plain.size(), TextEncoding::Utf8) == 2);
    CHECK(findNewline(bytes(plain), plain.size(), 4, TextEncoding::Utf8) == std::size_t{7});
}
