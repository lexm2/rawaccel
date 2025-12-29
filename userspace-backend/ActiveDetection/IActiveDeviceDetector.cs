using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using userspace_backend.Model;

namespace userspace_backend.ActiveDetection
{
    /// <summary>
    /// Interface for detecting which device is currently receiving input.
    /// Platform-specific implementations monitor input events to determine active device.
    /// </summary>
    public interface IActiveDeviceDetector
    {
        /// <summary>
        /// Detects which device is currently receiving input by monitoring for activity.
        /// </summary>
        /// <param name="devices">List of devices to monitor</param>
        /// <param name="timeout">Maximum time to wait for input detection</param>
        /// <returns>The device that generated input first, or null if timeout occurs</returns>
        Task<ISystemDevice?> DetectActiveDeviceAsync(
            IEnumerable<ISystemDevice> devices,
            TimeSpan timeout);
    }
}
