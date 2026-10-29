#include "platform/ntfs_volume.hpp"

#include "platform/win.hpp"

#include <windows.h>
#include <winioctl.h>

#include <cstring>
#include <cwchar>
#include <vector>

namespace instaseek::platform {

namespace {

constexpr std::size_t kUsnBufferBytes = 1u << 20;
constexpr std::size_t kDirectoryBufferBytes = 64u * 1024u;
constexpr int kMaxJournalBuffersPerRead = 64;

std::uint64_t lowQuadOfFileId128(const FILE_ID_128& id) {
    std::uint64_t value = 0;
    std::memcpy(&value, id.Identifier, sizeof(value));
    return value;
}

void parseUsnRecords(const std::uint8_t* buffer, DWORD bytes, const std::function<void(const UsnEntry&)>& on_entry) {
    DWORD offset = sizeof(USN);
    while (offset + sizeof(USN_RECORD_COMMON_HEADER) <= bytes) {
        const auto* header = reinterpret_cast<const USN_RECORD_COMMON_HEADER*>(buffer + offset);
        if (header->RecordLength < sizeof(USN_RECORD_COMMON_HEADER) || offset + header->RecordLength > bytes) break;
        UsnEntry entry;
        bool valid = false;
        if (header->MajorVersion == 2 && header->RecordLength >= sizeof(USN_RECORD_V2)) {
            const auto* record = reinterpret_cast<const USN_RECORD_V2*>(header);
            if (static_cast<DWORD>(record->FileNameOffset) + record->FileNameLength <= record->RecordLength) {
                entry.file_reference = record->FileReferenceNumber;
                entry.parent_reference = record->ParentFileReferenceNumber;
                entry.attributes = record->FileAttributes;
                entry.reason = record->Reason;
                entry.name = std::wstring_view(
                    reinterpret_cast<const wchar_t*>(reinterpret_cast<const std::uint8_t*>(record) + record->FileNameOffset),
                    record->FileNameLength / sizeof(wchar_t));
                valid = true;
            }
        } else if (header->MajorVersion == 3 && header->RecordLength >= sizeof(USN_RECORD_V3)) {
            const auto* record = reinterpret_cast<const USN_RECORD_V3*>(header);
            if (static_cast<DWORD>(record->FileNameOffset) + record->FileNameLength <= record->RecordLength) {
                entry.file_reference = lowQuadOfFileId128(record->FileReferenceNumber);
                entry.parent_reference = lowQuadOfFileId128(record->ParentFileReferenceNumber);
                entry.attributes = record->FileAttributes;
                entry.reason = record->Reason;
                entry.name = std::wstring_view(
                    reinterpret_cast<const wchar_t*>(reinterpret_cast<const std::uint8_t*>(record) + record->FileNameOffset),
                    record->FileNameLength / sizeof(wchar_t));
                valid = true;
            }
        }
        if (valid) on_entry(entry);
        offset += header->RecordLength;
    }
}

UniqueHandle openDirectory(NativeHandle volume, std::uint64_t reference, const std::wstring& path_fallback) {
    if (reference == 0) {
        return UniqueHandle(CreateFileW(path_fallback.c_str(), FILE_LIST_DIRECTORY,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    }
    FILE_ID_DESCRIPTOR descriptor{};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.Type = FileIdType;
    descriptor.FileId.QuadPart = static_cast<LONGLONG>(reference);
    return UniqueHandle(OpenFileById(volume, &descriptor, FILE_LIST_DIRECTORY,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                     FILE_FLAG_BACKUP_SEMANTICS));
}

}

bool isNtfsVolume(wchar_t drive_letter) {
    wchar_t root[] = L"X:\\";
    root[0] = drive_letter;
    wchar_t file_system[MAX_PATH + 1] = {};
    if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, file_system, MAX_PATH + 1)) return false;
    return _wcsicmp(file_system, L"NTFS") == 0;
}

VolumeHandle openVolume(wchar_t drive_letter) {
    wchar_t path[] = L"\\\\.\\X:";
    path[4] = drive_letter;
    VolumeHandle result;
    result.handle.reset(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                    nullptr));
    if (!result.handle) result.error = GetLastError();
    return result;
}

std::optional<JournalState> queryJournal(NativeHandle volume, unsigned long& error) {
    USN_JOURNAL_DATA_V2 data{};
    DWORD bytes = 0;
    if (!DeviceIoControl(volume, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &data, sizeof(data), &bytes, nullptr)) {
        error = GetLastError();
        return std::nullopt;
    }
    error = 0;
    return JournalState{data.UsnJournalID, data.NextUsn};
}

