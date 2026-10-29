#include "ui/content_page.hpp"

#include "core/format.hpp"
#include "core/utf8.hpp"
#include "platform/win.hpp"
#include "ui/i18n.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/mouse.hpp>

#include <algorithm>
#include <cmath>

namespace instaseek::ui {

using namespace ftxui;
using widgets::coloredText;

namespace {

constexpr std::size_t kMaxHits = 10'000;
constexpr int kLineColumn = 9;

const Event kCtrlF = Event::Special("\x06");
const Event kCtrlO = Event::Special("\x0F");
const Event kShiftF10 = Event::Special("\x1B[21;2~");

std::string stripPrefix(const std::string& path, const std::string& base) {
    if (base.empty() || path.size() <= base.size() || path.compare(0, base.size(), base) != 0) return path;
    std::size_t start = base.size();
    while (start < path.size() && path[start] == '\\') ++start;
    return path.substr(start);
}

}

ContentPage::ContentPage(Navigator& navigator, const SharedStyle& style, const Motion& motion,
                         SearchOptionsState& options)
    : navigator_(navigator), style_(style), motion_(motion), options_(options) {
    refreshTexts();
    const auto on_enter = [this] {
        if (!dirty_ && session_ && list_.selected()) openSelected(false);
        else startSearch(Clock::now());
    };
    const auto on_clipboard = [this](bool copied) {
        navigator_.showToast(tr(copied ? strings::toast_copied : strings::toast_copy_failed),
                             copied ? ToastKind::Success : ToastKind::Error);
    };
    pattern_input_ = makeTextField(&pattern_, &pattern_placeholder_, &style_,
                                   {[this] { markDirty(); }, on_enter, on_clipboard,
                                    [this](int x, int y) { navigator_.openContextMenu(pattern_input_->menuItems(), x, y); }});
    location_input_ = makeTextField(&location_, &location_placeholder_, &style_,
                                    {[this] { markDirty(); }, on_enter, on_clipboard, [this](int x, int y) {
                                         std::vector<MenuItem> items = location_input_->menuItems();
                                         items.push_back({tr(strings::menu_pick_file), "", [this] { browse(platform::PickKind::File); }, true, true});
                                         items.push_back({tr(strings::menu_pick_folder), "", [this] { browse(platform::PickKind::Folder); }});
                                         navigator_.openContextMenu(std::move(items), x, y);
                                     }});
    browse_button_ = Make<ButtonControl>([] { return std::string(tr(strings::button_browse)); },
                                         [this](int x, int y) { openBrowseMenu(x, y); }, &style_);

    const auto chip = [this](std::function<std::string()> label, std::function<bool()> active, std::function<void()> activate) {
        return Make<ChipControl>(std::move(label), std::move(active), std::move(activate), &style_);
    };
    options_row_ = Container::Horizontal({
        chip([] { return std::string(tr(strings::option_case)); }, [this] { return options_.case_sensitive; },
             [this] {
                 options_.case_sensitive = !options_.case_sensitive;
                 markDirty();
             }),
        chip([] { return std::string(tr(strings::option_word)); }, [this] { return options_.whole_word; },
             [this] {
                 options_.whole_word = !options_.whole_word;
                 markDirty();
             }),
        chip([] { return std::string(tr(strings::option_regex)); }, [this] { return options_.regex; },
             [this] {
                 options_.regex = !options_.regex;
                 markDirty();
             }),
        chip([] { return std::string(tr(strings::option_binary)); }, [this] { return options_.skip_binary; },
             [this] {
                 options_.skip_binary = !options_.skip_binary;
                 markDirty();
             }),
        chip([this] { return encodingLabel(); }, [this] { return options_.encoding.has_value(); },
             [this] {
                 cycleEncoding();
                 markDirty();
             }),
    });
    container_ = Container::Vertical({pattern_input_, Container::Horizontal({location_input_, browse_button_}), options_row_});
}

void ContentPage::openBrowseMenu(int x, int y) {
    std::vector<MenuItem> items;
    items.push_back({tr(strings::menu_pick_file), "", [this] { browse(platform::PickKind::File); }});
    items.push_back({tr(strings::menu_pick_folder), "", [this] { browse(platform::PickKind::Folder); }});
    navigator_.openContextMenu(std::move(items), x, y);
}

void ContentPage::browse(platform::PickKind kind) {
    const auto chosen = platform::pickPath(kind, platform::toWide(location_));
    navigator_.requestRedraw();
    if (!chosen) return;
    location_ = platform::toUtf8(*chosen);
    markDirty();
    if (pattern_.empty()) {
        pattern_input_->TakeFocus();
        return;
    }
    startSearch(Clock::now());
}

void ContentPage::openRowMenu(int x, int y) {
    if (!session_) return;
    const auto selected = list_.selected();
    if (!selected) return;
    const auto row = session_->row(*selected);
    if (!row) return;
    const auto file = session_->file(row->file);
    if (!file) return;
    const std::string path = file->display;
    const std::string line_text = row->hit.preview;
    const std::string location = path + ":" + std::to_string(row->hit.line_number);
    const std::wstring wide = file->path;

    std::vector<MenuItem> items;
    items.push_back({tr(strings::menu_open_at_line), "Enter", [this] { openSelected(false); }});
    items.push_back({tr(strings::menu_find_in_file), "Ctrl+F", [this] { openSelected(true); }});
    items.push_back({tr(strings::menu_copy_line), "", [this, line_text] { navigator_.copyText(line_text, tr(strings::toast_copied_line)); }, true, true});
    items.push_back({tr(strings::menu_copy_location), "", [this, location] { navigator_.copyText(location, tr(strings::toast_copied)); }});
    items.push_back({tr(strings::menu_copy_path), "", [this, path] { navigator_.copyText(path, tr(strings::toast_copied_path)); }});
    items.push_back({tr(strings::menu_show_explorer), "", [this, wide] { navigator_.revealInExplorer(wide); }, true, true});
    navigator_.openContextMenu(std::move(items), x, y);
}

void ContentPage::refreshTexts() {
    pattern_placeholder_ = tr(strings::placeholder_pattern);
    location_placeholder_ = tr(strings::placeholder_location);
}

void ContentPage::focusPrimary() { pattern_input_->TakeFocus(); }

void ContentPage::setLocation(const std::wstring& location) {
    location_ = platform::toUtf8(location);
    markDirty();
    pattern_input_->TakeFocus();
}

bool ContentPage::busy() const { return session_ && !session_->finished.load(); }

std::vector<widgets::HintItem> ContentPage::hints() const {
    const Glyphs& glyphs = *style_.glyphs;
    return {
        {"Enter", std::string(tr(strings::hint_search)) + "/" + tr(strings::hint_open)},
        {"Esc", tr(strings::hint_cancel)},
        {tr(strings::key_right_click), tr(strings::hint_menu)},
        {"Ctrl+O", tr(strings::hint_browse)},
        {"Tab", tr(strings::hint_options)},
        {"Space", tr(strings::hint_toggle)},
        {std::string(glyphs.arrows_vertical), tr(strings::hint_select)},
    };
}

std::string ContentPage::encodingLabel() const {
    const std::string value = options_.encoding ? std::string(core::encodingName(*options_.encoding)) : tr(strings::option_auto);
    return std::string(tr(strings::option_encoding)) + ": " + value;
}

void ContentPage::cycleEncoding() {
    if (!options_.encoding) {
        options_.encoding = core::kAllEncodings.front();
        return;
    }
    const auto it = std::find(core::kAllEncodings.begin(), core::kAllEncodings.end(), *options_.encoding);
    if (it == core::kAllEncodings.end() || it + 1 == core::kAllEncodings.end()) options_.encoding.reset();
    else options_.encoding = *(it + 1);
}

void ContentPage::startSearch(Clock::time_point now) {
    message_.clear();
    message_is_error_ = false;
    if (pattern_.empty()) {
        message_ = tr(strings::content_need_pattern);
        return;
    }
    if (location_.empty()) {
        message_ = tr(strings::content_need_location);
        return;
    }
    const core::MatcherOptions matcher_options{pattern_, options_.case_sensitive, options_.whole_word, options_.regex};
    core::TextMatcher::Created created = core::TextMatcher::create(matcher_options);
    if (!created.matcher) {
        message_is_error_ = true;
        if (created.error == core::MatcherError::MultilinePattern) message_ = tr(strings::content_multiline);
        else if (created.error == core::MatcherError::InvalidRegex)
            message_ = std::string(tr(strings::content_invalid_regex)) + ": " + created.detail;
        else message_ = tr(strings::content_need_pattern);
        return;
    }

    runner_.cancel();
    matcher_ = created.matcher;
    auto session = std::make_shared<platform::ContentSession>([this] { navigator_.requestRedraw(); });
    session_ = session;
    dirty_ = false;
    finish_reported_ = false;
    started_at_ = now;
    list_.replace(0, now);
    hits_value_.jump(0.0);
    progress_value_.jump(0.0);

    platform::ContentRequest request;
    request.location = platform::absolutePath(platform::toWide(location_));
    search_base_ = platform::toUtf8(platform::withoutExtendedPrefix(request.location));
    request.forced_encoding = options_.encoding;
    request.max_hits = kMaxHits;
    request.skip_binary = options_.skip_binary;
    const std::shared_ptr<const core::TextMatcher> matcher = matcher_;
    runner_.submit([request, matcher, session](const std::atomic<bool>& cancelled) {
        platform::runContentSearch(request, *matcher, *session, cancelled);
    });
}

void ContentPage::cancelSearch() {
    if (!busy()) return;
    runner_.cancel();
    navigator_.showToast(tr(strings::toast_search_cancelled), ToastKind::Warning);
}

void ContentPage::openSelected(bool focus_find) {
    if (!session_) return;
    const auto selected = list_.selected();
    if (!selected) return;
    const auto row = session_->row(*selected);
    if (!row) return;
    const auto file = session_->file(row->file);
    if (!file) return;
    ViewerTarget target{file->path, row->hit.line_offset, row->hit.line_number, matcher_};
    target.focus_find = focus_find;
    navigator_.openInViewer(std::move(target));
}

void ContentPage::poll(Clock::time_point now) {
    if (!session_) return;
    const std::size_t rows = session_->rowCount();
    if (rows != list_.count()) list_.grow(rows, now);
    hits_value_.set(static_cast<double>(rows), now, motion_);
    const std::uint64_t total = session_->bytes_total.load();
    const double fraction = total == 0 ? 0.0 : static_cast<double>(session_->bytes_done.load()) / static_cast<double>(total);
    progress_value_.set(std::clamp(fraction, 0.0, 1.0), now, motion_);
    if (session_->finished.load() && !finish_reported_) {
        finish_reported_ = true;
        navigator_.recordSearchTime(session_->elapsedSeconds());
        const platform::ContentError error = session_->error();
        if (error == platform::ContentError::LocationMissing) {
            message_ = tr(strings::content_location_missing);
            message_is_error_ = true;
        } else if (error == platform::ContentError::NothingToSearch) {
            message_ = tr(strings::content_nothing_to_search);
            message_is_error_ = false;
        }
    }
}

bool ContentPage::onEvent(Event& event, Clock::time_point now) {
    const bool right_click = event.is_mouse() && event.mouse().button == Mouse::Right;
    const int mouse_x = event.is_mouse() ? event.mouse().x : 0;
    const int mouse_y = event.is_mouse() ? event.mouse().y : 0;
    const ResultList::Action action = list_.onEvent(event, now);
    if (action == ResultList::Action::Activate) {
        openSelected(false);
        return true;
    }
    if (action == ResultList::Action::Context && right_click) {
        openRowMenu(mouse_x, mouse_y);
        return true;
    }
    if (event == kShiftF10 && list_.selected()) {
        const auto anchor = list_.selectedAnchor();
        if (anchor) openRowMenu(anchor->first, anchor->second);
        return true;
    }
    if (event == kCtrlO) {
        browse_button_->press();
        return true;
    }
    if (event == kCtrlF && list_.selected()) {
        openSelected(true);
        return true;
    }
    if (action == ResultList::Action::Moved) return true;
    if (event == Event::Escape && busy()) {
        cancelSearch();
        return true;
    }
    return false;
}

bool ContentPage::animating(Clock::time_point now) const {
    return busy() || list_.animating(now, motion_) || hits_value_.animating(now, motion_) ||
           progress_value_.animating(now, motion_);
}

Element ContentPage::renderTitle(const FrameContext& context) const {
    const core::Palette& palette = context.palette;
    const std::string dot = "  " + std::string(context.glyphs.dot_separator) + " ";
    Elements title{coloredText(tr(strings::label_results), palette.text_muted)};
    if (!session_) return hbox(std::move(title));
    const char separator = thousandsSeparator();
    const auto hits = static_cast<std::uint64_t>(std::llround(hits_value_.value(context.now, motion_)));
    title.push_back(coloredText("  " + core::formatCount(hits, separator) + " " + tr(strings::content_hits), palette.accent));
    title.push_back(coloredText(" " + std::string(tr(strings::content_in)) + " " +
                                    core::formatCount(session_->files_matched.load(), separator) + " " + tr(strings::content_files),
                                palette.text_muted));
    const double seconds = session_->elapsedSeconds();
    const double speed = seconds > 0.05 ? static_cast<double>(session_->bytes_done.load()) / seconds : 0.0;
    title.push_back(coloredText(dot + core::formatThroughput(speed), palette.text_faint));
    title.push_back(coloredText(dot + core::formatDuration(seconds), palette.text_faint));
    if (session_->limit_reached.load())
        title.push_back(coloredText(dot + tr(strings::content_limit), palette.warning));
    else if (session_->cancelled.load())
        title.push_back(coloredText(dot + tr(strings::content_cancelled), palette.warning));
    return hbox(std::move(title));
}

Element ContentPage::renderRow(std::size_t index, const RowVisual& visual, const FrameContext& context) {
    std::optional<platform::ContentRow> row;
    if (session_) row = session_->row(index);
    if (!row) return text(" ");
    const auto file = session_->file(row->file);
    const core::Palette& palette = visual.palette;
    const Glyphs& glyphs = context.glyphs;

    const int width = ResultList::rowWidth(context) - visual.slide;
    const int file_width = std::clamp(width * 30 / 100, 14, 48);
    const int preview_width = std::max(10, width - 1 - file_width - 1 - kLineColumn - 2);

    const std::string display = file ? stripPrefix(file->display, search_base_) : std::string();

    std::vector<core::MatchSpan> highlight{{row->hit.match_start, row->hit.match_length}};
    std::string preview = row->hit.preview;
    if (row->hit.preview_cut_left) {
        const std::string mark(glyphs.unicode ? "…" : "...");
        preview = mark + preview;
        highlight.front().start += mark.size();
    }

    Elements cells;
    if (visual.slide > 0) cells.push_back(widgets::spacer(visual.slide));
    cells.push_back(coloredText(std::string(glyphs.selection_marker), core::lerp(visual.background, palette.accent, visual.selection)));
    cells.push_back(widgets::fixedText(core::utf8::fitToWidthKeepingEnd(display, static_cast<std::size_t>(file_width)),
                                       static_cast<std::size_t>(file_width), palette.info, false, glyphs));
    cells.push_back(widgets::fixedText(core::formatCount(row->hit.line_number, thousandsSeparator()), kLineColumn,
                                       palette.text_faint, true, glyphs));
    cells.push_back(coloredText(" " + std::string(glyphs.vertical_bar) + " ", palette.border));
    cells.push_back(widgets::styledLine(preview, highlight, 0, static_cast<std::size_t>(preview_width),
                                        {palette.text, palette.match_text, palette.match_background}, true, glyphs));
    return hbox(std::move(cells));
}

Element ContentPage::renderBody(const FrameContext& context) {
    const core::Palette& palette = context.palette;
    const auto centered = [&](Elements lines) {
        Elements all{filler()};
        for (auto& line : lines) all.push_back(std::move(line) | hcenter);
        all.push_back(filler());
        return vbox(std::move(all));
    };

    if (!message_.empty() && (!session_ || session_->rowCount() == 0)) {
        return centered({coloredText(std::string(context.glyphs.warning) + " " + message_,
                                     message_is_error_ ? palette.error : palette.warning) |
                         bold});
    }
    if (!session_) {
        return centered({coloredText(tr(strings::empty_content_title), palette.text) | bold, text(" "),
                         coloredText(tr(strings::empty_content_hint), palette.text_faint)});
    }

    const char separator = thousandsSeparator();
    const bool running = !session_->finished.load();
    const double spin = motion_.seconds(started_at_, context.now);
    const double fraction = session_->enumeration_done.load() ? progress_value_.value(context.now, motion_) : -1.0;
    const std::string scanned = std::string(tr(strings::content_scanned)) + " " +
                                core::formatCount(session_->files_done.load(), separator) + " " + tr(strings::content_files) + "  " +
                                core::formatBytes(session_->bytes_done.load());
    const std::uint64_t skipped = session_->files_skipped.load();
    const std::string skipped_text =
        skipped > 0 ? "  " + std::string(context.glyphs.dot_separator) + " " + tr(strings::content_skipped) + " " +
                          core::formatCount(skipped, separator)
                    : std::string();

    if (list_.count() == 0) {
        if (running) {
            return centered({hbox({widgets::spinner(spin, palette.accent, context.glyphs),
                                   coloredText(" " + std::string(tr(strings::content_searching)), palette.text_muted)}),
                             text(" "), widgets::progressBar(fraction, 40, spin, palette, context.glyphs), text(" "),
                             coloredText(scanned + skipped_text, palette.text_faint)});
        }
        return centered({coloredText(tr(strings::content_no_hits), palette.text) | bold, text(" "),
                         coloredText(scanned + skipped_text, palette.text_faint)});
    }

    Element list = list_.render(context, nullptr, [&](std::size_t index, const RowVisual& visual) {
        return renderRow(index, visual, context);
    });
    Elements footer;
    if (running) {
        footer.push_back(widgets::spinner(spin, palette.accent, context.glyphs));
        footer.push_back(text(" "));
        footer.push_back(widgets::progressBar(fraction, 24, spin, palette, context.glyphs));
        footer.push_back(text(" "));
    } else {
        footer.push_back(coloredText(std::string(context.glyphs.check) + " ", palette.success));
    }
    footer.push_back(coloredText(scanned + skipped_text, palette.text_faint));
    footer.push_back(filler());
    return vbox({std::move(list) | flex, hbox({text(" "), hbox(std::move(footer))})});
}

Element ContentPage::render(const FrameContext& context) {
    const core::Palette& palette = context.palette;
    Element options = hbox({text(" "), options_row_->Render(), filler()});
    Element results = widgets::panel(renderTitle(context), renderBody(context) | flex, palette.border, context.glyphs);
    return vbox({
        inputPanel(tr(strings::label_pattern), pattern_input_, palette, context.glyphs),
        inputPanel(tr(strings::label_location), location_input_, palette, context.glyphs, browse_button_->Render()),
        std::move(options),
        std::move(results) | flex,
    });
}

}
