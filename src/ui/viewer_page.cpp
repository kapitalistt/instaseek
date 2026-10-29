#include "ui/viewer_page.hpp"

#include "core/easing.hpp"
#include "core/format.hpp"
#include "core/keyboard_layout.hpp"
#include "core/utf8.hpp"
#include "platform/shell.hpp"
#include "platform/win.hpp"
#include "ui/i18n.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/mouse.hpp>

#include <algorithm>
#include <array>
#include <cstdio>

namespace instaseek::ui {

using namespace ftxui;
using widgets::coloredText;
namespace easing = core::easing;

namespace {

constexpr double kFlashSeconds = 1.4;
constexpr int kHorizontalStep = 8;

const Event kCtrlF = Event::Special("\x06");
const Event kCtrlO = Event::Special("\x0F");
const Event kShiftF10 = Event::Special("\x1B[21;2~");
const Event kCtrlG = Event::Special("\x07");
const Event kShiftF3 = Event::Special("\x1B[1;2R");
const Event kShiftF3Alternative = Event::Special("\x1B[25~");

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t");
    return value.substr(begin, end - begin + 1);
}

std::optional<std::uint64_t> parseNumber(std::string_view text, int base) {
    if (text.empty() || text.size() > 20) return std::nullopt;
    std::uint64_t value = 0;
    for (const char c : text) {
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit < 0 || digit >= base) return std::nullopt;
        value = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
    }
    return value;
}

std::string hexOffset(std::uint64_t offset) {
    std::array<char, 24> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "0x%010llX", static_cast<unsigned long long>(offset));
    return buffer.data();
}

std::size_t digits(std::uint64_t value) {
    std::size_t count = 1;
    while (value >= 10) {
        value /= 10;
        ++count;
    }
    return count;
}

}

ViewerPage::ViewerPage(Navigator& navigator, const SharedStyle& style, const Motion& motion, SearchOptionsState& options)
    : navigator_(navigator), style_(style), motion_(motion), options_(options) {
    refreshTexts();
    const auto on_clipboard = [this](bool copied) {
        navigator_.showToast(tr(copied ? strings::toast_copied : strings::toast_copy_failed),
                             copied ? ToastKind::Success : ToastKind::Error);
    };
    const auto field_menu = [this](const std::shared_ptr<TextField>* field) {
        return [this, field](int x, int y) { navigator_.openContextMenu((*field)->menuItems(), x, y); };
    };
    path_field_ = makeTextField(&path_text_, &path_placeholder_, &style_,
                                {nullptr, [this] { openTypedPath(); }, on_clipboard, [this](int x, int y) {
                                     std::vector<MenuItem> items = path_field_->menuItems();
                                     items.push_back({tr(strings::menu_pick_file), "Ctrl+O", [this] { browse(); }, true, true});
                                     navigator_.openContextMenu(std::move(items), x, y);
                                 }});
    find_field_ = makeTextField(&find_text_, &find_placeholder_, &style_,
                                {[this] { updateHighlight(); }, [this] { findNext(false); }, on_clipboard, field_menu(&find_field_)});
    goto_field_ = makeTextField(&goto_text_, &goto_placeholder_, &style_,
                                {nullptr, [this] { submitGoto(); }, on_clipboard, field_menu(&goto_field_)});
    browse_button_ = Make<ButtonControl>([] { return std::string(tr(strings::button_browse)); },
                                         [this](int, int) { browse(); }, &style_);
    anchor_ = Make<FocusAnchor>([this](Event& event) { return onDocumentKey(event); });
    container_ = Container::Vertical({
        Container::Horizontal({path_field_, browse_button_, find_field_}),
        anchor_,
        Maybe(goto_field_, &goto_visible_),
    });
}

void ViewerPage::openTypedPath() {
    if (!openPath(platform::absolutePath(platform::toWide(path_text_)), std::nullopt)) return;
    if (!find_text_.empty()) updateHighlight();
    anchor_->TakeFocus();
}

