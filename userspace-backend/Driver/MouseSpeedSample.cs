namespace userspace_backend.Driver
{
    // Telemetry snapshot of current input speed. Units are implementation-
    // defined (counts/ms or in/s), matching IRawAccelDriver.GetCurrentMouseSpeed().
    // Combined is the lp-norm / hypot magnitude used when X and Y are combined;
    // X and Y are the per-axis speeds used when anisotropy runs in separate mode.
    // All-zero means "no data" (backend idle or telemetry unsupported).
    public readonly record struct MouseSpeedSample(double X, double Y, double Combined)
    {
        public static readonly MouseSpeedSample Zero = new(0, 0, 0);

        public bool IsZero => X == 0 && Y == 0 && Combined == 0;
    }
}
