#pragma once

#include "core/palette.hpp"
#include "core/text_matcher.hpp"
#include "ui/context_menu.hpp"
#include "ui/glyphs.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace instaseek::ui {

struct FrameContext {
    const core::Palette& palette;
    const Glyphs& glyphs;
    const Motion& motion;
    Clock::time_point now;
    int width = 80;
    int height = 24;
};

enum class ToastKind { Info, Success, Warning, Error };

struct ViewerTarget {
    std::wstring path;
    std::optional<std::uint64_t> line_offset;
    std::optional<std::uint64_t> line_number;
    std::shared_ptr<const core::TextMatcher> highlight;
    bool focus_find = false;
};

class Navigator {
public:
    virtual ~Navigator() = default;
    virtual void openInViewer(ViewerTarget target) = 0;
    virtual void searchContentIn(const std::wstring& location) = 0;
    virtual void showToast(std::string message, ToastKind kind) = 0;
    virtual void openContextMenu(std::vector<MenuItem> items, int x, int y) = 0;
    virtual void copyText(const std::string& text, std::string confirmation) = 0;
    virtual void revealInExplorer(const std::wstring& path) = 0;
    virtual void recordSearchTime(double seconds) = 0;
    virtual void requestRedraw() = 0;
};

class Page {
public:
    virtual ~Page() = default;
    virtual ftxui::Component component() = 0;
    virtual ftxui::Element render(const FrameContext& context) = 0;
    virtual bool onEvent(ftxui::Event& event, Clock::time_point now) = 0;
    virtual void poll(Clock::time_point now) = 0;
    virtual bool animating(Clock::time_point now) const = 0;
    virtual bool busy() const = 0;
    virtual void refreshTexts() = 0;
    virtual std::vector<widgets::HintItem> hints() const = 0;
    virtual void focusPrimary() = 0;
};

}
