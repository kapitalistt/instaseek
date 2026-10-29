#pragma once

#include "core/job_runner.hpp"
#include "core/name_query.hpp"
#include "platform/index_service.hpp"
#include "ui/animated_value.hpp"
#include "ui/controls.hpp"
#include "ui/page.hpp"
#include "ui/result_list.hpp"
#include "ui/text_field.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace instaseek::ui {

class FilesPage final : public Page {
public:
    FilesPage(platform::IndexService& index, Navigator& navigator, const SharedStyle& style, const Motion& motion);

    ftxui::Component component() override { return container_; }
    ftxui::Element render(const FrameContext& context) override;
    bool onEvent(ftxui::Event& event, Clock::time_point now) override;
    void poll(Clock::time_point now) override;
    bool animating(Clock::time_point now) const override;
    bool busy() const override { return runner_.busy(); }
    void refreshTexts() override;
    std::vector<widgets::HintItem> hints() const override;
    void focusPrimary() override { input_->TakeFocus(); }

private:
    struct Outcome {
        std::string text;
        core::NameQuery query;
        std::vector<std::uint32_t> records;
        std::size_t total = 0;
        double seconds = 0.0;
        std::uint64_t generation = 0;
        std::uint64_t epoch = 0;
        bool refresh = false;
    };

    void submitSearch(bool refresh);
    void activateSelected(bool focus_find);
    void openRowMenu(int x, int y);
    std::optional<std::string> selectedPath(bool* directory) const;
    ftxui::Element renderRow(const core::FileIndex& index, std::size_t row, const RowVisual& visual,
                             const FrameContext& context, std::int64_t local_offset) const;
    ftxui::Element renderHeader(const FrameContext& context) const;
    ftxui::Element renderBody(const FrameContext& context);

    platform::IndexService& index_;
    Navigator& navigator_;
    const SharedStyle& style_;
    const Motion& motion_;

    std::string query_;
    std::string placeholder_;
    std::shared_ptr<TextField> input_;
    ftxui::Component container_;

    Outcome current_;
    bool has_results_ = false;
    std::string requested_text_;
    std::uint64_t requested_generation_ = 0;
    Clock::time_point last_refresh_{};
    ResultList list_;
    AnimatedValue total_value_{0.45};

    std::mutex pending_mutex_;
    std::optional<Outcome> pending_;
    core::LatestJobRunner runner_;
};

}