void ViewerPage::browse() {
    const auto chosen = platform::pickPath(platform::PickKind::File, document_ ? document_->path() : platform::toWide(path_text_));
    navigator_.requestRedraw();
    if (!chosen) return;
    path_text_ = platform::toUtf8(*chosen);
    openTypedPath();
}

std::optional<std::uint64_t> ViewerPage::lineAt(int y) const {
    for (std::size_t i = 0; i < row_boxes_.size() && i < row_offsets_.size(); ++i)
        if (y >= row_boxes_[i].y_min && y <= row_boxes_[i].y_max) return row_offsets_[i];
    return std::nullopt;
}

void ViewerPage::openLineMenu(std::uint64_t line_start, int x, int y) {
    if (!document_) return;
    target_line_ = line_start;
    target_flash_at_ = Clock::time_point{};
    const auto lines = document_->readLines(line_start, 1);
    const std::string text = lines.empty() ? std::string() : lines.front().text;
    const std::string path = document_->displayPath();
    const auto number = document_->lineNumberAt(line_start);
    const std::string location = number ? path + ":" + std::to_string(*number) : path;
    const std::wstring wide = document_->path();

    std::vector<MenuItem> items;
    items.push_back({tr(strings::menu_copy_line), "", [this, text] { navigator_.copyText(text, tr(strings::toast_copied_line)); }});
    items.push_back({tr(strings::menu_copy_location), "", [this, location] { navigator_.copyText(location, tr(strings::toast_copied)); }});
    items.push_back({tr(strings::menu_copy_path), "", [this, path] { navigator_.copyText(path, tr(strings::toast_copied_path)); }});
    items.push_back({tr(strings::menu_find), "Ctrl+F", [this] { focusFind(); }, true, true});
    items.push_back({tr(strings::menu_goto), "g", [this] { openGoto(); }});
    items.push_back({tr(strings::menu_show_explorer), "", [this, wide] { navigator_.revealInExplorer(wide); }, true, true});
    navigator_.openContextMenu(std::move(items), x, y);
}

void ViewerPage::refreshTexts() {
    path_placeholder_ = tr(strings::placeholder_open_file);
    find_placeholder_ = tr(strings::placeholder_find);
    goto_placeholder_ = tr(strings::placeholder_goto);
}

void ViewerPage::focusPrimary() {
    if (document_) anchor_->TakeFocus();
    else path_field_->TakeFocus();
}

std::vector<widgets::HintItem> ViewerPage::hints() const {
    const Glyphs& glyphs = *style_.glyphs;
    if (!document_) return {{"Enter", tr(strings::hint_open)}, {"Ctrl+O", tr(strings::hint_browse)}, {"Ctrl+V", tr(strings::hint_paste)}};
    return {
        {"Ctrl+F", tr(strings::hint_find)},
        {"F3 Shift+F3", tr(strings::hint_next)},
        {"g", tr(strings::hint_goto)},
        {tr(strings::key_right_click), tr(strings::hint_menu)},
        {"Ctrl+O", tr(strings::hint_browse)},
        {std::string(glyphs.arrows_vertical) + " PgUp PgDn", tr(strings::hint_scroll)},
        {"Esc", tr(strings::hint_back)},
    };
}

void ViewerPage::setStatus(std::string message, bool error) {
    status_ = std::move(message);
    status_error_ = error;
}


