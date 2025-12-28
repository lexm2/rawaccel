using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Driver;
using userspace_backend.IO.PathProviders;
using userspace_backend.Model;

namespace userspace_backend.Platform.Linux
{
    /// <summary>
    /// Extension methods for registering Linux platform services.
    /// </summary>
    public static class DriverServiceRegistration
    {
        /// <summary>
        /// Registers Linux platform implementations with file persistence and debug driver services.
        /// </summary>
        public static void AddDebugDriver(this IServiceCollection services)
        {
            // Register path provider for Linux
            services.AddSingleton<ISettingsPathProvider, LinuxSettingsPathProvider>();

            // Register backend loader - NOW PERSISTS to ~/.config/rawaccel instead of mock data
            services.AddSingleton<IBackEndLoader, BackEndLoader>();

            // Debug driver services (log data to console)
            services.AddSingleton<ISystemDevicesRetriever, DebugSystemDevicesRetriever>();
            services.AddSingleton<IDriverService, DebugDriverService>();
            services.AddSingleton<IAccelerationCalculatorFactory, DebugAccelerationCalculatorFactory>();
        }
    }
}
