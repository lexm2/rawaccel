// Portability check.
//
// Includes only common/ math headers, never rawaccel-io.hpp /
// rawaccel-io-def.h (Windows-only: DWORD, GetLastError, <Windows.h>) which
// the Linux agent must not pull in. Must compile under clang/gcc.

#include "rawaccel.hpp"
#include "rawaccel-version.h"

int main()
{
    rawaccel::modifier_settings settings{};
    rawaccel::init_data(settings);
    rawaccel::modifier mod{settings};

    rawaccel::speed_processor sp{};
    sp.init(settings.prof.speed_processor_args);

    vec2d in{1.0, 1.0};
    mod.modify(in, sp, settings, 1.0, 1.0);

    return rawaccel::version.major == RA_VER_MAJOR ? 0 : 1;
}
