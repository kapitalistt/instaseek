#include "platform/document.hpp"

#include "core/chunk_search.hpp"
#include "platform/win.hpp"

#include <algorithm>
#include <chrono>

namespace instaseek::platform {

namespace {

constexpr std::size_t kSniffBytes = 64 * 1024;
constexpr std::uint64_t kIndexWindowBytes = 16ull << 20;
constexpr std::uint64_t kScanWindowBytes = 16ull << 20;
constexpr std::uint64_t kBackwardBlockBytes = 256ull << 10;
constexpr std::uint64_t kViewWindowBytes = 4ull << 20;
constexpr std::uint64_t kFindBlockBytes = 8ull << 20;

long long steadyMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct ConsumeContext {
    core::LineIndex* index = nullptr;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

void runConsume(void* raw) {
    auto* context = static_cast<ConsumeContext*>(raw);
    context->index->consume(context->data, context->size);
}

struct FindContext {
    const std::uint8_t* data = nullptr;
    core::ByteWindow window;
    core::ChunkRange chunk;
    core::DataExtent extent;
    const core::TextMatcher* matcher = nullptr;
    std::size_t max_hits = 0;
    const std::atomic<bool>* cancel = nullptr;
    core::ChunkOutcome outcome;
};

void runFind(void* raw) {
    auto* context = static_cast<FindContext*>(raw);
    context->outcome = core::searchChunk(context->data, context->window, context->chunk, context->extent,
                                         *context->matcher, context->max_hits, context->cancel);
}

struct NewlineContext {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    core::TextEncoding encoding = core::TextEncoding::Utf8;
    std::uint64_t count = 0;
};

void runCountNewlines(void* raw) {
    auto* context = static_cast<NewlineContext*>(raw);
    context->count = core::countNewlines(context->data, context->size, context->encoding);
}

}

std::unique_ptr<Document> Document::open(const std::wstring& path, std::optional<core::TextEncoding> forced,
                                         std::function<void()> notify, unsigned long& error) {
    auto mapped = MappedFile::open(path, error);
    if (!mapped) return nullptr;

    std::unique_ptr<Document> document(new Document());
    document->display_path_ = toUtf8(mapped->path());
    MappedView sniff;
    if (!mapped->map(0, kSniffBytes, sniff, error)) return nullptr;
    core::EncodingGuess guess = core::detectEncoding(sniff.data(), sniff.size());
    if (forced) {
        guess.encoding = *forced;
        guess.bom_length = core::bomLength(sniff.data(), sniff.size(), *forced);
    }
    document->encoding_ = guess.encoding;
    document->data_begin_ = std::min<std::uint64_t>(guess.bom_length, mapped->size());
    document->unit_ = core::encodingUnitSize(guess.encoding);
    document->looks_binary_ = guess.looks_binary;
    document->notify_ = std::move(notify);
    document->file_ = std::move(mapped);
    {
        std::lock_guard lock(document->index_mutex_);
        document->line_index_.reset(document->data_begin_, document->encoding_);
    }
    Document* raw = document.get();
    document->indexer_ = std::thread([raw] { raw->buildLineIndex(); });
    return document;
}

Document::~Document() {
    stop_ = true;
    if (indexer_.joinable()) indexer_.join();
}

std::uint64_t Document::alignUnit(std::uint64_t offset) const {
    offset = std::clamp(offset, data_begin_, size());
    if (unit_ == 2 && (offset - data_begin_) % 2 != 0) --offset;
    return offset;
}

void Document::buildLineIndex() {
    std::uint64_t position = data_begin_;
    const std::uint64_t total = size();
    long long last_notify = 0;
    while (position < total && !stop_) {
        std::uint64_t length = std::min(kIndexWindowBytes, total - position);
        if (unit_ == 2 && length % 2 != 0 && position + length < total) --length;
        MappedView view;
        unsigned long error = 0;
        if (!file_->map(position, static_cast<std::size_t>(length), view, error) || view.size() != length) {
            index_failed_ = true;
            break;
        }
        ConsumeContext context{&line_index_, view.data(), view.size()};
        bool consumed = false;
        {
            std::lock_guard lock(index_mutex_);
            consumed = runGuardedAgainstPageErrors(&runConsume, &context);
        }
        if (!consumed) {
            index_failed_ = true;
            break;
        }
        position += length;
        const long long now = steadyMilliseconds();
        if (notify_ && now - last_notify > 200) {
            last_notify = now;
            notify_();
        }
    }
    if (!index_failed_ && !stop_) {
        std::lock_guard lock(index_mutex_);
        line_index_.finish();
    }
    if (notify_ && !stop_) notify_();
}

double Document::indexProgress() const {
    const std::uint64_t total = size();
    if (total <= data_begin_) return 1.0;
    std::lock_guard lock(index_mutex_);
    if (line_index_.finished()) return 1.0;
    return static_cast<double>(line_index_.position() - data_begin_) / static_cast<double>(total - data_begin_);
}

bool Document::indexReady() const {
    std::lock_guard lock(index_mutex_);
    return line_index_.finished();
}

std::optional<std::uint64_t> Document::lineCount() const {
    std::lock_guard lock(index_mutex_);
    if (!line_index_.finished()) return std::nullopt;
    return line_index_.lineCount();
}

std::vector<DocumentLine> Document::readLines(std::uint64_t offset, std::size_t count) const {
    std::vector<DocumentLine> lines;
    const std::uint64_t total = size();
    std::uint64_t cursor = alignUnit(offset);
    std::lock_guard lock(view_mutex_);
    while (lines.size() < count && cursor < total) {
        const std::uint64_t wanted = std::min<std::uint64_t>(kMaxLineBytes + unit_, total - cursor);
        if (!view_.covers(cursor, wanted)) {
            unsigned long error = 0;
            const std::uint64_t length = std::min<std::uint64_t>(std::max(kViewWindowBytes, wanted), total - cursor);
            if (!file_->map(cursor, static_cast<std::size_t>(length), view_, error) || !view_.covers(cursor, wanted)) break;
        }
        const std::uint8_t* data = view_.data() + (cursor - view_.offset());
        std::size_t search = static_cast<std::size_t>(wanted);
        if (unit_ == 2) search -= search % 2;

        DocumentLine line;
        line.offset = cursor;
        std::size_t content = 0;
        const auto newline = core::findNewline(data, search, 0, encoding_);
        if (newline) {
            content = *newline;
            line.next = cursor + *newline + unit_;
        } else if (cursor + wanted >= total) {
            content = search;
            line.next = total;
        } else {
            content = kMaxLineBytes;
            line.next = cursor + kMaxLineBytes;
            line.truncated = true;
        }
        line.text = core::decodeToUtf8(data, content, encoding_);
        if (!line.text.empty() && line.text.back() == '\r') line.text.pop_back();
        lines.push_back(std::move(line));
        cursor = lines.back().next;
    }
    return lines;
}

std::optional<std::uint64_t> Document::previousNewline(std::uint64_t before, std::uint64_t limit) const {
    std::uint64_t block_end = alignUnit(before);
    limit = alignUnit(limit);
    while (block_end > limit) {
        std::uint64_t block_begin = block_end - limit > kBackwardBlockBytes ? block_end - kBackwardBlockBytes : limit;
        block_begin = alignUnit(block_begin);
        if (block_begin < limit) block_begin = limit;
        MappedView view;
        unsigned long error = 0;
        const std::uint64_t length = block_end - block_begin;
        if (!file_->map(block_begin, static_cast<std::size_t>(length), view, error) || view.size() != length)
            return std::nullopt;
        for (std::uint64_t position = length; position >= unit_;) {
            position -= unit_;
            if (core::isNewlineUnit(view.data() + position, encoding_)) return block_begin + position;
        }
        block_end = block_begin;
    }
    return std::nullopt;
}

std::uint64_t Document::alignToLineStart(std::uint64_t offset) const {
    const std::uint64_t position = alignUnit(offset);
    if (position <= data_begin_) return data_begin_;
    const std::uint64_t limit = position - data_begin_ > kMaxLineBytes ? alignUnit(position - kMaxLineBytes) : data_begin_;
    const auto newline = previousNewline(position, limit);
    if (newline) return *newline + unit_;
    return limit;
}

std::uint64_t Document::linesBefore(std::uint64_t line_start, std::size_t lines) const {
    std::uint64_t position = alignUnit(line_start);
    for (std::size_t i = 0; i < lines && position > data_begin_; ++i) position = alignToLineStart(position - unit_);
    return position;
}

std::uint64_t Document::lastPageStart(std::size_t rows) const {
    if (rows == 0) rows = 1;
    const std::uint64_t last = alignToLineStart(size());
    if (last == size() && last > data_begin_) return linesBefore(last, rows);
    return linesBefore(last, rows - 1);
}

std::uint64_t Document::countNewlinesBetween(std::uint64_t from, std::uint64_t to) const {
    from = alignUnit(from);
    to = alignUnit(to);
    std::uint64_t count = 0;
    while (from < to) {
        const std::uint64_t length = std::min(kScanWindowBytes, to - from);
        MappedView view;
        unsigned long error = 0;
        if (!file_->map(from, static_cast<std::size_t>(length), view, error) || view.size() != length) break;
        NewlineContext context{view.data(), view.size(), encoding_, 0};
        if (!runGuardedAgainstPageErrors(&runCountNewlines, &context)) break;
        count += context.count;
        from += length;
    }
    return count;
}

std::optional<std::uint64_t> Document::advanceLines(std::uint64_t from, std::uint64_t lines) const {
    std::uint64_t position = alignUnit(from);
    const std::uint64_t total = size();
    while (lines > 0) {
        if (position >= total) return std::nullopt;
        std::uint64_t length = std::min(kScanWindowBytes, total - position);
        if (unit_ == 2) length -= length % 2;
        if (length == 0) return std::nullopt;
        MappedView view;
        unsigned long error = 0;
        if (!file_->map(position, static_cast<std::size_t>(length), view, error) || view.size() != length)
            return std::nullopt;
        std::size_t cursor = 0;
        while (lines > 0) {
            const auto newline = core::findNewline(view.data(), view.size(), cursor, encoding_);
            if (!newline) break;
            cursor = *newline + unit_;
            --lines;
        }
        position += lines == 0 ? cursor : length;
    }
    return position;
}

std::optional<std::uint64_t> Document::lineNumberAt(std::uint64_t offset) const {
    core::LineIndex::Checkpoint checkpoint;
    {
        std::lock_guard lock(index_mutex_);
        if (offset > line_index_.position() && !line_index_.finished()) return std::nullopt;
        checkpoint = line_index_.checkpointForOffset(offset);
    }
    return checkpoint.line + countNewlinesBetween(checkpoint.offset, offset) + 1;
}

std::optional<std::uint64_t> Document::offsetOfLine(std::uint64_t line_number) const {
    if (line_number <= 1) return data_begin_;
    std::uint64_t target = line_number - 1;
    core::LineIndex::Checkpoint checkpoint;
    {
        std::lock_guard lock(index_mutex_);
        if (line_index_.finished()) target = std::min(target, line_index_.lineCount() - 1);
        else if (line_index_.newlineCount() < target) return std::nullopt;
        checkpoint = line_index_.checkpointForLine(target);
    }
    return advanceLines(checkpoint.offset, target - checkpoint.line);
}

std::optional<DocumentMatch> Document::find(const core::TextMatcher& matcher, std::uint64_t from, bool backward,
                                            const std::atomic<bool>& cancel) const {
    return backward ? findBackward(matcher, from, cancel) : findForward(matcher, from, cancel);
}

std::optional<DocumentMatch> Document::findForward(const core::TextMatcher& matcher, std::uint64_t from,
                                                   const std::atomic<bool>& cancel) const {
    const core::DataExtent extent{alignUnit(from), size(), encoding_};
    for (const core::ChunkRange& chunk : core::planChunks(extent, kFindBlockBytes)) {
        if (cancel.load()) return std::nullopt;
        const core::ByteWindow window = core::windowForChunk(chunk, extent);
        MappedView view;
        unsigned long error = 0;
        if (!file_->map(window.offset, static_cast<std::size_t>(window.length), view, error) ||
            view.size() != window.length)
            return std::nullopt;
        FindContext context;
        context.data = view.data();
        context.window = window;
        context.chunk = chunk;
        context.extent = extent;
        context.matcher = &matcher;
        context.max_hits = 1;
        context.cancel = &cancel;
        if (!runGuardedAgainstPageErrors(&runFind, &context)) return std::nullopt;
        if (!context.outcome.hits.empty()) {
            const core::TextHit& hit = context.outcome.hits.front();
            return DocumentMatch{hit.line_offset, hit.match_offset};
        }
    }
    return std::nullopt;
}

std::optional<DocumentMatch> Document::findBackward(const core::TextMatcher& matcher, std::uint64_t before,
                                                    const std::atomic<bool>& cancel) const {
    std::uint64_t end = alignToLineStart(before);
    while (end > data_begin_) {
        if (cancel.load()) return std::nullopt;
        const std::uint64_t guess = end - data_begin_ > kFindBlockBytes ? end - kFindBlockBytes : data_begin_;
        std::uint64_t begin = alignToLineStart(guess);
        if (begin >= end) begin = linesBefore(end, 1);
        if (begin >= end) break;

        const core::DataExtent extent{begin, end, encoding_};
        const core::ChunkRange chunk{begin, end};
        const core::ByteWindow window = core::windowForChunk(chunk, extent);
        MappedView view;
        unsigned long error = 0;
        if (!file_->map(window.offset, static_cast<std::size_t>(window.length), view, error) ||
            view.size() != window.length)
            return std::nullopt;
        FindContext context;
        context.data = view.data();
        context.window = window;
        context.chunk = chunk;
        context.extent = extent;
        context.matcher = &matcher;
        context.max_hits = 1'000'000;
        context.cancel = &cancel;
        if (!runGuardedAgainstPageErrors(&runFind, &context)) return std::nullopt;
        if (!context.outcome.hits.empty()) {
            const core::TextHit& hit = context.outcome.hits.back();
            return DocumentMatch{hit.line_offset, hit.match_offset};
        }
        end = begin;
    }
    return std::nullopt;
}

}
