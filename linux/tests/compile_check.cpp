// Portability check.
//
// Includes only the math headers from common/. Does NOT include
// rawaccel-io.hpp or rawaccel-io-def.h: those are intentionally
// Windows-only (DWORD, GetLastError, <Windows.h>) and the Linux agent
// must never pull them in. This file compiles under clang/gcc and
// exercises a modifier end-to-end against default settings.

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
