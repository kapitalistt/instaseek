#pragma once

#include "core/line_editor.hpp"
#include "ui/context_menu.hpp"
#include "ui/controls.hpp"
#include "ui/theme.hpp"

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace instaseek::ui {

class TextField : public ftxui::ComponentBase {
public:
    struct Callbacks {
        std::function<void()> on_change;
        std::function<void()> on_enter;
        std::function<void(bool copied)> on_clipboard;
        std::function<void(int x, int y)> on_context;
    };

    TextField(std::string* content, std::string* placeholder, const SharedStyle* style, Callbacks callbacks);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    bool hasSelection() const { return editor_.hasSelection(); }
    void selectAll();
    void moveCursorToEnd();
    bool copySelection(bool cut);
    bool paste();
    void clearText();
    std::vector<MenuItem> menuItems();

private:
    bool onMouse(ftxui::Event& event);
    bool onKey(const ftxui::Event& event);
    std::size_t indexAtColumn(int column) const;
    void changed();

    std::string* content_;
    std::string* placeholder_;
    const SharedStyle* style_;
    Callbacks callbacks_;
    core::LineEditor editor_;
    std::size_t scroll_ = 0;
    ftxui::Box box_{};
    bool dragging_ = false;
    std::size_t last_click_index_ = 0;
    Clock::time_point last_click_at_{};
};

std::shared_ptr<TextField> makeTextField(std::string* content, std::string* placeholder, const SharedStyle* style,
                                         TextField::Callbacks callbacks);

}
