using System.Collections.Generic;
using System.IO;
using userspace_backend.Model;

namespace userspace_backend.Driver.Linux
{
    // Device enumeration via /proc/bus/input/devices. Keeps entries whose
    // handler list has a "mouseN" token (emitted by the kernel mousedev
    // driver). HWIDs use the Windows shape (HID\VID_XXXX&PID_XXXX, uppercase
    // hex) so device JSON keys identically on either OS.
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
