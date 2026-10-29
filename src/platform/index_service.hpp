#pragma once

#include "core/file_index.hpp"
#include "platform/ntfs_volume.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace instaseek::platform {

enum class IndexSource { None, NtfsMft, DirectoryScan };
enum class IndexPhase { Starting, Scanning, Metadata, Ready, Failed };

struct IndexSnapshot {
    IndexPhase phase = IndexPhase::Starting;
    IndexSource source = IndexSource::None;
    wchar_t drive = L'C';
    std::uint64_t files = 0;
    std::uint64_t scanned = 0;
    std::uint64_t inaccessible = 0;
    double scan_seconds = 0.0;
    double metadata_progress = 0.0;
    bool metadata_complete = false;
    bool live_updates = false;
    bool elevation_would_help = false;
    bool not_ntfs = false;
    std::uint64_t generation = 0;
    std::uint64_t epoch = 0;
    std::uint64_t live_changes = 0;
    std::chrono::steady_clock::time_point last_change{};
    std::string error;
};

class IndexService {
public:
    using Notify = std::function<void()>;

    IndexService(wchar_t drive, Notify notify);
    ~IndexService();

    IndexService(const IndexService&) = delete;
    IndexService& operator=(const IndexService&) = delete;

    void start();
    void rescan();
    IndexSnapshot snapshot() const;

    template <typename Reader>
    decltype(auto) read(Reader&& reader) const {
        std::shared_lock lock(index_mutex_);
        return std::forward<Reader>(reader)(index_);
    }

private:
    struct OwnedChange {
        std::uint64_t file_reference = 0;
        std::uint64_t parent_reference = 0;
        std::uint32_t attributes = 0;
        std::uint32_t reason = 0;
        std::string name;
    };

    void run();
    bool buildFromMft(NativeHandle volume, std::optional<JournalState>& journal);
    void buildFromDirectoryScan();
    void fillMetadata(NativeHandle volume);
    void monitorJournal(NativeHandle volume, const JournalState& journal);
    void applyChanges(NativeHandle volume, std::vector<OwnedChange>& changes);
    bool waitFor(std::chrono::milliseconds duration);
    bool interrupted() const;
    void updateStatus(const std::function<void(IndexSnapshot&)>& update, bool force_notify);
    void notify(bool force);
    std::string rootName() const;

    wchar_t drive_;
    Notify notify_;

    mutable std::shared_mutex index_mutex_;
    core::FileIndex index_;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> directories_;

    mutable std::mutex status_mutex_;
    IndexSnapshot status_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> rescan_requested_{false};
    std::atomic<long long> last_notify_ms_{0};
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    std::thread worker_;
};

}
