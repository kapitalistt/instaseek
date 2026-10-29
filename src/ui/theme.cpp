#include "ui/theme.hpp"

namespace instaseek::ui {

ftxui::Color toColor(core::Rgb rgb) { return ftxui::Color::RGB(rgb.r, rgb.g, rgb.b); }

}
