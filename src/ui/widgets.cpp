#include "ui/widgets.hpp"

#include "core/easing.hpp"
#include "core/utf8.hpp"
#include "ui/theme.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace instaseek::ui::widgets {

using namespace ftxui;

namespace {

double gradientPosition(int index, int count) {
    return count > 1 ? static_cast<double>(index) / static_cast<double>(count - 1) : 0.0;
}

struct ColumnUnit {
    std::string glyph;
    std::size_t columns = 1;
};

void unitsForCodepoint(std::string_view source, char32_t codepoint, std::vector<ColumnUnit>& units) {
    units.clear();
    if (codepoint == U'\t') {
        for (int i = 0; i < 4; ++i) units.push_back({" ", 1});
        return;
    }
    if (codepoint < 0x20 || codepoint == 0x7F || (codepoint >= 0x80 && codepoint < 0xA0)) {
        units.push_back({" ", 1});
        return;
    }
    const std::size_t width = core::utf8::columnWidth(codepoint);
    if (width == 0) {
        units.push_back({std::string(source), 0});
        return;
    }
    units.push_back({std::string(source), width});
}

std::size_t columnsOf(std::string_view content) {
    std::size_t columns = 0;
    std::size_t offset = 0;
    std::vector<ColumnUnit> units;
    while (offset < content.size()) {
        const auto decoded = core::utf8::decode(content, offset);
        unitsForCodepoint(content.substr(offset, decoded.length), decoded.value, units);
        for (const ColumnUnit& unit : units) columns += unit.columns;
        offset += decoded.length;
    }
    return columns;
}

}

std::string repeatGlyph(std::string_view glyph, int count) {
    std::string out;
    if (count <= 0) return out;
    out.reserve(glyph.size() * static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) out.append(glyph);
    return out;
}

Element coloredText(std::string content, core::Rgb rgb) { return text(std::move(content)) | color(toColor(rgb)); }

Element spacer(int columns) { return emptyElement() | size(WIDTH, EQUAL, std::max(0, columns)); }

Element gradientText(std::string_view content, core::Rgb from, core::Rgb to) {
    const int count = static_cast<int>(core::utf8::codepointCount(content));
    Elements cells;
    cells.reserve(static_cast<std::size_t>(count));
    std::size_t offset = 0;
    int index = 0;
    while (offset < content.size()) {
        const auto decoded = core::utf8::decode(content, offset);
        std::string glyph(content.substr(offset, decoded.length));
        cells.push_back(coloredText(std::move(glyph), core::lerp(from, to, gradientPosition(index, count))));
        offset += decoded.length;
        ++index;
    }
    return hbox(std::move(cells));
}

Element chip(std::string content, core::Rgb foreground, core::Rgb background) {
    return text(" " + std::move(content) + " ") | color(toColor(foreground)) | bgcolor(toColor(background));
}

Element keyHint(std::string key, std::string label, const core::Palette& palette) {
    return hbox({
        chip(std::move(key), palette.accent, palette.accent_soft) | bold,
        coloredText(" " + std::move(label) + "  ", palette.text_muted),
    });
}

Element hintBar(const std::vector<HintItem>& items, int width, const core::Palette& palette) {
    Elements parts;
    parts.push_back(text(" "));
    int used = 1;
    for (const HintItem& item : items) {
        const int item_width = static_cast<int>(core::utf8::displayWidth(item.key) + core::utf8::displayWidth(item.label)) + 5;
        if (used + item_width > width) break;
        used += item_width;
        parts.push_back(keyHint(item.key, item.label, palette));
    }
    parts.push_back(filler());
    return hbox(std::move(parts));
}

Element outline(Element content, const Glyphs& glyphs) {
    return glyphs.unicode ? borderRounded(std::move(content)) : border(std::move(content));
}

Element panel(Element title, Element content, core::Rgb border_color, const Glyphs& glyphs) {
    Element body = outline(std::move(content), glyphs) | color(toColor(border_color));
    Element title_row = hbox({
        spacer(2),
        text(" "),
        std::move(title),
        text(" "),
        filler(),
    });
    return dbox({std::move(body), vbox({std::move(title_row), filler()})});
}

Element spinner(double seconds, core::Rgb rgb, const Glyphs& glyphs) {
    const auto& frames = glyphs.spinner_frames;
    if (frames.empty()) return emptyElement();
    const auto frame_count = static_cast<long long>(frames.size());
    const auto index =
        static_cast<std::size_t>(static_cast<long long>(std::floor(std::max(0.0, seconds) * 12.0)) % frame_count);
    return coloredText(std::string(frames[index]), rgb);
}

Element progressBar(double fraction, int width, double seconds, const core::Palette& palette, const Glyphs& glyphs) {
    width = std::max(1, width);
    Elements cells;
    cells.reserve(static_cast<std::size_t>(width));
    const std::string filled_glyph(glyphs.progress_filled);
    const std::string empty_glyph(glyphs.progress_empty);

    if (fraction < 0.0) {
        const double head = core::easing::pulse(seconds, 2.4) * static_cast<double>(width - 1);
        for (int i = 0; i < width; ++i) {
            const double glow = std::max(0.0, 1.0 - std::abs(static_cast<double>(i) - head) / 6.0);
            if (glow <= 0.05) {
                cells.push_back(coloredText(empty_glyph, palette.progress_track));
                continue;
            }
            const core::Rgb lit = core::lerp(palette.accent, palette.logo_end, gradientPosition(i, width));
            cells.push_back(coloredText(filled_glyph, core::lerp(palette.progress_track, lit, 0.25 + 0.75 * glow)));
        }
        return hbox(std::move(cells));
    }

    const int filled = static_cast<int>(std::floor(core::easing::clamp01(fraction) * static_cast<double>(width)));
    const double sweep = std::fmod(std::max(0.0, seconds) * 0.9, 1.6) - 0.3;
    const double shine_center = sweep * static_cast<double>(std::max(1, filled));
    for (int i = 0; i < width; ++i) {
        if (i >= filled) {
            cells.push_back(coloredText(empty_glyph, palette.progress_track));
            continue;
        }
        const core::Rgb base = core::lerp(palette.accent, palette.logo_end, gradientPosition(i, width));
        const double band = std::max(0.0, 1.0 - std::abs(static_cast<double>(i) - shine_center) / 4.0);
        cells.push_back(coloredText(filled_glyph, core::lerp(base, palette.logo_shine, 0.45 * band)));
    }
    return hbox(std::move(cells));
}

