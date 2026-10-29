#include "ui/result_list.hpp"

#include "core/easing.hpp"

#include <ftxui/component/mouse.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace instaseek::ui {

using namespace ftxui;
namespace easing = core::easing;

namespace {

constexpr double kAppearSeconds = 0.24;
constexpr double kSelectionSeconds = 0.14;
constexpr double kHoverSeconds = 0.16;
constexpr double kStaggerSeconds = 0.016;
constexpr double kMaxStaggerSeconds = 0.35;
constexpr auto kDoubleClick = std::chrono::milliseconds(450);

Clock::duration secondsToDuration(double seconds) {
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
}

}

void ResultList::replace(std::size_t count, Clock::time_point now) {
    count_ = count;
    selected_ = 0;
    previous_selected_ = 0;
    top_ = 0;
    selection_changed_at_ = Clock::time_point{};
    born_.assign(count, now);
    for (std::size_t i = 0; i < born_.size(); ++i)
        born_[i] = now + secondsToDuration(std::min(kMaxStaggerSeconds, static_cast<double>(i) * kStaggerSeconds));
}

void ResultList::grow(std::size_t count, Clock::time_point now) {
    if (count <= count_) {
        count_ = count;
        born_.resize(count);
        return;
    }
    const std::size_t previous = count_;
    born_.resize(count, now);
    for (std::size_t i = previous; i < count; ++i)
        born_[i] = now + secondsToDuration(std::min(kMaxStaggerSeconds, static_cast<double>(i - previous) * kStaggerSeconds));
    count_ = count;
}

void ResultList::clear() {
    count_ = 0;
    selected_ = 0;
    top_ = 0;
    born_.clear();
    hover_ = -1;
}

std::optional<std::size_t> ResultList::selected() const {
    if (count_ == 0) return std::nullopt;
    return std::min(selected_, count_ - 1);
}

std::optional<std::pair<int, int>> ResultList::selectedAnchor() const {
    if (count_ == 0 || selected_ < top_) return std::nullopt;
    const std::size_t slot = selected_ - top_;
    if (slot >= row_boxes_.size()) return std::nullopt;
    const Box& row = row_boxes_[slot];
    return std::make_pair(row.x_min + 4, row.y_max + 1);
}

void ResultList::select(std::size_t index, Clock::time_point now) {
    if (count_ == 0) return;
    index = std::min(index, count_ - 1);
    if (index != selected_) {
        previous_selected_ = selected_;
        selected_ = index;
        selection_changed_at_ = now;
    }
    ensureVisible();
}

void ResultList::ensureVisible() {
    const auto rows = static_cast<std::size_t>(std::max(1, rows_));
    if (selected_ < top_) top_ = selected_;
    if (selected_ >= top_ + rows) top_ = selected_ - rows + 1;
    const std::size_t max_top = count_ > rows ? count_ - rows : 0;
    top_ = std::min(top_, max_top);
}

void ResultList::setHover(int index, Clock::time_point now) {
    if (index == hover_) return;
    previous_hover_ = hover_;
    hover_ = index;
    hover_changed_at_ = now;
}

ResultList::Action ResultList::onEvent(Event& event, Clock::time_point now) {
    if (event.is_mouse()) {
        const Mouse& mouse = event.mouse();
        if (!box_.Contain(mouse.x, mouse.y)) {
            setHover(-1, now);
            return Action::None;
        }
        int hovered = -1;
        for (std::size_t r = 0; r < row_boxes_.size(); ++r) {
            if (row_boxes_[r].Contain(mouse.x, mouse.y) && top_ + r < count_) hovered = static_cast<int>(top_ + r);
        }
        setHover(hovered, now);
        const auto rows = static_cast<std::size_t>(std::max(1, rows_));
        const std::size_t max_top = count_ > rows ? count_ - rows : 0;
        if (mouse.button == Mouse::WheelUp) {
            top_ = top_ > 3 ? top_ - 3 : 0;
            return Action::Moved;
        }
        if (mouse.button == Mouse::WheelDown) {
            top_ = std::min(max_top, top_ + 3);
            return Action::Moved;
        }
        if (mouse.button == Mouse::Right && mouse.motion == Mouse::Pressed && hovered >= 0) {
            select(static_cast<std::size_t>(hovered), now);
            return Action::Context;
        }
        if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed && hovered >= 0) {
            const auto index = static_cast<std::size_t>(hovered);
            const bool double_click = last_click_index_ && *last_click_index_ == index && now - last_click_at_ < kDoubleClick;
            select(index, now);
            last_click_index_ = index;
            last_click_at_ = now;
            if (double_click) {
                last_click_index_.reset();
                return Action::Activate;
            }
            return Action::Moved;
        }
        return Action::None;
    }

    if (count_ == 0) return Action::None;
    const auto rows = static_cast<std::size_t>(std::max(1, rows_));
    if (event == Event::ArrowDown) {
        select(selected_ + 1, now);
        return Action::Moved;
    }
    if (event == Event::ArrowUp) {
        select(selected_ > 0 ? selected_ - 1 : 0, now);
        return Action::Moved;
    }
    if (event == Event::PageDown) {
        select(selected_ + rows, now);
        return Action::Moved;
    }
    if (event == Event::PageUp) {
        select(selected_ > rows ? selected_ - rows : 0, now);
        return Action::Moved;
    }
    return Action::None;
}