bool ViewerPage::openPath(const std::wstring& path, std::optional<core::TextEncoding> encoding) {
    if (path.empty()) return false;
    unsigned long error = 0;
    auto opened = platform::Document::open(path, encoding, [this] { navigator_.requestRedraw(); }, error);
    if (!opened) {
        navigator_.showToast(std::string(tr(strings::viewer_open_failed)) + ": " + platform::describeError(error), ToastKind::Error);
        return false;
    }
    runner_.cancel();
    document_ = std::shared_ptr<platform::Document>(std::move(opened));
    path_text_ = document_->displayPath();
    top_ = document_->dataBegin();
    horizontal_ = 0;
    top_line_ = 1;
    cached_top_offset_.reset();
    cached_top_line_.reset();
    target_line_.reset();
    current_match_line_.reset();
    pending_goto_line_.reset();
    pending_match_status_ = false;
    index_was_ready_ = false;
    find_running_ = false;
    setStatus({}, false);
    return true;
}

void ViewerPage::open(const ViewerTarget& target, Clock::time_point now) {
    if (!openPath(target.path, options_.encoding)) return;
    highlight_ = target.highlight;
    if (highlight_) find_text_ = highlight_->options().pattern;
    else if (!find_text_.empty()) updateHighlight();
    if (target.line_offset) {
        current_match_line_ = target.line_offset;
        jumpTo(*target.line_offset, target.line_number, true);
    }
    target_flash_at_ = now;
    if (target.focus_find) focusFind();
    else anchor_->TakeFocus();
}

std::uint64_t ViewerPage::stepBack(std::uint64_t position, std::size_t lines, std::size_t& moved) const {
    moved = 0;
    while (moved < lines && position > document_->dataBegin()) {
        position = document_->linesBefore(position, 1);
        ++moved;
    }
    return position;
}

void ViewerPage::jumpTo(std::uint64_t line_start, std::optional<std::uint64_t> line_number, bool flash) {
    if (!document_) return;
    const auto context_lines = static_cast<std::size_t>(std::max(1, rows_ / 3));
    std::size_t moved = 0;
    top_ = stepBack(line_start, context_lines, moved);
    top_line_.reset();
    if (line_number && *line_number > moved) top_line_ = *line_number - moved;
    target_line_ = line_start;
    if (flash) target_flash_at_ = Clock::now();
}

void ViewerPage::scrollLines(long long delta) {
    if (!document_ || delta == 0) return;
    if (delta > 0) {
        const auto lines = document_->readLines(top_, static_cast<std::size_t>(delta) + 1);
        const std::uint64_t last = document_->lastPageStart(static_cast<std::size_t>(std::max(1, rows_)));
        if (top_ >= last) return;
        const std::size_t moved = std::min<std::size_t>(static_cast<std::size_t>(delta), lines.empty() ? 0 : lines.size() - 1);
        std::uint64_t next = moved > 0 ? lines[moved].offset : top_;
        if (next > last) {
            next = last;
            top_line_.reset();
        } else if (top_line_) {
            top_line_ = *top_line_ + moved;
        }
        top_ = next;
        return;
    }
    std::size_t moved = 0;
    top_ = stepBack(top_, static_cast<std::size_t>(-delta), moved);
    if (top_line_) top_line_ = *top_line_ > moved ? *top_line_ - moved : 1;
    if (top_ == document_->dataBegin()) top_line_ = 1;
}

std::optional<std::uint64_t> ViewerPage::topLineNumber() {
    if (!document_) return std::nullopt;
    if (top_line_) return top_line_;
    if (cached_top_offset_ && *cached_top_offset_ == top_) return cached_top_line_;
    const auto number = document_->lineNumberAt(top_);
    if (number) {
        cached_top_offset_ = top_;
        cached_top_line_ = number;
        top_line_ = number;
    }
    return number;
}

void ViewerPage::focusFind() {
    goto_visible_ = false;
    find_field_->TakeFocus();
    find_field_->selectAll();
}

void ViewerPage::openGoto() {
    if (!document_) return;
    goto_text_.clear();
    goto_visible_ = true;
    goto_field_->TakeFocus();
}

void ViewerPage::closeGoto() {
    goto_visible_ = false;
    if (document_) anchor_->TakeFocus();
    else path_field_->TakeFocus();
}

void ViewerPage::submitGoto() {
    const std::string text = trim(goto_text_);
    closeGoto();
    if (!text.empty() && document_) applyGoto(text);
}

