#include "test_harness.hpp"

#include <cstdio>

namespace instaseek::testing {

namespace {

int& failureCount() {
    static int count = 0;
    return count;
}

}

std::vector<TestCase>& registeredTests() {
    static std::vector<TestCase> tests;
    return tests;
}

void recordFailure(const char* file, int line, const std::string& description) {
    ++failureCount();
    std::fprintf(stderr, "    FAILED %s(%d): %s\n", file, line, description.c_str());
}

}

int main() {
    using namespace instaseek::testing;
    int failed_tests = 0;
    for (const auto& test : registeredTests()) {
        const int failures_before = failureCount();
        test.body();
        const bool passed = failureCount() == failures_before;
        if (!passed) ++failed_tests;
        std::printf("[%s] %s\n", passed ? " OK " : "FAIL", test.name);
    }
    std::printf("\n%zu tests, %d failed\n", registeredTests().size(), failed_tests);
    return failed_tests == 0 ? 0 : 1;
}
