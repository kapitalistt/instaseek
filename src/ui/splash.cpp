#include "ui/splash.hpp"

#include "core/easing.hpp"
#include "core/utf8.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace instaseek::ui {

using namespace ftxui;
namespace easing = core::easing;

namespace {

constexpr int kLetterRows = 5;
constexpr int kCanvasRows = kLetterRows + 1;
constexpr int kLetterGap = 1;
constexpr std::string_view kLogoWord = "INSTASEEK";

constexpr double kLetterStagger = 0.08;
constexpr double kLetterReveal = 0.38;
constexpr double kShineStart = 0.95;
constexpr double kShineDuration = 0.7;
constexpr double kTaglineStart = 0.75;
constexpr double kTaglineDuration = 0.7;
constexpr double kProgressStart = 0.3;
constexpr double kProgressDuration = 1.7;
constexpr double kFadeOutStart = 2.0;
constexpr double kFadeOutDuration = kSplashSeconds - kFadeOutStart;

struct LetterShape {
    char letter;
    std::array<std::string_view, kLetterRows> rows;
};

constexpr std::array<LetterShape, 7> kLogoFont = {{
    {'I', {"###", " # ", " # ", " # ", "###"}},
    {'N', {"#   #", "##  #", "# # #", "#  ##", "#   #"}},
    {'S', {" ###", "#   ", " ## ", "   #", "### "}},
    {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  "}},
    {'A', {" ### ", "#   #", "#####", "#   #", "#   #"}},
    {'E', {"####", "#   ", "### ", "#   ", "####"}},
    {'K', {"#  #", "# # ", "##  ", "# # ", "#  #"}},
}};

const LetterShape& shapeFor(char letter) {
    for (const auto& shape : kLogoFont)
        if (shape.letter == letter) return shape;
    return kLogoFont.front();
}

struct LogoColumn {
    int letter_index = -1;
    int column_in_letter = 0;
};

std::vector<LogoColumn> layoutLogoColumns() {
    std::vector<LogoColumn> columns;
    for (std::size_t i = 0; i < kLogoWord.size(); ++i) {
        if (i > 0)
            for (int gap = 0; gap < kLetterGap; ++gap) columns.push_back({});
        const auto width = static_cast<int>(shapeFor(kLogoWord[i]).rows[0].size());
        for (int column = 0; column < width; ++column) columns.push_back({static_cast<int>(i), column});
    }
    return columns;
}

bool isLetterPixel(const LogoColumn& column, int shape_row) {
    if (column.letter_index < 0 || shape_row < 0 || shape_row >= kLetterRows) return false;
    const auto& rows = shapeFor(kLogoWord[static_cast<std::size_t>(column.letter_index)]).rows;
    const auto row = rows[static_cast<std::size_t>(shape_row)];
    const auto index = static_cast<std::size_t>(column.column_in_letter);
    return index < row.size() && row[index] == '#';
}

Element renderLogo(double seconds, const core::Palette& palette, const Glyphs& glyphs) {
    static const std::vector<LogoColumn> columns = layoutLogoColumns();
    const int width = static_cast<int>(columns.size());
    const double shine_progress = easing::inOutCubic(easing::progress(seconds, kShineStart, kShineDuration));
    const double shine_x = -6.0 + shine_progress * static_cast<double>(width + 12);
    const bool shine_active = seconds >= kShineStart && seconds <= kShineStart + kShineDuration;
    const std::string block(glyphs.logo_block);

    Elements rows;
    rows.reserve(kCanvasRows);
    for (int canvas_row = 0; canvas_row < kCanvasRows; ++canvas_row) {
        Elements cells;
        cells.reserve(static_cast<std::size_t>(width));
        for (int x = 0; x < width; ++x) {
            const LogoColumn& column = columns[static_cast<std::size_t>(x)];
            if (column.letter_index < 0) {
                cells.push_back(text(" "));
                continue;
            }
            const double reveal = easing::progress(seconds, kLetterStagger * column.letter_index, kLetterReveal);
            const int drop = reveal < 0.55 ? 0 : 1;
            if (reveal <= 0.0 || !isLetterPixel(column, canvas_row - drop)) {
                cells.push_back(text(" "));
                continue;
            }
            const double along = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.0;
            core::Rgb rgb = core::lerp(palette.logo_start, palette.logo_end, along);
            if (shine_active) {
                const double band = std::max(0.0, 1.0 - std::abs(static_cast<double>(x) - shine_x) / 3.5);
                rgb = core::lerp(rgb, palette.logo_shine, 0.75 * band);
            }
            rgb = core::lerp(palette.background, rgb, easing::outCubic(reveal));
            cells.push_back(widgets::coloredText(block, rgb));
        }
        rows.push_back(hbox(std::move(cells)));
    }
    return vbox(std::move(rows));
}

}

Element renderSplash(double seconds, const core::Palette& base_palette, const Glyphs& glyphs) {
    const double fade_out = easing::outCubic(easing::progress(seconds, kFadeOutStart, kFadeOutDuration));
    const core::Palette palette = core::fadeFromBackground(base_palette, 1.0 - fade_out);

    const std::string tagline = tr(strings::tagline);
    const auto tagline_length = core::utf8::codepointCount(tagline);
    const double typed = easing::progress(seconds, kTaglineStart, kTaglineDuration);
    const auto typed_count = static_cast<std::size_t>(std::lround(typed * static_cast<double>(tagline_length)));
    std::string visible_tagline = core::utf8::takeCodepoints(tagline, typed_count);
    const std::size_t hidden = tagline_length - std::min(tagline_length, typed_count);
    visible_tagline.append(hidden, ' ');

    const double loading = easing::outCubic(easing::progress(seconds, kProgressStart, kProgressDuration));
    const double hint_opacity = easing::progress(seconds, 1.0, 0.4) * (0.55 + 0.45 * easing::pulse(seconds, 1.6));

    Element loading_row = hbox({
        widgets::spinner(seconds, palette.accent, glyphs),
        widgets::coloredText(" " + std::string(tr(strings::splash_loading)) + "  ", palette.text_muted),
        widgets::progressBar(loading, 28, seconds, palette, glyphs),
    });

    return vbox({
               renderLogo(seconds, palette, glyphs) | hcenter,
               text(" "),
               widgets::coloredText(visible_tagline, palette.text_muted) | hcenter,
               text(" "),
               text(" "),
               std::move(loading_row) | hcenter,
               text(" "),
               widgets::coloredText(tr(strings::splash_skip), core::lerp(palette.background, palette.text_faint, hint_opacity)) |
                   hcenter,
           }) |
           center;
}

}