void ViewerPage::applyGoto(const std::string& text) {
    const std::uint64_t begin = document_->dataBegin();
    const std::uint64_t size = document_->size();
    if (text.back() == '%') {
        const auto percent = parseNumber(std::string_view(text).substr(0, text.size() - 1), 10);
        if (!percent || *percent > 100) {
            setStatus(tr(strings::viewer_bad_goto), true);
            return;
        }
        const auto offset = begin + static_cast<std::uint64_t>(static_cast<double>(size - begin) * static_cast<double>(*percent) / 100.0);
        jumpTo(document_->alignToLineStart(offset), std::nullopt, true);
        return;
    }
    std::optional<std::uint64_t> offset;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) offset = parseNumber(std::string_view(text).substr(2), 16);
    else if (text[0] == '@') offset = parseNumber(std::string_view(text).substr(1), 10);
    if (offset) {
        jumpTo(document_->alignToLineStart(std::min(*offset, size)), std::nullopt, true);
        return;
    }
    const auto line = parseNumber(text, 10);
    if (!line || *line == 0) {
        setStatus(tr(strings::viewer_bad_goto), true);
        return;
    }
    const auto line_offset = document_->offsetOfLine(*line);
    if (line_offset) {
        jumpTo(*line_offset, *line, true);
        setStatus({}, false);
    } else {
        pending_goto_line_ = *line;
        setStatus(tr(strings::viewer_goto_pending), false);
    }
}

void ViewerPage::updateHighlight() {
    current_match_line_.reset();
    if (find_text_.empty()) {
        highlight_.reset();
        setStatus({}, false);
        return;
    }
    const core::MatcherOptions matcher_options{find_text_, options_.case_sensitive, options_.whole_word, options_.regex};
    core::TextMatcher::Created created = core::TextMatcher::create(matcher_options);
    if (!created.matcher) {
        highlight_.reset();
        if (created.error == core::MatcherError::InvalidRegex)
            setStatus(std::string(tr(strings::content_invalid_regex)) + (created.detail.empty() ? "" : ": " + created.detail), true);
        return;
    }
    highlight_ = created.matcher;
    setStatus({}, false);
}

void ViewerPage::findNext(bool backward) {
    if (!document_) {
        setStatus(tr(strings::viewer_open_first), true);
        return;
    }
    if (!highlight_) updateHighlight();
    if (!highlight_) {
        setStatus(tr(strings::viewer_find_empty), false);
        focusFind();
        return;
    }
    std::uint64_t from = top_;
    if (current_match_line_) {
        if (backward) {
            from = *current_match_line_;
        } else {
            const auto lines = document_->readLines(*current_match_line_, 2);
            from = lines.size() > 1 ? lines[1].offset : document_->size();
        }
    }
    const std::shared_ptr<platform::Document> document = document_;
    const std::shared_ptr<const core::TextMatcher> matcher = highlight_;
    find_running_ = true;
    find_started_ = Clock::now();
    setStatus(tr(strings::viewer_searching), false);
    runner_.submit([this, document, matcher, from, backward](const std::atomic<bool>& cancelled) {
        const auto match = document->find(*matcher, from, backward, cancelled);
        if (cancelled.load()) return;
        {
            std::lock_guard lock(find_mutex_);
            find_result_ = match;
        }
        navigator_.requestRedraw();
    });
}


