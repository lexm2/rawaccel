using System;
using System.Collections.Generic;
using Newtonsoft.Json;
using RawAccel.Contracts;

namespace userspace_backend.Driver.Linux
{
    // Curve evaluator backed by the cross-OS C-ABI shim over common/. The shim
    // builds a full rawaccel::modifier from the profile and runs the same
    // modifier::modify the agent's LUT builder and the Windows driver use, so
    // the preview matches what the HID-BPF program applies. The profile is
    // handed over as a one-profile RawAccelConfig JSON -- the identical shape
    // (and serializer) the apply path already sends to the agent -- so there
    // is one math path and one JSON contract, with nothing reimplemented here.
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
            if (!shimAvailable || profile == null) return IdentityInstance.Instance;

            // Wrap the single profile in the same config shape the agent
            // consumes; defaultDeviceConfig/devices use their contract
            // defaults (irrelevant to a device-independent preview).
            var config = new RawAccelConfig
            {
                profiles = new List<RawAccelProfile> { profile },
            };
            var json = JsonConvert.SerializeObject(config);

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
        }

        private sealed class ShimInstance : IAccelInstance, IDisposable
        {
            private readonly IntPtr handle;

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
                if (handle != IntPtr.Zero)
                {
                    RaCurveNative.Destroy(handle);
                }
            }

            ~ShimInstance() => Dispose();
        }
    }
}
