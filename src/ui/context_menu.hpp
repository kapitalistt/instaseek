#pragma once

#include "core/palette.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <functional>
#include <string>
#include <vector>

namespace instaseek::ui {

struct MenuItem {
    std::string label;
    std::string shortcut;
    std::function<void()> action;
    bool enabled = true;
    bool separator_before = false;
};

class ContextMenu {
public:
    void open(std::vector<MenuItem> items, int x, int y, Clock::time_point now);
    void close();
    bool isOpen() const { return open_; }

    bool onEvent(ftxui::Event& event, Clock::time_point now);
    ftxui::Element render(const core::Palette& palette, const Glyphs& glyphs, int screen_width, int screen_height,
                          const Motion& motion, Clock::time_point now);
    bool animating(const Motion& motion, Clock::time_point now) const;

private:
    void move(int direction);
    void activate(std::size_t index);
    int width() const;

    std::vector<MenuItem> items_;
    bool open_ = false;
    int x_ = 0;
    int y_ = 0;
    std::size_t selected_ = 0;
    Clock::time_point opened_at_{};
    Clock::time_point hover_changed_at_{};
    ftxui::Box box_{};
    std::vector<ftxui::Box> item_boxes_;
};

}