void ViewerPage::poll(Clock::time_point now) {
    std::optional<std::optional<platform::DocumentMatch>> result;
    {
        std::lock_guard lock(find_mutex_);
        result.swap(find_result_);
    }
    if (result) {
        find_running_ = false;
        if (*result) {
            current_match_line_ = (*result)->line_offset;
            jumpTo((*result)->line_offset, std::nullopt, true);
            target_flash_at_ = now;
            pending_match_status_ = true;
            setStatus(tr(strings::viewer_match_found), false);
        } else {
            setStatus(tr(strings::viewer_not_found), false);
        }
    }
    if (!document_) return;
    if (pending_match_status_ && current_match_line_) {
        const auto number = document_->lineNumberAt(*current_match_line_);
        if (number) {
            setStatus(std::string(tr(strings::viewer_match_at)) + " " + core::formatCount(*number, thousandsSeparator()), false);
            pending_match_status_ = false;
        }
    }
    if (pending_goto_line_) {
        const auto line_offset = document_->offsetOfLine(*pending_goto_line_);
        if (line_offset) {
            jumpTo(*line_offset, *pending_goto_line_, true);
            pending_goto_line_.reset();
            setStatus({}, false);
        }
    }
    const bool ready = document_->indexReady();
    if (ready && !index_was_ready_) {
        index_was_ready_ = true;
        cached_top_offset_.reset();
    }
}

bool ViewerPage::onDocumentKey(Event& event) {
    if (!document_) return false;
    const auto rows = static_cast<long long>(std::max(1, rows_ - 1));
    if (event == Event::ArrowDown) scrollLines(1);
    else if (event == Event::ArrowUp) scrollLines(-1);
    else if (event == Event::PageDown) scrollLines(rows);
    else if (event == Event::PageUp) scrollLines(-rows);
    else if (event == Event::Home) {
        top_ = document_->dataBegin();
        top_line_ = 1;
    } else if (event == Event::End) {
        top_ = document_->lastPageStart(static_cast<std::size_t>(std::max(1, rows_)));
        top_line_.reset();
    } else if (event == Event::ArrowRight) {
        horizontal_ += kHorizontalStep;
    } else if (event == Event::ArrowLeft) {
        horizontal_ = horizontal_ > static_cast<std::size_t>(kHorizontalStep) ? horizontal_ - kHorizontalStep : 0;
    } else if (event == Event::Escape) {
        path_field_->TakeFocus();
    } else if (event == Event::Return) {
        findNext(false);
    } else if (event.is_character()) {
        if (event.character() == "/") {
            focusFind();
            return true;
        }
        const auto key = core::hotkeyFromTypedCharacter(event.character());
        if (!key) return false;
        if (key->latin == 'g') openGoto();
        else if (key->latin == 'f') focusFind();
        else if (key->latin == 'n') findNext(key->shifted);
        else return false;
    } else {
        return false;
    }
    return true;
}

bool ViewerPage::onEvent(Event& event, Clock::time_point) {
    if (goto_visible_ && event == Event::Escape) {
        closeGoto();
        return true;
    }
    if (event == kCtrlF) {
        focusFind();
        return true;
    }
    if (event == kCtrlG) {
        openGoto();
        return true;
    }
    if (event == Event::F3) {
        findNext(false);
        return true;
    }
    if (event == kShiftF3 || event == kShiftF3Alternative) {
        findNext(true);
        return true;
    }
    if (event == kCtrlO) {
        browse();
        return true;
    }
    if (event == kShiftF10 && document_) {
        const std::uint64_t line = target_line_ ? *target_line_ : top_;
        openLineMenu(line, document_box_.x_min + 8, document_box_.y_min + 1);
        return true;
    }
    if (find_field_->Focused() && event == Event::Escape) {
        anchor_->TakeFocus();
        return true;
    }
    if (event.is_mouse() && document_) {
        Mouse& mouse = event.mouse();
        if (document_box_.Contain(mouse.x, mouse.y)) {
            if (mouse.button == Mouse::WheelDown) {
                scrollLines(3);
                return true;
            }
            if (mouse.button == Mouse::WheelUp) {
                scrollLines(-3);
                return true;
            }
            if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
                anchor_->TakeFocus();
                return true;
            }
            if (mouse.button == Mouse::Right && mouse.motion == Mouse::Pressed) {
                anchor_->TakeFocus();
                const auto line = lineAt(mouse.y);
                if (line) openLineMenu(*line, mouse.x, mouse.y);
                return true;
            }
        }
    }
    return false;
}

