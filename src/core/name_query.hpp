#pragma once

#include "core/file_index.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace instaseek::core {

enum class EntryKind { Any, Files, Folders };

struct NameQuery {
    std::vector<std::vector<std::string>> term_groups;
    std::vector<std::string> excluded_terms;
    std::vector<std::string> path_terms;
    std::vector<std::string> extensions;
    std::optional<std::uint64_t> min_size;
    std::optional<std::uint64_t> max_size;
    std::optional<std::int64_t> modified_from;
    std::optional<std::int64_t> modified_before;
    EntryKind kind = EntryKind::Any;
    std::vector<std::string> invalid_tokens;

    bool empty() const;
    std::vector<std::string> highlightTerms() const;
    bool needsMetadata() const { return min_size || max_size || modified_from || modified_before; }
};

struct QueryClock {
    std::int64_t now_utc_ticks = 0;
    std::int64_t local_offset_ticks = 0;
};

NameQuery parseNameQuery(std::string_view text, const QueryClock& clock);
std::optional<std::uint64_t> parseByteSize(std::string_view text);

bool matchesNameQuery(const NameQuery& query, const FileIndex& index, std::uint32_t record);

struct NameSearchResult {
    std::vector<std::uint32_t> records;
    std::size_t total_matches = 0;
    double seconds = 0.0;
    bool cancelled = false;
    bool metadata_incomplete = false;
};

NameSearchResult searchNames(const FileIndex& index, const NameQuery& query, std::size_t limit, unsigned threads,
                             const std::atomic<bool>* cancel);

}
