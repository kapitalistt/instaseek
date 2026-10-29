#include "test_harness.hpp"

#include "core/format.hpp"

using namespace instaseek::core;

INSTASEEK_TEST(civil_dates_round_trip) {
    CHECK(daysFromCivil(1970, 1, 1) == 0);
    const CivilDate epoch = civilFromDays(0);
    CHECK(epoch.year == 1970 && epoch.month == 1 && epoch.day == 1);
    const CivilDate leap = civilFromDays(daysFromCivil(2024, 2, 29));
    CHECK(leap.year == 2024 && leap.month == 2 && leap.day == 29);
    CHECK(isValidCivilDate(2024, 2, 29));
    CHECK(!isValidCivilDate(2023, 2, 29));
    CHECK(!isValidCivilDate(2024, 13, 1));
    CHECK(fileTimeFromCivil(1601, 1, 1) == 0);
}

INSTASEEK_TEST(file_time_formatting) {
    const std::int64_t ticks = fileTimeFromCivil(2024, 5, 1) + (13 * 3600 + 45 * 60) * kTicksPerSecond;
    CHECK(formatFileTime(ticks) == "2024-05-01 13:45");
    const CivilDateTime t = civilFromFileTime(ticks + 7 * kTicksPerSecond);
    CHECK(t.hour == 13 && t.minute == 45 && t.second == 7);
    CHECK(formatFileTime(0) == "-");
}

INSTASEEK_TEST(byte_sizes_are_human_readable) {
    CHECK(formatBytes(0) == "0 B");
    CHECK(formatBytes(1023) == "1023 B");
    CHECK(formatBytes(1536) == "1.50 KB");
    CHECK(formatBytes(10ull * 1024 * 1024) == "10.0 MB");
    CHECK(formatBytes(300ull * 1024 * 1024 * 1024) == "300 GB");
}

INSTASEEK_TEST(counts_use_thousands_separators) {
    CHECK(formatCount(0, ',') == "0");
    CHECK(formatCount(12, ',') == "12");
    CHECK(formatCount(1234, ',') == "1,234");
    CHECK(formatCount(1234567, ',') == "1,234,567");
    CHECK(formatCount(123456, ' ') == "123 456");
}

INSTASEEK_TEST(durations_pick_a_sensible_unit) {
    CHECK(formatDuration(0.0123) == "12 ms");
    CHECK(formatDuration(1.5) == "1.50 s");
    CHECK(formatDuration(42.0) == "42.0 s");
    CHECK(formatDuration(75.0) == "1 min 15 s");
}
