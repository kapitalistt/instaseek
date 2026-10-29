#include "ui/files_page.hpp"

#include "core/file_kind.hpp"
#include "core/format.hpp"
#include "core/utf8.hpp"
#include "platform/win.hpp"
#include "ui/i18n.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/mouse.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace instaseek::ui {

using namespace ftxui;
using widgets::coloredText;

namespace {

constexpr std::size_t kResultLimit = 2000;
constexpr auto kRefreshInterval = std::chrono::milliseconds(700);
constexpr int kSizeColumn = 10;
constexpr int kDateColumn = 16;

const Event kCtrlF = Event::Special("\x06");
const Event kShiftF10 = Event::Special("\x1B[21;2~");

struct Columns {
    int name = 20;
    int folder = 20;
    bool show_size = true;
    bool show_date = true;
};

Columns columnsFor(int width) {
    Columns columns;
    columns.show_date = width >= 96;
    columns.show_size = width >= 64;
    int fixed = 3;
    if (columns.show_size) fixed += kSizeColumn + 1;
    if (columns.show_date) fixed += kDateColumn + 1;
    const int flexible = std::max(20, width - fixed - 1);
    columns.name = std::max(12, flexible * 42 / 100);
    columns.folder = std::max(6, flexible - columns.name);
    return columns;
}

}

FilesPage::FilesPage(platform::IndexService& index, Navigator& navigator, const SharedStyle& style,
                     const Motion& motion)
    : index_(index), navigator_(navigator), style_(style), motion_(motion) {
    refreshTexts();
    input_ = makeTextField(&query_, &placeholder_, &style_,
                           {[this] { submitSearch(false); }, [this] { activateSelected(false); },
                            [this](bool copied) {
                                navigator_.showToast(tr(copied ? strings::toast_copied : strings::toast_copy_failed),
                                                     copied ? ToastKind::Success : ToastKind::Error);
                            },
                            [this](int x, int y) { navigator_.openContextMenu(input_->menuItems(), x, y); }});
    container_ = Container::Vertical({input_});
}

void FilesPage::refreshTexts() { placeholder_ = tr(strings::placeholder_files); }

std::vector<widgets::HintItem> FilesPage::hints() const {
    const Glyphs& glyphs = *style_.glyphs;
    return {
        {"Enter", tr(strings::hint_open)},
        {std::string(glyphs.arrows_vertical), tr(strings::hint_select)},
        {tr(strings::key_right_click), tr(strings::hint_menu)},
        {"Ctrl+F", tr(strings::hint_find_in_file)},
        {"F4", tr(strings::hint_rescan)},
    };
}

void FilesPage::submitSearch(bool refresh) {
    const std::string text = query_;
    requested_text_ = text;
    const core::QueryClock clock{platform::nowUtcTicks(), platform::localUtcOffsetTicks()};
    core::NameQuery query = core::parseNameQuery(text, clock);
    const platform::IndexSnapshot snapshot = index_.snapshot();
    requested_generation_ = snapshot.generation;
    last_refresh_ = Clock::now();

    if (query.empty()) {
        runner_.cancel();
        Outcome outcome;
        outcome.text = text;
        outcome.query = std::move(query);
        outcome.generation = snapshot.generation;
        outcome.epoch = snapshot.epoch;
        std::lock_guard lock(pending_mutex_);
        pending_ = std::move(outcome);
        return;
    }

    runner_.submit([this, text, query = std::move(query), refresh](const std::atomic<bool>& cancelled) {
        const platform::IndexSnapshot state = index_.snapshot();
        const unsigned threads = std::max(1u, std::thread::hardware_concurrency());
        core::NameSearchResult result = index_.read([&](const core::FileIndex& index) {
            return core::searchNames(index, query, kResultLimit, threads, &cancelled);
        });
        if (result.cancelled || cancelled.load()) return;
        Outcome outcome;
        outcome.text = text;
        outcome.query = query;
        outcome.records = std::move(result.records);
        outcome.total = result.total_matches;
        outcome.seconds = result.seconds;
        outcome.generation = state.generation;
        outcome.epoch = state.epoch;
        outcome.refresh = refresh;
        {
            std::lock_guard lock(pending_mutex_);
            pending_ = std::move(outcome);
        }
        navigator_.requestRedraw();
    });
}

