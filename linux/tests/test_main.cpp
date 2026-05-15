#include "test_harness.hpp"

#include <cstdio>

int main()
{
    int passed = 0;
    int failed = 0;

    for (auto& tc : ra_test::registry()) {
        ra_test::current_name() = tc.name;
        const int before = ra_test::current_fails();
        tc.fn();
        const bool ok = ra_test::current_fails() == before;
        if (ok) {
            ++passed;
            std::printf("  PASS  %s\n", tc.name);
        } else {
            ++failed;
        }
    }

    const int total = static_cast<int>(ra_test::registry().size());
    std::printf("\n%d/%d tests passed (%d failed)\n", passed, total, failed);
    return failed == 0 ? 0 : 1;
}
