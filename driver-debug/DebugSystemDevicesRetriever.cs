using System.Collections.Generic;
using userspace_backend.Model;

namespace userspace_backend.Driver.Debug
{
    /// <summary>
    /// Debug system devices retriever for non-Windows platforms.
    /// Logs when device enumeration is requested.
    /// Returns an empty list since device enumeration requires Windows APIs.
    /// </summary>
    public class DebugSystemDevicesRetriever : ISystemDevicesRetriever
    {
        public IList<ISystemDevice> GetSystemDevices()
        {
            DebugLogger.LogHeader("GET SYSTEM DEVICES");
            DebugLogger.Log("Device enumeration requested - returning empty list (no platform-specific implementation)");
            return new List<ISystemDevice>();
        }
    }
}
