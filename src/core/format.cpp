#include "core/format.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace instaseek::core {

namespace {

std::string printfString(const char* format, double value) {
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), format, value);
    return buffer.data();
}

std::string twoDigits(int value) {
    std::array<char, 8> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%02d", value);
    return buffer.data();
}

}

std::int64_t daysFromCivil(int year, int month, int day) {
    const std::int64_t y = static_cast<std::int64_t>(year) - (month <= 2 ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t year_of_era = y - era * 400;
    const std::int64_t shifted_month = month > 2 ? month - 3 : month + 9;
    const std::int64_t day_of_year = (153 * shifted_month + 2) / 5 + day - 1;
    const std::int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

CivilDate civilFromDays(std::int64_t days_since_1970) {
    const std::int64_t z = days_since_1970 + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t day_of_era = z - era * 146097;
    const std::int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t shifted_month = (5 * day_of_year + 2) / 153;
    const std::int64_t day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    const std::int64_t month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    const std::int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
    return {static_cast<int>(year), static_cast<int>(month), static_cast<int>(day)};
}

bool isValidCivilDate(int year, int month, int day) {
    if (year < 1601 || year > 30827 || month < 1 || month > 12 || day < 1) return false;
    const CivilDate round_trip = civilFromDays(daysFromCivil(year, month, day));
    return round_trip.year == year && round_trip.month == month && round_trip.day == day;
}

std::int64_t fileTimeFromCivil(int year, int month, int day) {
    return (daysFromCivil(year, month, day) + kDaysFrom1601To1970) * kTicksPerDay;
}

CivilDateTime civilFromFileTime(std::int64_t ticks) {
    if (ticks < 0) ticks = 0;
    const std::int64_t days = ticks / kTicksPerDay;
    const std::int64_t seconds_of_day = (ticks % kTicksPerDay) / kTicksPerSecond;
    CivilDateTime result;
    result.date = civilFromDays(days - kDaysFrom1601To1970);
    result.hour = static_cast<int>(seconds_of_day / 3600);
    result.minute = static_cast<int>((seconds_of_day / 60) % 60);
    result.second = static_cast<int>(seconds_of_day % 60);
    return result;
}

std::string formatFileTime(std::int64_t ticks) {
    if (ticks <= 0) return "-";
    const CivilDateTime t = civilFromFileTime(ticks);
    return std::to_string(t.date.year) + "-" + twoDigits(t.date.month) + "-" + twoDigits(t.date.day) + " " +
           twoDigits(t.hour) + ":" + twoDigits(t.minute);
}

std::string formatBytes(std::uint64_t bytes) {
    static constexpr std::array<const char*, 6> units = {"B", "KB", "MB", "GB", "TB", "PB"};
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    const char* format = value < 10.0 ? "%.2f" : (value < 100.0 ? "%.1f" : "%.0f");
    return printfString(format, value) + " " + units[unit];
}

std::string formatCount(std::uint64_t value, char separator) {
    std::string digits = std::to_string(value);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    const std::size_t lead = digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && i >= lead && (i - lead) % 3 == 0) out.push_back(separator);
        out.push_back(digits[i]);
    }
    return out;
}

std::string formatDuration(double seconds) {
    if (!(seconds >= 0.0)) seconds = 0.0;
    if (seconds < 1.0) return std::to_string(static_cast<long long>(std::llround(seconds * 1000.0))) + " ms";
    if (seconds < 60.0) return printfString(seconds < 10.0 ? "%.2f s" : "%.1f s", seconds);
    const auto total = static_cast<long long>(std::llround(seconds));
    return std::to_string(total / 60) + " min " + twoDigits(static_cast<int>(total % 60)) + " s";
}

std::string formatThroughput(double bytes_per_second) {
    if (!(bytes_per_second > 0.0)) return "-";
    return formatBytes(static_cast<std::uint64_t>(bytes_per_second)) + "/s";
}

}
