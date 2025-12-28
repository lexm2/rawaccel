using System.Collections.Generic;
using System.Linq;
using userspace_backend.Model;

namespace userspace_backend.Platform.Windows
{
    /// <summary>
    /// Windows implementation that retrieves devices from the system using the native wrapper.
    /// </summary>
    public class WindowsSystemDevicesRetriever : ISystemDevicesRetriever
    {
        public IList<ISystemDevice> GetSystemDevices()
        {
            IList<MultiHandleDevice> rawDevices = MultiHandleDevice.GetList();
            return rawDevices.Select(d => new WindowsSystemDevice(d) as ISystemDevice).ToList();
        }
    }

    /// <summary>
    /// Wrapper for MultiHandleDevice from the native wrapper.
    /// </summary>
    internal class WindowsSystemDevice : ISystemDevice
    {
        private readonly MultiHandleDevice _rawDevice;

        public WindowsSystemDevice(MultiHandleDevice rawDevice)
        {
            _rawDevice = rawDevice;
        }

        public string Name => _rawDevice.name;

        public string HWID => _rawDevice.id;
    }
}
