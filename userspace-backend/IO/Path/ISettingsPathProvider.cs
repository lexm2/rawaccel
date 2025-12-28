using System;

namespace userspace_backend.IO.PathProviders
{
    /// <summary>
    /// Provides platform-specific paths for application settings storage.
    /// </summary>
    public interface ISettingsPathProvider
    {
        /// <summary>
        /// Gets the root directory where all application settings are stored.
        /// Linux: ~/.config/rawaccel or $XDG_CONFIG_HOME/rawaccel
        /// Windows: AppData/Roaming/rawaccel or executable directory (current behavior)
        /// </summary>
        string GetSettingsRootDirectory();

        /// <summary>
        /// Gets the full path for the devices configuration file.
        /// </summary>
        string GetDevicesFilePath();

        /// <summary>
        /// Gets the full path for the mappings configuration file.
        /// </summary>
        string GetMappingsFilePath();

        /// <summary>
        /// Gets the full path for the application settings file.
        /// </summary>
        string GetSettingsFilePath();

        /// <summary>
        /// Gets the directory path where profile files are stored.
        /// </summary>
        string GetProfilesDirectoryPath();

        /// <summary>
        /// Gets the full path for a specific profile file.
        /// </summary>
        /// <param name="profileName">The name of the profile.</param>
        /// <returns>Full path to the profile JSON file.</returns>
        string GetProfileFilePath(string profileName);
    }
}
