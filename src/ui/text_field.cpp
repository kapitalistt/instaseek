#include "ui/text_field.hpp"

#include "core/utf8.hpp"
#include "platform/clipboard.hpp"
#include "ui/i18n.hpp"

#include <ftxui/component/mouse.hpp>

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

namespace instaseek::ui {

using namespace ftxui;

namespace {

const Event kShiftLeft = Event::Special("\x1B[1;2D");
const Event kShiftRight = Event::Special("\x1B[1;2C");
const Event kShiftHome = Event::Special("\x1B[1;2H");
const Event kShiftEnd = Event::Special("\x1B[1;2F");
const Event kCtrlShiftLeft = Event::Special("\x1B[1;6D");
const Event kCtrlShiftRight = Event::Special("\x1B[1;6C");
const Event kHomeAlternative = Event::Special("\x1B[1~");
const Event kEndAlternative = Event::Special("\x1B[4~");
const Event kCtrlA = Event::Special("\x01");
const Event kCtrlV = Event::Special("\x16");
const Event kCtrlX = Event::Special("\x18");
const Event kCtrlU = Event::Special("\x15");
constexpr auto kDoubleClick = std::chrono::milliseconds(450);

struct Glyph {
    std::string text;
    int columns = 1;
};

std::vector<Glyph> splitGlyphs(const std::string& content) {
    std::vector<Glyph> glyphs;
    std::size_t offset = 0;
    while (offset < content.size()) {
        const auto decoded = core::utf8::decode(content, offset);
        glyphs.push_back({content.substr(offset, decoded.length), static_cast<int>(std::max<std::size_t>(1, core::utf8::columnWidth(decoded.value)))});
        offset += decoded.length;
    }
    return glyphs;
}

}

TextField::TextField(std::string* content, std::string* placeholder, const SharedStyle* style, Callbacks callbacks)
    : content_(content), placeholder_(placeholder), style_(style), callbacks_(std::move(callbacks)), editor_(content) {}

std::shared_ptr<TextField> makeTextField(std::string* content, std::string* placeholder, const SharedStyle* style,
                                         TextField::Callbacks callbacks) {
    return std::make_shared<TextField>(content, placeholder, style, std::move(callbacks));
}

void TextField::selectAll() { editor_.selectAll(); }

void TextField::clearText() {
    if (editor_.clear()) changed();
}

std::vector<MenuItem> TextField::menuItems() {
    editor_.sync();
    const bool selection = editor_.hasSelection();
    const bool has_text = !content_->empty();
    std::vector<MenuItem> items;
    items.push_back({tr(strings::menu_paste), "Ctrl+V", [this] { paste(); }, true, false});
    items.push_back({tr(strings::menu_copy), "", [this] { copySelection(false); }, selection, false});
    items.push_back({tr(strings::menu_cut), "Ctrl+X", [this] { copySelection(true); }, selection, false});
    items.push_back({tr(strings::menu_select_all), "Ctrl+A", [this] { selectAll(); }, has_text, true});
    items.push_back({tr(strings::menu_clear), "Ctrl+U", [this] { clearText(); }, has_text, false});
    return items;
}

void TextField::moveCursorToEnd() { editor_.end(false); }

void TextField::changed() {
    if (callbacks_.on_change) callbacks_.on_change();
}

std::size_t TextField::indexAtColumn(int column) const {
    const auto glyphs = splitGlyphs(*content_);
    std::size_t index = std::min(scroll_, glyphs.size());
    int used = 0;
    while (index < glyphs.size() && used + glyphs[index].columns <= column) {
        used += glyphs[index].columns;
        ++index;
    }
    return index;
}

bool TextField::copySelection(bool cut) {
    if (!editor_.hasSelection()) return false;
    const bool copied = platform::copyToClipboard(editor_.selectedText());
    if (callbacks_.on_clipboard) callbacks_.on_clipboard(copied);
    if (cut && copied && editor_.deleteSelection()) changed();
    return true;
}

bool TextField::paste() {
    const auto clipboard = platform::readClipboard();
    if (!clipboard) return true;
    std::string text = *clipboard;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (editor_.insert(text)) changed();
    return true;
}

bool TextField::onMouse(Event& event) {
    Mouse& mouse = event.mouse();
    const bool inside = box_.Contain(mouse.x, mouse.y);
    if (mouse.button == Mouse::Left && mouse.motion == Mouse::Released) {
        const bool was_dragging = dragging_;
        dragging_ = false;
        return was_dragging;
    }
    if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
        if (dragging_) {
            editor_.moveTo(indexAtColumn(mouse.x - box_.x_min), true);
            return true;
        }
        if (!inside) return false;
        TakeFocus();
        const std::size_t index = indexAtColumn(mouse.x - box_.x_min);
        const auto now = Clock::now();
        if (index == last_click_index_ && now - last_click_at_ < kDoubleClick) {
            editor_.selectWordAt(index);
            last_click_at_ = Clock::time_point{};
            return true;
        }
        last_click_index_ = index;
        last_click_at_ = now;
        editor_.moveTo(index, mouse.shift);
        dragging_ = true;
        return true;
    }
    if (mouse.button == Mouse::Right && mouse.motion == Mouse::Pressed && inside) {
        TakeFocus();
        if (callbacks_.on_context) {
            callbacks_.on_context(mouse.x, mouse.y);
            return true;
        }
        return paste();
    }
    return false;
}

