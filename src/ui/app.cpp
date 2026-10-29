#include "ui/app.hpp"

#include "core/easing.hpp"
#include "core/format.hpp"
#include "core/keyboard_layout.hpp"
#include "core/utf8.hpp"
#include "platform/index_service.hpp"
#include "platform/clipboard.hpp"
#include "platform/shell.hpp"
#include "platform/win.hpp"
#include "ui/context_menu.hpp"
#include "ui/content_page.hpp"
#include "ui/controls.hpp"
#include "ui/files_page.hpp"
#include "ui/glyphs.hpp"
#include "ui/page.hpp"
#include "ui/splash.hpp"
#include "ui/theme.hpp"
#include "ui/viewer_page.hpp"
#include "ui/widgets.hpp"

#include <ftxui/component/animation.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/terminal.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace instaseek::ui {

using namespace ftxui;
namespace easing = core::easing;
using widgets::coloredText;

namespace {

constexpr int kMinimumWidth = 80;
constexpr int kMinimumHeight = 24;
constexpr double kEntranceSeconds = 0.55;
constexpr double kModeSlideSeconds = 0.32;
constexpr double kHoverFadeSeconds = 0.18;
constexpr double kHelpFadeSeconds = 0.22;
constexpr double kToastFadeSeconds = 0.25;
constexpr double kBannerSeconds = 0.5;
constexpr double kLiveFlashSeconds = 1.2;
constexpr int kModeSlideColumns = 6;

const Event kCtrlQ = Event::Special("\x11");
const Event kCtrlC = Event::Special("\x03");

enum class Mode { Files, Content, Viewer };

constexpr std::array<Mode, 3> kModes = {Mode::Files, Mode::Content, Mode::Viewer};

int modeIndex(Mode mode) { return static_cast<int>(mode); }

const Text& modeLabel(Mode mode) {
    switch (mode) {
        case Mode::Content: return strings::tab_content;
        case Mode::Viewer: return strings::tab_viewer;
        case Mode::Files: break;
    }
    return strings::tab_files;
}

const char* modeKey(Mode mode) {
    switch (mode) {
        case Mode::Content: return "F6";
        case Mode::Viewer: return "F7";
        case Mode::Files: break;
    }
    return "F5";
}

struct HoverTracker {
    int current = -1;
    int previous = -1;
    Clock::time_point changed_at{};

    void update(int hovered, Clock::time_point now) {
        if (hovered == current) return;
        previous = current;
        current = hovered;
        changed_at = now;
    }

    double amount(int index, double elapsed_seconds) const {
        const double t = easing::outCubic(easing::progress(elapsed_seconds, 0.0, kHoverFadeSeconds));
        if (index == current) return t;
        if (index == previous) return 1.0 - t;
        return 0.0;
    }
};

struct Toast {
    std::string message;
    ToastKind kind = ToastKind::Info;
    Clock::time_point shown_at{};
    double seconds = 1.8;
};

class App final : public Navigator {
public:
    explicit App(const AppOptions& options);
    int run();

    void openInViewer(ViewerTarget target) override;
    void searchContentIn(const std::wstring& location) override;
    void showToast(std::string message, ToastKind kind) override;
    void openContextMenu(std::vector<MenuItem> items, int x, int y) override;
    void copyText(const std::string& text, std::string confirmation) override;
    void revealInExplorer(const std::wstring& path) override;
    void recordSearchTime(double seconds) override;
    void requestRedraw() override;

private:
    Page& page(Mode mode) const;
    Page& currentPage() const { return page(mode_); }

    bool handleEvent(Event event);
    bool handleMouse(Event& event, Clock::time_point now);
    void finishSplash(Clock::time_point now);
    void switchMode(Mode mode, Clock::time_point now);
    void toggleLanguage();
    void toggleHelp(Clock::time_point now);
    void requestElevation();
    void trackIndex(const platform::IndexSnapshot& snapshot, Clock::time_point now);
    void applyInitialPath();

    bool isAnimating(const platform::IndexSnapshot& snapshot, Clock::time_point now) const;
    double helpAmount(Clock::time_point now) const;
    double toastOpacity(Clock::time_point now) const;
    bool bannerVisible(const platform::IndexSnapshot& snapshot) const;