double ResultList::appearAmount(std::size_t index, Clock::time_point now, const Motion& motion) const {
    if (!motion.enabled() || index >= born_.size()) return 1.0;
    if (now < born_[index]) return 0.0;
    const double elapsed = std::chrono::duration<double>(now - born_[index]).count();
    return easing::outCubic(easing::progress(elapsed, 0.0, kAppearSeconds));
}

Element ResultList::render(const FrameContext& context, Element header, const RowRenderer& row) {
    has_header_ = static_cast<bool>(header);
    const int box_height = box_.y_max - box_.y_min + 1;
    rows_ = box_height > 1 ? box_height - (has_header_ ? 1 : 0) : std::max(3, context.height - 14);
    rows_ = std::max(1, rows_);
    ensureVisible();

    const core::Palette& palette = context.palette;
    const double selection_t =
        easing::outCubic(easing::progress(context.motion.elapsed(selection_changed_at_, context.now), 0.0, kSelectionSeconds));
    const double hover_t =
        easing::outCubic(easing::progress(context.motion.elapsed(hover_changed_at_, context.now), 0.0, kHoverSeconds));

    const auto rows = static_cast<std::size_t>(rows_);
    const std::size_t end = std::min(count_, top_ + rows);
    row_boxes_.resize(rows);

    const std::size_t thumb_size = count_ > rows ? std::max<std::size_t>(1, rows * rows / count_) : rows;
    const std::size_t max_top = count_ > rows ? count_ - rows : 0;
    const std::size_t thumb_start = max_top == 0 ? 0 : (rows - thumb_size) * top_ / max_top;
    const bool scrollable = count_ > rows;

    Elements lines;
    if (header) lines.push_back(hbox({std::move(header), text(" ")}));
    for (std::size_t i = top_; i < end; ++i) {
        RowVisual visual;
        visual.selected = i == selected_;
        visual.selection = visual.selected ? selection_t : (i == previous_selected_ ? 1.0 - selection_t : 0.0);
        visual.hover = static_cast<int>(i) == hover_ ? hover_t : (static_cast<int>(i) == previous_hover_ ? 1.0 - hover_t : 0.0);
        visual.appear = appearAmount(i, context.now, context.motion);
        visual.slide = static_cast<int>(std::lround((1.0 - visual.appear) * 4.0));
        visual.palette = core::fadeFromBackground(palette, visual.appear);
        core::Rgb background = core::lerp(palette.background, palette.hover, visual.hover);
        background = core::lerp(background, palette.selection, visual.selection);
        visual.background = core::lerp(palette.background, background, visual.appear);

        const std::size_t slot = i - top_;
        const bool thumb = scrollable && slot >= thumb_start && slot < thumb_start + thumb_size;
        Element bar = scrollable ? widgets::coloredText(std::string(thumb ? context.glyphs.vertical_bar : std::string_view(" ")),
                                                        thumb ? palette.accent : palette.border)
                                 : text(" ");
        lines.push_back(hbox({row(i, visual) | xflex, std::move(bar)}) | bgcolor(toColor(visual.background)) |
                        reflect(row_boxes_[slot]));
    }
    for (std::size_t slot = end - top_; slot < rows; ++slot) row_boxes_[slot] = Box{};
    lines.push_back(filler());
    return vbox(std::move(lines)) | reflect(box_) | flex;
}

bool ResultList::animating(Clock::time_point now, const Motion& motion) const {
    if (!motion.enabled()) return false;
    if (motion.elapsed(selection_changed_at_, now) < kSelectionSeconds) return true;
    if (motion.elapsed(hover_changed_at_, now) < kHoverSeconds) return true;
    const auto rows = static_cast<std::size_t>(std::max(1, rows_));
    const std::size_t end = std::min(born_.size(), top_ + rows);
    for (std::size_t i = top_; i < end; ++i)
        if (now < born_[i] + secondsToDuration(kAppearSeconds)) return true;
    return false;
}

}
