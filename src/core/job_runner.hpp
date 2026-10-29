#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace instaseek::core {

class LatestJobRunner {
public:
    using Job = std::function<void(const std::atomic<bool>& cancelled)>;

    LatestJobRunner();
    ~LatestJobRunner();

    LatestJobRunner(const LatestJobRunner&) = delete;
    LatestJobRunner& operator=(const LatestJobRunner&) = delete;

    void submit(Job job);
    void cancel();
    bool busy() const { return busy_.load(); }

private:
    void loop();

    std::mutex mutex_;
    std::condition_variable wake_;
    Job pending_;
    std::shared_ptr<std::atomic<bool>> running_cancel_;
    bool stopping_ = false;
    std::atomic<bool> busy_{false};
    std::thread thread_;
};

}
