#pragma once

#include "core/job_runner.hpp"
#include "core/text_matcher.hpp"
#include "platform/document.hpp"
#include "ui/content_page.hpp"
#include "ui/controls.hpp"
#include "ui/page.hpp"
#include "ui/text_field.hpp"

#include <ftxui/screen/box.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace instaseek::ui {

class ViewerPage final : public Page {
public:
    ViewerPage(Navigator& navigator, const SharedStyle& style, const Motion& motion, SearchOptionsState& options);

    ftxui::Component component() override { return container_; }
    ftxui::Element render(const FrameContext& context) override;
    bool onEvent(ftxui::Event& event, Clock::time_point now) override;
    void poll(Clock::time_point now) override;
    bool animating(Clock::time_point now) const override;
    bool busy() const override { return find_running_; }
    void refreshTexts() override;
    std::vector<widgets::HintItem> hints() const override;
    void focusPrimary() override;

    void open(const ViewerTarget& target, Clock::time_point now);

private:
    bool openPath(const std::wstring& path, std::optional<core::TextEncoding> encoding);
    bool onDocumentKey(ftxui::Event& event);
    void scrollLines(long long delta);
    void jumpTo(std::uint64_t line_start, std::optional<std::uint64_t> line_number, bool flash);
    std::uint64_t stepBack(std::uint64_t position, std::size_t lines, std::size_t& moved) const;
    std::optional<std::uint64_t> topLineNumber();
    void focusFind();
    void openGoto();
    void closeGoto();
    void submitGoto();
    void applyGoto(const std::string& text);
    void updateHighlight();
    void findNext(bool backward);
    void setStatus(std::string message, bool error);
    void openTypedPath();
    void browse();
    std::optional<std::uint64_t> lineAt(int y) const;
    void openLineMenu(std::uint64_t line_start, int x, int y);

    ftxui::Element renderDocument(const FrameContext& context);
    ftxui::Element renderTitle(const FrameContext& context) const;
    ftxui::Element renderFooter(const FrameContext& context);
    std::string findLabel() const;

    Navigator& navigator_;
    const SharedStyle& style_;
    const Motion& motion_;
    SearchOptionsState& options_;

    std::string path_text_;
    std::string path_placeholder_;
    std::string find_text_;
    std::string find_placeholder_;
    std::string goto_text_;
    std::string goto_placeholder_;
    std::shared_ptr<TextField> path_field_;
    std::shared_ptr<TextField> find_field_;
    std::shared_ptr<TextField> goto_field_;
    std::shared_ptr<ButtonControl> browse_button_;
    ftxui::Component anchor_;
    ftxui::Component container_;
    bool goto_visible_ = false;

    std::shared_ptr<platform::Document> document_;
    std::uint64_t top_ = 0;
    std::size_t horizontal_ = 0;
    std::optional<std::uint64_t> top_line_;
    std::optional<std::uint64_t> cached_top_offset_;
    std::optional<std::uint64_t> cached_top_line_;
    std::optional<std::uint64_t> target_line_;
    Clock::time_point target_flash_at_{};
    std::optional<std::uint64_t> current_match_line_;
    std::shared_ptr<const core::TextMatcher> highlight_;
    std::optional<std::uint64_t> pending_goto_line_;
    bool pending_match_status_ = false;
    bool index_was_ready_ = false;
    int rows_ = 20;
    ftxui::Box document_box_{};
    std::vector<ftxui::Box> row_boxes_;
    std::vector<std::uint64_t> row_offsets_;
    std::string status_;
    bool status_error_ = false;
    Clock::time_point find_started_{};
    bool find_running_ = false;

    std::mutex find_mutex_;
    std::optional<std::optional<platform::DocumentMatch>> find_result_;
    core::LatestJobRunner runner_;
};

}
