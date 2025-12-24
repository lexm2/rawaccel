using System;

namespace userspace_backend.Driver
{
    /// <summary>
    /// Abstraction for acceleration calculation, used for curve preview generation.
    /// On Windows, wraps ManagedAccel from the native wrapper.
    /// On Linux, provides a stub implementation.
    /// </summary>
    public interface IAccelerationCalculator
    {
        /// <summary>
        /// Calculates the accelerated output for given input coordinates.
        /// </summary>
        /// <param name="x">Input X velocity</param>
        /// <param name="y">Input Y velocity</param>
        /// <param name="dpiFactor">DPI scaling factor</param>
        /// <param name="time">Time delta</param>
        /// <returns>Tuple of (accelerated X, accelerated Y)</returns>
        (double x, double y) Accelerate(double x, double y, double dpiFactor, double time);

        /// <summary>
        /// Creates a copy of this calculator with smoothing disabled, suitable for graphing.
        /// </summary>
        IAccelerationCalculator CreateStatelessCopy();
    }
}
