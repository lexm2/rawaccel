namespace userspace_backend.Driver.Types
{
    /// <summary>
    /// Acceleration mode types matching the driver's supported formulas.
    /// </summary>
    public enum AccelMode
    {
        Classic,
        Jump,
        Natural,
        Synchronous,
        Power,
        Lut,
        NoAccel
    }

    /// <summary>
    /// Cap application modes.
    /// </summary>
    public enum CapMode
    {
        InOut,
        Input,
        Output
    }

    /// <summary>
    /// Generic 2D vector struct.
    /// </summary>
    public struct Vec2<T>
    {
        public T X;
        public T Y;

        public Vec2(T x, T y)
        {
            X = x;
            Y = y;
        }
    }

    /// <summary>
    /// Acceleration formula parameters.
    /// </summary>
    public struct DriverAccelArgs
    {
        public const int MaxLutPoints = 128;
        public const int LutRawDataCapacity = 514;

        public AccelMode Mode;
        public bool Gain;
        public double InputOffset;
        public double OutputOffset;
        public double Acceleration;
        public double DecayRate;
        public double Gamma;
        public double Motivity;
        public double ExponentClassic;
        public double Scale;
        public double ExponentPower;
        public double Limit;
        public double SyncSpeed;
        public double Smooth;
        public Vec2<double> Cap;
        public CapMode CapMode;
        public int LutLength;
        public float[] LutData;
    }

    /// <summary>
    /// Input speed calculation parameters.
    /// </summary>
    public struct DriverSpeedArgs
    {
        public bool CombineMagnitudes;
        public double LPNorm;
        public double InputSmoothHalflife;
        public double ScaleSmoothHalflife;
        public double OutputSmoothHalflife;
    }

    /// <summary>
    /// Complete acceleration profile configuration.
    /// </summary>
    public struct DriverProfile
    {
        public string Name;
        public Vec2<double> DomainXY;
        public Vec2<double> RangeXY;
        public DriverAccelArgs ArgsX;
        public DriverAccelArgs ArgsY;
        public DriverSpeedArgs SpeedArgs;
        public double OutputDPI;
        public double YXOutputDPIRatio;
        public double LROutputDPIRatio;
        public double UDOutputDPIRatio;
        public double Rotation;
        public double Snap;
        public double MinimumSpeed;
        public double MaximumSpeed;
    }

    /// <summary>
    /// Device-specific configuration.
    /// </summary>
    public struct DriverDeviceConfig
    {
        public bool Disable;
        public bool SetExtraInfo;
        public bool PollTimeLock;
        public int DPI;
        public int PollingRate;
        public double MinimumTime;
        public double MaximumTime;
    }

    /// <summary>
    /// Device settings including profile assignment.
    /// </summary>
    public struct DriverDeviceSettings
    {
        public string Name;
        public string ProfileName;
        public string Id;
        public DriverDeviceConfig Config;
    }
}
