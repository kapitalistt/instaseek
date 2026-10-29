#include "test_harness.hpp"

#include "core/palette.hpp"

using namespace instaseek::core;

INSTASEEK_TEST(rgb_lerp_returns_endpoints) {
    const Rgb black{0, 0, 0};
    const Rgb white{255, 255, 255};
    CHECK(lerp(black, white, 0.0) == black);
    CHECK(lerp(black, white, 1.0) == white);
}

INSTASEEK_TEST(rgb_lerp_rounds_midpoint) {
    const Rgb mid = lerp(Rgb{0, 100, 255}, Rgb{255, 200, 0}, 0.5);
    CHECK(mid.r == 128);
    CHECK(mid.g == 150);
    CHECK(mid.b == 128);
}

INSTASEEK_TEST(rgb_lerp_clamps_out_of_range_factor) {
    const Rgb from{10, 20, 30};
    const Rgb to{200, 210, 220};
    CHECK(lerp(from, to, -3.0) == from);
    CHECK(lerp(from, to, 7.5) == to);
}

INSTASEEK_TEST(palette_lerp_interpolates_every_member) {
    const Palette& black = blackPalette();
    const Palette invisible = fadeFromBackground(black, 0.0);
    CHECK(lerp(invisible, black, 0.0).text == invisible.text);
    CHECK(lerp(invisible, black, 1.0).icon_other == black.icon_other);
    const Palette halfway = lerp(invisible, black, 0.5);
    for (const auto member : kPaletteColors) {
        const Rgb expected = lerp(invisible.*member, black.*member, 0.5);
        CHECK(halfway.*member == expected);
    }
}

INSTASEEK_TEST(fade_from_background_keeps_background) {
    const Palette& black = blackPalette();
    const Palette invisible = fadeFromBackground(black, 0.0);
    for (const auto member : kPaletteColors) CHECK(invisible.*member == black.background);
    const Palette opaque = fadeFromBackground(black, 1.0);
    CHECK(opaque.accent == black.accent);
    CHECK(opaque.background == black.background);
    const Palette half = fadeFromBackground(black, 0.5);
    CHECK(half.text == lerp(black.background, black.text, 0.5));
}

INSTASEEK_TEST(background_is_pure_black) {
    const Rgb black{0, 0, 0};
    CHECK(blackPalette().background == black);
}

INSTASEEK_TEST(contrast_ratio_matches_reference_values) {
    CHECK_NEAR(contrastRatio(Rgb{0, 0, 0}, Rgb{255, 255, 255}), 21.0, 0.01);
    CHECK_NEAR(contrastRatio(Rgb{120, 120, 120}, Rgb{120, 120, 120}), 1.0, 1e-9);
}

INSTASEEK_TEST(palette_has_readable_contrast) {
    const Palette& p = blackPalette();
    CHECK(contrastRatio(p.text, p.background) >= 7.0);
    CHECK(contrastRatio(p.text, p.selection) >= 7.0);
    CHECK(contrastRatio(p.text, p.surface_raised) >= 7.0);
    CHECK(contrastRatio(p.text_muted, p.background) >= 4.5);
    CHECK(contrastRatio(p.text_faint, p.background) >= 3.0);
    CHECK(contrastRatio(p.accent, p.background) >= 4.5);
    CHECK(contrastRatio(p.on_accent, p.accent) >= 4.5);
    CHECK(contrastRatio(p.match_text, p.match_background) >= 4.5);
    CHECK(contrastRatio(p.error, p.background) >= 4.5);
    CHECK(contrastRatio(p.success, p.background) >= 4.5);
    CHECK(contrastRatio(p.warning, p.background) >= 4.5);
    for (const Rgb icon : {p.icon_folder, p.icon_document, p.icon_code, p.icon_image, p.icon_archive, p.icon_media,
                           p.icon_binary, p.icon_other})
        CHECK(contrastRatio(icon, p.background) >= 3.0);
}
