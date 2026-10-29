#include "ui/controls.hpp"

#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <ftxui/component/mouse.hpp>

#include <utility>

namespace instaseek::ui {

using namespace ftxui;

Element inputPanel(const std::string& label, const Component& input, const core::Palette& palette,
                   const Glyphs& glyphs, Element trailing) {
    const bool focused = input->Focused();
    Elements row;
    row.push_back(widgets::coloredText(" " + std::string(glyphs.prompt) + " ", focused ? palette.accent : palette.text_faint));
    row.push_back(input->Render() | flex);
    if (trailing) row.push_back(std::move(trailing));
    row.push_back(text(" "));
    return widgets::panel(widgets::coloredText(label, focused ? palette.accent : palette.text_muted), hbox(std::move(row)),
                          focused ? palette.border_focus : palette.border, glyphs);
}

ButtonControl::ButtonControl(std::function<std::string()> label, std::function<void(int x, int y)> press,
                             const SharedStyle* style)
    : label_(std::move(label)), press_(std::move(press)), style_(style) {}

void ButtonControl::press() {
    if (press_) press_(box_.x_min, box_.y_max + 1);
}

Element ButtonControl::Render() {
    const core::Palette& palette = style_->palette;
    const bool lit = Focused() || hovered_;
    Element button = text(" " + (label_ ? label_() : std::string()) + " ") |
                     color(toColor(lit ? palette.on_accent : palette.accent)) |
                     bgcolor(toColor(lit ? palette.accent : palette.accent_soft));
    if (lit) button = std::move(button) | bold;
    return std::move(button) | reflect(box_);
}

bool ButtonControl::OnEvent(Event event) {
    if (event.is_mouse()) {
        Mouse& mouse = event.mouse();
        hovered_ = box_.Contain(mouse.x, mouse.y);
        if (hovered_ && mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
            TakeFocus();
            press();
            return true;
        }
        return false;
    }
    if (!Focused()) return false;
    if (event == Event::Return || event == Event::Character(' ')) {
        press();
        return true;
    }
    return false;
}

ChipControl::ChipControl(std::function<std::string()> label, std::function<bool()> active,
                         std::function<void()> activate, const SharedStyle* style)
    : label_(std::move(label)), active_(std::move(active)), activate_(std::move(activate)), style_(style) {}

Element ChipControl::Render() {
    const core::Palette& palette = style_->palette;
    const Glyphs& glyphs = *style_->glyphs;
    const bool on = active_ && active_();
    const bool focused = Focused();
    const std::string marker(on ? glyphs.option_on : glyphs.option_off);
    const core::Rgb foreground = on ? palette.accent : (focused ? palette.text : palette.text_muted);
    const core::Rgb background = focused ? palette.accent_soft : (on ? palette.accent_soft : palette.surface_raised);
    Element chip = text(" " + marker + " " + (label_ ? label_() : std::string()) + " ") | color(toColor(foreground)) |
                   bgcolor(toColor(background));
    if (focused) chip = std::move(chip) | bold;
    return hbox({std::move(chip) | reflect(box_), text(" ")});
}

bool ChipControl::OnEvent(Event event) {
    if (event.is_mouse()) {
        Mouse& mouse = event.mouse();
        if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed && box_.Contain(mouse.x, mouse.y)) {
            TakeFocus();
            if (activate_) activate_();
            return true;
        }
        return false;
    }
    if (!Focused()) return false;
    if (event == Event::Character(' ') || event == Event::Return) {
        if (activate_) activate_();
        return true;
    }
    return false;
}

}
