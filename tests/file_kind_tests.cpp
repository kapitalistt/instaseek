#include "test_harness.hpp"

#include "core/file_kind.hpp"

using namespace instaseek::core;

INSTASEEK_TEST(extension_is_text_after_last_dot) {
    CHECK(extensionOf("archive.tar.gz") == "gz");
    CHECK(extensionOf(".gitignore").empty());
    CHECK(extensionOf("Makefile").empty());
    CHECK(extensionOf("trailing.").empty());
}

INSTASEEK_TEST(file_names_are_classified_case_insensitively) {
    CHECK(classifyFileName("Report.PDF", false) == FileKind::Document);
    CHECK(classifyFileName("main.cpp", false) == FileKind::Code);
    CHECK(classifyFileName("photo.JPEG", false) == FileKind::Image);
    CHECK(classifyFileName("backup.7z", false) == FileKind::Archive);
    CHECK(classifyFileName("song.flac", false) == FileKind::Media);
    CHECK(classifyFileName("setup.exe", false) == FileKind::Binary);
    CHECK(classifyFileName("notes.unknownext", false) == FileKind::Other);
    CHECK(classifyFileName("src.cpp", true) == FileKind::Folder);
    CHECK(classifyFileName("a.c", false) == FileKind::Code);
    CHECK(classifyFileName("a.cc", false) == FileKind::Code);
}
