#pragma once

#include "core/encoding.hpp"
#include "core/job_runner.hpp"
#include "core/text_matcher.hpp"
#include "platform/content_scanner.hpp"
#include "platform/shell.hpp"
#include "ui/animated_value.hpp"
#include "ui/controls.hpp"
#include "ui/page.hpp"
#include "ui/result_list.hpp"
#include "ui/text_field.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace instaseek::ui {

struct SearchOptionsState {
    bool case_sensitive = false;
    bool whole_word = false;
    bool regex = false;
    bool skip_binary = true;
    std::optional<core::TextEncoding> encoding;
};

class ContentPage final : public Page {
public:
    ContentPage(Navigator& navigator, const SharedStyle& style, const Motion& motion, SearchOptionsState& options);

    ftxui::Component component() override { return container_; }
    ftxui::Element render(const FrameContext& context) override;
    bool onEvent(ftxui::Event& event, Clock::time_point now) override;
    void poll(Clock::time_point now) override;
    bool animating(Clock::time_point now) const override;
    bool busy() const override;
    void refreshTexts() override;
    std::vector<widgets::HintItem> hints() const override;
    void focusPrimary() override;

    void setLocation(const std::wstring& location);

private:
    void startSearch(Clock::time_point now);
    void cancelSearch();
    void openSelected(bool focus_find);
    void openRowMenu(int x, int y);
    void openBrowseMenu(int x, int y);
    void browse(platform::PickKind kind);
    void markDirty() { dirty_ = true; }
    std::string encodingLabel() const;
    void cycleEncoding();
    ftxui::Element renderRow(std::size_t index, const RowVisual& visual, const FrameContext& context);
    ftxui::Element renderBody(const FrameContext& context);
    ftxui::Element renderTitle(const FrameContext& context) const;

    Navigator& navigator_;
    const SharedStyle& style_;
    const Motion& motion_;
    SearchOptionsState& options_;

    std::string pattern_;
    std::string location_;
    std::string pattern_placeholder_;
    std::string location_placeholder_;
    std::shared_ptr<TextField> pattern_input_;
    std::shared_ptr<TextField> location_input_;
    std::shared_ptr<ButtonControl> browse_button_;
    ftxui::Component options_row_;
    ftxui::Component container_;

    std::shared_ptr<platform::ContentSession> session_;
    std::shared_ptr<const core::TextMatcher> matcher_;
    std::string search_base_;
    std::string message_;
    bool message_is_error_ = false;
    bool dirty_ = true;
    bool finish_reported_ = false;
    Clock::time_point started_at_{};
    ResultList list_;
    AnimatedValue progress_value_{0.3};
    AnimatedValue hits_value_{0.35};
    core::LatestJobRunner runner_;
};

}
