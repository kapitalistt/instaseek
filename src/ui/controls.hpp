#pragma once

#include "core/palette.hpp"
#include "ui/glyphs.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <functional>
#include <string>

namespace instaseek::ui {

struct SharedStyle {
    core::Palette palette;
    const Glyphs* glyphs = nullptr;
};

ftxui::Element inputPanel(const std::string& label, const ftxui::Component& input, const core::Palette& palette,
                          const Glyphs& glyphs, ftxui::Element trailing = nullptr);

class ChipControl : public ftxui::ComponentBase {
public:
    ChipControl(std::function<std::string()> label, std::function<bool()> active, std::function<void()> activate,
                const SharedStyle* style);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

private:
    std::function<std::string()> label_;
    std::function<bool()> active_;
    std::function<void()> activate_;
    const SharedStyle* style_;
    ftxui::Box box_{};
};

class ButtonControl : public ftxui::ComponentBase {
public:
    ButtonControl(std::function<std::string()> label, std::function<void(int x, int y)> press, const SharedStyle* style);

    ftxui::Element Render() override;
    bool OnEvent(ftxui::Event event) override;
    bool Focusable() const override { return true; }

    void press();

private:
    std::function<std::string()> label_;
    std::function<void(int x, int y)> press_;
    const SharedStyle* style_;
    ftxui::Box box_{};
    bool hovered_ = false;
};

class FocusAnchor : public ftxui::ComponentBase {
public:
    explicit FocusAnchor(std::function<bool(ftxui::Event&)> handler) : handler_(std::move(handler)) {}

    ftxui::Element Render() override { return ftxui::emptyElement(); }
    bool OnEvent(ftxui::Event event) override { return Focused() && handler_ && handler_(event); }
    bool Focusable() const override { return true; }

private:
    std::function<bool(ftxui::Event&)> handler_;
};

}
