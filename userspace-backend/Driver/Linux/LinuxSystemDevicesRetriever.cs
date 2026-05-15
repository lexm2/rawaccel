using System.Collections.Generic;
using userspace_backend.Model;

namespace userspace_backend.Driver.Linux
{
    // Linux device enumeration stub. The agent already enumerates HID/evdev
    // mice in-process; future work can either expose that list over the
    // control socket or have this class read /dev/input directly via libudev.
    // For now the UI shows no auto-detected devices on Linux: the user
    // creates entries manually using the device's HWID (visible in
    // `cat /proc/bus/input/devices` or via the agent log).
    public sealed class LinuxSystemDevicesRetriever : ISystemDevicesRetriever
    {
        public IList<ISystemDevice> GetSystemDevices() => new List<ISystemDevice>();
    }
}
