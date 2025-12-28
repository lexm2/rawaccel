using userspace_backend.Driver.Types;

namespace userspace_backend.Platform.Windows
{
    /// <summary>
    /// Converts shared driver types to wrapper types for native interop.
    /// </summary>
    public static class WrapperTypeConverter
    {
        /// <summary>
        /// Converts a shared DriverProfile to a wrapper Profile.
        /// </summary>
        public static Profile ToWrapperProfile(DriverProfile source)
        {
            return new Profile
            {
                name = source.Name,
                outputDPI = source.OutputDPI,
                yxOutputDPIRatio = source.YXOutputDPIRatio,
                lrOutputDPIRatio = source.LROutputDPIRatio,
                udOutputDPIRatio = source.UDOutputDPIRatio,
                rotation = source.Rotation,
                snap = source.Snap,
                minimumSpeed = source.MinimumSpeed,
                maximumSpeed = source.MaximumSpeed,
                domainXY = new Vec2<double> { x = source.DomainXY.X, y = source.DomainXY.Y },
                rangeXY = new Vec2<double> { x = source.RangeXY.X, y = source.RangeXY.Y },
                argsX = ToWrapperAccelArgs(source.ArgsX),
                argsY = ToWrapperAccelArgs(source.ArgsY),
                inputSpeedArgs = ToWrapperSpeedArgs(source.SpeedArgs),
            };
        }

        /// <summary>
        /// Converts shared DriverAccelArgs to wrapper AccelArgs.
        /// </summary>
        public static AccelArgs ToWrapperAccelArgs(DriverAccelArgs source)
        {
            return new AccelArgs
            {
                mode = ToWrapperAccelMode(source.Mode),
                gain = source.Gain,
                inputOffset = source.InputOffset,
                outputOffset = source.OutputOffset,
                acceleration = source.Acceleration,
                decayRate = source.DecayRate,
                gamma = source.Gamma,
                motivity = source.Motivity,
                exponentClassic = source.ExponentClassic,
                scale = source.Scale,
                exponentPower = source.ExponentPower,
                limit = source.Limit,
                syncSpeed = source.SyncSpeed,
                smooth = source.Smooth,
                cap = new Vec2<double> { x = source.Cap.X, y = source.Cap.Y },
                capMode = ToWrapperCapMode(source.CapMode),
                length = source.LutLength,
                data = source.LutData ?? new float[DriverAccelArgs.LutRawDataCapacity],
            };
        }

        /// <summary>
        /// Converts shared DriverSpeedArgs to wrapper SpeedArgs.
        /// </summary>
        public static SpeedArgs ToWrapperSpeedArgs(DriverSpeedArgs source)
        {
            return new SpeedArgs
            {
                combineMagnitudes = source.CombineMagnitudes,
                lpNorm = source.LPNorm,
                inputSmoothHalflife = source.InputSmoothHalflife,
                scaleSmoothHalflife = source.ScaleSmoothHalflife,
                outputSmoothHalflife = source.OutputSmoothHalflife,
            };
        }

        /// <summary>
        /// Converts shared DriverDeviceSettings to wrapper DeviceSettings.
        /// </summary>
        public static DeviceSettings ToWrapperDeviceSettings(DriverDeviceSettings source)
        {
            return new DeviceSettings
            {
                id = source.Id,
                name = source.Name,
                profile = source.ProfileName,
                config = ToWrapperDeviceConfig(source.Config),
            };
        }

        /// <summary>
        /// Converts shared DriverDeviceConfig to wrapper DeviceConfig.
        /// </summary>
        public static DeviceConfig ToWrapperDeviceConfig(DriverDeviceConfig source)
        {
            return new DeviceConfig
            {
                disable = source.Disable,
                setExtraInfo = source.SetExtraInfo,
                pollTimeLock = source.PollTimeLock,
                dpi = source.DPI,
                pollingRate = source.PollingRate,
                minimumTime = source.MinimumTime,
                maximumTime = source.MaximumTime,
            };
        }

        /// <summary>
        /// Converts shared AccelMode to wrapper AccelMode.
        /// </summary>
        public static AccelMode ToWrapperAccelMode(Types.AccelMode source)
        {
            return source switch
            {
                Types.AccelMode.Classic => AccelMode.classic,
                Types.AccelMode.Jump => AccelMode.jump,
                Types.AccelMode.Natural => AccelMode.natural,
                Types.AccelMode.Synchronous => AccelMode.synchronous,
                Types.AccelMode.Power => AccelMode.power,
                Types.AccelMode.Lut => AccelMode.lut,
                Types.AccelMode.NoAccel => AccelMode.noaccel,
                _ => AccelMode.noaccel,
            };
        }

        /// <summary>
        /// Converts shared CapMode to wrapper CapMode.
        /// </summary>
        public static CapMode ToWrapperCapMode(Types.CapMode source)
        {
            return source switch
            {
                Types.CapMode.InOut => CapMode.in_out,
                Types.CapMode.Input => CapMode.input,
                Types.CapMode.Output => CapMode.output,
                _ => CapMode.output,
            };
        }
    }
}