bool TextField::onKey(const Event& event) {
    if (event == Event::Return) {
        if (callbacks_.on_enter) callbacks_.on_enter();
        return true;
    }
    if (event == kCtrlX) return copySelection(true);
    if (event == kCtrlV) return paste();
    if (event == kCtrlA) {
        editor_.selectAll();
        return true;
    }
    if (event == kCtrlU) {
        if (editor_.clear()) changed();
        return true;
    }
    if (event == Event::Backspace) {
        if (editor_.backspace()) changed();
        return true;
    }
    if (event == Event::Delete) {
        if (editor_.deleteForward()) changed();
        return true;
    }
    if (event == Event::ArrowLeft) editor_.moveLeft(false);
    else if (event == Event::ArrowRight) editor_.moveRight(false);
    else if (event == kShiftLeft) editor_.moveLeft(true);
    else if (event == kShiftRight) editor_.moveRight(true);
    else if (event == Event::ArrowLeftCtrl) editor_.moveWordLeft(false);
    else if (event == Event::ArrowRightCtrl) editor_.moveWordRight(false);
    else if (event == kCtrlShiftLeft) editor_.moveWordLeft(true);
    else if (event == kCtrlShiftRight) editor_.moveWordRight(true);
    else if (event == Event::Home || event == kHomeAlternative) editor_.home(false);
    else if (event == Event::End || event == kEndAlternative) editor_.end(false);
    else if (event == kShiftHome) editor_.home(true);
    else if (event == kShiftEnd) editor_.end(true);
    else if (event.is_character()) {
        if (editor_.insert(event.character())) changed();
    } else {
        return false;
    }
    return true;
}

bool TextField::OnEvent(Event event) {
    editor_.sync();
    if (event.is_mouse()) return onMouse(event);
    if (!Focused()) return false;
    return onKey(event);
}

Element TextField::Render() {
    editor_.sync();
    const core::Palette& palette = style_->palette;
    const bool focused = Focused();
    const int width = std::max(8, box_.x_max - box_.x_min + 1);
    const auto glyphs = splitGlyphs(*content_);
    const std::size_t cursor = std::min(editor_.cursor(), glyphs.size());

    if (cursor < scroll_) scroll_ = cursor;
    const auto columnsBetween = [&](std::size_t from, std::size_t to) {
        int columns = 0;
        for (std::size_t i = from; i < to && i < glyphs.size(); ++i) columns += glyphs[i].columns;
        return columns;
    };
    while (scroll_ < cursor && columnsBetween(scroll_, cursor) >= width - 1) ++scroll_;
    if (scroll_ > glyphs.size()) scroll_ = glyphs.size();
    while (scroll_ > 0 && columnsBetween(scroll_ - 1, glyphs.size()) + 1 <= width) --scroll_;

    const Color text_color = toColor(palette.text);
    const Color cursor_background = toColor(palette.accent);
    const Color cursor_foreground = toColor(palette.on_accent);
    const Color selection_background = toColor(palette.selection);

    Elements parts;
    if (glyphs.empty()) {
        if (focused) parts.push_back(text(" ") | bgcolor(cursor_background));
        parts.push_back(text(placeholder_ != nullptr ? *placeholder_ : std::string()) | color(toColor(palette.text_faint)));
        return hbox(std::move(parts)) | reflect(box_);
    }

    const auto range = editor_.selection();
    enum class Style { Plain, Selected, Cursor };
    std::string run;
    Style run_style = Style::Plain;
    const auto flush = [&] {
        if (run.empty()) return;
        Element part = text(run);
        switch (run_style) {
            case Style::Plain: part = std::move(part) | color(text_color); break;
            case Style::Selected: part = std::move(part) | color(text_color) | bgcolor(selection_background); break;
            case Style::Cursor: part = std::move(part) | color(cursor_foreground) | bgcolor(cursor_background); break;
        }
        parts.push_back(std::move(part));
        run.clear();
    };

    int used = 0;
    for (std::size_t i = scroll_; i < glyphs.size(); ++i) {
        if (used + glyphs[i].columns > width) break;
        Style style = Style::Plain;
        if (range && i >= range->first && i < range->second) style = Style::Selected;
        if (focused && !range && i == cursor) style = Style::Cursor;
        if (style != run_style) {
            flush();
            run_style = style;
        }
        run += glyphs[i].text;
        used += glyphs[i].columns;
    }
    flush();
    if (focused && cursor >= glyphs.size() && used < width) parts.push_back(text(" ") | bgcolor(cursor_background));
    return hbox(std::move(parts)) | reflect(box_);
}

}
