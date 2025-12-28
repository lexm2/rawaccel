using System;
using System.IO;

namespace userspace_backend.IO.PathProviders
{
    /// <summary>
    /// Windows implementation of settings path provider.
    /// Supports either executable directory (backwards compatible) or AppData/Roaming.
    /// </summary>
    public class WindowsSettingsPathProvider : ISettingsPathProvider
    {
        private const string ApplicationName = "rawaccel";
        private readonly string _settingsRoot;
        private readonly bool _useAppData;

        /// <summary>
        /// Creates a new Windows settings path provider.
        /// </summary>
        /// <param name="useAppData">
        /// If true, uses AppData/Roaming/rawaccel.
        /// If false, uses the executable directory (backwards compatible default).
        /// </param>
        public WindowsSettingsPathProvider(bool useAppData = false)
        {
            _useAppData = useAppData;

            if (_useAppData)
            {
                // Use AppData/Roaming for better Windows integration
                var appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
                _settingsRoot = System.IO.Path.Combine(appData, ApplicationName);
            }
            else
            {
                // Current behavior: use executable directory (backwards compatible)
                _settingsRoot = AppDomain.CurrentDomain.BaseDirectory;
            }

            // Ensure directory exists
            Directory.CreateDirectory(_settingsRoot);
            Console.WriteLine($"Windows settings directory: {_settingsRoot}");
        }

        public string GetSettingsRootDirectory() => _settingsRoot;

        public string GetDevicesFilePath() => System.IO.Path.Combine(_settingsRoot, "devices.json");

        public string GetMappingsFilePath() => System.IO.Path.Combine(_settingsRoot, "mappings.json");

        public string GetSettingsFilePath() => System.IO.Path.Combine(_settingsRoot, "settings.json");

        public string GetProfilesDirectoryPath() => System.IO.Path.Combine(_settingsRoot, "profiles");

        public string GetProfileFilePath(string profileName) =>
            System.IO.Path.Combine(GetProfilesDirectoryPath(), $"{profileName}.json");
    }
}