void FilesPage::poll(Clock::time_point now) {
    std::optional<Outcome> arrived;
    {
        std::lock_guard lock(pending_mutex_);
        arrived.swap(pending_);
    }
    if (arrived && arrived->text == query_) {
        const bool refresh = arrived->refresh && has_results_;
        current_ = std::move(*arrived);
        has_results_ = !current_.query.empty();
        if (refresh) list_.grow(current_.records.size(), now);
        else list_.replace(current_.records.size(), now);
        total_value_.set(static_cast<double>(current_.total), now, motion_);
        if (has_results_ && !refresh) navigator_.recordSearchTime(current_.seconds);
    }

    const platform::IndexSnapshot snapshot = index_.snapshot();
    if (has_results_ && current_.epoch != snapshot.epoch && !runner_.busy()) {
        list_.clear();
        has_results_ = false;
        submitSearch(false);
        return;
    }
    if (!query_.empty() && snapshot.generation != requested_generation_ && !runner_.busy() &&
        now - last_refresh_ > kRefreshInterval)
        submitSearch(true);
}

std::optional<std::string> FilesPage::selectedPath(bool* directory) const {
    const auto selected = list_.selected();
    if (!has_results_ || !selected || *selected >= current_.records.size()) return std::nullopt;
    const std::uint32_t record = current_.records[*selected];
    return index_.read([&](const core::FileIndex& index) -> std::optional<std::string> {
        if (record >= index.recordCount()) return std::nullopt;
        if (directory != nullptr) *directory = index.record(record).isDirectory();
        return index.fullPath(record);
    });
}

void FilesPage::activateSelected(bool focus_find) {
    bool directory = false;
    const auto path = selectedPath(&directory);
    if (!path) return;
    const std::wstring wide = platform::toWide(*path);
    if (directory) {
        navigator_.searchContentIn(wide);
        return;
    }
    ViewerTarget target{wide, std::nullopt, std::nullopt, nullptr};
    target.focus_find = focus_find;
    navigator_.openInViewer(std::move(target));
}

void FilesPage::openRowMenu(int x, int y) {
    bool directory = false;
    const auto path = selectedPath(&directory);
    if (!path) return;
    const std::string full = *path;
    const auto slash = full.find_last_of('\\');
    const std::string name = slash == std::string::npos ? full : full.substr(slash + 1);
    const std::string folder = slash == std::string::npos ? full : full.substr(0, slash == 2 ? 3 : slash);
    const std::wstring wide = platform::toWide(full);

    std::vector<MenuItem> items;
    if (directory) {
        items.push_back({tr(strings::menu_search_in_folder), "Enter", [this, wide] { navigator_.searchContentIn(wide); }});
    } else {
        items.push_back({tr(strings::menu_open_viewer), "Enter", [this] { activateSelected(false); }});
        items.push_back({tr(strings::menu_find_in_file), "Ctrl+F", [this] { activateSelected(true); }});
        items.push_back({tr(strings::menu_search_in_parent), "",
                         [this, folder] { navigator_.searchContentIn(platform::toWide(folder)); }});
    }
    items.push_back({tr(strings::menu_copy_path), "", [this, full] { navigator_.copyText(full, tr(strings::toast_copied_path)); }, true, true});
    items.push_back({tr(strings::menu_copy_name), "", [this, name] { navigator_.copyText(name, tr(strings::toast_copied)); }});
    if (!directory)
        items.push_back({tr(strings::menu_copy_folder), "", [this, folder] { navigator_.copyText(folder, tr(strings::toast_copied_path)); }});
    items.push_back({tr(directory ? strings::menu_open_explorer : strings::menu_show_explorer), "",
                     [this, wide] { navigator_.revealInExplorer(wide); }, true, true});
    navigator_.openContextMenu(std::move(items), x, y);
}

