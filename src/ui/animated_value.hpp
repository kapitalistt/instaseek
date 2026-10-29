#pragma once

#include "core/easing.hpp"
#include "ui/theme.hpp"

namespace instaseek::ui {

class AnimatedValue {
public:
    explicit AnimatedValue(double duration_seconds = 0.4) : duration_(duration_seconds) {}

    void set(double target, Clock::time_point now, const Motion& motion) {
        if (target == to_) return;
        from_ = value(now, motion);
        to_ = target;
        started_ = now;
    }

    void jump(double target) {
        from_ = target;
        to_ = target;
        started_ = Clock::time_point{};
    }

    double value(Clock::time_point now, const Motion& motion) const {
        const double t = core::easing::progress(motion.elapsed(started_, now), 0.0, duration_);
        return from_ + (to_ - from_) * core::easing::outCubic(t);
    }

    double target() const { return to_; }

    bool animating(Clock::time_point now, const Motion& motion) const {
        return motion.elapsed(started_, now) < duration_;
    }

private:
    double duration_;
    double from_ = 0.0;
    double to_ = 0.0;
    Clock::time_point started_{};
};

}
