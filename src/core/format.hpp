#pragma once

#include <cstdint>
#include <string>

namespace instaseek::core {

inline constexpr std::int64_t kTicksPerSecond = 10'000'000;
inline constexpr std::int64_t kTicksPerDay = kTicksPerSecond * 86'400;
inline constexpr std::int64_t kDaysFrom1601To1970 = 134'774;

struct CivilDate {
    int year = 1601;
    int month = 1;
    int day = 1;
};

struct CivilDateTime {
    CivilDate date;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

std::int64_t daysFromCivil(int year, int month, int day);
CivilDate civilFromDays(std::int64_t days_since_1970);
bool isValidCivilDate(int year, int month, int day);

std::int64_t fileTimeFromCivil(int year, int month, int day);
CivilDateTime civilFromFileTime(std::int64_t ticks);

std::string formatFileTime(std::int64_t ticks);
std::string formatBytes(std::uint64_t bytes);
std::string formatCount(std::uint64_t value, char separator);
std::string formatDuration(double seconds);
std::string formatThroughput(double bytes_per_second);

}
