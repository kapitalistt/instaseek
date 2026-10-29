#include "core/name_query.hpp"

#include "core/file_kind.hpp"
#include "core/format.hpp"
#include "core/utf8.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace instaseek::core {

namespace {

std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    bool has_token = false;
    for (const char c : text) {
        if (c == '"') {
            in_quotes = !in_quotes;
            has_token = true;
            continue;
        }
        if (!in_quotes && (c == ' ' || c == '\t')) {
            if (has_token && !current.empty()) tokens.push_back(current);
            current.clear();
            has_token = false;
            continue;
        }
        current.push_back(c);
        has_token = true;
    }
    if (has_token && !current.empty()) tokens.push_back(current);
    return tokens;
}

std::vector<std::string> splitList(std::string_view value) {
    std::vector<std::string> items;
    std::string current;
    for (const char c : value) {
        if (c == ',' || c == ';' || c == '|') {
            if (!current.empty()) items.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) items.push_back(current);
    return items;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool parseUnsigned(std::string_view text, int& value) {
    if (text.empty() || text.size() > 9) return false;
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    return true;
}

std::optional<CivilDate> parseCivilDate(std::string_view text) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == '-' || c == '.' || c == '/') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    if (parts.size() != 3) return std::nullopt;
    int a = 0;
    int b = 0;
    int c = 0;
    if (!parseUnsigned(parts[0], a) || !parseUnsigned(parts[1], b) || !parseUnsigned(parts[2], c)) return std::nullopt;
    CivilDate date;
    if (parts[0].size() == 4) date = {a, b, c};
    else if (parts[2].size() == 4) date = {c, b, a};
    else return std::nullopt;
    if (!isValidCivilDate(date.year, date.month, date.day)) return std::nullopt;
    return date;
}

struct TickRange {
    std::optional<std::int64_t> from;
    std::optional<std::int64_t> before;
};

std::optional<std::int64_t> dayStartUtc(std::string_view text, const QueryClock& clock, int extra_days) {
    const auto date = parseCivilDate(text);
    if (!date) return std::nullopt;
    return fileTimeFromCivil(date->year, date->month, date->day) + extra_days * kTicksPerDay - clock.local_offset_ticks;
}

std::int64_t todayStartUtc(const QueryClock& clock) {
    const std::int64_t local_now = clock.now_utc_ticks + clock.local_offset_ticks;
    return local_now - local_now % kTicksPerDay - clock.local_offset_ticks;
}

std::optional<TickRange> parseDateFilter(std::string_view value, const QueryClock& clock) {
    TickRange range;
    if (value == "today") {
        range.from = todayStartUtc(clock);
        return range;
    }
    if (value == "yesterday") {
        range.before = todayStartUtc(clock);
        range.from = *range.before - kTicksPerDay;
        return range;
    }
    const auto recentDays = [&](std::int64_t days) {
        TickRange recent;
        recent.from = clock.now_utc_ticks - days * kTicksPerDay;
        return recent;
    };
    if (value == "week") return recentDays(7);
    if (value == "month") return recentDays(30);
    if (value == "year") return recentDays(365);
    if (value.size() >= 2 && value.back() == 'd') {
        int days = 0;
        if (parseUnsigned(value.substr(0, value.size() - 1), days)) return recentDays(days);
    }

    const auto range_separator = value.find("..");
    if (range_separator != std::string_view::npos) {
        range.from = dayStartUtc(value.substr(0, range_separator), clock, 0);
        range.before = dayStartUtc(value.substr(range_separator + 2), clock, 1);
        if (!range.from || !range.before) return std::nullopt;
        return range;
    }
    if (startsWith(value, ">=")) range.from = dayStartUtc(value.substr(2), clock, 0);
    else if (startsWith(value, "<=")) range.before = dayStartUtc(value.substr(2), clock, 1);
    else if (startsWith(value, ">")) range.from = dayStartUtc(value.substr(1), clock, 1);
    else if (startsWith(value, "<")) range.before = dayStartUtc(value.substr(1), clock, 0);
    else {
        range.from = dayStartUtc(value, clock, 0);
        range.before = dayStartUtc(value, clock, 1);
        if (!range.from || !range.before) return std::nullopt;
        return range;
    }
    if (!range.from && !range.before) return std::nullopt;
    return range;
}

