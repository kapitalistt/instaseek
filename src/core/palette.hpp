#pragma once

#include <array>
#include <cstdint>

namespace instaseek::core {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend constexpr bool operator==(const Rgb&, const Rgb&) = default;
};

struct Palette {
    Rgb background;
    Rgb surface;
    Rgb surface_raised;
    Rgb text;
    Rgb text_muted;
    Rgb text_faint;
    Rgb border;
    Rgb border_focus;
    Rgb accent;
    Rgb accent_soft;
    Rgb on_accent;
    Rgb selection;
    Rgb hover;
    Rgb match_background;
    Rgb match_text;
    Rgb success;
    Rgb warning;
    Rgb error;
    Rgb info;
    Rgb progress_track;
    Rgb logo_start;
    Rgb logo_end;
    Rgb logo_shine;
    Rgb icon_folder;
    Rgb icon_document;
    Rgb icon_code;
    Rgb icon_image;
    Rgb icon_archive;
    Rgb icon_media;
    Rgb icon_binary;
    Rgb icon_other;
};

inline constexpr std::array<Rgb Palette::*, 31> kPaletteColors = {
    &Palette::background,     &Palette::surface,          &Palette::surface_raised, &Palette::text,
    &Palette::text_muted,     &Palette::text_faint,       &Palette::border,         &Palette::border_focus,
    &Palette::accent,         &Palette::accent_soft,      &Palette::on_accent,      &Palette::selection,
    &Palette::hover,          &Palette::match_background, &Palette::match_text,     &Palette::success,
    &Palette::warning,        &Palette::error,            &Palette::info,           &Palette::progress_track,
    &Palette::logo_start,     &Palette::logo_end,         &Palette::logo_shine,     &Palette::icon_folder,
    &Palette::icon_document,  &Palette::icon_code,        &Palette::icon_image,     &Palette::icon_archive,
    &Palette::icon_media,     &Palette::icon_binary,      &Palette::icon_other,
};

static_assert(sizeof(Palette) == kPaletteColors.size() * sizeof(Rgb),
              "every Palette member must be listed in kPaletteColors");

Rgb lerp(Rgb from, Rgb to, double t);
Palette lerp(const Palette& from, const Palette& to, double t);
Palette fadeFromBackground(const Palette& palette, double opacity);

const Palette& blackPalette();

double relativeLuminance(Rgb color);
double contrastRatio(Rgb first, Rgb second);

}
