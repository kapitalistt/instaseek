#include "test_harness.hpp"

#include "core/format.hpp"
#include "core/name_query.hpp"

using namespace instaseek::core;

namespace {

QueryClock middayJune15() {
    QueryClock clock;
    clock.now_utc_ticks = fileTimeFromCivil(2024, 6, 15) + 12 * 3600 * kTicksPerSecond;
    clock.local_offset_ticks = 0;
    return clock;
}

}

INSTASEEK_TEST(terms_and_extension_filters) {
    const NameQuery query = parseNameQuery("Report ext:PDF,.docx", middayJune15());
    CHECK(query.term_groups.size() == 1 && query.term_groups[0].size() == 1 && query.term_groups[0][0] == "report");
    CHECK(query.extensions.size() == 2);
    CHECK(query.extensions[0] == "pdf" && query.extensions[1] == "docx");
    CHECK(query.invalid_tokens.empty());
}

INSTASEEK_TEST(size_filters_accept_units_and_ranges) {
    const NameQuery greater = parseNameQuery("size:>10mb", middayJune15());
    CHECK(greater.min_size == 10ull * 1024 * 1024 + 1);
    const NameQuery range = parseNameQuery("size:1kb..2kb", middayJune15());
    CHECK(range.min_size == 1024ull && range.max_size == 2048ull);
    const NameQuery less = parseNameQuery("size:<1k", middayJune15());
    CHECK(less.max_size == 1023ull);
    CHECK(parseByteSize("1.5mb") == 1572864ull);
    CHECK(parseByteSize("10мб") == 10ull * 1024 * 1024);
    CHECK(parseByteSize("2GB") == 2ull * 1024 * 1024 * 1024);
    CHECK(!parseByteSize("ten").has_value());
    CHECK(!parseByteSize("5 parsecs").has_value());
    CHECK(parseNameQuery("size:abc", middayJune15()).invalid_tokens.size() == 1);
}

INSTASEEK_TEST(date_filters_use_calendar_days) {
    const QueryClock clock = middayJune15();
    const NameQuery after = parseNameQuery("modified:>2024-01-01", clock);
    CHECK(after.modified_from == fileTimeFromCivil(2024, 1, 2));
    const NameQuery exact = parseNameQuery("date:15.03.2024", clock);
    CHECK(exact.modified_from == fileTimeFromCivil(2024, 3, 15));
    CHECK(exact.modified_before == fileTimeFromCivil(2024, 3, 16));
    const NameQuery today = parseNameQuery("dm:today", clock);
    CHECK(today.modified_from == fileTimeFromCivil(2024, 6, 15));
    const NameQuery recent = parseNameQuery("modified:7d", clock);
    CHECK(recent.modified_from == clock.now_utc_ticks - 7 * kTicksPerDay);
    const NameQuery range = parseNameQuery("modified:2024-01-01..2024-01-31", clock);
    CHECK(range.modified_from == fileTimeFromCivil(2024, 1, 1));
    CHECK(range.modified_before == fileTimeFromCivil(2024, 2, 1));
    CHECK(parseNameQuery("modified:2024-02-30", clock).invalid_tokens.size() == 1);
}

INSTASEEK_TEST(local_time_offset_shifts_day_boundaries) {
    QueryClock clock = middayJune15();
    clock.local_offset_ticks = 3 * 3600 * kTicksPerSecond;
    const NameQuery exact = parseNameQuery("modified:2024-03-15", clock);
    CHECK(exact.modified_from == fileTimeFromCivil(2024, 3, 15) - clock.local_offset_ticks);
}

INSTASEEK_TEST(kinds_exclusions_paths_and_quotes) {
    const NameQuery query = parseNameQuery("type:folder !tmp path:Docs \"my file\" users\\me", middayJune15());
    CHECK(query.kind == EntryKind::Folders);
    CHECK(query.excluded_terms.size() == 1 && query.excluded_terms[0] == "tmp");
    CHECK(query.path_terms.size() == 2);
    CHECK(query.path_terms[0] == "docs" && query.path_terms[1] == "users\\me");
    CHECK(query.term_groups.size() == 1 && query.term_groups[0][0] == "my file");
    CHECK(parseNameQuery("file:", middayJune15()).kind == EntryKind::Files);
    CHECK(parseNameQuery("", middayJune15()).empty());
    CHECK(parseNameQuery("C:notes", middayJune15()).term_groups.size() == 1);
}