    Element render();
    Element renderMain(const core::Palette& palette, const platform::IndexSnapshot& snapshot, Clock::time_point now,
                       const Dimensions& terminal);
    Element renderTooSmall(const core::Palette& palette, const Dimensions& terminal) const;
    Element renderHeader(const core::Palette& palette, Clock::time_point now);
    Element renderTab(Mode mode, double indicator, double hover, const core::Palette& palette, Clock::time_point now);
    Element renderBanner(const core::Palette& palette, const platform::IndexSnapshot& snapshot, Clock::time_point now,
                         int width) const;
    Element renderModeBody(const core::Palette& palette, Clock::time_point now, const Dimensions& terminal);
    Element renderStatusBar(const core::Palette& palette, const platform::IndexSnapshot& snapshot, Clock::time_point now) const;
    Element renderHintBar(const core::Palette& palette, const platform::IndexSnapshot& snapshot, int width) const;
    Element renderHelp(const core::Palette& palette) const;
    Element renderToast(const core::Palette& palette, Clock::time_point now) const;

    AppOptions options_;
    Motion motion_;
    const Glyphs& glyphs_;
    ScreenInteractive screen_ = ScreenInteractive::Fullscreen();
    SharedStyle style_;
    SearchOptionsState search_options_;

    Clock::time_point started_at_;
    bool splash_visible_ = false;
    Clock::time_point main_entered_at_{};

    Mode mode_ = Mode::Files;
    Mode previous_mode_ = Mode::Files;
    Clock::time_point mode_changed_at_{};
    int mode_tab_index_ = 0;
    std::array<Box, 3> tab_boxes_{};
    HoverTracker tab_hover_;
    ContextMenu context_menu_;
    bool console_configured_ = false;

    bool help_visible_ = false;
    Clock::time_point help_toggled_at_{};
    Toast toast_;
    std::optional<double> last_search_seconds_;
    platform::IndexPhase last_phase_ = platform::IndexPhase::Starting;
    std::uint64_t announced_epoch_ = 0;
    std::optional<Clock::time_point> banner_since_;
    bool initial_path_applied_ = false;