struct SizeRange {
    std::optional<std::uint64_t> min;
    std::optional<std::uint64_t> max;
};

std::optional<SizeRange> parseSizeFilter(std::string_view value) {
    SizeRange range;
    const auto range_separator = value.find("..");
    if (range_separator != std::string_view::npos) {
        range.min = parseByteSize(value.substr(0, range_separator));
        range.max = parseByteSize(value.substr(range_separator + 2));
        if (!range.min || !range.max) return std::nullopt;
        return range;
    }
    if (startsWith(value, ">=")) {
        range.min = parseByteSize(value.substr(2));
    } else if (startsWith(value, "<=")) {
        range.max = parseByteSize(value.substr(2));
    } else if (startsWith(value, ">")) {
        const auto size = parseByteSize(value.substr(1));
        if (size) range.min = *size + 1;
    } else if (startsWith(value, "<")) {
        const auto size = parseByteSize(value.substr(1));
        if (size && *size > 0) range.max = *size - 1;
        else if (size) return std::nullopt;
    } else {
        range.min = parseByteSize(value);
        range.max = range.min;
    }
    if (!range.min && !range.max) return std::nullopt;
    return range;
}

std::vector<std::string> splitAlternatives(std::string_view value) {
    std::vector<std::string> alternatives;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t bar = value.find('|', start);
        const std::size_t end = bar == std::string_view::npos ? value.size() : bar;
        if (end > start) alternatives.emplace_back(value.substr(start, end - start));
        if (bar == std::string_view::npos) break;
        start = bar + 1;
    }
    return alternatives;
}

void addTerm(NameQuery& query, const std::string& folded, bool& join_next) {
    if (folded.empty()) return;
    if (folded.find('\\') != std::string::npos && folded.find('|') == std::string::npos) {
        query.path_terms.push_back(folded);
        join_next = false;
        return;
    }
    const bool joins_previous = join_next || folded.front() == '|';
    join_next = folded.back() == '|';
    auto alternatives = splitAlternatives(folded);
    if (alternatives.empty()) return;
    if (joins_previous && !query.term_groups.empty()) {
        auto& group = query.term_groups.back();
        group.insert(group.end(), alternatives.begin(), alternatives.end());
        return;
    }
    query.term_groups.push_back(std::move(alternatives));
}

int rankClass(std::string_view folded_name, std::string_view term) {
    if (term.empty()) return 3;
    if (folded_name == term) return 0;
    if (startsWith(folded_name, term)) return 1;
    std::size_t position = folded_name.find(term);
    while (position != std::string_view::npos) {
        const char before = folded_name[position - 1];
        if (before == ' ' || before == '.' || before == '_' || before == '-' || before == '(' || before == '[')
            return 2;
        position = folded_name.find(term, position + 1);
    }
    return 3;
}

}

std::vector<std::string> NameQuery::highlightTerms() const {
    std::vector<std::string> all;
    for (const auto& group : term_groups) all.insert(all.end(), group.begin(), group.end());
    return all;
}

bool NameQuery::empty() const {
    return term_groups.empty() && excluded_terms.empty() && path_terms.empty() && extensions.empty() && !min_size &&
           !max_size && !modified_from && !modified_before && kind == EntryKind::Any;
}

