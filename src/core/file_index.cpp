#include "core/file_index.hpp"

#include "core/utf8.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace instaseek::core {

FileIndex::FileIndex(std::string root) : root_(std::move(root)) {}

void FileIndex::reserve(std::size_t records, std::size_t name_bytes) {
    records_.reserve(records);
    names_.reserve(name_bytes);
    folded_names_.reserve(name_bytes);
}

void FileIndex::clear() {
    records_.clear();
    names_.clear();
    folded_names_.clear();
    dense_keys_.clear();
    sparse_keys_.clear();
    deleted_ = 0;
}

std::uint32_t FileIndex::storeName(std::string_view utf8_name) {
    if (utf8_name.size() > std::numeric_limits<std::uint16_t>::max())
        utf8_name = utf8_name.substr(0, utf8::alignToCodepointStart(utf8_name, std::numeric_limits<std::uint16_t>::max()));
    const auto offset = static_cast<std::uint32_t>(names_.size());
    names_.append(utf8_name);
    std::string folded = utf8::foldCase(utf8_name);
    folded.resize(utf8_name.size(), ' ');
    folded_names_.append(folded);
    return offset;
}

std::uint32_t FileIndex::append(std::string_view utf8_name, std::uint32_t parent, std::uint16_t flags) {
    FileRecord record;
    record.name_offset = storeName(utf8_name);
    record.name_length = static_cast<std::uint16_t>(names_.size() - record.name_offset);
    record.parent = parent;
    record.flags = flags;
    records_.push_back(record);
    if (record.isDeleted()) ++deleted_;
    return static_cast<std::uint32_t>(records_.size() - 1);
}

void FileIndex::setParent(std::uint32_t index, std::uint32_t parent) {
    if (index < records_.size()) records_[index].parent = parent == index ? kNoParent : parent;
}

void FileIndex::rename(std::uint32_t index, std::string_view utf8_name) {
    if (index >= records_.size()) return;
    if (name(index) == utf8_name) return;
    FileRecord& record = records_[index];
    record.name_offset = storeName(utf8_name);
    record.name_length = static_cast<std::uint16_t>(names_.size() - record.name_offset);
}

void FileIndex::setFlags(std::uint32_t index, std::uint16_t flags) {
    if (index >= records_.size()) return;
    FileRecord& record = records_[index];
    const bool was_deleted = record.isDeleted();
    record.flags = flags;
    if (was_deleted && !record.isDeleted()) --deleted_;
    if (!was_deleted && record.isDeleted()) ++deleted_;
}

void FileIndex::markDeleted(std::uint32_t index) {
    if (index >= records_.size() || records_[index].isDeleted()) return;
    records_[index].flags |= kRecordDeleted;
    ++deleted_;
}

void FileIndex::setMetadata(std::uint32_t index, std::uint64_t size, std::int64_t modified) {
    if (index >= records_.size()) return;
    FileRecord& record = records_[index];
    record.size = size;
    record.modified = modified;
    record.flags |= kRecordMetadataKnown;
}

void FileIndex::bindKey(std::uint64_t key, std::uint32_t index) {
    if (key < kDenseKeyLimit) {
        const auto slot = static_cast<std::size_t>(key);
        if (slot >= dense_keys_.size()) dense_keys_.resize(std::max<std::size_t>(slot + 1, dense_keys_.size() * 3 / 2), kNoParent);
        dense_keys_[slot] = index;
        return;
    }
    sparse_keys_[key] = index;
}

void FileIndex::unbindKey(std::uint64_t key) {
    if (key < kDenseKeyLimit) {
        const auto slot = static_cast<std::size_t>(key);
        if (slot < dense_keys_.size()) dense_keys_[slot] = kNoParent;
        return;
    }
    sparse_keys_.erase(key);
}

std::optional<std::uint32_t> FileIndex::findKey(std::uint64_t key) const {
    if (key < kDenseKeyLimit) {
        const auto slot = static_cast<std::size_t>(key);
        if (slot >= dense_keys_.size() || dense_keys_[slot] == kNoParent) return std::nullopt;
        return dense_keys_[slot];
    }
    const auto found = sparse_keys_.find(key);
    if (found == sparse_keys_.end()) return std::nullopt;
    return found->second;
}

std::string_view FileIndex::name(std::uint32_t index) const {
    const FileRecord& record = records_[index];
    return std::string_view(names_).substr(record.name_offset, record.name_length);
}

std::string_view FileIndex::foldedName(std::uint32_t index) const {
    const FileRecord& record = records_[index];
    return std::string_view(folded_names_).substr(record.name_offset, record.name_length);
}

std::string FileIndex::parentPath(std::uint32_t index) const {
    if (index >= records_.size()) return root_;
    const std::uint32_t parent = records_[index].parent;
    if (parent == kNoParent || parent >= records_.size()) return root_.empty() ? std::string() : root_ + "\\";
    return fullPath(parent);
}

std::string FileIndex::fullPath(std::uint32_t index) const {
    if (index >= records_.size()) return root_;
    std::vector<std::uint32_t> chain;
    std::uint32_t current = index;
    while (current != kNoParent && current < records_.size() && chain.size() < kMaxPathDepth) {
        chain.push_back(current);
        current = records_[current].parent;
    }
    std::string path = root_;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (!path.empty() && path.back() != '\\') path.push_back('\\');
        path.append(name(*it));
    }
    return path;
}

std::size_t FileIndex::memoryBytes() const {
    return records_.capacity() * sizeof(FileRecord) + names_.capacity() + folded_names_.capacity() +
           dense_keys_.capacity() * sizeof(std::uint32_t) + sparse_keys_.size() * 32;
}

}
