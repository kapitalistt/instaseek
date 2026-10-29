#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace instaseek::testing {

using TestBody = void (*)();

struct TestCase {
    const char* name;
    TestBody body;
};

std::vector<TestCase>& registeredTests();

struct TestRegistrar {
    TestRegistrar(const char* name, TestBody body) { registeredTests().push_back({name, body}); }
};

void recordFailure(const char* file, int line, const std::string& description);

inline bool nearlyEqual(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

}

#define INSTASEEK_TEST(test_name)                                                                     \
    static void test_name();                                                                          \
    static const ::instaseek::testing::TestRegistrar test_name##_registrar(#test_name, &test_name);   \
    static void test_name()

#define CHECK(condition)                                                                              \
    do {                                                                                              \
        if (!(condition)) ::instaseek::testing::recordFailure(__FILE__, __LINE__, #condition);       \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                                       \
    do {                                                                                              \
        if (!::instaseek::testing::nearlyEqual((actual), (expected), (tolerance)))                    \
            ::instaseek::testing::recordFailure(__FILE__, __LINE__,                                   \
                                                #actual " ~= " #expected " (got " +                   \
                                                    std::to_string(actual) + ")");                    \
    } while (false)
