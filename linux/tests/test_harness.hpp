#pragma once

// Minimal header-only test harness for the Linux parity tests.
// Kept purposefully small (no vendored doctest/Catch2): registration
// via static-ctor side effect, RA_TEST macro defines + registers a fn.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ra_test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> r;
    return r;
}

inline const char*& current_name()
{
    static const char* s = "";
    return s;
}

inline int& current_fails()
{
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};

inline void report_fail(const char* file, int line, const char* msg)
{
    std::fprintf(stderr, "  FAIL [%s] %s:%d  %s\n", current_name(), file, line, msg);
    ++current_fails();
}

inline bool approx_eq(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

} // namespace ra_test

#define RA_CONCAT2(a, b) a##b
#define RA_CONCAT(a, b) RA_CONCAT2(a, b)

#define RA_TEST(name)                                                              \
    static void RA_CONCAT(ra_test_fn_, __LINE__)();                                \
    static ra_test::Registrar RA_CONCAT(ra_test_reg_, __LINE__)                    \
        {name, &RA_CONCAT(ra_test_fn_, __LINE__)};                                 \
    static void RA_CONCAT(ra_test_fn_, __LINE__)()

#define RA_CHECK(cond)                                                             \
    do {                                                                           \
        if (!(cond)) ra_test::report_fail(__FILE__, __LINE__, #cond);              \
    } while (0)

#define RA_CHECK_EQ(a, b)                                                          \
    do {                                                                           \
        auto _ra_a = (a);                                                          \
        auto _ra_b = (b);                                                          \
        if (!(_ra_a == _ra_b)) {                                                   \
            char _buf[256];                                                        \
            std::snprintf(_buf, sizeof(_buf),                                      \
                          "%s == %s  (lhs=%.17g rhs=%.17g)",                       \
                          #a, #b, (double)_ra_a, (double)_ra_b);                   \
            ra_test::report_fail(__FILE__, __LINE__, _buf);                        \
        }                                                                          \
    } while (0)

#define RA_CHECK_NEAR(a, b, tol)                                                   \
    do {                                                                           \
        double _ra_a = (a), _ra_b = (b), _ra_t = (tol);                            \
        if (!ra_test::approx_eq(_ra_a, _ra_b, _ra_t)) {                            \
            char _buf[256];                                                        \
            std::snprintf(_buf, sizeof(_buf),                                      \
                          "%s ~= %s  (got %.17g vs %.17g, tol %.17g)",             \
                          #a, #b, _ra_a, _ra_b, _ra_t);                            \
            ra_test::report_fail(__FILE__, __LINE__, _buf);                        \
        }                                                                          \
    } while (0)
