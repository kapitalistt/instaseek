#include "test_harness.hpp"

#include "core/line_index.hpp"

#include <string>
#include <vector>

using namespace instaseek::core;

namespace {

std::string numberedLines(int count) {
    std::string text;
    for (int i = 0; i < count; ++i) text += "row " + std::to_string(i) + "\n";
    return text;
}

std::vector<std::uint64_t> lineStarts(const std::string& text) {
    std::vector<std::uint64_t> starts{0};
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n' && i + 1 < text.size()) starts.push_back(i + 1);
    return starts;
}

const std::uint8_t* bytes(const std::string& text) { return reinterpret_cast<const std::uint8_t*>(text.data()); }

}

INSTASEEK_TEST(line_index_builds_sparse_checkpoints) {
    const std::string text = numberedLines(1000);
    const auto starts = lineStarts(text);
    for (const std::size_t piece : {std::size_t{1}, std::size_t{7}, std::size_t{4096}, text.size()}) {
        LineIndex index;
        index.reset(0, TextEncoding::Utf8);
        for (std::size_t offset = 0; offset < text.size(); offset += piece)
            index.consume(bytes(text) + offset, std::min(piece, text.size() - offset));
        index.finish();
        CHECK(index.newlineCount() == 1000);
        CHECK(index.lineCount() == 1000);
        const auto by_line = index.checkpointForLine(300);
        CHECK(by_line.line == 256 && by_line.offset == starts[256]);
        const auto by_offset = index.checkpointForOffset(starts[600] + 2);
        CHECK(by_offset.line == 512 && by_offset.offset == starts[512]);
        const auto first = index.checkpointForLine(10);
        CHECK(first.line == 0 && first.offset == 0);
    }
}

INSTASEEK_TEST(line_count_handles_trailing_newline) {
    LineIndex index;
    index.reset(0, TextEncoding::Utf8);
    const std::string unterminated = "a\nb";
    index.consume(bytes(unterminated), unterminated.size());
    CHECK(index.lineCount() == 2);
    index.reset(0, TextEncoding::Utf8);
    const std::string terminated = "a\nb\n";
    index.consume(bytes(terminated), terminated.size());
    CHECK(index.lineCount() == 2);
    index.reset(0, TextEncoding::Utf8);
    CHECK(index.lineCount() == 1);
}

INSTASEEK_TEST(line_index_respects_utf16_units_and_bom) {
    const std::string utf8 = numberedLines(600);
    const auto encoded = encodeFromUtf8(utf8, TextEncoding::Utf16Le);
    CHECK(encoded.has_value());
    const std::string text = std::string("\xFF\xFE", 2) + *encoded;
    LineIndex index;
    index.reset(2, TextEncoding::Utf16Le);
    for (std::size_t offset = 2; offset < text.size(); offset += 64)
        index.consume(bytes(text) + offset, std::min<std::size_t>(64, text.size() - offset));
    CHECK(index.newlineCount() == 600);
    const auto starts = lineStarts(utf8);
    const auto checkpoint = index.checkpointForLine(512);
    CHECK(checkpoint.line == 512 && checkpoint.offset == 2 + starts[512] * 2);
}