INSTASEEK_TEST(vertical_bar_means_or) {
    const QueryClock clock = middayJune15();
    const NameQuery joined = parseNameQuery("Report|Invoice", clock);
    CHECK(joined.term_groups.size() == 1 && joined.term_groups[0].size() == 2);
    CHECK(joined.term_groups[0][0] == "report" && joined.term_groups[0][1] == "invoice");

    const NameQuery spaced = parseNameQuery("2024 report | invoice | act", clock);
    CHECK(spaced.term_groups.size() == 2);
    CHECK(spaced.term_groups[0].size() == 1 && spaced.term_groups[0][0] == "2024");
    CHECK(spaced.term_groups[1].size() == 3);

    const NameQuery dangling = parseNameQuery("report| invoice", clock);
    CHECK(dangling.term_groups.size() == 1 && dangling.term_groups[0].size() == 2);

    const NameQuery excluded = parseNameQuery("log !tmp|bak", clock);
    CHECK(excluded.excluded_terms.size() == 2);
    CHECK(excluded.highlightTerms().size() == 1);

    FileIndex index("C:");
    const auto report = index.append("report_2024.pdf", kNoParent, 0);
    const auto invoice = index.append("invoice_2024.pdf", kNoParent, 0);
    const auto other = index.append("notes_2024.pdf", kNoParent, 0);
    const auto old_report = index.append("report_2023.pdf", kNoParent, 0);
    CHECK(matchesNameQuery(spaced, index, report));
    CHECK(matchesNameQuery(spaced, index, invoice));
    CHECK(!matchesNameQuery(spaced, index, other));
    CHECK(!matchesNameQuery(spaced, index, old_report));
    const NameSearchResult result = searchNames(index, joined, 10, 1, nullptr);
    CHECK(result.total_matches == 3);
}

INSTASEEK_TEST(queries_match_index_records) {
    FileIndex index("C:");
    const auto docs = index.append("Docs", kNoParent, kRecordDirectory);
    const auto big = index.append("Annual Report.PDF", docs, 0);
    index.setMetadata(big, 20ull * 1024 * 1024, fileTimeFromCivil(2024, 5, 1));
    const auto small = index.append("report-draft.pdf", docs, 0);
    index.setMetadata(small, 1024, fileTimeFromCivil(2023, 1, 1));
    const auto unknown = index.append("report.tmp.pdf", kNoParent, 0);

    const QueryClock clock = middayJune15();
    CHECK(matchesNameQuery(parseNameQuery("report ext:pdf", clock), index, big));
    CHECK(matchesNameQuery(parseNameQuery("report size:>10mb", clock), index, big));
    CHECK(!matchesNameQuery(parseNameQuery("report size:>10mb", clock), index, small));
    CHECK(!matchesNameQuery(parseNameQuery("report size:>10mb", clock), index, unknown));
    CHECK(matchesNameQuery(parseNameQuery("report modified:>2024-01-01", clock), index, big));
    CHECK(!matchesNameQuery(parseNameQuery("report !draft !tmp", clock), index, small));
    CHECK(matchesNameQuery(parseNameQuery("path:docs\\annual", clock), index, big));
    CHECK(!matchesNameQuery(parseNameQuery("path:docs", clock), index, unknown));
    CHECK(matchesNameQuery(parseNameQuery("type:folder doc", clock), index, docs));
    CHECK(!matchesNameQuery(parseNameQuery("type:file doc", clock), index, docs));
    index.markDeleted(big);
    CHECK(!matchesNameQuery(parseNameQuery("report", clock), index, big));
}

INSTASEEK_TEST(search_ranks_exact_and_prefix_matches_first) {
    FileIndex index("C:");
    const auto inside = index.append("myreport.pdf", kNoParent, 0);
    const auto word = index.append("annual report.pdf", kNoParent, 0);
    const auto prefix = index.append("report.pdf", kNoParent, 0);
    const auto exact = index.append("report", kNoParent, kRecordDirectory);
    index.append("unrelated.txt", kNoParent, 0);

    const NameSearchResult result = searchNames(index, parseNameQuery("REPORT", middayJune15()), 10, 4, nullptr);
    CHECK(result.total_matches == 4);
    CHECK(result.records.size() == 4);
    if (result.records.size() == 4) {
        CHECK(result.records[0] == exact);
        CHECK(result.records[1] == prefix);
        CHECK(result.records[2] == word);
        CHECK(result.records[3] == inside);
    }
    const NameSearchResult limited = searchNames(index, parseNameQuery("report", middayJune15()), 2, 1, nullptr);
    CHECK(limited.total_matches == 4 && limited.records.size() == 2);
    CHECK(searchNames(index, parseNameQuery("", middayJune15()), 10, 1, nullptr).records.empty());
}

INSTASEEK_TEST(parallel_search_matches_sequential_search) {
    FileIndex index("C:");
    for (int i = 0; i < 120000; ++i) index.append("file_" + std::to_string(i) + (i % 3 == 0 ? ".log" : ".txt"), kNoParent, 0);
    const NameQuery query = parseNameQuery("file_1 ext:log", middayJune15());
    const NameSearchResult single = searchNames(index, query, 100000, 1, nullptr);
    const NameSearchResult parallel = searchNames(index, query, 100000, 8, nullptr);
    CHECK(single.total_matches == parallel.total_matches);
    CHECK(single.records == parallel.records);
    CHECK(single.total_matches > 1000);
}
