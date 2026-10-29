#pragma once

#include "core/palette.hpp"

#include <ftxui/screen/color.hpp>

#include <chrono>

namespace instaseek::ui {

using Clock = std::chrono::steady_clock;

ftxui::Color toColor(core::Rgb rgb);

class Motion {
public:
    static constexpr double kSettledSeconds = 1.0e6;

    explicit Motion(bool enabled) : enabled_(enabled) {}

    bool enabled() const { return enabled_; }

    double elapsed(Clock::time_point since, Clock::time_point now) const {
        if (!enabled_) return kSettledSeconds;
        return std::chrono::duration<double>(now - since).count();
    }

    double seconds(Clock::time_point origin, Clock::time_point now) const {
        if (!enabled_) return 0.0;
        return std::chrono::duration<double>(now - origin).count();
    }

private:
    bool enabled_;
};

}
