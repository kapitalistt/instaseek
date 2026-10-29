#pragma once

#include "core/encoding.hpp"
#include "core/line_index.hpp"
#include "core/text_matcher.hpp"
#include "platform/mapped_file.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace instaseek::platform {

struct DocumentLine {
    std::uint64_t offset = 0;
    std::uint64_t next = 0;
    std::string text;
    bool truncated = false;
};

struct DocumentMatch {
    std::uint64_t line_offset = 0;
    std::uint64_t match_offset = 0;
};

class Document {
public:
    static constexpr std::size_t kMaxLineBytes = 64 * 1024;

    static std::unique_ptr<Document> open(const std::wstring& path, std::optional<core::TextEncoding> forced,
                                          std::function<void()> notify, unsigned long& error);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    const std::wstring& path() const { return file_->path(); }
    const std::string& displayPath() const { return display_path_; }
    std::uint64_t size() const { return file_->size(); }
    std::uint64_t dataBegin() const { return data_begin_; }
    core::TextEncoding encoding() const { return encoding_; }
    bool looksBinary() const { return looks_binary_; }

    std::vector<DocumentLine> readLines(std::uint64_t offset, std::size_t count) const;
    std::uint64_t alignToLineStart(std::uint64_t offset) const;
    std::uint64_t linesBefore(std::uint64_t line_start, std::size_t lines) const;
    std::uint64_t lastPageStart(std::size_t rows) const;

    double indexProgress() const;
    bool indexReady() const;
    bool indexFailed() const { return index_failed_.load(); }
    std::optional<std::uint64_t> lineCount() const;
    std::optional<std::uint64_t> lineNumberAt(std::uint64_t offset) const;
    std::optional<std::uint64_t> offsetOfLine(std::uint64_t line_number) const;

    std::optional<DocumentMatch> find(const core::TextMatcher& matcher, std::uint64_t from, bool backward,
                                      const std::atomic<bool>& cancel) const;

private:
    Document() = default;

    void buildLineIndex();
    std::uint64_t alignUnit(std::uint64_t offset) const;
    std::optional<std::uint64_t> previousNewline(std::uint64_t before, std::uint64_t limit) const;
    std::uint64_t countNewlinesBetween(std::uint64_t from, std::uint64_t to) const;
    std::optional<std::uint64_t> advanceLines(std::uint64_t from, std::uint64_t lines) const;
    std::optional<DocumentMatch> findForward(const core::TextMatcher& matcher, std::uint64_t from,
                                             const std::atomic<bool>& cancel) const;
    std::optional<DocumentMatch> findBackward(const core::TextMatcher& matcher, std::uint64_t before,
                                              const std::atomic<bool>& cancel) const;

    std::unique_ptr<MappedFile> file_;
    std::string display_path_;
    core::TextEncoding encoding_ = core::TextEncoding::Utf8;
    std::uint64_t data_begin_ = 0;
    std::size_t unit_ = 1;
    bool looks_binary_ = false;
    std::function<void()> notify_;

    mutable std::mutex view_mutex_;
    mutable MappedView view_;

    mutable std::mutex index_mutex_;
    core::LineIndex line_index_;
    std::atomic<bool> index_failed_{false};
    std::atomic<bool> stop_{false};
    std::thread indexer_;
};

}