bool ViewerPage::animating(Clock::time_point now) const {
    if (find_running_) return true;
    return target_line_ && motion_.elapsed(target_flash_at_, now) < kFlashSeconds;
}

std::string ViewerPage::findLabel() const {
    std::string label = tr(strings::label_find_in_file);
    if (options_.case_sensitive) label += " Aa";
    if (options_.whole_word) label += " " + std::string(tr(strings::option_word));
    if (options_.regex) label += " .*";
    return label;
}

Element ViewerPage::renderTitle(const FrameContext& context) const {
    const core::Palette& palette = context.palette;
    if (!document_) return coloredText(tr(strings::label_document), palette.text_muted);
    const std::string dot = "  " + std::string(context.glyphs.dot_separator) + "  ";
    std::string name = document_->displayPath();
    const auto slash = name.find_last_of('\\');
    if (slash != std::string::npos) name = name.substr(slash + 1);

    Elements title{coloredText(name, palette.accent) | bold};
    title.push_back(coloredText(dot + std::string(core::encodingName(document_->encoding())), palette.text_muted));
    title.push_back(coloredText(dot + core::formatBytes(document_->size()), palette.text_muted));
    const auto count = document_->lineCount();
    if (count) {
        title.push_back(coloredText(dot + core::formatCount(*count, thousandsSeparator()) + " " + tr(strings::viewer_lines),
                                    palette.text_muted));
    } else if (!document_->indexFailed()) {
        const double progress = document_->indexProgress();
        title.push_back(coloredText(dot + tr(strings::viewer_indexing) + " ", palette.text_faint));
        title.push_back(widgets::progressBar(progress, 12, 0.0, palette, context.glyphs));
        title.push_back(coloredText(" " + std::to_string(static_cast<int>(progress * 100.0)) + "%", palette.text_faint));
    }
    if (document_->looksBinary())
        title.push_back(coloredText(dot + std::string(context.glyphs.warning) + " " + tr(strings::viewer_binary), palette.warning));
    return hbox(std::move(title));
}

Element ViewerPage::renderDocument(const FrameContext& context) {
    const core::Palette& palette = context.palette;
    const Glyphs& glyphs = context.glyphs;
    if (!document_) {
        return vbox({filler(), coloredText(tr(strings::empty_viewer_title), palette.text) | bold | hcenter, text(" "),
                     coloredText(tr(strings::empty_viewer_hint), palette.text_faint) | hcenter,
                     coloredText(tr(strings::empty_viewer_keys), palette.text_faint) | hcenter, filler()}) |
               reflect(document_box_);
    }
    if (document_->size() <= document_->dataBegin())
        return vbox({filler(), coloredText(tr(strings::viewer_empty_file), palette.text_muted) | hcenter, filler()}) |
               reflect(document_box_);

    const int box_height = document_box_.y_max - document_box_.y_min + 1;
    rows_ = box_height > 1 ? box_height : std::max(3, context.height - 12);
    const auto lines = document_->readLines(top_, static_cast<std::size_t>(rows_));
    const auto first_line = topLineNumber();

    std::size_t gutter = 12;
    if (first_line) gutter = std::max<std::size_t>(4, digits(*first_line + static_cast<std::uint64_t>(rows_)));
    const int text_width = std::max(10, context.width - 4 - static_cast<int>(gutter) - 3);

    const double flash = easing::outCubic(easing::progress(motion_.elapsed(target_flash_at_, context.now), 0.0, kFlashSeconds));
    const core::Rgb flash_start = core::lerp(palette.accent_soft, palette.accent, 0.35);
    const core::Rgb target_background = core::lerp(flash_start, palette.selection, flash);

    Elements rows;
    row_boxes_.resize(lines.size());
    row_offsets_.resize(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const platform::DocumentLine& line = lines[i];
        row_offsets_[i] = line.offset;
        const bool is_target = target_line_ && *target_line_ == line.offset;
        const std::string gutter_text = first_line ? std::to_string(*first_line + i) : hexOffset(line.offset);
        std::vector<core::MatchSpan> spans;
        if (highlight_) spans = widgets::findAll(*highlight_, line.text, 128);
        Element row = hbox({
            widgets::fixedText(gutter_text, gutter, is_target ? palette.accent : palette.text_faint, true, glyphs),
            coloredText(" " + std::string(glyphs.vertical_bar) + " ", is_target ? palette.accent : palette.border),
            widgets::styledLine(line.text, spans, horizontal_, static_cast<std::size_t>(text_width),
                                {palette.text, palette.match_text, palette.match_background}, false, glyphs),
        });
        if (is_target) row = std::move(row) | bgcolor(toColor(target_background));
        rows.push_back(std::move(row) | reflect(row_boxes_[i]));
    }
    for (std::size_t i = lines.size(); i < static_cast<std::size_t>(rows_); ++i)
        rows.push_back(hbox({text(std::string(gutter, ' ')), coloredText(" ~", palette.text_faint)}));
    return vbox(std::move(rows)) | reflect(document_box_) | flex;
}

