#include "core/line_index.hpp"

#include <algorithm>

namespace instaseek::core {

void LineIndex::reset(std::uint64_t data_begin, TextEncoding encoding) {
    encoding_ = encoding;
    starts_.assign(1, data_begin);
    data_begin_ = data_begin;
    position_ = data_begin;
    newlines_ = 0;
    last_was_newline_ = false;
    finished_ = false;
}

void LineIndex::consume(const std::uint8_t* data, std::size_t size) {
    const std::size_t unit = encodingUnitSize(encoding_);
    std::size_t from = 0;
    while (true) {
        const auto newline = findNewline(data, size, from, encoding_);
        if (!newline) break;
        ++newlines_;
        if (newlines_ % kStride == 0) starts_.push_back(position_ + *newline + unit);
        from = *newline + unit;
    }
    if (size >= unit) last_was_newline_ = isNewlineUnit(data + (size - size % unit) - unit, encoding_);
    position_ += size;
}

void LineIndex::finish() { finished_ = true; }

std::uint64_t LineIndex::lineCount() const {
    if (position_ == data_begin_) return 1;
    return newlines_ + (last_was_newline_ ? 0 : 1);
}

LineIndex::Checkpoint LineIndex::checkpointForLine(std::uint64_t line) const {
    if (starts_.empty()) return {0, data_begin_};
    const std::uint64_t slot = std::min<std::uint64_t>(line / kStride, starts_.size() - 1);
    return {slot * kStride, starts_[static_cast<std::size_t>(slot)]};
}

LineIndex::Checkpoint LineIndex::checkpointForOffset(std::uint64_t offset) const {
    if (starts_.empty()) return {0, data_begin_};
    const auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
    const std::size_t slot = it == starts_.begin() ? 0 : static_cast<std::size_t>(it - starts_.begin()) - 1;
    return {static_cast<std::uint64_t>(slot) * kStride, starts_[slot]};
}

}
