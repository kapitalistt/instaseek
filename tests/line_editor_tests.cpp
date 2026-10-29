#include "test_harness.hpp"

#include "core/line_editor.hpp"

using namespace instaseek::core;

INSTASEEK_TEST(typing_inserts_at_cursor) {
    std::string text;
    LineEditor editor(&text);
    CHECK(editor.insert("отчёт"));
    CHECK(editor.insert(" "));
    CHECK(editor.insert("2024"));
    CHECK(text == "отчёт 2024");
    editor.moveTo(5, false);
    editor.insert("ы");
    CHECK(text == "отчёты 2024");
    CHECK(editor.cursor() == 6);
}

INSTASEEK_TEST(selection_with_shift_and_word_jumps) {
    std::string text = "find this word";
    LineEditor editor(&text);
    editor.home(false);
    editor.moveWordRight(true);
    CHECK(editor.selectedText() == "find");
    editor.moveWordRight(true);
    CHECK(editor.selectedText() == "find this");
    editor.moveLeft(false);
    CHECK(!editor.hasSelection());
    CHECK(editor.cursor() == 0);
    editor.end(false);
    editor.moveWordLeft(true);
    CHECK(editor.selectedText() == "word");
}

INSTASEEK_TEST(typing_replaces_the_selection) {
    std::string text = "C:\\old\\path.txt";
    LineEditor editor(&text);
    editor.selectAll();
    CHECK(editor.selectedText() == text);
    editor.insert("D:\\new.log");
    CHECK(text == "D:\\new.log");
    CHECK(!editor.hasSelection());
}

INSTASEEK_TEST(paste_flattens_multiline_text) {
    std::string text;
    LineEditor editor(&text);
    editor.insert("line one\r\nline two\tend");
    CHECK(text == "line one line two end");
    CHECK(LineEditor::sanitize(std::string("a\x01" "b\x7F" "c")) == "abc");
}

INSTASEEK_TEST(deleting_respects_codepoints) {
    std::string text = "ёжик";
    LineEditor editor(&text);
    CHECK(editor.backspace());
    CHECK(text == "ёжи");
    editor.home(false);
    CHECK(editor.deleteForward());
    CHECK(text == "жи");
    CHECK(!editor.backspace());
    editor.selectAll();
    CHECK(editor.backspace());
    CHECK(text.empty());
}

INSTASEEK_TEST(external_changes_move_the_cursor_to_the_end) {
    std::string text = "abc";
    LineEditor editor(&text);
    editor.home(false);
    text = "D:\\logs";
    editor.sync();
    CHECK(editor.cursor() == 7);
    CHECK(!editor.hasSelection());
}

INSTASEEK_TEST(double_click_selects_a_word) {
    std::string text = "error: disk full";
    LineEditor editor(&text);
    editor.selectWordAt(8);
    CHECK(editor.selectedText() == "disk");
    editor.selectWordAt(0);
    CHECK(editor.selectedText() == "error");
}