std::optional<std::uint64_t> parseByteSize(std::string_view text) {
    const std::string folded = utf8::foldCase(text);
    std::size_t position = 0;
    double value = 0.0;
    bool any_digit = false;
    while (position < folded.size() && folded[position] >= '0' && folded[position] <= '9') {
        value = value * 10.0 + (folded[position] - '0');
        any_digit = true;
        ++position;
    }
    if (position < folded.size() && (folded[position] == '.' || folded[position] == ',')) {
        ++position;
        double scale = 0.1;
        while (position < folded.size() && folded[position] >= '0' && folded[position] <= '9') {
            value += (folded[position] - '0') * scale;
            scale /= 10.0;
            any_digit = true;
            ++position;
        }
    }
    if (!any_digit) return std::nullopt;

    const std::string_view unit = std::string_view(folded).substr(position);
    double multiplier = 1.0;
    if (unit.empty() || unit == "b" || unit == "б") multiplier = 1.0;
    else if (unit == "k" || unit == "kb" || unit == "к" || unit == "кб") multiplier = 1024.0;
    else if (unit == "m" || unit == "mb" || unit == "м" || unit == "мб") multiplier = 1024.0 * 1024.0;
    else if (unit == "g" || unit == "gb" || unit == "г" || unit == "гб") multiplier = 1024.0 * 1024.0 * 1024.0;
    else if (unit == "t" || unit == "tb" || unit == "т" || unit == "тб") multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    else return std::nullopt;

    const double bytes = value * multiplier;
    if (bytes < 0.0 || bytes > 1.8e19) return std::nullopt;
    return static_cast<std::uint64_t>(bytes + 0.5);
}

NameQuery parseNameQuery(std::string_view text, const QueryClock& clock) {
    NameQuery query;
    bool join_next = false;
    for (const std::string& token : tokenize(text)) {
        const std::string folded = utf8::foldCase(token);
        if (folded == "|") {
            join_next = true;
            continue;
        }
        if (folded.size() > 1 && folded[0] == '!') {
            for (auto& excluded : splitAlternatives(std::string_view(folded).substr(1)))
                query.excluded_terms.push_back(std::move(excluded));
            join_next = false;
            continue;
        }

        const auto colon = folded.find(':');
        if (colon == std::string::npos || colon == 0) {
            addTerm(query, folded, join_next);
            continue;
        }
        join_next = false;
        const std::string key = folded.substr(0, colon);
        const std::string value = folded.substr(colon + 1);

        if (key == "ext" || key == "extension") {
            const auto items = splitList(value);
            if (items.empty()) query.invalid_tokens.push_back(token);
            for (std::string item : items) {
                while (!item.empty() && item.front() == '.') item.erase(item.begin());
                if (!item.empty()) query.extensions.push_back(item);
            }
        } else if (key == "size") {
            const auto range = parseSizeFilter(value);
            if (!range) {
                query.invalid_tokens.push_back(token);
                continue;
            }
            if (range->min) query.min_size = query.min_size ? std::max(*query.min_size, *range->min) : *range->min;
            if (range->max) query.max_size = query.max_size ? std::min(*query.max_size, *range->max) : *range->max;
        } else if (key == "modified" || key == "date" || key == "dm") {
            const auto range = parseDateFilter(value, clock);
            if (!range) {
                query.invalid_tokens.push_back(token);
                continue;
            }
            if (range->from) query.modified_from = query.modified_from ? std::max(*query.modified_from, *range->from) : *range->from;
            if (range->before)
                query.modified_before = query.modified_before ? std::min(*query.modified_before, *range->before) : *range->before;
        } else if (key == "type" || key == "kind") {
            if (value == "file" || value == "files" || value == "f") query.kind = EntryKind::Files;
            else if (value == "folder" || value == "folders" || value == "dir" || value == "d") query.kind = EntryKind::Folders;
            else query.invalid_tokens.push_back(token);
        } else if ((key == "file" || key == "files") && value.empty()) {
            query.kind = EntryKind::Files;
        } else if ((key == "folder" || key == "folders" || key == "dir") && value.empty()) {
            query.kind = EntryKind::Folders;
        } else if (key == "path" || key == "in") {
            if (value.empty()) query.invalid_tokens.push_back(token);
            else query.path_terms.push_back(value);
        } else {
            addTerm(query, folded, join_next);
        }
    }
    return query;
}

