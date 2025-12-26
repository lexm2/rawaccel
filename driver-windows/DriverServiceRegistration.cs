using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Driver;
using userspace_backend.Model;

namespace userspace_backend.Driver.Windows
{
    /// <summary>
    /// Extension methods for registering Windows driver services.
    /// </summary>
    public static class DriverServiceRegistration
    {
        /// <summary>
        /// Registers Windows driver implementations.
        /// </summary>
        public static void AddWindowsDriver(this IServiceCollection services)
        {
            services.AddSingleton<ISystemDevicesRetriever, WindowsSystemDevicesRetriever>();
            services.AddSingleton<IDriverService, WindowsDriverService>();
            services.AddSingleton<IAccelerationCalculatorFactory, WindowsAccelerationCalculatorFactory>();
        }
    }
}
