#pragma once

#include "core/chunk_search.hpp"
#include "core/encoding.hpp"
#include "core/text_matcher.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace instaseek::platform {

struct ContentRequest {
    std::wstring location;
    std::optional<core::TextEncoding> forced_encoding;
    std::size_t max_hits = 10'000;
    bool skip_binary = true;
    unsigned threads = 0;
};

struct ContentFile {
    std::wstring path;
    std::string display;
    core::TextEncoding encoding = core::TextEncoding::Utf8;
    std::uint64_t size = 0;
};

struct ContentRow {
    std::uint32_t file = 0;
    core::TextHit hit;
};

enum class ContentError { None, LocationMissing, NothingToSearch };

class ContentSession {
public:
    using Clock = std::chrono::steady_clock;

    explicit ContentSession(std::function<void()> notify);

    std::uint32_t registerFile(ContentFile file);
    void appendRows(std::uint32_t file, std::vector<core::TextHit>&& hits);
    void finish(bool cancelled);
    void setError(ContentError error);
    void notify(bool force);

    std::size_t rowCount() const;
    std::optional<ContentRow> row(std::size_t index) const;
    std::optional<ContentFile> file(std::uint32_t index) const;
    ContentError error() const;
    double elapsedSeconds() const;

    template <typename Visitor>
    void visitRows(std::size_t first, std::size_t count, Visitor&& visitor) const {
        std::lock_guard lock(mutex_);
        const std::size_t end = std::min(rows_.size(), first + count);
        for (std::size_t i = first; i < end; ++i) visitor(i, rows_[i], files_[rows_[i].file]);
    }

    std::atomic<std::uint64_t> bytes_total{0};
    std::atomic<std::uint64_t> bytes_done{0};
    std::atomic<std::uint64_t> files_total{0};
    std::atomic<std::uint64_t> files_done{0};
    std::atomic<std::uint64_t> files_matched{0};
    std::atomic<std::uint64_t> files_skipped{0};
    std::atomic<bool> enumeration_done{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> limit_reached{false};

private:
    std::function<void()> notify_;
    std::atomic<long long> last_notify_ms_{0};
    mutable std::mutex mutex_;
    std::vector<ContentFile> files_;
    std::vector<ContentRow> rows_;
    ContentError error_ = ContentError::None;
    Clock::time_point started_ = Clock::now();
    Clock::time_point finished_at_{};
};

void runContentSearch(const ContentRequest& request, const core::TextMatcher& matcher, ContentSession& session,
                      const std::atomic<bool>& cancel);

}
