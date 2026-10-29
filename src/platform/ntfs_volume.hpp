#pragma once

#include "platform/unique_handle.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace instaseek::platform {

inline constexpr std::uint64_t kNtfsRootRecord = 5;

inline std::uint64_t mftRecordNumber(std::uint64_t file_reference) { return file_reference & 0x0000FFFFFFFFFFFFull; }

struct VolumeHandle {
    UniqueHandle handle;
    unsigned long error = 0;
};

struct JournalState {
    std::uint64_t journal_id = 0;
    std::int64_t next_usn = 0;
};

struct UsnEntry {
    std::uint64_t file_reference = 0;
    std::uint64_t parent_reference = 0;
    std::uint32_t attributes = 0;
    std::uint32_t reason = 0;
    std::wstring_view name;
};

struct DirectoryEntryMetadata {
    std::uint64_t file_reference = 0;
    std::uint64_t size = 0;
    std::int64_t modified = 0;
    std::uint32_t attributes = 0;
};

struct JournalRead {
    unsigned long error = 0;
    std::int64_t next_usn = 0;
};

bool isNtfsVolume(wchar_t drive_letter);
VolumeHandle openVolume(wchar_t drive_letter);
std::optional<JournalState> queryJournal(NativeHandle volume, unsigned long& error);

unsigned long enumerateMft(NativeHandle volume, std::int64_t high_usn, const std::function<void(const UsnEntry&)>& on_entry,
                           const std::atomic<bool>& stop);

JournalRead readJournal(NativeHandle volume, const JournalState& journal, std::int64_t start_usn,
                        const std::function<void(const UsnEntry&)>& on_entry);

unsigned long listDirectoryMetadata(NativeHandle volume, std::uint64_t directory_reference, const std::wstring& path_fallback,
                                    const std::function<void(const DirectoryEntryMetadata&)>& on_entry);

std::optional<DirectoryEntryMetadata> queryMetadataByReference(NativeHandle volume, std::uint64_t file_reference);

}
