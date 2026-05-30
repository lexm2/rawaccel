using System;
using Newtonsoft.Json;
using RawAccel.Contracts;

namespace userspace_backend.Driver.Linux
{
    // Runs the same modifier::modify the agent and Windows driver use.
    public sealed class LinuxAccelEvaluator : IAccelEvaluator
    {
        private readonly bool shimAvailable;

        public LinuxAccelEvaluator()
        {
            RaCurveNative.EnsureResolverRegistered();
            try
            {
                shimAvailable =
                    RaCurveNative.AbiVersion() == RaCurveNative.ExpectedAbiVersion;
                if (!shimAvailable)
                {
                    System.Diagnostics.Debug.WriteLine(
                        "[LinuxAccelEvaluator] rawaccel_common shim ABI mismatch; "
                        + "curve preview falls back to identity.");
                }
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
            if (profile == null) throw new ArgumentNullException(nameof(profile));
            if (!shimAvailable) return IdentityInstance.Instance;

            // shim takes a single profile (modifier_settings) JSON object
            var json = JsonConvert.SerializeObject(profile);

            var handle = RaCurveNative.CreateFromConfigJson(json);
            if (handle == IntPtr.Zero) return IdentityInstance.Instance;
            return new ShimInstance(handle);
        }

        private sealed class IdentityInstance : IAccelInstance
        {
            public static readonly IdentityInstance Instance = new();
            public (double x, double y) Accelerate(
                double x, double y, double dpiFactor, double timeMs)
                => (x, y);

            public void Dispose() { }
        }

        private sealed class ShimInstance : IAccelInstance
        {
            private readonly IntPtr handle;
            private bool disposed;

            public ShimInstance(IntPtr handle)
            {
                this.handle = handle;
            }

            public (double x, double y) Accelerate(
                double x, double y, double dpiFactor, double timeMs)
            {
                if (handle == IntPtr.Zero) return (x, y);
                RaCurveNative.Modify(handle, x, y, dpiFactor, timeMs,
                    out double ox, out double oy);
                return (ox, oy);
            }

            public void Dispose()
            {
                if (disposed) return;
                disposed = true;
                if (handle != IntPtr.Zero)
                {
                    RaCurveNative.Destroy(handle);
                }
                GC.SuppressFinalize(this);
            }

            ~ShimInstance() => Dispose();
        }
    }
}
