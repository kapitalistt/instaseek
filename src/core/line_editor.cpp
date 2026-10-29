#include "core/line_editor.hpp"

#include "core/utf8.hpp"

#include <algorithm>

namespace instaseek::core {

LineEditor::LineEditor(std::string* text) : text_(text) {
    cursor_ = length();
    anchor_ = cursor_;
    remember();
}

void LineEditor::remember() { last_seen_ = *text_; }

void LineEditor::sync() {
    if (*text_ != last_seen_) {
        cursor_ = length();
        anchor_ = cursor_;
        remember();
        return;
    }
    const std::size_t size = length();
    cursor_ = std::min(cursor_, size);
    anchor_ = std::min(anchor_, size);
}

std::size_t LineEditor::length() const { return utf8::codepointCount(*text_); }

std::vector<char32_t> LineEditor::codepoints() const {
    std::vector<char32_t> result;
    std::size_t offset = 0;
    while (offset < text_->size()) {
        const auto decoded = utf8::decode(*text_, offset);
        result.push_back(decoded.value);
        offset += decoded.length;
    }
    return result;
}

std::size_t LineEditor::byteOffset(std::size_t index) const {
    std::size_t offset = 0;
    while (offset < text_->size() && index > 0) {
        offset += utf8::decode(*text_, offset).length;
        --index;
    }
    return offset;
}

std::optional<std::pair<std::size_t, std::size_t>> LineEditor::selection() const {
    if (!hasSelection()) return std::nullopt;
    return std::make_pair(std::min(anchor_, cursor_), std::max(anchor_, cursor_));
}

std::string LineEditor::selectedText() const {
    const auto range = selection();
    if (!range) return {};
    const std::size_t begin = byteOffset(range->first);
    const std::size_t end = byteOffset(range->second);
    return text_->substr(begin, end - begin);
}

std::string LineEditor::sanitize(std::string_view utf8_text) {
    std::string clean;
    clean.reserve(utf8_text.size());
    std::size_t offset = 0;
    while (offset < utf8_text.size()) {
        const auto decoded = utf8::decode(utf8_text, offset);
        const char32_t c = decoded.value;
        offset += decoded.length;
        if (c == U'\r') continue;
        if (c == U'\n' || c == U'\t') clean.push_back(' ');
        else if (c >= 0x20 && c != 0x7F && !(c >= 0x80 && c < 0xA0)) utf8::append(clean, c);
    }
    return clean;
}

bool LineEditor::deleteSelection() {
    const auto range = selection();
    if (!range) return false;
    const std::size_t begin = byteOffset(range->first);
    const std::size_t end = byteOffset(range->second);
    text_->erase(begin, end - begin);
    cursor_ = range->first;
    anchor_ = cursor_;
    remember();
    return true;
}

bool LineEditor::insert(std::string_view utf8_text) {
    sync();
    const std::string clean = sanitize(utf8_text);
    const bool removed = deleteSelection();
    if (clean.empty()) return removed;
    text_->insert(byteOffset(cursor_), clean);
    cursor_ += utf8::codepointCount(clean);
    anchor_ = cursor_;
    remember();
    return true;
}

bool LineEditor::backspace() {
    sync();
    if (deleteSelection()) return true;
    if (cursor_ == 0) return false;
    const std::size_t begin = byteOffset(cursor_ - 1);
    const std::size_t end = byteOffset(cursor_);
    text_->erase(begin, end - begin);
    --cursor_;
    anchor_ = cursor_;
    remember();
    return true;
}

bool LineEditor::deleteForward() {
    sync();
    if (deleteSelection()) return true;
    if (cursor_ >= length()) return false;
    const std::size_t begin = byteOffset(cursor_);
    const std::size_t end = byteOffset(cursor_ + 1);
    text_->erase(begin, end - begin);
    remember();
    return true;
}

bool LineEditor::clear() {
    sync();
    if (text_->empty()) return false;
    text_->clear();
    cursor_ = 0;
    anchor_ = 0;
    remember();
    return true;
}

void LineEditor::moveTo(std::size_t index, bool extend) {
    sync();
    cursor_ = std::min(index, length());
    if (!extend) anchor_ = cursor_;
}

void LineEditor::moveLeft(bool extend) {
    sync();
    if (!extend && hasSelection()) {
        moveTo(selection()->first, false);
        return;
    }
    moveTo(cursor_ > 0 ? cursor_ - 1 : 0, extend);
}

void LineEditor::moveRight(bool extend) {
    sync();
    if (!extend && hasSelection()) {
        moveTo(selection()->second, false);
        return;
    }
    moveTo(cursor_ + 1, extend);
}

void LineEditor::moveWordLeft(bool extend) {
    sync();
    const auto points = codepoints();
    std::size_t index = cursor_;
    while (index > 0 && !utf8::isWordCodepoint(points[index - 1])) --index;
    while (index > 0 && utf8::isWordCodepoint(points[index - 1])) --index;
    moveTo(index, extend);
}

void LineEditor::moveWordRight(bool extend) {
    sync();
    const auto points = codepoints();
    std::size_t index = cursor_;
    while (index < points.size() && !utf8::isWordCodepoint(points[index])) ++index;
    while (index < points.size() && utf8::isWordCodepoint(points[index])) ++index;
    moveTo(index, extend);
}

void LineEditor::selectAll() {
    sync();
    anchor_ = 0;
    cursor_ = length();
}

void LineEditor::selectWordAt(std::size_t index) {
    sync();
    const auto points = codepoints();
    if (points.empty()) return;
    index = std::min(index, points.size() - 1);
    const bool word = utf8::isWordCodepoint(points[index]);
    std::size_t begin = index;
    std::size_t end = index;
    while (begin > 0 && utf8::isWordCodepoint(points[begin - 1]) == word) --begin;
    while (end < points.size() && utf8::isWordCodepoint(points[end]) == word) ++end;
    anchor_ = begin;
    cursor_ = end;
}

}
