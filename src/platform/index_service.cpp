#include "platform/index_service.hpp"

#include "platform/directory_walker.hpp"
#include "platform/win.hpp"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace instaseek::platform {

namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr std::uint64_t kFirstUserRecord = 16;
constexpr std::size_t kMetadataBatch = 8192;
constexpr std::size_t kMaxMetadataRefreshPerBatch = 512;

std::uint16_t flagsFromAttributes(std::uint32_t attributes) {
    std::uint16_t flags = 0;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) flags |= core::kRecordDirectory;
    if ((attributes & FILE_ATTRIBUTE_HIDDEN) != 0) flags |= core::kRecordHidden;
    if ((attributes & FILE_ATTRIBUTE_SYSTEM) != 0) flags |= core::kRecordSystem;
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) flags |= core::kRecordReparsePoint;
    return flags;
}

bool isSystemMetadataEntry(std::uint64_t record, std::uint64_t parent_record) {
    if (record < kFirstUserRecord) return true;
    return parent_record < kFirstUserRecord && parent_record != kNtfsRootRecord;
}

long long steadyMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now().time_since_epoch()).count();
}

struct MetadataUpdate {
    std::uint64_t record = 0;
    std::uint64_t size = 0;
    std::int64_t modified = 0;
};

}

IndexService::IndexService(wchar_t drive, Notify notify)
    : drive_(drive), notify_(std::move(notify)), index_(rootName()) {
    status_.drive = drive_;
}

