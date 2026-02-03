using Microsoft.Extensions.DependencyInjection;
using userspace_backend.ActiveDetection;
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

            // Device detection services
            services.AddSingleton<ISystemDevicesRetriever, LinuxSystemDevicesRetriever>();
            services.AddSingleton<IActiveDeviceDetector, LinuxActiveDeviceDetector>();

            // Driver services
            services.AddSingleton<IDriverService, DebugDriverService>();
            services.AddSingleton<IAccelerationCalculatorFactory, DebugAccelerationCalculatorFactory>();
        }

        /// <summary>
        /// Registers Linux platform implementations with userspace driver (libevdev + uinput).
        /// </summary>
        public static void AddUserspaceDriver(this IServiceCollection services)
        {
            // Register path provider for Linux
            services.AddSingleton<ISettingsPathProvider, LinuxSettingsPathProvider>();

            // Register backend loader
            services.AddSingleton<IBackEndLoader, BackEndLoader>();

            // Device detection services
            services.AddSingleton<ISystemDevicesRetriever, LinuxSystemDevicesRetriever>();
            services.AddSingleton<IActiveDeviceDetector, LinuxActiveDeviceDetector>();

            // Driver services - Use userspace driver instead of debug
            services.AddSingleton<IDriverService, LinuxUserspaceDriverService>();
            services.AddSingleton<IAccelerationCalculatorFactory, DebugAccelerationCalculatorFactory>();
        }
    }
}
