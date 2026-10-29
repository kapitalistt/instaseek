#pragma once

#include "core/palette.hpp"
#include "ui/glyphs.hpp"

#include <ftxui/dom/elements.hpp>

namespace instaseek::ui {

inline constexpr double kSplashSeconds = 2.3;

ftxui::Element renderSplash(double seconds, const core::Palette& palette, const Glyphs& glyphs);

}
