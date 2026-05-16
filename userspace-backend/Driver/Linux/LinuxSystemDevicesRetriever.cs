using System.Collections.Generic;
using System.IO;
using userspace_backend.Model;

namespace userspace_backend.Driver.Linux
{
    // Linux device enumeration via /proc/bus/input/devices, the same source
    // X11/libinput query. Filters to entries whose handler list contains a
    // "mouseN" token, which the kernel's mousedev driver emits for anything
    // it classifies as a mouse. HWIDs are formatted to match the Windows
    // shape (HID\VID_XXXX&PID_XXXX, uppercase hex) so device JSON written on
    // either OS keys against the same string.
    public sealed class LinuxSystemDevicesRetriever : ISystemDevicesRetriever
    {
        private const string ProcInputDevices = "/proc/bus/input/devices";

        public IList<ISystemDevice> GetSystemDevices()
        {
            var result = new List<ISystemDevice>();
            if (!File.Exists(ProcInputDevices)) return result;

            string content;
            try
            {
                content = File.ReadAllText(ProcInputDevices);
            }
            catch (IOException)
            {
                return result;
            }
            catch (System.UnauthorizedAccessException)
            {
                return result;
            }

            foreach (var device in ProcInputParser.Parse(content))
            {
                if (!device.IsMouse) continue;
                result.Add(new LinuxSystemDevice(device.Name, device.Hwid));
            }
            return result;
        }
    }

    public sealed class LinuxSystemDevice : ISystemDevice
    {
        public LinuxSystemDevice(string name, string hwid)
        {
            Name = name;
            HWID = hwid;
        }

        public string Name { get; }
        public string HWID { get; }
    }
}
