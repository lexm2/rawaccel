using System.Collections.Generic;
using userspace_backend.Model;

namespace userspace_backend.Driver.Stub
{
    /// <summary>
    /// Stub driver service for non-Windows platforms.
    /// All operations are no-ops since there is no driver on Linux.
    /// </summary>
    public class StubDriverService : IDriverService
    {
        public bool IsAvailable => false;

        public void Activate(MappingModel mapping, IEnumerable<IDeviceModel> devices)
        {
            // No-op on non-Windows platforms
        }

        public void Deactivate()
        {
            // No-op on non-Windows platforms
        }
    }
}
