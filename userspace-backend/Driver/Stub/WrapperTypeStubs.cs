#if !WINDOWS
// Stub types for non-Windows platforms to satisfy interface requirements.
// These are never used at runtime on non-Windows platforms.

namespace userspace_backend
{
    // Stub enum for acceleration modes
    public enum AccelMode
    {
        classic,
        jump,
        natural,
        synchronous,
        power,
        lut,
        noaccel
    }

    // Stub enum for cap modes
    public enum CapMode
    {
        in_out,
        input,
        output
    }

    // Stub struct for 2D vectors
    public struct Vec2<T>
    {
        public T x;
        public T y;
    }

    // Stub struct for acceleration arguments
    public struct AccelArgs
    {
        public const int MaxLutPoints = 128;

        public AccelMode mode;
        public double gain;
        public double inputOffset;
        public double outputOffset;
        public double acceleration;
        public double decayRate;
        public double gamma;
        public double motivity;
        public double exponentClassic;
        public double scale;
        public double exponentPower;
        public double limit;
        public double syncSpeed;
        public double smooth;
        public Vec2<double> cap;
        public CapMode capMode;
        public int length;
        public float[] data;
    }

    // Stub struct for speed arguments
    public struct SpeedArgs
    {
        public bool combineMagnitudes;
        public double lpNorm;
        public double inputSmoothHalflife;
        public double scaleSmoothHalflife;
        public double outputSmoothHalflife;
    }

    // Stub struct for profile
    public struct Profile
    {
        public string name;
        public Vec2<double> domainXY;
        public Vec2<double> rangeXY;
        public AccelArgs argsX;
        public AccelArgs argsY;
        public SpeedArgs inputSpeedArgs;
        public double outputDPI;
        public double yxOutputDPIRatio;
        public double lrOutputDPIRatio;
        public double udOutputDPIRatio;
        public double rotation;
        public double snap;
        public double minimumSpeed;
        public double maximumSpeed;
    }
}
#endif
