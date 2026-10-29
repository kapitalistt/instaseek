#pragma once

#include "core/encoding.hpp"
#include "core/text_matcher.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace instaseek::core {

inline constexpr std::uint64_t kMaxLineSpillBytes = 1u << 20;
inline constexpr std::size_t kPreviewBytesBefore = 80;
inline constexpr std::size_t kPreviewBytesAfter = 220;

struct TextHit {
    std::uint64_t line_number = 0;
    std::uint64_t line_offset = 0;
    std::uint64_t match_offset = 0;
    std::string preview;
    std::uint32_t match_start = 0;
    std::uint32_t match_length = 0;
    bool preview_cut_left = false;
    bool preview_cut_right = false;
};

struct ChunkRange {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

struct ByteWindow {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

struct DataExtent {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    TextEncoding encoding = TextEncoding::Utf8;
};

struct ChunkOutcome {
    std::vector<TextHit> hits;
    std::uint64_t newline_count = 0;
    std::uint64_t bytes_covered = 0;
    bool hit_limit = false;
    bool cancelled = false;
};

std::vector<ChunkRange> planChunks(const DataExtent& extent, std::uint64_t chunk_bytes);
ByteWindow windowForChunk(const ChunkRange& chunk, const DataExtent& extent);

ChunkOutcome searchChunk(const std::uint8_t* window_data, const ByteWindow& window, const ChunkRange& chunk,
                         const DataExtent& extent, const TextMatcher& matcher, std::size_t max_hits,
                         const std::atomic<bool>* cancel);

void assignLineNumbers(std::vector<ChunkOutcome>& outcomes);

std::vector<TextHit> searchBuffer(const std::uint8_t* data, std::size_t size, const DataExtent& extent,
                                  const TextMatcher& matcher, std::uint64_t chunk_bytes, std::size_t max_hits);

std::string makePreview(std::string_view line, std::size_t match_start, std::size_t match_length,
                        std::uint32_t& preview_match_start, std::uint32_t& preview_match_length, bool& cut_left,
                        bool& cut_right);

}
