#include "test_harness.hpp"

#include "core/chunk_search.hpp"

#include <string>
#include <vector>

using namespace instaseek::core;

namespace {

struct Expected {
    std::uint64_t line_number;
    std::uint64_t line_offset;
};

std::string buildCorpus() {
    std::string text;
    for (int i = 1; i <= 400; ++i) {
        text += "line " + std::to_string(i) + ":";
        if (i % 7 == 0) text += " needle";
        if (i % 13 == 0) text += std::string(static_cast<std::size_t>(i % 40), '.') + "needleneedle";
        if (i % 31 == 0) text += " Needle in caps";
        text += (i % 5 == 0) ? "\r\n" : "\n";
    }
    text += "tail without newline needle";
    return text;
}

std::vector<Expected> naiveSearch(const std::string& text, const std::string& needle) {
    std::vector<Expected> expected;
    std::uint64_t line = 1;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::size_t stop = end == std::string::npos ? text.size() : end;
        if (text.substr(start, stop - start).find(needle) != std::string::npos) expected.push_back({line, start});
        if (end == std::string::npos) break;
        start = end + 1;
        ++line;
    }
    return expected;
}

const std::uint8_t* bytes(const std::string& text) { return reinterpret_cast<const std::uint8_t*>(text.data()); }

std::shared_ptr<const TextMatcher> literal(const std::string& pattern, bool case_sensitive) {
    return TextMatcher::create({pattern, case_sensitive, false, false}).matcher;
}

}

INSTASEEK_TEST(chunks_cover_the_extent_without_gaps) {
    const DataExtent extent{3, 1003, TextEncoding::Utf8};
    const auto chunks = planChunks(extent, 64);
    CHECK(!chunks.empty());
    CHECK(chunks.front().begin == 3);
    CHECK(chunks.back().end == 1003);
    for (std::size_t i = 1; i < chunks.size(); ++i) CHECK(chunks[i].begin == chunks[i - 1].end);

    const DataExtent wide{2, 1002, TextEncoding::Utf16Le};
    for (const auto& chunk : planChunks(wide, 33)) CHECK((chunk.begin - 2) % 2 == 0);
}

INSTASEEK_TEST(results_do_not_depend_on_chunk_size) {
    const std::string text = buildCorpus();
    const auto matcher = literal("needle", true);
    const auto expected = naiveSearch(text, "needle");
    CHECK(expected.size() > 50);
    const DataExtent extent{0, text.size(), TextEncoding::Utf8};
    for (const std::uint64_t chunk : {1ull, 5ull, 7ull, 13ull, 64ull, 100ull, 997ull, 4096ull, 1ull << 20}) {
        const auto hits = searchBuffer(bytes(text), text.size(), extent, *matcher, chunk, 100000);
        CHECK(hits.size() == expected.size());
        const std::size_t count = std::min(hits.size(), expected.size());
        for (std::size_t i = 0; i < count; ++i) {
            CHECK(hits[i].line_number == expected[i].line_number);
            CHECK(hits[i].line_offset == expected[i].line_offset);
            CHECK(hits[i].preview.substr(hits[i].match_start, hits[i].match_length) == "needle");
            CHECK(hits[i].preview.empty() || hits[i].preview.back() != '\r');
        }
    }
}

INSTASEEK_TEST(matches_across_block_boundaries_are_found_once) {
    std::string text(100, 'x');
    text.replace(60, 6, "abcdef");
    const auto matcher = literal("abcdef", true);
    const DataExtent extent{0, text.size(), TextEncoding::Utf8};
    for (const std::uint64_t chunk : {2ull, 3ull, 61ull, 62ull, 63ull, 64ull, 65ull}) {
        const auto hits = searchBuffer(bytes(text), text.size(), extent, *matcher, chunk, 100);
        CHECK(hits.size() == 1);
        if (!hits.empty()) CHECK(hits.front().match_offset == 60);
    }
}

