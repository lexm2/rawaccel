using System.Collections.Generic;
using userspace_backend.Model;

namespace userspace_backend.Driver.Stub
{
    /// <summary>
    /// Stub system devices retriever for non-Windows platforms.
    /// Returns an empty list since device enumeration requires Windows APIs.
    /// </summary>
    public class StubSystemDevicesRetriever : ISystemDevicesRetriever
    {
        public IList<ISystemDevice> GetSystemDevices()
        {
            return new List<ISystemDevice>();
        }
    }
}
