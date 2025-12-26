using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Driver;
using userspace_backend.Model;

namespace userspace_backend.Driver.Debug
{
    /// <summary>
    /// Extension methods for registering debug driver services.
    /// </summary>
    public static class DriverServiceRegistration
    {
        /// <summary>
        /// Registers debug driver implementations that log all data to console/debug output.
        /// </summary>
        public static void AddDebugDriver(this IServiceCollection services)
        {
            services.AddSingleton<ISystemDevicesRetriever, DebugSystemDevicesRetriever>();
            services.AddSingleton<IDriverService, DebugDriverService>();
            services.AddSingleton<IAccelerationCalculatorFactory, DebugAccelerationCalculatorFactory>();
        }
    }
}
