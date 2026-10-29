#pragma once

#include "core/palette.hpp"
#include "ui/page.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace instaseek::ui {

struct RowVisual {
    core::Palette palette;
    core::Rgb background;
    double selection = 0.0;
    double hover = 0.0;
    double appear = 1.0;
    int slide = 0;
    bool selected = false;
};

class ResultList {
public:
    enum class Action { None, Moved, Activate, Context };
    using RowRenderer = std::function<ftxui::Element(std::size_t index, const RowVisual& visual)>;

    void replace(std::size_t count, Clock::time_point now);
    void grow(std::size_t count, Clock::time_point now);
    void clear();

    std::size_t count() const { return count_; }
    std::optional<std::size_t> selected() const;
    int visibleRows() const { return rows_; }
    std::optional<std::pair<int, int>> selectedAnchor() const;

    Action onEvent(ftxui::Event& event, Clock::time_point now);
    ftxui::Element render(const FrameContext& context, ftxui::Element header, const RowRenderer& row);
    bool animating(Clock::time_point now, const Motion& motion) const;

    static int rowWidth(const FrameContext& context) { return context.width - 5; }

private:
    void select(std::size_t index, Clock::time_point now);
    void ensureVisible();
    void setHover(int index, Clock::time_point now);
    double appearAmount(std::size_t index, Clock::time_point now, const Motion& motion) const;

    std::size_t count_ = 0;
    std::size_t selected_ = 0;
    std::size_t previous_selected_ = 0;
    std::size_t top_ = 0;
    int rows_ = 10;
    Clock::time_point selection_changed_at_{};
    int hover_ = -1;
    int previous_hover_ = -1;
    Clock::time_point hover_changed_at_{};
    std::vector<Clock::time_point> born_;
    ftxui::Box box_{};
    std::vector<ftxui::Box> row_boxes_;
    bool has_header_ = false;
    std::optional<std::size_t> last_click_index_;
    Clock::time_point last_click_at_{};
};

}
