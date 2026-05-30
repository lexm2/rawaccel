// Dump a canonical driver_config (one default profile + one device) via the
// C++ json_io serializer. Used to generate the cross-OS JSON contract fixture
// the Rust daemon's config parser is tested against, and as a parity-harness
// reference. Output goes to stdout.

#include "json_io.hpp"

#include "rawaccel.hpp"

#include <cstdio>

int main()
{
    rajson::driver_config cfg{};

    // mirrors DriverConfig::FromProfile(new Profile()) on Windows
    rawaccel::modifier_settings mod{};
    rawaccel::init_data(mod);
    cfg.profiles.push_back(mod);

    // one device entry so devices[] / device_settings is exercised
    rawaccel::device_settings dev{};
    rajson::utf8_to_wchar("Test Mouse", dev.name, rawaccel::MAX_NAME_LEN);
    rajson::utf8_to_wchar("", dev.profile, rawaccel::MAX_NAME_LEN);
    rajson::utf8_to_wchar("0003:046D:C54D.000A", dev.id, rawaccel::MAX_DEV_ID_LEN);
    cfg.devices.push_back(dev);

    std::printf("%s\n", rajson::to_string(cfg).c_str());
    return 0;
}
