using System;
using System.Linq;
using userspace_backend.Model;

namespace userspace_backend.Services
{
    /// <summary>
    /// Service for automatically populating devices from system device list.
    /// Handles device auto-population on startup and provides reusable device addition logic.
    /// </summary>
    public interface IDeviceDetectionService
    {
        /// <summary>
        /// Auto-populate all system devices into the configuration.
        /// Replaces default device if it exists, otherwise adds all as new devices.
        /// </summary>
        void AutoPopulateAllSystemDevices(DevicesModel devicesModel);
    }

    public class DeviceDetectionService : IDeviceDetectionService
    {
        private readonly ISystemDevicesRetriever systemDevicesRetriever;

        public DeviceDetectionService(ISystemDevicesRetriever systemDevicesRetriever)
        {
            this.systemDevicesRetriever = systemDevicesRetriever;
        }

        public void AutoPopulateAllSystemDevices(DevicesModel devicesModel)
        {
            // 1. Refresh system devices
            devicesModel.SystemDevices.RefreshSystemDevices();

            // 2. Get all system devices
            var systemDevices = devicesModel.SystemDevices.SystemDevices.ToList();

            if (systemDevices.Count == 0)
            {
                Console.WriteLine("No system devices found for auto-population");
                return;
            }

            Console.WriteLine($"Auto-populating {systemDevices.Count} system device(s)");

            // 3. Check if we should replace default device
            bool hasDefaultDevice = devicesModel.Elements.Count == 1 &&
                devicesModel.Elements[0].HardwareID.ModelValue == "DEFAULT_DEVICE_ID";

            // 4. Add devices
            if (hasDefaultDevice)
            {
                // Replace default device with first system device
                var defaultDevice = devicesModel.Elements[0];
                var firstDevice = systemDevices[0];

                defaultDevice.Name.InterfaceValue = firstDevice.Name;
                defaultDevice.HardwareID.InterfaceValue = firstDevice.HWID;
                defaultDevice.Name.TryUpdateFromInterface();
                defaultDevice.HardwareID.TryUpdateFromInterface();

                Console.WriteLine($"  Replaced default device with: {firstDevice.Name}");

                // Add remaining devices
                for (int i = 1; i < systemDevices.Count; i++)
                {
                    AddSystemDevice(devicesModel, systemDevices[i]);
                }
            }
            else
            {
                // Add all devices
                foreach (var systemDevice in systemDevices)
                {
                    AddSystemDevice(devicesModel, systemDevice);
                }
            }
        }

        private void AddSystemDevice(DevicesModel devicesModel, ISystemDevice systemDevice)
        {
            // Check if device already exists
            bool exists = devicesModel.Elements.Any(d =>
                d.HardwareID.ModelValue == systemDevice.HWID);

            if (exists)
            {
                Console.WriteLine($"  Skipping duplicate: {systemDevice.Name}");
                return;
            }

            // Add new device
            if (devicesModel.TryAddNewDefault())
            {
                var newDevice = devicesModel.Elements.Last();
                newDevice.Name.InterfaceValue = systemDevice.Name;
                newDevice.HardwareID.InterfaceValue = systemDevice.HWID;
                newDevice.Name.TryUpdateFromInterface();
                newDevice.HardwareID.TryUpdateFromInterface();

                Console.WriteLine($"  Added: {systemDevice.Name}");
            }
        }
    }
}
