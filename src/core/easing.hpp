#pragma once

#include <algorithm>
#include <cmath>

namespace instaseek::core::easing {

inline constexpr double kPi = 3.14159265358979323846;

inline double clamp01(double t) { return std::clamp(t, 0.0, 1.0); }

inline double progress(double elapsed_seconds, double delay_seconds, double duration_seconds) {
    if (duration_seconds <= 0.0) return elapsed_seconds >= delay_seconds ? 1.0 : 0.0;
    return clamp01((elapsed_seconds - delay_seconds) / duration_seconds);
}

inline double outCubic(double t) {
    t = clamp01(t);
    const double inverse = 1.0 - t;
    return 1.0 - inverse * inverse * inverse;
}

inline double inOutCubic(double t) {
    t = clamp01(t);
    if (t < 0.5) return 4.0 * t * t * t;
    const double shifted = -2.0 * t + 2.0;
    return 1.0 - shifted * shifted * shifted / 2.0;
}

inline double inOutSine(double t) { return 0.5 - 0.5 * std::cos(clamp01(t) * kPi); }

inline double outBack(double t) {
    t = clamp01(t);
    constexpr double overshoot = 1.70158;
    constexpr double scaled = overshoot + 1.0;
    const double shifted = t - 1.0;
    return 1.0 + scaled * shifted * shifted * shifted + overshoot * shifted * shifted;
}

inline double pulse(double seconds, double period_seconds) {
    if (period_seconds <= 0.0) return 1.0;
    return 0.5 - 0.5 * std::cos(seconds / period_seconds * 2.0 * kPi);
}

}