    std::unique_ptr<platform::IndexService> index_;
    std::unique_ptr<FilesPage> files_page_;
    std::unique_ptr<ContentPage> content_page_;
    std::unique_ptr<ViewerPage> viewer_page_;
    Component mode_container_;
    Component root_;
};

App::App(const AppOptions& options)
    : options_(options),
      motion_(options.animations),
      glyphs_(glyphsFor(options.unicode_glyphs)),
      started_at_(Clock::now()),
      splash_visible_(options.animations) {
    activeLanguage() = options.language;
    if (!splash_visible_) main_entered_at_ = started_at_;
    style_.palette = core::blackPalette();
    style_.glyphs = &glyphs_;

    index_ = std::make_unique<platform::IndexService>(options_.drive, [this] { requestRedraw(); });
    files_page_ = std::make_unique<FilesPage>(*index_, *this, style_, motion_);
    content_page_ = std::make_unique<ContentPage>(*this, style_, motion_, search_options_);
    viewer_page_ = std::make_unique<ViewerPage>(*this, style_, motion_, search_options_);

    mode_container_ = Container::Tab(
        {files_page_->component(), content_page_->component(), viewer_page_->component()}, &mode_tab_index_);
    root_ = CatchEvent(Renderer(mode_container_, [this] { return render(); }),
                       [this](Event event) { return handleEvent(std::move(event)); });
}

int App::run() {
    index_->start();
    screen_.Loop(root_);
    return 0;
}

Page& App::page(Mode mode) const {
    switch (mode) {
        case Mode::Content: return *content_page_;
        case Mode::Viewer: return *viewer_page_;
        case Mode::Files: break;
    }
    return *files_page_;
}

void App::requestRedraw() { screen_.PostEvent(Event::Custom); }

void App::openContextMenu(std::vector<MenuItem> items, int x, int y) {
    context_menu_.open(std::move(items), x, y, Clock::now());
}

void App::copyText(const std::string& text, std::string confirmation) {
    if (platform::copyToClipboard(text)) showToast(std::move(confirmation), ToastKind::Success);
    else showToast(tr(strings::toast_copy_failed), ToastKind::Error);
}

void App::revealInExplorer(const std::wstring& path) {
    if (!platform::revealInExplorer(path)) showToast(tr(strings::toast_explorer_failed), ToastKind::Error);
}

void App::openInViewer(ViewerTarget target) {
    const auto now = Clock::now();
    switchMode(Mode::Viewer, now);
    viewer_page_->open(target, now);
}

void App::searchContentIn(const std::wstring& location) {
    switchMode(Mode::Content, Clock::now());
    content_page_->setLocation(location);
}

void App::showToast(std::string message, ToastKind kind) {
    toast_.message = std::move(message);
    toast_.kind = kind;
    toast_.shown_at = Clock::now();
    toast_.seconds = kind == ToastKind::Error ? 3.5 : (kind == ToastKind::Success ? 2.6 : 1.8);
}

void App::recordSearchTime(double seconds) { last_search_seconds_ = seconds; }

void App::applyInitialPath() {
    if (initial_path_applied_ || options_.initial_path.empty()) return;
    initial_path_applied_ = true;
    const std::wstring path = platform::absolutePath(options_.initial_path);
    switch (platform::pathKind(path)) {
        case platform::PathKind::Directory: searchContentIn(path); break;
        case platform::PathKind::File: openInViewer(ViewerTarget{path, std::nullopt, std::nullopt, nullptr}); break;
        case platform::PathKind::Missing:
            showToast(std::string(tr(strings::content_location_missing)) + ": " + platform::toUtf8(path), ToastKind::Error);
            break;
    }
}

bool App::handleEvent(Event event) {
    const auto now = Clock::now();
    if (event == Event::Custom) return true;

    if (splash_visible_) {
        if (event.is_mouse() && !(event.mouse().motion == Mouse::Pressed)) return true;
        finishSplash(now);
        return true;
    }

    if (event == kCtrlC) return true;
    if (context_menu_.isOpen()) return context_menu_.onEvent(event, now);

    if (event.is_mouse()) return handleMouse(event, now);

    if (event == Event::F10 || event == kCtrlQ) {
        screen_.Exit();
        return true;
    }
    if (event == Event::F1) {
        toggleHelp(now);
        return true;
    }
    if (event == Event::F2) {
        toggleLanguage();
        return true;
    }
    if (help_visible_) {
        std::optional<core::HotkeyLetter> letter;
        if (event.is_character()) letter = core::hotkeyFromTypedCharacter(event.character());
        if (event == Event::Escape || (letter && letter->latin == 'q')) toggleHelp(now);
        return true;
    }
    if (event == Event::F4) {
        index_->rescan();
        showToast(tr(strings::toast_rescan), ToastKind::Info);
        return true;
    }
    if (event == Event::F9) {
        requestElevation();
        return true;
    }
    if (event == Event::F5) {
        switchMode(Mode::Files, now);
        return true;
    }
    if (event == Event::F6) {
        switchMode(Mode::Content, now);
        return true;
    }
    if (event == Event::F7) {
        switchMode(Mode::Viewer, now);
        return true;
    }
    return currentPage().onEvent(event, now);
}

bool App::handleMouse(Event& event, Clock::time_point now) {
    const Mouse& mouse = event.mouse();
    int hovered = -1;
    for (int i = 0; i < static_cast<int>(tab_boxes_.size()); ++i)
        if (tab_boxes_[static_cast<std::size_t>(i)].Contain(mouse.x, mouse.y)) hovered = i;
    tab_hover_.update(hovered, now);

    const bool left_press = mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed;
    if (help_visible_) {
        if (left_press) toggleHelp(now);
        return true;
    }
    if (left_press && hovered >= 0) {
        switchMode(kModes[static_cast<std::size_t>(hovered)], now);
        return true;
    }
    return currentPage().onEvent(event, now);
}

void App::finishSplash(Clock::time_point now) {
    splash_visible_ = false;
    main_entered_at_ = now;
}

void App::switchMode(Mode mode, Clock::time_point now) {
    if (mode == mode_) return;
    previous_mode_ = mode_;
    mode_ = mode;
    mode_changed_at_ = now;
    mode_tab_index_ = modeIndex(mode);
    currentPage().focusPrimary();
}

void App::toggleLanguage() {
    activeLanguage() = otherLanguage(activeLanguage());
    files_page_->refreshTexts();
    content_page_->refreshTexts();
    viewer_page_->refreshTexts();
    showToast(tr(strings::toast_language), ToastKind::Info);
}

void App::toggleHelp(Clock::time_point now) {
    help_visible_ = !help_visible_;
    help_toggled_at_ = now;
}

void App::requestElevation() {
    if (options_.elevated) return;
    switch (platform::relaunchElevated(options_.relaunch_arguments)) {
        case platform::ElevationResult::Started: screen_.Exit(); break;
        case platform::ElevationResult::Declined: showToast(tr(strings::toast_elevation_declined), ToastKind::Warning); break;
        case platform::ElevationResult::Failed: showToast(tr(strings::toast_elevation_failed), ToastKind::Error); break;
    }
}

void App::trackIndex(const platform::IndexSnapshot& snapshot, Clock::time_point now) {
    if (snapshot.phase != platform::IndexPhase::Scanning && snapshot.phase != platform::IndexPhase::Starting &&
        last_phase_ == platform::IndexPhase::Scanning && snapshot.epoch != announced_epoch_ && snapshot.scan_seconds > 0.0) {
        announced_epoch_ = snapshot.epoch;
        showToast(std::string(tr(strings::toast_indexed)) + " " + core::formatCount(snapshot.files, thousandsSeparator()) + " " +
                      tr(strings::toast_files_in) + " " + core::formatDuration(snapshot.scan_seconds),
                  ToastKind::Success);
    }
    last_phase_ = snapshot.phase;
    if (bannerVisible(snapshot)) {
        if (!banner_since_) banner_since_ = now;
    } else {
        banner_since_.reset();
    }
}

bool App::bannerVisible(const platform::IndexSnapshot& snapshot) const {
    return (snapshot.elevation_would_help && !options_.elevated) || snapshot.not_ntfs;
}

bool App::isAnimating(const platform::IndexSnapshot& snapshot, Clock::time_point now) const {
    if (!motion_.enabled()) return false;
    if (splash_visible_) return true;
    if (motion_.elapsed(main_entered_at_, now) < kEntranceSeconds) return true;
    if (motion_.elapsed(mode_changed_at_, now) < kModeSlideSeconds) return true;
    if (motion_.elapsed(tab_hover_.changed_at, now) < kHoverFadeSeconds) return true;
    if (motion_.elapsed(help_toggled_at_, now) < kHelpFadeSeconds) return true;
    if (!toast_.message.empty() && motion_.elapsed(toast_.shown_at, now) < toast_.seconds) return true;
    if (context_menu_.animating(motion_, now)) return true;
    if (banner_since_ && motion_.elapsed(*banner_since_, now) < kBannerSeconds) return true;
    if (snapshot.phase == platform::IndexPhase::Scanning || snapshot.phase == platform::IndexPhase::Metadata ||
        snapshot.phase == platform::IndexPhase::Starting)
        return true;
    if (motion_.elapsed(snapshot.last_change, now) < kLiveFlashSeconds) return true;
    for (const Mode mode : kModes)
        if (page(mode).animating(now)) return true;
    return false;
}

double App::helpAmount(Clock::time_point now) const {
    const double t = easing::outCubic(easing::progress(motion_.elapsed(help_toggled_at_, now), 0.0, kHelpFadeSeconds));
    return help_visible_ ? t : 1.0 - t;
}

double App::toastOpacity(Clock::time_point now) const {
    if (toast_.message.empty()) return 0.0;
    const double elapsed = std::chrono::duration<double>(now - toast_.shown_at).count();
    if (elapsed >= toast_.seconds) return 0.0;
    if (!motion_.enabled()) return 1.0;
    const double fade_in = easing::progress(elapsed, 0.0, kToastFadeSeconds);
    const double fade_out = 1.0 - easing::progress(elapsed, toast_.seconds - kToastFadeSeconds, kToastFadeSeconds);
    return easing::outCubic(std::min(fade_in, fade_out));
}

Element App::render() {
    const auto now = Clock::now();
    const core::Palette& palette = core::blackPalette();
    if (!console_configured_) {
        platform::deliverCtrlCAsKey();
        console_configured_ = true;
    }
    const platform::IndexSnapshot snapshot = index_->snapshot();

    for (const Mode mode : kModes) page(mode).poll(now);
    trackIndex(snapshot, now);

    if (splash_visible_ && motion_.seconds(started_at_, now) >= kSplashSeconds) finishSplash(now);
    if (!splash_visible_) applyInitialPath();

    Element content;
    const Dimensions terminal = Terminal::Size();
    if (splash_visible_)
        content = renderSplash(motion_.seconds(started_at_, now), palette, glyphs_);
    else if (terminal.dimx < kMinimumWidth || terminal.dimy < kMinimumHeight)
        content = renderTooSmall(palette, terminal);
    else
        content = renderMain(palette, snapshot, now, terminal);

    if (isAnimating(snapshot, now)) animation::RequestAnimationFrame();
    return std::move(content) | color(toColor(palette.text)) | bgcolor(toColor(palette.background));
}

Element App::renderMain(const core::Palette& palette, const platform::IndexSnapshot& snapshot, Clock::time_point now,
                        const Dimensions& terminal) {
    const double entrance = easing::outCubic(easing::progress(motion_.elapsed(main_entered_at_, now), 0.0, kEntranceSeconds));
    const double help = helpAmount(now);
    const core::Palette visible = core::fadeFromBackground(palette, entrance);
    const core::Palette dimmed = core::fadeFromBackground(visible, 1.0 - 0.5 * help);

    Elements rows;
    rows.push_back(renderHeader(dimmed, now));
    rows.push_back(separator() | color(toColor(dimmed.border)));
    if (bannerVisible(snapshot)) rows.push_back(renderBanner(dimmed, snapshot, now, terminal.dimx));
    rows.push_back(renderModeBody(dimmed, now, terminal) | flex);
    rows.push_back(renderStatusBar(dimmed, snapshot, now));
    rows.push_back(renderHintBar(dimmed, snapshot, terminal.dimx));

    Elements layers;
    layers.push_back(vbox(std::move(rows)));
    if (toastOpacity(now) > 0.0) layers.push_back(renderToast(palette, now));
    if (help > 0.0) layers.push_back(renderHelp(core::fadeFromBackground(palette, help)) | clear_under | center);
    if (context_menu_.isOpen())
        layers.push_back(context_menu_.render(palette, glyphs_, terminal.dimx, terminal.dimy, motion_, now));
    return dbox(std::move(layers));
}

Element App::renderTooSmall(const core::Palette& palette, const Dimensions& terminal) const {
    const std::string times(glyphs_.times);
    const std::string needed = std::string(tr(strings::terminal_needed_size)) + " " + std::to_string(kMinimumWidth) + times +
                               std::to_string(kMinimumHeight);
    const std::string current = std::to_string(terminal.dimx) + times + std::to_string(terminal.dimy);
    return vbox({
               coloredText(tr(strings::terminal_too_small), palette.warning) | bold | hcenter,
               text(" "),
               coloredText(needed, palette.text_muted) | hcenter,
               coloredText(current, palette.text_faint) | hcenter,
           }) |
           center;
}

Element App::renderTab(Mode mode, double indicator, double hover, const core::Palette& palette, Clock::time_point now) {
    const int index = modeIndex(mode);
    const bool active = mode == mode_;
    const bool busy = page(mode).busy();
    const std::string key = modeKey(mode);
    const std::string label = tr(modeLabel(mode));
    const int width = static_cast<int>(core::utf8::displayWidth(key) + core::utf8::displayWidth(label)) + 5 + (busy ? 2 : 0);

    const core::Rgb label_color = active ? palette.accent : core::lerp(palette.text_muted, palette.text, hover);
    const core::Rgb key_color = active ? palette.accent : palette.text_faint;
    const core::Rgb background = core::lerp(palette.background, palette.hover, hover);

    Element label_element = coloredText(label, label_color);
    if (active) label_element = std::move(label_element) | bold;
    Elements label_parts{text("  "), coloredText(key, key_color), text(" "), std::move(label_element)};
    if (busy) {
        label_parts.push_back(text(" "));
        label_parts.push_back(widgets::spinner(motion_.seconds(started_at_, now), palette.accent, glyphs_));
    }
    label_parts.push_back(text("  "));
    Element label_row = hbox(std::move(label_parts)) | bgcolor(toColor(background));

    const double coverage = easing::clamp01(1.0 - std::abs(indicator - static_cast<double>(index)));
    const int underline_cells = static_cast<int>(std::lround(coverage * static_cast<double>(width - 2)));
    const int left = (width - underline_cells) / 2;
    const int right = width - underline_cells - left;
    Element underline = hbox({
        widgets::spacer(left),
        coloredText(widgets::repeatGlyph(glyphs_.tab_underline, underline_cells), palette.accent),
        widgets::spacer(right),
    });
    return vbox({std::move(label_row), std::move(underline)}) | reflect(tab_boxes_[static_cast<std::size_t>(index)]);
}

Element App::renderHeader(const core::Palette& palette, Clock::time_point now) {
    const double slide = easing::outCubic(easing::progress(motion_.elapsed(mode_changed_at_, now), 0.0, kModeSlideSeconds));
    const double indicator = static_cast<double>(modeIndex(previous_mode_)) +
                             (static_cast<double>(modeIndex(mode_)) - static_cast<double>(modeIndex(previous_mode_))) * slide;
    const double hover_elapsed = motion_.elapsed(tab_hover_.changed_at, now);

    Elements tabs;
    for (const Mode mode : kModes)
        tabs.push_back(renderTab(mode, indicator, tab_hover_.amount(modeIndex(mode), hover_elapsed), palette, now));

    Element logo = vbox({
        hbox({
            text(" "),
            coloredText(std::string(glyphs_.logo_mark), palette.logo_start),
            text(" "),
            widgets::gradientText(tr(strings::app_name), palette.logo_start, palette.logo_end) | bold,
            text("  "),
        }),
        text(" "),
    });

    const char* language_code = activeLanguage() == Language::Russian ? "RU" : "EN";
    Element badges = vbox({
        hbox({
            widgets::chip(language_code, palette.text_muted, palette.surface_raised),
            text(" "),
        }),
        text(" "),
    });
    return hbox({std::move(logo), hbox(std::move(tabs)), filler(), std::move(badges)});
}

Element App::renderBanner(const core::Palette& palette, const platform::IndexSnapshot& snapshot, Clock::time_point now,
                          int width) const {
    const double appear =
        banner_since_ ? easing::outCubic(easing::progress(motion_.elapsed(*banner_since_, now), 0.0, kBannerSeconds)) : 1.0;
    const core::Palette faded = core::fadeFromBackground(palette, appear);
    const bool elevation = snapshot.elevation_would_help && !options_.elevated;
    const std::string message = std::string(glyphs_.warning) + " " + tr(elevation ? strings::banner_elevation : strings::banner_not_ntfs);
    Elements parts{text(" ")};
    int used = 2;
    if (elevation) {
        const std::string key_label = tr(strings::banner_elevation_key);
        parts.push_back(widgets::keyHint("F9", key_label, faded));
        used += static_cast<int>(core::utf8::displayWidth(key_label)) + 8;
    }
    const int message_width = std::max(10, width - used - 1);
    parts.push_back(widgets::styledLine(message, {}, 0, static_cast<std::size_t>(message_width),
                                        {faded.warning, faded.warning, faded.warning}, true, glyphs_));
    return hbox(std::move(parts)) | bgcolor(toColor(core::lerp(palette.background, palette.surface_raised, appear)));
}

Element App::renderModeBody(const core::Palette& palette, Clock::time_point now, const Dimensions& terminal) {
    const double t = easing::outCubic(easing::progress(motion_.elapsed(mode_changed_at_, now), 0.0, kModeSlideSeconds));
    const core::Palette body_palette = core::fadeFromBackground(palette, t);
    style_.palette = body_palette;

    const FrameContext context{body_palette, glyphs_, motion_, now, terminal.dimx, terminal.dimy};
    Element body = currentPage().render(context);

    const int offset = static_cast<int>(std::lround((1.0 - t) * kModeSlideColumns));
    Element pad = widgets::spacer(offset);
    if (modeIndex(mode_) >= modeIndex(previous_mode_))
        return hbox({widgets::spacer(1), std::move(pad), std::move(body) | flex, widgets::spacer(1)});
    return hbox({widgets::spacer(1), std::move(body) | flex, std::move(pad), widgets::spacer(1)});
}

Element App::renderStatusBar(const core::Palette& palette, const platform::IndexSnapshot& snapshot, Clock::time_point now) const {
    const std::string dot = " " + std::string(glyphs_.dot_separator) + " ";
    const char separator = thousandsSeparator();
    auto field = [&](const Text& name, Element value) {
        return hbox({coloredText(std::string(tr(name)) + " ", palette.text_faint), std::move(value)});
    };

    std::string volume;
    volume.push_back(static_cast<char>(snapshot.drive));
    volume += ":";

    Elements index_parts;
    const double spin = motion_.seconds(started_at_, now);
    const Text& source = snapshot.source == platform::IndexSource::NtfsMft ? strings::index_ntfs : strings::index_scan;
    switch (snapshot.phase) {
        case platform::IndexPhase::Starting:
            index_parts.push_back(widgets::spinner(spin, palette.accent, glyphs_));
            index_parts.push_back(coloredText(" " + std::string(tr(strings::index_starting)), palette.text_muted));
            break;
        case platform::IndexPhase::Scanning:
            index_parts.push_back(widgets::spinner(spin, palette.accent, glyphs_));
            index_parts.push_back(coloredText(" " + std::string(tr(strings::index_scanning)) + " " +
                                                  core::formatCount(std::max(snapshot.scanned, snapshot.files), separator),
                                              palette.text_muted));
            break;
        case platform::IndexPhase::Metadata:
            index_parts.push_back(coloredText(std::string(tr(source)) + " ", palette.text));
            index_parts.push_back(widgets::progressBar(snapshot.metadata_progress, 10, spin, palette, glyphs_));
            index_parts.push_back(coloredText(" " + std::string(tr(strings::index_metadata)), palette.text_muted));
            break;
        case platform::IndexPhase::Ready: {
            index_parts.push_back(coloredText(tr(source), palette.text));
            if (snapshot.live_updates) {
                const double flash = 1.0 - easing::outCubic(easing::progress(motion_.elapsed(snapshot.last_change, now), 0.0, kLiveFlashSeconds));
                const core::Rgb dot_color = core::lerp(palette.success, palette.accent, flash);
                index_parts.push_back(text(" "));
                index_parts.push_back(coloredText(std::string(glyphs_.live_dot), dot_color));
                index_parts.push_back(coloredText(" " + std::string(tr(strings::index_live)), palette.text_faint));
            }
            break;
        }
        case platform::IndexPhase::Failed:
            index_parts.push_back(coloredText(tr(strings::index_failed), palette.error));
            break;
    }

    Element privilege = options_.elevated
                            ? coloredText(std::string(glyphs_.shield) + " " + tr(strings::status_admin), palette.success)
                            : coloredText(tr(strings::status_user), palette.text_faint);
    const std::string last_search = last_search_seconds_ ? core::formatDuration(*last_search_seconds_) : std::string("-");

    return hbox({
               text(" "),
               field(strings::status_volume, coloredText(volume, palette.text)),
               coloredText(dot, palette.text_faint),
               field(strings::status_index, hbox(std::move(index_parts))),
               coloredText(dot, palette.text_faint),
               field(strings::status_files, coloredText(core::formatCount(snapshot.files, separator), palette.text)),
               coloredText(dot, palette.text_faint),
               field(strings::status_last_search, coloredText(last_search, palette.text)),
               filler(),
               std::move(privilege),
               text(" "),
           }) |
           bgcolor(toColor(palette.surface_raised));
}

Element App::renderHintBar(const core::Palette& palette, const platform::IndexSnapshot& snapshot, int width) const {
    std::vector<widgets::HintItem> items = currentPage().hints();
    items.insert(items.begin(), {"F10", tr(strings::hint_quit)});
    items.insert(items.begin(), {"F1", tr(strings::hint_help)});
    items.push_back({"F5-F7", tr(strings::hint_modes)});
    items.push_back({"F2", tr(strings::hint_language)});
    if (snapshot.elevation_would_help && !options_.elevated) items.push_back({"F9", tr(strings::banner_elevation_key)});
    return widgets::hintBar(items, width, palette);
}

Element App::renderHelp(const core::Palette& palette) const {
    struct HelpLine {
        const char* keys;
        const Text& description;
    };
    const std::array<HelpLine, 16> lines = {{
        {"F5", strings::help_files_mode},
        {"F6", strings::help_content_mode},
        {"F7", strings::help_viewer_mode},
        {"Up Dn PgUp PgDn", strings::help_navigate},
        {"Enter", strings::help_open_result},
        {"Tab", strings::help_next_field},
        {"Esc", strings::help_escape},
        {"F4", strings::help_rescan},
        {"F9", strings::help_elevate},
        {"Ctrl+F  F3", strings::help_find_in_file},
        {"Right click", strings::help_clipboard},
        {"Ctrl+O", strings::help_browse},
        {"Shift+arrows", strings::help_select_text},
        {"F2", strings::help_toggle_language},
        {"F1", strings::help_show_help},
        {"F10  Ctrl+Q", strings::help_quit},
    }};

    Elements rows;
    rows.push_back(text(" "));
    for (const auto& line : lines) {
        rows.push_back(hbox({
            text("  "),
            coloredText(line.keys, palette.accent) | bold | size(WIDTH, EQUAL, 18),
            coloredText(tr(line.description), palette.text),
            text("  "),
        }));
    }
    rows.push_back(text(" "));
    for (const Text* note : {&strings::help_viewer_keys, &strings::help_filters, &strings::help_mouse, &strings::help_layout_note})
        rows.push_back(hbox({text("  "), coloredText(tr(*note), palette.text_muted), text("  ")}));
    rows.push_back(text(" "));
    rows.push_back(coloredText(std::string("InstaSeek ") + INSTASEEK_VERSION + "  " + tr(strings::help_close), palette.text_faint) |
                   hcenter);

    Element title = coloredText(tr(strings::help_title), palette.accent) | bold;
    return widgets::panel(std::move(title), vbox(std::move(rows)) | bgcolor(toColor(palette.surface)), palette.border_focus,
                          glyphs_) |
           bgcolor(toColor(palette.surface)) | size(WIDTH, LESS_THAN, 110);
}

Element App::renderToast(const core::Palette& palette, Clock::time_point now) const {
    const core::Palette faded = core::fadeFromBackground(palette, toastOpacity(now));
    core::Rgb marker = faded.accent;
    if (toast_.kind == ToastKind::Success) marker = faded.success;
    if (toast_.kind == ToastKind::Warning) marker = faded.warning;
    if (toast_.kind == ToastKind::Error) marker = faded.error;
    const double elapsed = std::chrono::duration<double>(now - toast_.shown_at).count();
    const int slide = motion_.enabled() ? static_cast<int>(std::lround((1.0 - easing::outBack(easing::progress(elapsed, 0.0, 0.35))) * 6.0)) : 0;
    const std::string icon(toast_.kind == ToastKind::Success ? glyphs_.check
                                                             : (toast_.kind == ToastKind::Info ? glyphs_.logo_mark : glyphs_.warning));
    Element toast = hbox({
                        coloredText(std::string(glyphs_.selection_marker), marker),
                        coloredText(" " + icon + " ", marker),
                        coloredText(toast_.message + "  ", faded.text),
                    }) |
                    bgcolor(toColor(faded.surface_raised));
    return vbox({
        emptyElement() | size(HEIGHT, EQUAL, 3),
        hbox({filler(), std::move(toast), widgets::spacer(2 + std::max(0, slide))}),
        filler(),
    });
}

}

int runApp(const AppOptions& options) {
    App app(options);
    return app.run();
}

}
