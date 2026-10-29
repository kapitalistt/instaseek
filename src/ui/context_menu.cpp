#include "ui/context_menu.hpp"

#include "core/easing.hpp"
#include "core/utf8.hpp"
#include "ui/widgets.hpp"

#include <ftxui/component/mouse.hpp>

#include <algorithm>
#include <utility>

namespace instaseek::ui {

using namespace ftxui;

namespace {

constexpr double kAppearSeconds = 0.14;

}

void ContextMenu::open(std::vector<MenuItem> items, int x, int y, Clock::time_point now) {
    items_ = std::move(items);
    open_ = !items_.empty();
    x_ = std::max(0, x);
    y_ = std::max(0, y);
    selected_ = 0;
    while (selected_ < items_.size() && !items_[selected_].enabled) ++selected_;
    opened_at_ = now;
    hover_changed_at_ = now;
    item_boxes_.assign(items_.size(), Box{});
    box_ = Box{};
}

void ContextMenu::close() {
    open_ = false;
    items_.clear();
    item_boxes_.clear();
}

void ContextMenu::move(int direction) {
    if (items_.empty()) return;
    const auto count = static_cast<int>(items_.size());
    int index = static_cast<int>(selected_);
    for (int step = 0; step < count; ++step) {
        index = (index + direction + count) % count;
        if (items_[static_cast<std::size_t>(index)].enabled) {
            selected_ = static_cast<std::size_t>(index);
            return;
        }
    }
}

void ContextMenu::activate(std::size_t index) {
    if (index >= items_.size() || !items_[index].enabled) return;
    std::function<void()> action = items_[index].action;
    close();
    if (action) action();
}

bool ContextMenu::onEvent(Event& event, Clock::time_point now) {
    if (!open_) return false;
    if (event.is_mouse()) {
        const Mouse& mouse = event.mouse();
        for (std::size_t i = 0; i < item_boxes_.size(); ++i) {
            if (item_boxes_[i].Contain(mouse.x, mouse.y) && items_[i].enabled && selected_ != i) {
                selected_ = i;
                hover_changed_at_ = now;
            }
        }
        if (mouse.motion != Mouse::Pressed) return true;
        if (mouse.button == Mouse::Left) {
            if (!box_.Contain(mouse.x, mouse.y)) {
                close();
                return true;
            }
            for (std::size_t i = 0; i < item_boxes_.size(); ++i)
                if (item_boxes_[i].Contain(mouse.x, mouse.y)) activate(i);
            return true;
        }
        if (mouse.button == Mouse::Right && !box_.Contain(mouse.x, mouse.y)) close();
        return true;
    }
    if (event == Event::Escape) close();
    else if (event == Event::ArrowDown || event == Event::Tab) move(1);
    else if (event == Event::ArrowUp || event == Event::TabReverse) move(-1);
    else if (event == Event::Return || event == Event::Character(' ')) activate(selected_);
    return true;
}

int ContextMenu::width() const {
    std::size_t widest = 0;
    for (const MenuItem& item : items_) {
        const std::size_t shortcut = item.shortcut.empty() ? 0 : core::utf8::displayWidth(item.shortcut) + 3;
        widest = std::max(widest, core::utf8::displayWidth(item.label) + shortcut);
    }
    return static_cast<int>(widest) + 6;
}

Element ContextMenu::render(const core::Palette& base_palette, const Glyphs& glyphs, int screen_width, int screen_height,
                            const Motion& motion, Clock::time_point now) {
    if (!open_) return emptyElement();
    const double appear = core::easing::outCubic(core::easing::progress(motion.elapsed(opened_at_, now), 0.0, kAppearSeconds));
    const core::Palette palette = core::fadeFromBackground(base_palette, appear);

    int separators = 0;
    for (const MenuItem& item : items_)
        if (item.separator_before) ++separators;
    const int menu_width = std::min(width(), std::max(10, screen_width - 2));
    const int menu_height = static_cast<int>(items_.size()) + separators + 2;
    const int x = std::clamp(x_, 0, std::max(0, screen_width - menu_width));
    int y = y_;
    if (y + menu_height > screen_height) y = std::max(0, y_ - menu_height + 1);

    Elements rows;
    item_boxes_.resize(items_.size());
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& item = items_[i];
        if (item.separator_before) rows.push_back(separator() | color(toColor(palette.border)));
        const bool selected = i == selected_ && item.enabled;
        const core::Rgb foreground = !item.enabled ? palette.text_faint : (selected ? palette.accent : palette.text);
        const core::Rgb background = selected ? palette.accent_soft : palette.surface_raised;
        Element marker = widgets::coloredText(selected ? std::string(glyphs.selection_marker) : std::string(" "),
                                              palette.accent);
        Element label = widgets::coloredText(" " + item.label, foreground);
        if (selected) label = std::move(label) | bold;
        rows.push_back(hbox({
                           std::move(marker),
                           std::move(label),
                           filler(),
                           widgets::coloredText(item.shortcut.empty() ? std::string() : "   " + item.shortcut, palette.text_faint),
                           text(" "),
                       }) |
                       bgcolor(toColor(background)) | reflect(item_boxes_[i]));
    }

    Element menu = widgets::outline(vbox(std::move(rows)) | bgcolor(toColor(palette.surface_raised)), glyphs) |
                   color(toColor(palette.border_focus)) | bgcolor(toColor(palette.surface_raised)) |
                   size(WIDTH, EQUAL, menu_width) | reflect(box_);
    return vbox({
        emptyElement() | size(HEIGHT, EQUAL, y),
        hbox({emptyElement() | size(WIDTH, EQUAL, x), std::move(menu) | clear_under, filler()}),
        filler(),
    });
}

bool ContextMenu::animating(const Motion& motion, Clock::time_point now) const {
    return open_ && motion.elapsed(opened_at_, now) < kAppearSeconds;
}

}