bool matchesNameQuery(const NameQuery& query, const FileIndex& index, std::uint32_t record_index) {
    const FileRecord& record = index.record(record_index);
    if (record.isDeleted()) return false;
    if (query.kind == EntryKind::Files && record.isDirectory()) return false;
    if (query.kind == EntryKind::Folders && !record.isDirectory()) return false;

    const std::string_view folded = index.foldedName(record_index);
    for (const auto& group : query.term_groups) {
        const bool any = std::any_of(group.begin(), group.end(),
                                     [&](const std::string& term) { return folded.find(term) != std::string_view::npos; });
        if (!any) return false;
    }
    for (const auto& term : query.excluded_terms)
        if (folded.find(term) != std::string_view::npos) return false;

    if (!query.extensions.empty()) {
        if (record.isDirectory()) return false;
        const std::string_view extension = extensionOf(folded);
        if (std::find(query.extensions.begin(), query.extensions.end(), extension) == query.extensions.end()) return false;
    }

    if (query.min_size || query.max_size) {
        if (record.isDirectory() || !record.hasMetadata()) return false;
        if (query.min_size && record.size < *query.min_size) return false;
        if (query.max_size && record.size > *query.max_size) return false;
    }
    if (query.modified_from || query.modified_before) {
        if (!record.hasMetadata()) return false;
        if (query.modified_from && record.modified < *query.modified_from) return false;
        if (query.modified_before && record.modified >= *query.modified_before) return false;
    }

    if (!query.path_terms.empty()) {
        const std::string path = utf8::foldCase(index.fullPath(record_index));
        for (const auto& term : query.path_terms)
            if (path.find(term) == std::string::npos) return false;
    }
    return true;
}

NameSearchResult searchNames(const FileIndex& index, const NameQuery& query, std::size_t limit, unsigned threads,
                             const std::atomic<bool>* cancel) {
    const auto started = std::chrono::steady_clock::now();
    NameSearchResult result;
    const std::size_t count = index.recordCount();
    if (query.empty() || count == 0) return result;

    const std::vector<std::string> first_group = query.term_groups.empty() ? std::vector<std::string>() : query.term_groups.front();
    threads = std::max(1u, threads);
    if (count < 50'000) threads = 1;
    const std::size_t slice = (count + threads - 1) / threads;

    std::vector<std::vector<std::uint64_t>> partial(threads);
    std::atomic<bool> stopped{false};
    const auto work = [&](unsigned worker) {
        const std::size_t begin = worker * slice;
        const std::size_t end = std::min(count, begin + slice);
        auto& out = partial[worker];
        for (std::size_t i = begin; i < end; ++i) {
            if ((i & 0xFFF) == 0 && cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
                stopped = true;
                return;
            }
            const auto record_index = static_cast<std::uint32_t>(i);
            if (!matchesNameQuery(query, index, record_index)) continue;
            int best_rank = 3;
            for (const auto& alternative : first_group)
                best_rank = std::min(best_rank, rankClass(index.foldedName(record_index), alternative));
            const auto rank = static_cast<std::uint64_t>(best_rank);
            const auto length = static_cast<std::uint64_t>(index.record(record_index).name_length);
            out.push_back((rank << 48) | (length << 32) | record_index);
        }
    };

    if (threads == 1) {
        work(0);
    } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (unsigned worker = 0; worker < threads; ++worker) pool.emplace_back(work, worker);
        for (auto& thread : pool) thread.join();
    }

    std::vector<std::uint64_t> keys;
    for (auto& part : partial) result.total_matches += part.size();
    keys.reserve(result.total_matches);
    for (auto& part : partial) keys.insert(keys.end(), part.begin(), part.end());

    if (keys.size() > limit) {
        std::nth_element(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(limit), keys.end());
        keys.resize(limit);
    }
    std::sort(keys.begin(), keys.end());
    result.records.reserve(keys.size());
    for (const auto key : keys) result.records.push_back(static_cast<std::uint32_t>(key & 0xFFFFFFFFu));

    result.cancelled = stopped.load();
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return result;
}

}
