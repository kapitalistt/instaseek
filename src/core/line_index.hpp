#pragma once

#include "core/encoding.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace instaseek::core {

class LineIndex {
public:
    static constexpr std::uint64_t kStride = 256;

    struct Checkpoint {
        std::uint64_t line = 0;
        std::uint64_t offset = 0;
    };

    void reset(std::uint64_t data_begin, TextEncoding encoding);
    void consume(const std::uint8_t* data, std::size_t size);
    void finish();

    std::uint64_t position() const { return position_; }
    std::uint64_t newlineCount() const { return newlines_; }
    bool finished() const { return finished_; }
    std::uint64_t lineCount() const;

    Checkpoint checkpointForLine(std::uint64_t line) const;
    Checkpoint checkpointForOffset(std::uint64_t offset) const;

private:
    TextEncoding encoding_ = TextEncoding::Utf8;
    std::vector<std::uint64_t> starts_;
    std::uint64_t data_begin_ = 0;
    std::uint64_t position_ = 0;
    std::uint64_t newlines_ = 0;
    bool last_was_newline_ = false;
    bool finished_ = false;
};

}
