#pragma once

#include "core/file_kind.hpp"
#include "core/palette.hpp"
#include "core/text_matcher.hpp"
#include "ui/glyphs.hpp"

#include <ftxui/dom/elements.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace instaseek::ui::widgets {

struct HintItem {
    std::string key;
    std::string label;
};

struct LineStyle {
    core::Rgb foreground;
    core::Rgb highlight_foreground;
    core::Rgb highlight_background;
};

ftxui::Element coloredText(std::string text, core::Rgb color);
ftxui::Element gradientText(std::string_view text, core::Rgb from, core::Rgb to);
ftxui::Element chip(std::string text, core::Rgb foreground, core::Rgb background);
ftxui::Element keyHint(std::string key, std::string label, const core::Palette& palette);
ftxui::Element hintBar(const std::vector<HintItem>& items, int width, const core::Palette& palette);
ftxui::Element spacer(int columns);

ftxui::Element outline(ftxui::Element content, const Glyphs& glyphs);
ftxui::Element panel(ftxui::Element title, ftxui::Element content, core::Rgb border, const Glyphs& glyphs);

ftxui::Element spinner(double seconds, core::Rgb color, const Glyphs& glyphs);
ftxui::Element progressBar(double fraction, int width, double seconds, const core::Palette& palette,
                           const Glyphs& glyphs);

core::Rgb fileIconColor(core::FileKind kind, const core::Palette& palette);
ftxui::Element fileIcon(core::FileKind kind, const core::Palette& palette, const Glyphs& glyphs);

ftxui::Element styledLine(std::string_view text, const std::vector<core::MatchSpan>& highlights,
                          std::size_t skip_columns, std::size_t width, const LineStyle& style, bool ellipsis,
                          const Glyphs& glyphs);
ftxui::Element fixedText(std::string_view text, std::size_t width, core::Rgb color, bool align_right,
                         const Glyphs& glyphs);

std::vector<core::MatchSpan> findAll(const core::TextMatcher& matcher, std::string_view text, std::size_t limit);
std::vector<core::MatchSpan> findTerms(std::string_view folded_text, const std::vector<std::string>& terms);

std::string repeatGlyph(std::string_view glyph, int count);

}