bool FilesPage::onEvent(Event& event, Clock::time_point now) {
    const bool right_click = event.is_mouse() && event.mouse().button == Mouse::Right;
    const int mouse_x = event.is_mouse() ? event.mouse().x : 0;
    const int mouse_y = event.is_mouse() ? event.mouse().y : 0;
    const ResultList::Action action = list_.onEvent(event, now);
    if (action == ResultList::Action::Activate) {
        activateSelected(false);
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
    if (event == kCtrlF && list_.selected()) {
        activateSelected(true);
        return true;
    }
    if (action == ResultList::Action::Moved) return true;
    if (event == Event::Escape && !query_.empty()) {
        query_.clear();
        submitSearch(false);
        return true;
    }
    return false;
}

bool FilesPage::animating(Clock::time_point now) const {
    return list_.animating(now, motion_) || total_value_.animating(now, motion_);
}

Element FilesPage::renderHeader(const FrameContext& context) const {
    const Columns columns = columnsFor(ResultList::rowWidth(context));
    const core::Rgb muted = context.palette.text_faint;
    Elements cells;
    cells.push_back(text("   "));
    cells.push_back(widgets::fixedText(tr(strings::column_name), static_cast<std::size_t>(columns.name), muted, false, context.glyphs));
    cells.push_back(text(" "));
    cells.push_back(widgets::fixedText(tr(strings::column_folder), static_cast<std::size_t>(columns.folder), muted, false, context.glyphs));
    if (columns.show_size) {
        cells.push_back(text(" "));
        cells.push_back(widgets::fixedText(tr(strings::column_size), kSizeColumn, muted, true, context.glyphs));
    }
    if (columns.show_date) {
        cells.push_back(text(" "));
        cells.push_back(widgets::fixedText(tr(strings::column_modified), kDateColumn, muted, false, context.glyphs));
    }
    return hbox(std::move(cells));
}

Element FilesPage::renderRow(const core::FileIndex& index, std::size_t row, const RowVisual& visual,
                             const FrameContext& context, std::int64_t local_offset) const {
    const core::Palette& palette = visual.palette;
    const Glyphs& glyphs = context.glyphs;
    const Columns columns = columnsFor(ResultList::rowWidth(context) - visual.slide);
    if (row >= current_.records.size() || current_.records[row] >= index.recordCount()) return text(" ");
    const std::uint32_t record_index = current_.records[row];
    const core::FileRecord& record = index.record(record_index);
    const std::string_view name = index.name(record_index);
    const core::FileKind kind = core::classifyFileName(name, record.isDirectory());

    const core::Rgb marker_color = core::lerp(visual.background, palette.accent, visual.selection);
    const auto highlights = widgets::findTerms(index.foldedName(record_index), current_.query.highlightTerms());
    const widgets::LineStyle name_style{record.isDeleted() ? palette.text_faint : palette.text, palette.match_text,
                                        palette.match_background};

    Elements cells;
    if (visual.slide > 0) cells.push_back(widgets::spacer(visual.slide));
    cells.push_back(coloredText(std::string(glyphs.selection_marker), marker_color));
    cells.push_back(widgets::fileIcon(kind, palette, glyphs));
    cells.push_back(text(" "));
    Element name_element =
        widgets::styledLine(name, highlights, 0, static_cast<std::size_t>(columns.name), name_style, true, glyphs);
    if (visual.selected) name_element = std::move(name_element) | bold;
    cells.push_back(std::move(name_element));
    cells.push_back(text(" "));
    const std::string folder = index.parentPath(record_index);
    cells.push_back(widgets::fixedText(core::utf8::fitToWidthKeepingEnd(folder, static_cast<std::size_t>(columns.folder)),
                                       static_cast<std::size_t>(columns.folder), palette.text_muted, false, glyphs));
    if (columns.show_size) {
        std::string size_text;
        if (!record.isDirectory()) size_text = record.hasMetadata() ? core::formatBytes(record.size) : std::string(glyphs.ellipsis);
        cells.push_back(text(" "));
        cells.push_back(widgets::fixedText(size_text, kSizeColumn, palette.text_muted, true, glyphs));
    }
    if (columns.show_date) {
        const std::string date = record.hasMetadata() ? core::formatFileTime(record.modified + local_offset) : std::string();
        cells.push_back(text(" "));
        cells.push_back(widgets::fixedText(date, kDateColumn, palette.text_faint, false, glyphs));
    }
    return hbox(std::move(cells));
}

Element FilesPage::renderBody(const FrameContext& context) {
    const core::Palette& palette = context.palette;
    const platform::IndexSnapshot snapshot = index_.snapshot();

    if (query_.empty() || !has_results_) {
        Elements lines{filler()};
        lines.push_back(coloredText(tr(strings::empty_files_title), palette.text) | bold | hcenter);
        lines.push_back(text(" "));
        lines.push_back(coloredText(tr(strings::empty_files_hint), palette.text_faint) | hcenter);
        if (!query_.empty() && runner_.busy()) {
            lines.push_back(text(" "));
            lines.push_back(hbox({widgets::spinner(motion_.seconds(Clock::time_point{}, context.now), palette.accent, context.glyphs),
                                  coloredText(" " + std::string(tr(strings::content_searching)), palette.text_muted)}) |
                            hcenter);
        }
        lines.push_back(filler());
        return vbox(std::move(lines));
    }

    Elements notes;
    if (!current_.query.invalid_tokens.empty()) {
        std::string invalid;
        for (const auto& token : current_.query.invalid_tokens) invalid += (invalid.empty() ? "" : ", ") + token;
        notes.push_back(hbox({text(" "), coloredText(std::string(context.glyphs.warning) + " " + tr(strings::files_invalid_filter) + ": " + invalid,
                                                     palette.warning)}));
    }
    if (snapshot.phase == platform::IndexPhase::Scanning || snapshot.phase == platform::IndexPhase::Starting) {
        notes.push_back(hbox({text(" "), coloredText(tr(strings::files_index_building), palette.info)}));
    } else if (current_.query.needsMetadata() && !snapshot.metadata_complete) {
        notes.push_back(hbox({text(" "), coloredText(std::string(context.glyphs.warning) + " " + tr(strings::files_metadata_pending),
                                                     palette.warning)}));
    }

    if (current_.records.empty()) {
        Elements lines{filler()};
        lines.push_back(coloredText(tr(strings::files_no_results), palette.text) | bold | hcenter);
        lines.push_back(text(" "));
        lines.push_back(coloredText(tr(strings::files_no_results_hint), palette.text_faint) | hcenter);
        lines.push_back(filler());
        for (auto& note : notes) lines.push_back(std::move(note));
        return vbox(std::move(lines));
    }

    const std::int64_t local_offset = platform::localUtcOffsetTicks();
    Element list = index_.read([&](const core::FileIndex& index) {
        return list_.render(context, renderHeader(context), [&](std::size_t row, const RowVisual& visual) {
            return renderRow(index, row, visual, context, local_offset);
        });
    });
    Elements lines{std::move(list) | flex};
    for (auto& note : notes) lines.push_back(std::move(note));
    return vbox(std::move(lines));
}

Element FilesPage::render(const FrameContext& context) {
    const core::Palette& palette = context.palette;
    const char separator = thousandsSeparator();

    Elements title{coloredText(tr(strings::label_results), palette.text_muted)};
    if (has_results_ && !query_.empty()) {
        const auto shown_total = static_cast<std::uint64_t>(std::llround(total_value_.value(context.now, motion_)));
        title.push_back(coloredText("  " + core::formatCount(shown_total, separator) + " " + tr(strings::results_word), palette.accent));
        if (current_.total > current_.records.size())
            title.push_back(coloredText("  " + std::string(context.glyphs.dot_separator) + " " + tr(strings::results_shown) + " " +
                                            core::formatCount(current_.records.size(), separator),
                                        palette.text_faint));
        title.push_back(coloredText("  " + std::string(context.glyphs.dot_separator) + " " + core::formatDuration(current_.seconds),
                                    palette.text_faint));
    }

    Element results = widgets::panel(hbox(std::move(title)), renderBody(context) | flex, palette.border, context.glyphs);
    return vbox({
        inputPanel(tr(strings::label_query), input_, palette, context.glyphs),
        std::move(results) | flex,
    });
}

}