unsigned long enumerateMft(NativeHandle volume, std::int64_t high_usn, const std::function<void(const UsnEntry&)>& on_entry,
                           const std::atomic<bool>& stop) {
    std::vector<std::uint64_t> storage(kUsnBufferBytes / sizeof(std::uint64_t));
    auto* buffer = reinterpret_cast<std::uint8_t*>(storage.data());
    MFT_ENUM_DATA_V0 request{};
    request.StartFileReferenceNumber = 0;
    request.LowUsn = 0;
    request.HighUsn = high_usn;
    while (!stop.load(std::memory_order_relaxed)) {
        DWORD bytes = 0;
        if (!DeviceIoControl(volume, FSCTL_ENUM_USN_DATA, &request, sizeof(request), buffer,
                             static_cast<DWORD>(kUsnBufferBytes), &bytes, nullptr)) {
            const DWORD error = GetLastError();
            return error == ERROR_HANDLE_EOF ? 0 : error;
        }
        if (bytes < sizeof(USN)) return 0;
        parseUsnRecords(buffer, bytes, on_entry);
        DWORDLONG next = 0;
        std::memcpy(&next, buffer, sizeof(next));
        if (next == request.StartFileReferenceNumber) return 0;
        request.StartFileReferenceNumber = next;
    }
    return ERROR_CANCELLED;
}

JournalRead readJournal(NativeHandle volume, const JournalState& journal, std::int64_t start_usn,
                        const std::function<void(const UsnEntry&)>& on_entry) {
    std::vector<std::uint64_t> storage(kUsnBufferBytes / sizeof(std::uint64_t));
    auto* buffer = reinterpret_cast<std::uint8_t*>(storage.data());
    READ_USN_JOURNAL_DATA_V0 request{};
    request.StartUsn = start_usn;
    request.ReasonMask = 0xFFFFFFFFu;
    request.ReturnOnlyOnClose = FALSE;
    request.Timeout = 0;
    request.BytesToWaitFor = 0;
    request.UsnJournalID = journal.journal_id;

    JournalRead result;
    result.next_usn = start_usn;
    for (int round = 0; round < kMaxJournalBuffersPerRead; ++round) {
        DWORD bytes = 0;
        if (!DeviceIoControl(volume, FSCTL_READ_USN_JOURNAL, &request, sizeof(request), buffer,
                             static_cast<DWORD>(kUsnBufferBytes), &bytes, nullptr)) {
            result.error = GetLastError();
            return result;
        }
        if (bytes < sizeof(USN)) return result;
        USN next = 0;
        std::memcpy(&next, buffer, sizeof(next));
        parseUsnRecords(buffer, bytes, on_entry);
        result.next_usn = next;
        if (bytes <= sizeof(USN) || next == request.StartUsn) return result;
        request.StartUsn = next;
    }
    return result;
}

unsigned long listDirectoryMetadata(NativeHandle volume, std::uint64_t directory_reference, const std::wstring& path_fallback,
                                    const std::function<void(const DirectoryEntryMetadata&)>& on_entry) {
    const UniqueHandle directory = openDirectory(volume, directory_reference, path_fallback);
    if (!directory) return GetLastError();

    std::vector<std::uint64_t> storage(kDirectoryBufferBytes / sizeof(std::uint64_t));
    auto* buffer = reinterpret_cast<std::uint8_t*>(storage.data());
    FILE_INFO_BY_HANDLE_CLASS info_class = FileIdBothDirectoryRestartInfo;
    while (true) {
        if (!GetFileInformationByHandleEx(directory.get(), info_class, buffer, static_cast<DWORD>(kDirectoryBufferBytes))) {
            const DWORD error = GetLastError();
            return error == ERROR_NO_MORE_FILES ? 0 : error;
        }
        info_class = FileIdBothDirectoryInfo;
        std::size_t offset = 0;
        while (true) {
            const auto* entry = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer + offset);
            const std::wstring_view name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") {
                DirectoryEntryMetadata metadata;
                metadata.file_reference = static_cast<std::uint64_t>(entry->FileId.QuadPart);
                metadata.size = static_cast<std::uint64_t>(entry->EndOfFile.QuadPart);
                metadata.modified = entry->LastWriteTime.QuadPart;
                metadata.attributes = entry->FileAttributes;
                on_entry(metadata);
            }
            if (entry->NextEntryOffset == 0) break;
            offset += entry->NextEntryOffset;
            if (offset >= kDirectoryBufferBytes) break;
        }
    }
}

std::optional<DirectoryEntryMetadata> queryMetadataByReference(NativeHandle volume, std::uint64_t file_reference) {
    FILE_ID_DESCRIPTOR descriptor{};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.Type = FileIdType;
    descriptor.FileId.QuadPart = static_cast<LONGLONG>(file_reference);
    const UniqueHandle file(OpenFileById(volume, &descriptor, FILE_READ_ATTRIBUTES,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                         FILE_FLAG_BACKUP_SEMANTICS));
    if (!file) return std::nullopt;
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    if (!GetFileInformationByHandleEx(file.get(), FileBasicInfo, &basic, sizeof(basic))) return std::nullopt;
    if (!GetFileInformationByHandleEx(file.get(), FileStandardInfo, &standard, sizeof(standard))) return std::nullopt;
    DirectoryEntryMetadata metadata;
    metadata.file_reference = file_reference;
    metadata.size = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    metadata.modified = basic.LastWriteTime.QuadPart;
    metadata.attributes = basic.FileAttributes;
    return metadata;
}

}
