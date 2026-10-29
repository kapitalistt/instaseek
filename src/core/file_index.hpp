#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instaseek::core {

inline constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;

enum RecordFlag : std::uint16_t {
    kRecordDirectory = 1u << 0,
    kRecordHidden = 1u << 1,
    kRecordSystem = 1u << 2,
    kRecordDeleted = 1u << 3,
    kRecordMetadataKnown = 1u << 4,
    kRecordReparsePoint = 1u << 5,
};

struct FileRecord {
    std::uint64_t size = 0;
    std::int64_t modified = 0;
    std::uint32_t name_offset = 0;
    std::uint32_t parent = kNoParent;
    std::uint16_t name_length = 0;
    std::uint16_t flags = 0;

    bool isDirectory() const { return (flags & kRecordDirectory) != 0; }
    bool isDeleted() const { return (flags & kRecordDeleted) != 0; }
    bool hasMetadata() const { return (flags & kRecordMetadataKnown) != 0; }
};

class FileIndex {
public:
    explicit FileIndex(std::string root = {});

    void reserve(std::size_t records, std::size_t name_bytes);
    void clear();

    std::uint32_t append(std::string_view utf8_name, std::uint32_t parent, std::uint16_t flags);
    void setParent(std::uint32_t index, std::uint32_t parent);
    void rename(std::uint32_t index, std::string_view utf8_name);
    void setFlags(std::uint32_t index, std::uint16_t flags);
    void markDeleted(std::uint32_t index);
    void setMetadata(std::uint32_t index, std::uint64_t size, std::int64_t modified);

    void bindKey(std::uint64_t key, std::uint32_t index);
    void unbindKey(std::uint64_t key);
    std::optional<std::uint32_t> findKey(std::uint64_t key) const;

    std::size_t recordCount() const { return records_.size(); }
    std::size_t liveCount() const { return records_.size() - deleted_; }
    const FileRecord& record(std::uint32_t index) const { return records_[index]; }
    std::string_view name(std::uint32_t index) const;
    std::string_view foldedName(std::uint32_t index) const;
    std::string fullPath(std::uint32_t index) const;
    std::string parentPath(std::uint32_t index) const;
    const std::string& root() const { return root_; }
    void setRoot(std::string root) { root_ = std::move(root); }

    std::size_t memoryBytes() const;

private:
    static constexpr std::uint64_t kDenseKeyLimit = 1ull << 25;
    static constexpr std::size_t kMaxPathDepth = 4096;

    std::uint32_t storeName(std::string_view utf8_name);

    std::string root_;
    std::vector<FileRecord> records_;
    std::string names_;
    std::string folded_names_;
    std::vector<std::uint32_t> dense_keys_;
    std::unordered_map<std::uint64_t, std::uint32_t> sparse_keys_;
    std::size_t deleted_ = 0;
};

}