core::Rgb fileIconColor(core::FileKind kind, const core::Palette& palette) {
    switch (kind) {
        case core::FileKind::Folder: return palette.icon_folder;
        case core::FileKind::Document: return palette.icon_document;
        case core::FileKind::Code: return palette.icon_code;
        case core::FileKind::Image: return palette.icon_image;
        case core::FileKind::Archive: return palette.icon_archive;
        case core::FileKind::Media: return palette.icon_media;
        case core::FileKind::Binary: return palette.icon_binary;
        case core::FileKind::Other: break;
    }
    return palette.icon_other;
}

Element fileIcon(core::FileKind kind, const core::Palette& palette, const Glyphs& glyphs) {
    return coloredText(std::string(glyphs.fileIcon(kind)), fileIconColor(kind, palette));
}

Element styledLine(std::string_view content, const std::vector<core::MatchSpan>& highlights, std::size_t skip_columns,
                   std::size_t width, const LineStyle& style, bool ellipsis, const Glyphs& glyphs) {
    Elements parts;
    std::string current;
    int current_style = -1;
    const auto flush = [&] {
        if (current.empty()) return;
        Element part = text(current);
        if (current_style == 1)
            part = std::move(part) | color(toColor(style.highlight_foreground)) | bgcolor(toColor(style.highlight_background));
        else
            part = std::move(part) | color(toColor(style.foreground));
        parts.push_back(std::move(part));
        current.clear();
    };

    const std::size_t total_width = columnsOf(content);
    const bool needs_ellipsis = ellipsis && total_width > skip_columns + width && width > 0;
    const std::size_t budget = needs_ellipsis ? width - 1 : width;

    std::size_t column = 0;
    std::size_t emitted = 0;
    std::size_t highlight = 0;
    std::size_t offset = 0;
    std::vector<ColumnUnit> units;
    bool full = false;
    while (offset < content.size() && !full) {
        const auto decoded = core::utf8::decode(content, offset);
        while (highlight < highlights.size() && highlights[highlight].start + highlights[highlight].length <= offset)
            ++highlight;
        const bool highlighted = highlight < highlights.size() && highlights[highlight].length > 0 &&
                                 offset >= highlights[highlight].start;
        unitsForCodepoint(content.substr(offset, decoded.length), decoded.value, units);
        for (const ColumnUnit& unit : units) {
            if (column < skip_columns) {
                column += unit.columns;
                continue;
            }
            if (emitted + unit.columns > budget) {
                full = true;
                break;
            }
            const int unit_style = highlighted ? 1 : 0;
            if (unit_style != current_style) {
                flush();
                current_style = unit_style;
            }
            current += unit.glyph;
            emitted += unit.columns;
            column += unit.columns;
        }
        offset += decoded.length;
    }
    flush();
    if (needs_ellipsis) {
        parts.push_back(coloredText(std::string(glyphs.unicode ? std::string_view("…") : std::string_view(">")),
                                    style.foreground));
        ++emitted;
    }
    if (emitted < width) parts.push_back(text(std::string(width - emitted, ' ')));
    return hbox(std::move(parts));
}

Element fixedText(std::string_view content, std::size_t width, core::Rgb rgb, bool align_right, const Glyphs& glyphs) {
    const std::size_t text_width = columnsOf(content);
    if (align_right && text_width < width) {
        return hbox({
            text(std::string(width - text_width, ' ')),
            coloredText(std::string(content), rgb),
        });
    }
    return styledLine(content, {}, 0, width, LineStyle{rgb, rgb, rgb}, true, glyphs);
}

std::vector<core::MatchSpan> findAll(const core::TextMatcher& matcher, std::string_view content, std::size_t limit) {
    std::vector<core::MatchSpan> spans;
    std::size_t position = 0;
    while (position <= content.size() && spans.size() < limit) {
        const auto match = matcher.find(content, position);
        if (!match) break;
        if (match->length > 0) spans.push_back(*match);
        const std::size_t step = match->length > 0 ? match->length : 1;
        position = match->start + step;
    }
    return spans;
}

std::vector<core::MatchSpan> findTerms(std::string_view folded_text, const std::vector<std::string>& terms) {
    std::vector<core::MatchSpan> spans;
    for (const std::string& term : terms) {
        if (term.empty()) continue;
        std::size_t position = folded_text.find(term);
        while (position != std::string_view::npos) {
            spans.push_back({position, term.size()});
            position = folded_text.find(term, position + term.size());
        }
    }
    std::sort(spans.begin(), spans.end(), [](const core::MatchSpan& a, const core::MatchSpan& b) { return a.start < b.start; });
    std::vector<core::MatchSpan> merged;
    for (const core::MatchSpan& span : spans) {
        if (!merged.empty() && span.start <= merged.back().start + merged.back().length) {
            const std::size_t end = std::max(merged.back().start + merged.back().length, span.start + span.length);
            merged.back().length = end - merged.back().start;
        } else {
            merged.push_back(span);
        }
    }
    return merged;
}

}
