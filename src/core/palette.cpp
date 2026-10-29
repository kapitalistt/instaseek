#include "core/palette.hpp"

#include <algorithm>
#include <cmath>

namespace instaseek::core {

namespace {

std::uint8_t mixChannel(std::uint8_t from, std::uint8_t to, double t) {
    const double value = static_cast<double>(from) + (static_cast<double>(to) - static_cast<double>(from)) * t;
    return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

double linearizeChannel(std::uint8_t channel) {
    const double srgb = static_cast<double>(channel) / 255.0;
    return srgb <= 0.04045 ? srgb / 12.92 : std::pow((srgb + 0.055) / 1.055, 2.4);
}

Palette makeBlackPalette() {
    Palette p;
    p.background = {0, 0, 0};
    p.surface = {10, 11, 14};
    p.surface_raised = {20, 22, 28};
    p.text = {230, 235, 242};
    p.text_muted = {150, 160, 176};
    p.text_faint = {104, 112, 128};
    p.border = {46, 50, 62};
    p.border_focus = {94, 177, 255};
    p.accent = {94, 177, 255};
    p.accent_soft = {18, 38, 64};
    p.on_accent = {0, 0, 0};
    p.selection = {26, 46, 76};
    p.hover = {16, 18, 24};
    p.match_background = {92, 68, 6};
    p.match_text = {255, 218, 110};
    p.success = {74, 222, 128};
    p.warning = {251, 191, 36};
    p.error = {248, 113, 113};
    p.info = {125, 211, 252};
    p.progress_track = {32, 36, 46};
    p.logo_start = {94, 177, 255};
    p.logo_end = {196, 132, 252};
    p.logo_shine = {240, 248, 255};
    p.icon_folder = {250, 204, 21};
    p.icon_document = {147, 197, 253};
    p.icon_code = {167, 139, 250};
    p.icon_image = {244, 114, 182};
    p.icon_archive = {251, 146, 60};
    p.icon_media = {52, 211, 153};
    p.icon_binary = {148, 163, 184};
    p.icon_other = {203, 213, 225};
    return p;
}

}

Rgb lerp(Rgb from, Rgb to, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return {mixChannel(from.r, to.r, t), mixChannel(from.g, to.g, t), mixChannel(from.b, to.b, t)};
}

Palette lerp(const Palette& from, const Palette& to, double t) {
    Palette result;
    for (const auto member : kPaletteColors) result.*member = lerp(from.*member, to.*member, t);
    return result;
}

Palette fadeFromBackground(const Palette& palette, double opacity) {
    Palette invisible;
    for (const auto member : kPaletteColors) invisible.*member = palette.background;
    return lerp(invisible, palette, opacity);
}

const Palette& blackPalette() {
    static const Palette palette = makeBlackPalette();
    return palette;
}

double relativeLuminance(Rgb color) {
    return 0.2126 * linearizeChannel(color.r) + 0.7152 * linearizeChannel(color.g) +
           0.0722 * linearizeChannel(color.b);
}

double contrastRatio(Rgb first, Rgb second) {
    const double a = relativeLuminance(first);
    const double b = relativeLuminance(second);
    const double lighter = std::max(a, b);
    const double darker = std::min(a, b);
    return (lighter + 0.05) / (darker + 0.05);
}

}
