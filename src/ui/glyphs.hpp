#pragma once

#include "core/file_kind.hpp"

#include <array>
#include <string_view>
#include <vector>

namespace instaseek::ui {

struct Glyphs {
    bool unicode = true;
    std::string_view logo_mark;
    std::string_view logo_block;
    std::string_view prompt;
    std::string_view tab_underline;
    std::string_view vertical_bar;
    std::string_view dot_separator;
    std::string_view option_on;
    std::string_view option_off;
    std::string_view progress_filled;
    std::string_view progress_empty;
    std::string_view ellipsis;
    std::string_view selection_marker;
    std::string_view shield;
    std::string_view warning;
    std::string_view check;
    std::string_view live_dot;
    std::string_view arrows_vertical;
    std::string_view arrows_horizontal;
    std::string_view times;
    std::vector<std::string_view> spinner_frames;
    std::array<std::string_view, core::kFileKindCount> file_icons;

    std::string_view fileIcon(core::FileKind kind) const { return file_icons[static_cast<std::size_t>(kind)]; }
};

inline const Glyphs& unicodeGlyphs() {
    static const Glyphs glyphs{
        true,
        "◆",
        "█",
        "»",
        "━",
        "│",
        "·",
        "●",
        "○",
        "━",
        "─",
        "…",
        "▌",
        "♦",
        "▲",
        "√",
        "●",
        "↑↓",
        "←→",
        "×",
        {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"},
        {"■", "≡", "♦", "☼", "▬", "♫", "◘", "○"},
    };
    return glyphs;
}

inline const Glyphs& asciiGlyphs() {
    static const Glyphs glyphs{
        false,
        "*",
        "#",
        ">",
        "=",
        "|",
        "-",
        "[x]",
        "[ ]",
        "#",
        "-",
        "...",
        ">",
        "!",
        "!",
        "+",
        "*",
        "Up/Dn",
        "Left/Right",
        "x",
        {"|", "/", "-", "\\"},
        {"/", "=", "#", "%", "@", "~", "*", "-"},
    };
    return glyphs;
}

inline const Glyphs& glyphsFor(bool unicode) { return unicode ? unicodeGlyphs() : asciiGlyphs(); }

}
