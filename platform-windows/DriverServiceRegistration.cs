using Microsoft.Extensions.DependencyInjection;
using userspace_backend.Driver;
using userspace_backend.IO.PathProviders;
using userspace_backend.Model;

namespace userspace_backend.Platform.Windows
{
    /// <summary>
    /// Extension methods for registering Windows platform services.
    /// </summary>
    public static class DriverServiceRegistration
    {
        /// <summary>
        /// Registers Windows platform implementations.
        /// </summary>
        public static void AddWindowsDriver(this IServiceCollection services)
        {
            // Register path provider (useAppData: false for backwards compatibility)
            services.AddSingleton<ISettingsPathProvider>(sp =>
                new WindowsSettingsPathProvider(useAppData: false));

            // Register backend loader
            services.AddSingleton<IBackEndLoader, BackEndLoader>();

            // Windows driver services
            services.AddSingleton<ISystemDevicesRetriever, WindowsSystemDevicesRetriever>();
            services.AddSingleton<IDriverService, WindowsDriverService>();
            services.AddSingleton<IAccelerationCalculatorFactory, WindowsAccelerationCalculatorFactory>();
        }
    }
}