Element ViewerPage::renderFooter(const FrameContext& context) {
    const core::Palette& palette = context.palette;
    if (goto_visible_) {
        return hbox({
            coloredText(" " + std::string(tr(strings::label_goto)) + " " + std::string(context.glyphs.prompt) + " ", palette.accent) | bold,
            goto_field_->Render() | flex,
        });
    }
    if (!document_) return text(" ");
    const std::string dot = "  " + std::string(context.glyphs.dot_separator) + "  ";
    Elements parts{text(" ")};
    const auto line = topLineNumber();
    if (line) parts.push_back(coloredText(std::string(tr(strings::viewer_line)) + " " + core::formatCount(*line, thousandsSeparator()), palette.text));
    const std::uint64_t span = document_->size() - document_->dataBegin();
    const double fraction = span == 0 ? 1.0 : static_cast<double>(top_ - document_->dataBegin()) / static_cast<double>(span);
    parts.push_back(coloredText((line ? dot : std::string()) + std::to_string(static_cast<int>(fraction * 100.0)) + "%", palette.text_muted));
    parts.push_back(coloredText(dot + tr(strings::viewer_offset) + " " + hexOffset(top_), palette.text_faint));
    if (horizontal_ > 0) parts.push_back(coloredText(dot + "+" + std::to_string(horizontal_) + " col", palette.text_faint));
    if (find_running_) {
        parts.push_back(text("  "));
        parts.push_back(widgets::spinner(motion_.seconds(find_started_, context.now), palette.accent, context.glyphs));
    }
    if (!status_.empty()) parts.push_back(coloredText(dot + status_, status_error_ ? palette.error : palette.info));
    parts.push_back(filler());
    return hbox(std::move(parts));
}

Element ViewerPage::render(const FrameContext& context) {
    const core::Palette& palette = context.palette;
    const bool document_focused = anchor_->Focused() || goto_visible_;
    const int path_width = std::max(30, (context.width - 2) * 58 / 100);
    Element fields = hbox({
        inputPanel(tr(strings::label_file), path_field_, palette, context.glyphs, browse_button_->Render()) |
            size(WIDTH, EQUAL, path_width),
        inputPanel(findLabel(), find_field_, palette, context.glyphs) | flex,
    });
    Element body = vbox({
        renderDocument(context) | flex,
        separator() | color(toColor(palette.border)),
        renderFooter(context),
    });
    Element document = widgets::panel(renderTitle(context), std::move(body),
                                      document_focused ? palette.border_focus : palette.border, context.glyphs);
    return vbox({std::move(fields), std::move(document) | flex});
}

}
