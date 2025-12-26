using System.Collections.Generic;
using System.Linq;
using userspace_backend.Driver.Types;
using userspace_backend.Model;

namespace userspace_backend.Driver
{
    /// <summary>
    /// Shared mapping logic for converting domain models to driver types.
    /// This class is platform-agnostic and produces shared driver types
    /// that can be consumed by platform-specific driver implementations.
    /// </summary>
    public static class DriverMapper
    {
        /// <summary>
        /// Maps a ProfileModel to a DriverProfile.
        /// </summary>
        public static DriverProfile MapProfile(ProfileModel model)
        {
            // Get gain value from FormulaAccel if available, otherwise default to false
            bool gain = model.Acceleration.FormulaAccel?.Gain.ModelValue ?? false;
            DriverAccelArgs accelArgs = model.Acceleration.MapToDriver(gain);

            return new DriverProfile
            {
                Name = model.Name.ModelValue,
                OutputDPI = model.OutputDPI.ModelValue,
                YXOutputDPIRatio = model.YXRatio.ModelValue,
                LROutputDPIRatio = model.Hidden.LeftRightRatio.ModelValue,
                UDOutputDPIRatio = model.Hidden.UpDownRatio.ModelValue,
                Rotation = model.Hidden.RotationDegrees.ModelValue,
                Snap = model.Hidden.AngleSnappingDegrees.ModelValue,
                MaximumSpeed = model.Hidden.SpeedCap.ModelValue,
                MinimumSpeed = 0,
                ArgsX = accelArgs,
                ArgsY = accelArgs,
                DomainXY = new Vec2<double>(
                    model.Acceleration.Anisotropy.DomainX.ModelValue,
                    model.Acceleration.Anisotropy.DomainY.ModelValue),
                RangeXY = new Vec2<double>(
                    model.Acceleration.Anisotropy.RangeX.ModelValue,
                    model.Acceleration.Anisotropy.RangeY.ModelValue),
                SpeedArgs = new DriverSpeedArgs
                {
                    CombineMagnitudes = model.Acceleration.Anisotropy.CombineXYComponents.ModelValue,
                    LPNorm = model.Acceleration.Anisotropy.LPNorm.ModelValue,
                    OutputSmoothHalflife = model.Hidden.OutputSmoothingHalfLife.ModelValue,
                    InputSmoothHalflife = model.Acceleration.Coalescion.InputSmoothingHalfLife.ModelValue,
                    ScaleSmoothHalflife = model.Acceleration.Coalescion.ScaleSmoothingHalfLife.ModelValue,
                }
            };
        }

        /// <summary>
        /// Maps a device model to driver device settings.
        /// </summary>
        public static DriverDeviceSettings MapDevice(IDeviceModel deviceModel, string profileName)
        {
            return new DriverDeviceSettings
            {
                Id = deviceModel.HardwareID.ModelValue,
                Name = deviceModel.Name.ModelValue,
                ProfileName = profileName,
                Config = new DriverDeviceConfig
                {
                    Disable = deviceModel.Ignore.ModelValue,
                    DPI = deviceModel.DPI.ModelValue,
                    PollingRate = deviceModel.PollRate.ModelValue,
                    PollTimeLock = false,
                    SetExtraInfo = false,
                    MaximumTime = 200,
                    MinimumTime = 0.1,
                }
            };
        }

        /// <summary>
        /// Maps devices in a device group to driver device settings.
        /// </summary>
        public static IEnumerable<DriverDeviceSettings> MapDevicesInGroup(
            string deviceGroup,
            string profileName,
            IEnumerable<IDeviceModel> allDevices)
        {
            return allDevices
                .Where(d => d.DeviceGroup.ModelValue.Equals(deviceGroup))
                .Select(dm => MapDevice(dm, profileName));
        }

        /// <summary>
        /// Maps all devices from a mapping to driver device settings.
        /// </summary>
        public static IEnumerable<DriverDeviceSettings> MapDevicesFromMapping(
            MappingModel mapping,
            IEnumerable<IDeviceModel> allDevices)
        {
            return mapping.IndividualMappings.SelectMany(
                dg => MapDevicesInGroup(dg.DeviceGroup, dg.Profile.Name.ModelValue, allDevices));
        }

        /// <summary>
        /// Maps all profiles from a mapping to driver profiles.
        /// </summary>
        public static IEnumerable<DriverProfile> MapProfilesFromMapping(MappingModel mapping)
        {
            return mapping.IndividualMappings
                .Select(m => m.Profile)
                .Distinct()
                .OfType<ProfileModel>()
                .Select(MapProfile);
        }
    }
}
