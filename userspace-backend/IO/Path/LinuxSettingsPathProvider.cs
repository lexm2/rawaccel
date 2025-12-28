using System;
using System.IO;

namespace userspace_backend.IO.PathProviders
{
    /// <summary>
    /// Linux implementation of settings path provider.
    /// Uses XDG Base Directory specification: $XDG_CONFIG_HOME/rawaccel or ~/.config/rawaccel
    /// </summary>
    public class LinuxSettingsPathProvider : ISettingsPathProvider
    {
        private const string ApplicationName = "rawaccel";
        private readonly string _settingsRoot;

        public LinuxSettingsPathProvider()
        {
            // Check XDG_CONFIG_HOME first, fallback to ~/.config
            var xdgConfigHome = Environment.GetEnvironmentVariable("XDG_CONFIG_HOME");
            var configBase = !string.IsNullOrEmpty(xdgConfigHome)
                ? xdgConfigHome
                : System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config");

            _settingsRoot = System.IO.Path.Combine(configBase, ApplicationName);

            // Ensure directory exists
            Directory.CreateDirectory(_settingsRoot);
            Console.WriteLine($"Linux settings directory: {_settingsRoot}");
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
