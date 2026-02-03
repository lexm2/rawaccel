using System.Collections.Generic;
using userspace_backend.Model;

namespace userspace_backend.Driver
{
    /// <summary>
    /// Abstraction for driver communication.
    /// On Windows, communicates with the kernel driver via the native wrapper.
    /// On Linux, provides a no-op stub.
    /// </summary>
    public interface IDriverService
    {
        /// <summary>
        /// Gets whether the driver is available on this platform.
        /// </summary>
        bool IsAvailable { get; }

        /// <summary>
        /// Activates the driver with the specified mapping configuration.
        /// </summary>
        /// <param name="mapping">The mapping to activate</param>
        /// <param name="devices">All device models</param>
        void Activate(MappingModel mapping, IEnumerable<IDeviceModel> devices);

        /// <summary>
        /// Deactivates the driver, resetting to default behavior.
        /// </summary>
        void Deactivate();

        /// <summary>
        /// Gets the current mouse speed in counts/ms from the driver.
        /// Returns 0 if no data available or driver not active.
        /// </summary>
        double GetCurrentMouseSpeed();
    }
}
