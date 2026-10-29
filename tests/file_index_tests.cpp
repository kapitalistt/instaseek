#include "test_harness.hpp"

#include "core/file_index.hpp"

using namespace instaseek::core;

INSTASEEK_TEST(full_paths_follow_the_parent_chain) {
    FileIndex index("C:");
    const auto users = index.append("Users", kNoParent, kRecordDirectory);
    const auto me = index.append("me", users, kRecordDirectory);
    const auto file = index.append("Отчёт.TXT", me, 0);
    CHECK(index.fullPath(file) == "C:\\Users\\me\\Отчёт.TXT");
    CHECK(index.parentPath(file) == "C:\\Users\\me");
    CHECK(index.parentPath(users) == "C:\\");
    CHECK(index.fullPath(users) == "C:\\Users");
    CHECK(index.foldedName(file) == "отчёт.txt");
    CHECK(index.name(file) == "Отчёт.TXT");
}

INSTASEEK_TEST(renames_and_moves_update_paths) {
    FileIndex index("D:");
    const auto a = index.append("a", kNoParent, kRecordDirectory);
    const auto b = index.append("b", a, kRecordDirectory);
    const auto file = index.append("old.log", b, 0);
    index.rename(file, "new.log");
    index.setParent(file, a);
    CHECK(index.fullPath(file) == "D:\\a\\new.log");
    index.rename(b, "renamed");
    CHECK(index.fullPath(b) == "D:\\a\\renamed");
}

INSTASEEK_TEST(deleted_records_are_counted) {
    FileIndex index("C:");
    const auto first = index.append("one", kNoParent, 0);
    index.append("two", kNoParent, 0);
    CHECK(index.liveCount() == 2);
    index.markDeleted(first);
    index.markDeleted(first);
    CHECK(index.liveCount() == 1);
    CHECK(index.record(first).isDeleted());
    index.setFlags(first, 0);
    CHECK(index.liveCount() == 2);
}

INSTASEEK_TEST(keys_map_to_records) {
    FileIndex index("C:");
    const auto dense = index.append("dense", kNoParent, 0);
    const auto sparse = index.append("sparse", kNoParent, 0);
    index.bindKey(5000, dense);
    index.bindKey(1ull << 40, sparse);
    CHECK(index.findKey(5000) == dense);
    CHECK(index.findKey(1ull << 40) == sparse);
    CHECK(!index.findKey(4999).has_value());
    index.unbindKey(5000);
    index.unbindKey(1ull << 40);
    CHECK(!index.findKey(5000).has_value());
    CHECK(!index.findKey(1ull << 40).has_value());
}

INSTASEEK_TEST(metadata_is_tracked_per_record) {
    FileIndex index("C:");
    const auto file = index.append("data.bin", kNoParent, 0);
    CHECK(!index.record(file).hasMetadata());
    index.setMetadata(file, 4096, 123456789);
    CHECK(index.record(file).hasMetadata());
    CHECK(index.record(file).size == 4096);
    CHECK(index.record(file).modified == 123456789);
}

INSTASEEK_TEST(parent_cycles_do_not_hang) {
    FileIndex index("C:");
    const auto a = index.append("a", kNoParent, kRecordDirectory);
    const auto b = index.append("b", a, kRecordDirectory);
    index.setParent(a, b);
    const std::string path = index.fullPath(a);
    CHECK(!path.empty());
    CHECK(path.size() < 20000);
    index.setParent(a, a);
    CHECK(index.fullPath(a) == "C:\\a");
}
