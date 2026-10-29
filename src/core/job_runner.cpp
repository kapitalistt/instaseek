#include "core/job_runner.hpp"

#include <utility>

namespace instaseek::core {

LatestJobRunner::LatestJobRunner() : thread_([this] { loop(); }) {}

LatestJobRunner::~LatestJobRunner() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        pending_ = nullptr;
        if (running_cancel_) running_cancel_->store(true);
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void LatestJobRunner::submit(Job job) {
    {
        std::lock_guard lock(mutex_);
        if (running_cancel_) running_cancel_->store(true);
        pending_ = std::move(job);
        busy_ = true;
    }
    wake_.notify_one();
}

void LatestJobRunner::cancel() {
    std::lock_guard lock(mutex_);
    pending_ = nullptr;
    if (running_cancel_) running_cancel_->store(true);
}

void LatestJobRunner::loop() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stopping_ || static_cast<bool>(pending_); });
        if (stopping_) return;
        Job job = std::move(pending_);
        pending_ = nullptr;
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        running_cancel_ = cancel;
        busy_ = true;
        lock.unlock();
        job(*cancel);
        lock.lock();
        running_cancel_.reset();
        busy_ = static_cast<bool>(pending_);
    }
}

}
