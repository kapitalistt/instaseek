#include "core/chunk_search.hpp"

#include "core/utf8.hpp"

#include <algorithm>

namespace instaseek::core {

namespace {

void appendSanitized(std::string& out, std::string_view text) {
    std::string decoded;
    appendAsUtf8(decoded, reinterpret_cast<const std::uint8_t*>(text.data()), text.size(), TextEncoding::Utf8);
    for (char& c : decoded) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) c = ' ';
    }
    out += decoded;
}

std::string_view withoutCarriageReturn(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}

}

std::vector<ChunkRange> planChunks(const DataExtent& extent, std::uint64_t chunk_bytes) {
    std::vector<ChunkRange> chunks;
    if (extent.end <= extent.begin) return chunks;
    const std::uint64_t unit = encodingUnitSize(extent.encoding);
    chunk_bytes = std::max<std::uint64_t>(chunk_bytes, unit);
    chunk_bytes -= chunk_bytes % unit;
    for (std::uint64_t begin = extent.begin; begin < extent.end; begin += chunk_bytes)
        chunks.push_back({begin, std::min(extent.end, begin + chunk_bytes)});
    return chunks;
}

ByteWindow windowForChunk(const ChunkRange& chunk, const DataExtent& extent) {
    const std::uint64_t unit = encodingUnitSize(extent.encoding);
    const std::uint64_t begin = chunk.begin > extent.begin ? chunk.begin - unit : chunk.begin;
    const std::uint64_t spill_end = chunk.end + kMaxLineSpillBytes;
    const std::uint64_t end = std::min(extent.end, spill_end);
    return {begin, end - begin};
}

std::string makePreview(std::string_view line, std::size_t match_start, std::size_t match_length,
                        std::uint32_t& preview_match_start, std::uint32_t& preview_match_length, bool& cut_left,
                        bool& cut_right) {
    match_start = std::min(match_start, line.size());
    match_length = std::min(match_length, line.size() - match_start);
    const std::size_t from =
        match_start > kPreviewBytesBefore ? utf8::alignToCodepointStart(line, match_start - kPreviewBytesBefore) : 0;
    const std::size_t wanted_end = match_start + match_length + kPreviewBytesAfter;
    const std::size_t to = wanted_end >= line.size() ? line.size() : utf8::alignToCodepointStart(line, wanted_end);
    cut_left = from > 0;
    cut_right = to < line.size();

    std::string preview;
    appendSanitized(preview, line.substr(from, match_start - from));
    preview_match_start = static_cast<std::uint32_t>(preview.size());
    appendSanitized(preview, line.substr(match_start, match_length));
    preview_match_length = static_cast<std::uint32_t>(preview.size()) - preview_match_start;
    appendSanitized(preview, line.substr(match_start + match_length, to - match_start - match_length));
    return preview;
}

