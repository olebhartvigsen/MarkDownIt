#pragma once

// Minimal test framework: no Google Test dependency.
// Provides TEST(), EXPECT_EQ(), EXPECT_TRUE(), EXPECT_FALSE(), ASSERT_TRUE(),
// and RUN_ALL_TESTS() which defines main().

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

namespace testlite {

inline int& checks() { static int n = 0; return n; }
inline int& failures() { static int n = 0; return n; }

struct Test {
    const char* name;
    void (*fn)();
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void(*fn)()) {
        registry().push_back({name, fn});
    }
};

}  // namespace testlite

#define TEST(suite, name) static void run_##suite##_##name(); static testlite::Registrar reg_##suite##_##name(#suite "." #name, run_##suite##_##name); static void run_##suite##_##name()

#define EXPECT_EQ(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va == _vb)) { testlite::failures()++; std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_EQ(" #a ", " #b ")\n"; } } while(0)

#define EXPECT_TRUE(a) do { testlite::checks()++; if (!(a)) { testlite::failures()++; std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_TRUE(" #a ")\n"; } } while(0)

#define EXPECT_FALSE(a) do { testlite::checks()++; if (a) { testlite::failures()++; std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_FALSE(" #a ")\n"; } } while(0)

#define ASSERT_TRUE(a) do { testlite::checks()++; if (!(a)) { testlite::failures()++; std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " ASSERT_TRUE(" #a ")\n"; return; } } while(0)

#define RUN_ALL_TESTS() int main() { int passed = 0; for (auto& t : testlite::registry()) { size_t before = testlite::failures(); std::cout << "[ RUN ] " << t.name << "\n"; t.fn(); if (testlite::failures() == before) { std::cout << "[ OK ] " << t.name << "\n"; passed++; } else { std::cout << "[ FAIL ] " << t.name << "\n"; } } std::cout << "\n" << testlite::checks() << " checks, " << testlite::failures() << " failures, " << passed << " passed\n"; return testlite::failures() ? 1 : 0; }
