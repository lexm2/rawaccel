using System;

namespace userspace_backend.Driver.Windows
{
    /// <summary>
    /// Windows implementation that wraps ManagedAccel from the native wrapper.
    /// </summary>
    public class WindowsAccelerationCalculator : IAccelerationCalculator
    {
        private readonly ManagedAccel _accel;

        public WindowsAccelerationCalculator(ManagedAccel accel)
        {
            _accel = accel ?? throw new ArgumentNullException(nameof(accel));
        }

        public (double x, double y) Accelerate(double x, double y, double dpiFactor, double time)
        {
            var result = _accel.Accelerate(x, y, dpiFactor, time);
            return (result.Item1, result.Item2);
        }

        public IAccelerationCalculator CreateStatelessCopy()
        {
            return new WindowsAccelerationCalculator(_accel.CreateStatelessCopy());
        }
    }
}
