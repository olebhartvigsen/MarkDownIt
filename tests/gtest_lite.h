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

// Known-failing tests. A test that documents an unimplemented budget should
// report loudly on every run without blocking unrelated work, so failures
// inside it are counted as expected. A TEST_XFAIL whose assertions all pass
// is itself a failure, so a closed gap cannot sit behind a stale marker.
inline int& xfail_checks() { static int n = 0; return n; }
inline int& xfail_unexpected_passes() { static int n = 0; return n; }
inline bool& in_xfail() { static bool b = false; return b; }

inline void NoteXfailFailure() {
    if (in_xfail()) { xfail_checks()++; return; }
    failures()++;
}
inline void NoteXfailUnexpectedPass() {
    if (in_xfail()) { xfail_unexpected_passes()++; return; }
}

struct Test {
    const char* name;
    void (*fn)();
    bool xfail;
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void(*fn)(), bool xfail = false) {
        registry().push_back({name, fn, xfail});
    }
};

}  // namespace testlite

// A test documenting a known gap. Failures inside are reported but do not fail
// the run; if all of its assertions pass, the runner fails instead, so a fixed
// bug cannot hide behind the marker.
#define TEST_XFAIL(suite, name) static void run_##suite##_##name(); static testlite::Registrar reg_##suite##_##name(#suite "." #name, run_##suite##_##name, true); static void run_##suite##_##name()

// Run the following assertion as expected-to-fail within a TEST_XFAIL body.
#define EXPECT_GAP_TRUE(a) do { testlite::in_xfail() = true; EXPECT_TRUE(a); testlite::in_xfail() = false; } while(0)
#define EXPECT_GAP_EQ(a, b) do { testlite::in_xfail() = true; EXPECT_EQ(a, b); testlite::in_xfail() = false; } while(0)

#define TEST(suite, name) static void run_##suite##_##name(); static testlite::Registrar reg_##suite##_##name(#suite "." #name, run_##suite##_##name); static void run_##suite##_##name()

#define EXPECT_EQ(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va == _vb)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_EQ(" #a ", " #b ")\n"; } } while(0)
#define EXPECT_NE(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va != _vb)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_NE(" #a ", " #b ")\n"; } } while(0)

#define EXPECT_TRUE(a) do { testlite::checks()++; if (!(a)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_TRUE(" #a ")\n"; } } while(0)

#define EXPECT_FALSE(a) do { testlite::checks()++; if (a) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_FALSE(" #a ")\n"; } } while(0)

#define ASSERT_TRUE(a) do { testlite::checks()++; if (!(a)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " ASSERT_TRUE(" #a ")\n"; return; } } while(0)

#define ASSERT_EQ(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va == _vb)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " ASSERT_EQ(" #a ", " #b ")\n"; return; } } while(0)

#define ASSERT_GE(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va >= _vb)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " ASSERT_GE(" #a ", " #b ")\n"; return; } } while(0)

#define EXPECT_LT(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va < _vb)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_LT(" #a ", " #b ")\n"; } } while(0)

#define EXPECT_LE(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va <= _vb)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_LE(" #a ", " #b ")\n"; } } while(0)

#define EXPECT_GT(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va > _vb)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_GT(" #a ", " #b ")\n"; } } while(0)

#define EXPECT_GE(a, b) do { testlite::checks()++; auto _va = (a); auto _vb = (b); if (!(_va >= _vb)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_GE(" #a ", " #b ")\n"; } } while(0)

#define EXPECT_NEAR(a, b, tol) do { testlite::checks()++; double _va = static_cast<double>(a); double _vb = static_cast<double>(b); double _vt = static_cast<double>(tol); double _diff = _va - _vb; if (_diff < 0) _diff = -_diff; if (!(_diff <= _vt)) { testlite::NoteXfailFailure(); std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << " EXPECT_NEAR(" #a ", " #b ", " #tol ") diff=" << _diff << "\n"; } } while(0)

#define RUN_ALL_TESTS() int main() { \
    int passed = 0; int xfailed = 0; \
    std::vector<std::string> unexpected; \
    for (auto& t : testlite::registry()) { \
        size_t before = testlite::failures(); \
        int xb = testlite::xfail_checks(); \
        int up = testlite::xfail_unexpected_passes(); \
        std::cout << "[ RUN ] " << t.name << "\n"; \
        t.fn(); \
        if (t.xfail) { \
            if (testlite::failures() > before || testlite::xfail_checks() > xb) { \
                std::cout << "[ XFAIL ] " << t.name << " (known gap, see comment)\n"; \
                xfailed++; \
            } else { \
                std::cout << "[ FAIL ] " << t.name << " (marked XFAIL but passed)\n"; \
                unexpected.push_back(t.name); \
            } \
        } else if (testlite::failures() == before) { \
            std::cout << "[ OK ] " << t.name << "\n"; passed++; \
        } else { \
            std::cout << "[ FAIL ] " << t.name << "\n"; \
        } \
    } \
    std::cout << "\n" << testlite::checks() << " checks, " \
              << testlite::failures() << " failures, " << passed \
              << " passed, " << xfailed << " known gaps\n"; \
    for (const auto& n : unexpected) { \
        std::cerr << "  UNEXPECTED PASS: " << n << " (remove its XFAIL marker)\n"; \
    } \
    return (testlite::failures() || !unexpected.empty()) ? 1 : 0; \
}
