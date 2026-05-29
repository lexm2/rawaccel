using System;
using RawAccel.Contracts;

namespace userspace_backend.Driver
{
    // Separate from IRawAccelDriver: preview is pure math, doesn't touch the backend.
    //
    // Windows: wraps wrapper.ManagedAccel.CreateStatelessCopy over the same common/ math.
    // Linux: P/Invokes the rawaccel_common shim for the same common/ math; identity fallback if absent.
    public interface IAccelEvaluator
    {
        IAccelInstance CreateInstance(RawAccelProfile profile);
    }

    // IDisposable: the Linux ShimInstance holds an unmanaged ra_curve handle,
    // and preview callers create one per refresh.
    public interface IAccelInstance : IDisposable
    {
        // dpiFactor: device DPI / NORMALIZED_DPI (1000). timeMs: slice for this sample.
        // Returns same units as input.
        (double x, double y) Accelerate(double x, double y, double dpiFactor, double timeMs);
    }
}
