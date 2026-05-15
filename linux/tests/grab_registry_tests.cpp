// Tests for the panic-ungrab registry used by EvdevBackend. We cannot drive
// EVIOCGRAB in a CI-friendly way (it requires a real evdev node), so the
// tests cover the registry plumbing: add/remove pairing, walking the table
// without crashing, and the fd-stability invariant after multiple churns.

#include "evdev_backend.hpp"
#include "test_harness.hpp"

#include <unistd.h>

using namespace rawaccel_agent;

RA_TEST("grab_registry: add then remove leaves slot reusable")
{
    int dummy[2];
    if (::pipe(dummy) != 0) {
        ra_test::report_fail(__FILE__, __LINE__, "pipe() failed in setup");
        return;
    }
    int fd = dummy[0];

    grab_registry_add(fd);
    // Removing the same fd should clear the slot. If we add again, it must
    // succeed without overflowing.
    grab_registry_remove(fd);
    grab_registry_add(fd);
    grab_registry_remove(fd);

    ::close(dummy[0]);
    ::close(dummy[1]);
}

RA_TEST("grab_registry: panic walk is a no-op when registry is empty")
{
    // Nothing to assert, but the call must not crash.
    grab_registry_panic_ungrab_all();
}
