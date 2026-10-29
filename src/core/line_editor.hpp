#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace instaseek::core {

class LineEditor {
public:
    explicit LineEditor(std::string* text);

    void sync();

    std::size_t length() const;
    std::size_t cursor() const { return cursor_; }
    bool hasSelection() const { return anchor_ != cursor_; }
    std::optional<std::pair<std::size_t, std::size_t>> selection() const;
    std::string selectedText() const;
    std::size_t byteOffset(std::size_t index) const;

    bool insert(std::string_view utf8);
    bool deleteSelection();
    bool backspace();
    bool deleteForward();
    bool clear();

    void moveTo(std::size_t index, bool extend);
    void moveLeft(bool extend);
    void moveRight(bool extend);
    void moveWordLeft(bool extend);
    void moveWordRight(bool extend);
    void home(bool extend) { moveTo(0, extend); }
    void end(bool extend) { moveTo(length(), extend); }
    void selectAll();
    void selectWordAt(std::size_t index);

    static std::string sanitize(std::string_view utf8);

private:
    std::vector<char32_t> codepoints() const;
    void remember();

    std::string* text_;
    std::string last_seen_;
    std::size_t cursor_ = 0;
    std::size_t anchor_ = 0;
};

}