IndexService::~IndexService() {
    stop_ = true;
    {
        std::lock_guard lock(wait_mutex_);
    }
    wait_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void IndexService::start() {
    if (worker_.joinable()) return;
    worker_ = std::thread([this] { run(); });
}

void IndexService::rescan() {
    rescan_requested_ = true;
    {
        std::lock_guard lock(wait_mutex_);
    }
    wait_cv_.notify_all();
}

IndexSnapshot IndexService::snapshot() const {
    std::lock_guard lock(status_mutex_);
    return status_;
}

std::string IndexService::rootName() const {
    std::string root;
    root.push_back(static_cast<char>(drive_));
    root.push_back(':');
    return root;
}

bool IndexService::interrupted() const { return stop_.load() || rescan_requested_.load(); }

bool IndexService::waitFor(std::chrono::milliseconds duration) {
    std::unique_lock lock(wait_mutex_);
    wait_cv_.wait_for(lock, duration, [this] { return interrupted(); });
    return !interrupted();
}

void IndexService::notify(bool force) {
    if (!notify_) return;
    const long long now = steadyMilliseconds();
    const long long last = last_notify_ms_.load();
    if (!force && now - last < 120) return;
    last_notify_ms_ = now;
    notify_();
}

void IndexService::updateStatus(const std::function<void(IndexSnapshot&)>& update, bool force_notify) {
    {
        std::lock_guard lock(status_mutex_);
        update(status_);
    }
    notify(force_notify);
}

void IndexService::run() {
    while (!stop_) {
        rescan_requested_ = false;
        directories_.clear();
        updateStatus(
            [](IndexSnapshot& s) {
                s.phase = IndexPhase::Scanning;
                s.scanned = 0;
                s.inaccessible = 0;
                s.metadata_progress = 0.0;
                s.metadata_complete = false;
                s.live_updates = false;
                s.error.clear();
            },
            true);

        bool built_from_mft = false;
        VolumeHandle volume;
        std::optional<JournalState> journal;
        if (isNtfsVolume(drive_)) {
            volume = openVolume(drive_);
            if (volume.handle) {
                built_from_mft = buildFromMft(volume.handle.get(), journal);
            } else if (volume.error == ERROR_ACCESS_DENIED) {
                updateStatus([](IndexSnapshot& s) { s.elevation_would_help = true; }, true);
            }
        } else {
            updateStatus([](IndexSnapshot& s) { s.not_ntfs = true; }, true);
        }
        if (stop_) break;
        if (rescan_requested_) continue;

        if (built_from_mft) {
            updateStatus([](IndexSnapshot& s) { s.phase = IndexPhase::Metadata; }, true);
            fillMetadata(volume.handle.get());
            if (stop_) break;
            if (rescan_requested_) continue;
            updateStatus(
                [&](IndexSnapshot& s) {
                    s.phase = IndexPhase::Ready;
                    s.metadata_complete = true;
                    s.metadata_progress = 1.0;
                    s.live_updates = journal.has_value();
                    ++s.generation;
                },
                true);
            if (journal) monitorJournal(volume.handle.get(), *journal);
            else waitFor(std::chrono::hours(24 * 365));
        } else {
            buildFromDirectoryScan();
            if (stop_) break;
            if (rescan_requested_) continue;
            updateStatus(
                [](IndexSnapshot& s) {
                    s.phase = IndexPhase::Ready;
                    s.metadata_complete = true;
                    s.metadata_progress = 1.0;
                    ++s.generation;
                },
                true);
            waitFor(std::chrono::hours(24 * 365));
        }
    }
}

bool IndexService::buildFromMft(NativeHandle volume, std::optional<JournalState>& journal) {
    const auto started = SteadyClock::now();
    unsigned long journal_error = 0;
    journal = queryJournal(volume, journal_error);
    const std::int64_t high_usn = journal ? journal->next_usn : std::numeric_limits<std::int64_t>::max();

    core::FileIndex fresh(rootName());
    fresh.reserve(1u << 20, 32u << 20);
    std::vector<std::uint64_t> parent_records;
    parent_records.reserve(1u << 20);
    std::vector<std::pair<std::uint32_t, std::uint64_t>> directories;
    std::uint64_t scanned = 0;

    const unsigned long error = enumerateMft(
        volume, high_usn,
        [&](const UsnEntry& entry) {
            const std::uint64_t record = mftRecordNumber(entry.file_reference);
            const std::uint64_t parent_record = mftRecordNumber(entry.parent_reference);
            ++scanned;
            if (isSystemMetadataEntry(record, parent_record)) return;
            const std::uint16_t flags = flagsFromAttributes(entry.attributes);
            const std::uint32_t index = fresh.append(toUtf8(entry.name), core::kNoParent, flags);
            fresh.bindKey(record, index);
            parent_records.push_back(parent_record);
            if ((flags & core::kRecordDirectory) != 0) directories.emplace_back(index, entry.file_reference);
            if ((scanned & 0xFFFF) == 0) updateStatus([&](IndexSnapshot& s) { s.scanned = scanned; }, false);
        },
        stop_);
    if (error != 0) {
        updateStatus([&](IndexSnapshot& s) { s.error = describeError(error); }, true);
        return false;
    }

    for (std::size_t i = 0; i < parent_records.size(); ++i) {
        const auto parent = fresh.findKey(parent_records[i]);
        if (parent && *parent != i) fresh.setParent(static_cast<std::uint32_t>(i), *parent);
    }

    const std::uint64_t live = fresh.liveCount();
    {
        std::unique_lock lock(index_mutex_);
        index_ = std::move(fresh);
    }
    directories_ = std::move(directories);
    const double seconds = std::chrono::duration<double>(SteadyClock::now() - started).count();
    updateStatus(
        [&](IndexSnapshot& s) {
            s.source = IndexSource::NtfsMft;
            s.files = live;
            s.scanned = scanned;
            s.scan_seconds = seconds;
            s.elevation_would_help = false;
            ++s.generation;
            ++s.epoch;
        },
        true);
    return true;
}

void IndexService::fillMetadata(NativeHandle volume) {
    std::vector<MetadataUpdate> batch;
    batch.reserve(kMetadataBatch);
    const auto flush = [&] {
        if (batch.empty()) return;
        std::unique_lock lock(index_mutex_);
        for (const MetadataUpdate& update : batch) {
            const auto index = index_.findKey(update.record);
            if (index) index_.setMetadata(*index, update.size, update.modified);
        }
        batch.clear();
    };
    const auto collect = [&](const DirectoryEntryMetadata& entry) {
        batch.push_back({mftRecordNumber(entry.file_reference), entry.size, entry.modified});
        if (batch.size() >= kMetadataBatch) flush();
    };

    const std::wstring root_path = toExtendedPath(std::wstring(1, drive_) + L":\\");
    std::uint64_t inaccessible = 0;
    if (listDirectoryMetadata(volume, 0, root_path, collect) != 0) ++inaccessible;

    const std::size_t total = directories_.size();
    auto last_generation_bump = SteadyClock::now();
    for (std::size_t i = 0; i < total; ++i) {
        if (interrupted()) return;
        if (listDirectoryMetadata(volume, directories_[i].second, root_path, collect) != 0) ++inaccessible;
        if ((i & 0xFF) == 0) {
            flush();
            const bool bump = SteadyClock::now() - last_generation_bump > std::chrono::seconds(2);
            if (bump) last_generation_bump = SteadyClock::now();
            const double progress = total == 0 ? 1.0 : static_cast<double>(i) / static_cast<double>(total);
            updateStatus(
                [&](IndexSnapshot& s) {
                    s.metadata_progress = progress;
                    s.inaccessible = inaccessible;
                    if (bump) ++s.generation;
                },
                false);
        }
    }
    flush();
    updateStatus([&](IndexSnapshot& s) { s.inaccessible = inaccessible; }, false);
}

void IndexService::buildFromDirectoryScan() {
    const auto started = SteadyClock::now();
    {
        std::unique_lock lock(index_mutex_);
        index_ = core::FileIndex(rootName());
    }
    updateStatus(
        [](IndexSnapshot& s) {
            s.source = IndexSource::DirectoryScan;
            s.files = 0;
            ++s.generation;
            ++s.epoch;
        },
        true);

    struct Pending {
        std::wstring path;
        std::uint32_t index;
    };
    std::vector<Pending> stack;
    stack.push_back({toExtendedPath(std::wstring(1, drive_) + L":\\"), core::kNoParent});
    std::vector<DirectoryEntry> entries;
    std::uint64_t scanned = 0;
    std::uint64_t inaccessible = 0;
    auto last_report = SteadyClock::now();

    while (!stack.empty()) {
        if (interrupted()) return;
        Pending current = std::move(stack.back());
        stack.pop_back();
        if (listDirectory(current.path, entries) != 0) {
            ++inaccessible;
            continue;
        }
        std::uint64_t live = 0;
        {
            std::unique_lock lock(index_mutex_);
            for (const DirectoryEntry& entry : entries) {
                const std::uint16_t flags = flagsFromAttributes(entry.attributes);
                const std::uint32_t index = index_.append(toUtf8(entry.name), current.index, flags);
                index_.setMetadata(index, entry.isDirectory() ? 0 : entry.size, entry.modified);
                if (entry.isDirectory() && !entry.isReparsePoint())
                    stack.push_back({joinPath(current.path, entry.name), index});
            }
            live = index_.liveCount();
        }
        scanned += entries.size();
        if (SteadyClock::now() - last_report > std::chrono::milliseconds(250)) {
            last_report = SteadyClock::now();
            updateStatus(
                [&](IndexSnapshot& s) {
                    s.files = live;
                    s.scanned = scanned;
                    s.inaccessible = inaccessible;
                    ++s.generation;
                },
                false);
        }
    }

    std::uint64_t live = 0;
    {
        std::shared_lock lock(index_mutex_);
        live = index_.liveCount();
    }
    const double seconds = std::chrono::duration<double>(SteadyClock::now() - started).count();
    updateStatus(
        [&](IndexSnapshot& s) {
            s.files = live;
            s.scanned = scanned;
            s.inaccessible = inaccessible;
            s.scan_seconds = seconds;
        },
        true);
}

void IndexService::monitorJournal(NativeHandle volume, const JournalState& journal) {
    std::int64_t next_usn = journal.next_usn;
    std::vector<OwnedChange> changes;
    while (!interrupted()) {
        changes.clear();
        const JournalRead read = readJournal(volume, journal, next_usn, [&](const UsnEntry& entry) {
            OwnedChange change;
            change.file_reference = entry.file_reference;
            change.parent_reference = entry.parent_reference;
            change.attributes = entry.attributes;
            change.reason = entry.reason;
            change.name = toUtf8(entry.name);
            changes.push_back(std::move(change));
        });
        if (read.error != 0) {
            if (read.error == ERROR_JOURNAL_ENTRY_DELETED) {
                rescan_requested_ = true;
                return;
            }
            updateStatus(
                [&](IndexSnapshot& s) {
                    s.live_updates = false;
                    s.error = describeError(read.error);
                },
                true);
            waitFor(std::chrono::hours(24 * 365));
            return;
        }
        next_usn = read.next_usn;
        if (!changes.empty()) applyChanges(volume, changes);
        waitFor(std::chrono::milliseconds(1000));
    }
}

void IndexService::applyChanges(NativeHandle volume, std::vector<OwnedChange>& changes) {
    std::vector<std::uint64_t> refresh;
    std::uint64_t live = 0;
    {
        std::unique_lock lock(index_mutex_);
        for (const OwnedChange& change : changes) {
            const std::uint64_t record = mftRecordNumber(change.file_reference);
            const std::uint64_t parent_record = mftRecordNumber(change.parent_reference);
            if (isSystemMetadataEntry(record, parent_record)) continue;
            const auto existing = index_.findKey(record);

            if ((change.reason & USN_REASON_FILE_DELETE) != 0) {
                if (existing) {
                    index_.markDeleted(*existing);
                    index_.unbindKey(record);
                }
                continue;
            }
            if ((change.reason & USN_REASON_RENAME_OLD_NAME) != 0) continue;

            const auto parent = index_.findKey(parent_record);
            const std::uint32_t parent_index = parent ? *parent : core::kNoParent;
            const std::uint16_t flags = flagsFromAttributes(change.attributes);
            if (existing) {
                const auto kept = static_cast<std::uint16_t>(index_.record(*existing).flags & core::kRecordMetadataKnown);
                index_.rename(*existing, change.name);
                index_.setParent(*existing, parent_index);
                index_.setFlags(*existing, static_cast<std::uint16_t>(flags | kept));
            } else {
                const std::uint32_t index = index_.append(change.name, parent_index, flags);
                index_.bindKey(record, index);
                if ((flags & core::kRecordDirectory) != 0) directories_.emplace_back(index, change.file_reference);
            }
            if ((change.reason & USN_REASON_CLOSE) != 0 && refresh.size() < kMaxMetadataRefreshPerBatch)
                refresh.push_back(change.file_reference);
        }
        live = index_.liveCount();
    }

    std::unordered_set<std::uint64_t> seen;
    std::vector<MetadataUpdate> updates;
    for (const std::uint64_t reference : refresh) {
        if (!seen.insert(reference).second) continue;
        const auto metadata = queryMetadataByReference(volume, reference);
        if (metadata) updates.push_back({mftRecordNumber(reference), metadata->size, metadata->modified});
    }
    if (!updates.empty()) {
        std::unique_lock lock(index_mutex_);
        for (const MetadataUpdate& update : updates) {
            const auto index = index_.findKey(update.record);
            if (index) index_.setMetadata(*index, index_.record(*index).isDirectory() ? 0 : update.size, update.modified);
        }
    }

    const auto count = static_cast<std::uint64_t>(changes.size());
    updateStatus(
        [&](IndexSnapshot& s) {
            s.files = live;
            s.live_changes += count;
            s.last_change = SteadyClock::now();
            ++s.generation;
        },
        true);
}

}