INSTASEEK_TEST(case_insensitive_and_regex_modes_work_on_chunks) {
    const std::string text = buildCorpus();
    const auto insensitive = literal("NEEDLE", false);
    const DataExtent extent{0, text.size(), TextEncoding::Utf8};
    const auto hits = searchBuffer(bytes(text), text.size(), extent, *insensitive, 50, 100000);
    std::size_t expected = 0;
    for (const auto& item : naiveSearch(text, "eedle")) {
        (void)item;
        ++expected;
    }
    CHECK(hits.size() == expected);

    const auto regex = TextMatcher::create({R"(line \d*5:)", true, false, true}).matcher;
    const auto regex_hits = searchBuffer(bytes(text), text.size(), extent, *regex, 77, 100000);
    CHECK(regex_hits.size() == 40);
}

INSTASEEK_TEST(hit_limit_stops_the_search) {
    const std::string text = buildCorpus();
    const auto matcher = literal("needle", true);
    const DataExtent extent{0, text.size(), TextEncoding::Utf8};
    const auto hits = searchBuffer(bytes(text), text.size(), extent, *matcher, 128, 3);
    CHECK(hits.size() == 3);
    CHECK(hits[0].line_number == 7);
}

INSTASEEK_TEST(utf16_files_report_original_offsets) {
    const std::string utf8 = buildCorpus();
    const auto encoded = encodeFromUtf8(utf8, TextEncoding::Utf16Le);
    CHECK(encoded.has_value());
    const std::string text = std::string("\xFF\xFE", 2) + *encoded;
    const auto matcher = literal("needle", true);
    const auto expected = naiveSearch(utf8, "needle");
    const DataExtent extent{2, text.size(), TextEncoding::Utf16Le};
    for (const std::uint64_t chunk : {6ull, 64ull, 1000ull, 1ull << 20}) {
        const auto hits = searchBuffer(bytes(text), text.size(), extent, *matcher, chunk, 100000);
        CHECK(hits.size() == expected.size());
        const std::size_t count = std::min(hits.size(), expected.size());
        for (std::size_t i = 0; i < count; ++i) {
            CHECK(hits[i].line_number == expected[i].line_number);
            CHECK(hits[i].line_offset == 2 + expected[i].line_offset * 2);
        }
    }
}

INSTASEEK_TEST(windows1251_files_are_searched_as_text) {
    const std::string utf8 = "первая строка\nвторая: ПРИВЕТ мир\nтретья\nпривет снова";
    const auto encoded = encodeFromUtf8(utf8, TextEncoding::Windows1251);
    CHECK(encoded.has_value());
    const auto matcher = literal("привет", false);
    const DataExtent extent{0, encoded->size(), TextEncoding::Windows1251};
    const auto hits = searchBuffer(bytes(*encoded), encoded->size(), extent, *matcher, 5, 100);
    CHECK(hits.size() == 2);
    if (hits.size() == 2) {
        CHECK(hits[0].line_number == 2);
        CHECK(hits[1].line_number == 4);
        CHECK(hits[0].preview.substr(hits[0].match_start, hits[0].match_length) == "ПРИВЕТ");
    }
}

INSTASEEK_TEST(previews_are_trimmed_around_long_matches) {
    std::string line(5000, 'a');
    line.replace(2500, 6, "needle");
    const auto matcher = literal("needle", true);
    const DataExtent extent{0, line.size(), TextEncoding::Utf8};
    const auto hits = searchBuffer(bytes(line), line.size(), extent, *matcher, 1 << 20, 10);
    CHECK(hits.size() == 1);
    if (!hits.empty()) {
        CHECK(hits[0].preview_cut_left && hits[0].preview_cut_right);
        CHECK(hits[0].preview.size() < 400);
        CHECK(hits[0].preview.substr(hits[0].match_start, hits[0].match_length) == "needle");
    }
}