ChunkOutcome searchChunk(const std::uint8_t* window_data, const ByteWindow& window, const ChunkRange& chunk,
                         const DataExtent& extent, const TextMatcher& matcher, std::size_t max_hits,
                         const std::atomic<bool>* cancel) {
    ChunkOutcome outcome;
    const TextEncoding encoding = extent.encoding;
    const std::uint64_t unit = encodingUnitSize(encoding);
    const std::uint64_t window_begin = window.offset;
    const std::uint64_t window_end = window.offset + window.length;
    const auto at = [&](std::uint64_t absolute) { return window_data + (absolute - window_begin); };
    const auto window_size = static_cast<std::size_t>(window.length);

    outcome.bytes_covered = chunk.end - chunk.begin;
    outcome.newline_count = countNewlines(at(chunk.begin), static_cast<std::size_t>(chunk.end - chunk.begin), encoding);
    if (max_hits == 0) return outcome;

    std::uint64_t owned_begin = chunk.begin;
    if (chunk.begin > extent.begin) {
        const auto newline =
            findNewline(window_data, window_size, static_cast<std::size_t>(chunk.begin - unit - window_begin), encoding);
        if (!newline) return outcome;
        owned_begin = window_begin + *newline + unit;
    }
    if (owned_begin >= chunk.end) return outcome;

    std::uint64_t owned_end = extent.end;
    if (chunk.end < extent.end) {
        const auto newline =
            findNewline(window_data, window_size, static_cast<std::size_t>(chunk.end - unit - window_begin), encoding);
        owned_end = newline ? window_begin + *newline + unit : window_end;
    }

    const std::uint64_t lines_before_owned =
        countNewlines(at(chunk.begin), static_cast<std::size_t>(owned_begin - chunk.begin), encoding);

    const bool identity = encoding == TextEncoding::Utf8;
    std::string decoded;
    std::vector<std::uint64_t> line_starts;
    std::string_view text;
    if (identity) {
        text = std::string_view(reinterpret_cast<const char*>(at(owned_begin)),
                                static_cast<std::size_t>(owned_end - owned_begin));
    } else {
        const std::uint8_t* region = at(owned_begin);
        const auto region_size = static_cast<std::size_t>(owned_end - owned_begin);
        decoded.reserve(region_size);
        std::size_t cursor = 0;
        while (cursor < region_size) {
            line_starts.push_back(owned_begin + cursor);
            const auto newline = findNewline(region, region_size, cursor, encoding);
            const std::size_t line_end = newline ? *newline + static_cast<std::size_t>(unit) : region_size;
            appendAsUtf8(decoded, region + cursor, line_end - cursor, encoding);
            cursor = line_end;
        }
        if (line_starts.empty()) line_starts.push_back(owned_begin);
        text = decoded;
    }

    std::size_t position = 0;
    std::size_t counted_to = 0;
    std::uint64_t line_index = 0;
    while (position <= text.size()) {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            outcome.cancelled = true;
            break;
        }
        const auto match = matcher.find(text, position);
        if (!match) break;

        std::size_t line_start = match->start;
        while (line_start > position && text[line_start - 1] != '\n') --line_start;
        const std::size_t newline = text.find('\n', match->start + match->length);
        const std::size_t line_end = newline == std::string_view::npos ? text.size() : newline;

        line_index += static_cast<std::uint64_t>(
            std::count(text.begin() + static_cast<std::ptrdiff_t>(counted_to),
                       text.begin() + static_cast<std::ptrdiff_t>(line_start), '\n'));
        counted_to = line_start;

        const std::string_view line = withoutCarriageReturn(text.substr(line_start, line_end - line_start));
        TextHit hit;
        hit.line_number = lines_before_owned + line_index;
        if (identity) {
            hit.line_offset = owned_begin + line_start;
            hit.match_offset = owned_begin + match->start;
        } else {
            const std::size_t index = std::min<std::size_t>(static_cast<std::size_t>(line_index), line_starts.size() - 1);
            hit.line_offset = line_starts[index];
            hit.match_offset = hit.line_offset;
        }
        hit.preview = makePreview(line, match->start - line_start, match->length, hit.match_start, hit.match_length,
                                  hit.preview_cut_left, hit.preview_cut_right);
        outcome.hits.push_back(std::move(hit));

        if (outcome.hits.size() >= max_hits) {
            outcome.hit_limit = true;
            break;
        }
        position = line_end + 1;
    }
    return outcome;
}

void assignLineNumbers(std::vector<ChunkOutcome>& outcomes) {
    std::uint64_t newlines_before = 0;
    for (auto& outcome : outcomes) {
        for (auto& hit : outcome.hits) hit.line_number = 1 + newlines_before + hit.line_number;
        newlines_before += outcome.newline_count;
    }
}

std::vector<TextHit> searchBuffer(const std::uint8_t* data, std::size_t size, const DataExtent& extent,
                                  const TextMatcher& matcher, std::uint64_t chunk_bytes, std::size_t max_hits) {
    DataExtent clamped = extent;
    clamped.end = std::min<std::uint64_t>(clamped.end, size);
    std::vector<ChunkOutcome> outcomes;
    std::size_t found = 0;
    for (const ChunkRange& chunk : planChunks(clamped, chunk_bytes)) {
        const ByteWindow window = windowForChunk(chunk, clamped);
        const std::size_t remaining = found >= max_hits ? 0 : max_hits - found;
        outcomes.push_back(searchChunk(data + window.offset, window, chunk, clamped, matcher, remaining, nullptr));
        found += outcomes.back().hits.size();
    }
    assignLineNumbers(outcomes);
    std::vector<TextHit> hits;
    for (auto& outcome : outcomes)
        for (auto& hit : outcome.hits) hits.push_back(std::move(hit));
    return hits;
}

}
