using System;
using System.Runtime.InteropServices;
using RawAccel.Contracts;

namespace userspace_backend.Driver.Linux
{
    // Curve evaluator backed by the cross-OS C-ABI shim over common/.
    // Builds one ra_curve_t per profile from the X-axis accel args (preview
    // is whole-mode 1D; the Y axis is not exercised) and applies range
    // weight + DPI adjustment outside the shim to keep the ABI minimal.
    public sealed class LinuxAccelEvaluator : IAccelEvaluator
    {
        private readonly bool shimAvailable;

        public LinuxAccelEvaluator()
        {
            RaCurveNative.EnsureResolverRegistered();
            try
            {
                _ = RaCurveNative.AbiVersion();
                shimAvailable = true;
            }
            catch (DllNotFoundException)
            {
                shimAvailable = false;
                System.Diagnostics.Debug.WriteLine(
                    "[LinuxAccelEvaluator] rawaccel_common shim not found; "
                    + "curve preview falls back to identity. Build "
                    + "linux/build/librawaccel_common.so or set "
                    + "RAWACCEL_NATIVE_LIB.");
            }
        }

        public IAccelInstance CreateInstance(RawAccelProfile profile)
        {
            if (!shimAvailable) return IdentityInstance.Instance;
            return new ShimInstance(profile.argsX, profile);
        }

        private sealed class IdentityInstance : IAccelInstance
        {
            public static readonly IdentityInstance Instance = new();
            public (double x, double y) Accelerate(
                double x, double y, double dpiFactor, double timeMs)
                => (x, y);
        }

        private sealed class ShimInstance : IAccelInstance, IDisposable
        {
            private readonly IntPtr handle;
            private readonly double rangeWeightX;
            private readonly double domainWeightX;
            private readonly double domainWeightY;
            private readonly double outputDpiAdjustment;
            private readonly double yxRatio;
            private GCHandle pinnedData;

            public ShimInstance(RawAccelAccelArgs args, RawAccelProfile profile)
            {
                rangeWeightX = profile.rangeXY.x;
                domainWeightX = profile.domainXY.x;
                domainWeightY = profile.domainXY.y;
                outputDpiAdjustment = profile.outputDPI / 1000.0;
                yxRatio = profile.yxOutputDPIRatio;

                IntPtr dataPtr = IntPtr.Zero;
                if (args.data != null && args.data.Length > 0)
                {
                    pinnedData = GCHandle.Alloc(args.data, GCHandleType.Pinned);
                    dataPtr = pinnedData.AddrOfPinnedObject();
                }

                var abi = new RaCurveNative.AccelArgsAbi
                {
                    Mode = (int)args.mode,
                    Gain = args.gain ? 1 : 0,
                    InputOffset = args.inputOffset,
                    OutputOffset = args.outputOffset,
                    Acceleration = args.acceleration,
                    DecayRate = args.decayRate,
                    Gamma = args.gamma,
                    Motivity = args.motivity,
                    ExponentClassic = args.exponentClassic,
                    Scale = args.scale,
                    ExponentPower = args.exponentPower,
                    Limit = args.limit,
                    SyncSpeed = args.syncSpeed,
                    Smooth = args.smooth,
                    CapX = args.cap.x,
                    CapY = args.cap.y,
                    CapMode = (int)args.capMode,
                    Length = args.data?.Length ?? 0,
                    Data = dataPtr,
                };

                handle = RaCurveNative.Create(in abi);

                if (pinnedData.IsAllocated)
                {
                    pinnedData.Free();
                }

                if (handle == IntPtr.Zero)
                {
                    throw new InvalidOperationException(
                        "ra_curve_create returned null");
                }
            }

            public (double x, double y) Accelerate(
                double x, double y, double dpiFactor, double timeMs)
            {
                if (handle == IntPtr.Zero) return (x, y);

                double ipsFactor = dpiFactor / timeMs;
                double sx = Math.Abs(x * ipsFactor * domainWeightX);
                double sy = Math.Abs(y * ipsFactor * domainWeightY);
                double speed = Math.Sqrt(sx * sx + sy * sy);

                double raw = RaCurveNative.Evaluate(handle, speed);
                double scale = 1.0 + (raw - 1.0) * rangeWeightX;

                double dpiAdj = outputDpiAdjustment * dpiFactor;
                double ox = x * scale * dpiAdj;
                double oy = y * scale * dpiAdj * yxRatio;
                return (ox, oy);
            }

            public void Dispose()
            {
                if (handle != IntPtr.Zero)
                {
                    RaCurveNative.Destroy(handle);
                }
            }

            ~ShimInstance() => Dispose();
        }
    }
}
