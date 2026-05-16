using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using RawAccel.Contracts;

namespace userspace_backend.Driver.Linux
{
    // P/Invoke surface for shim/ra_curve.h. Resolves librawaccel_common.so
    // (Linux) / rawaccel_common.dll (Windows) via NativeLibrary.SetDllImport-
    // Resolver so dev builds can find the .so next to the CMake artifact
    // tree without requiring LD_LIBRARY_PATH or a system install.
    internal static class RaCurveNative
    {
        public const string LibraryName = "rawaccel_common";

        // Mirrors ra_accel_args in shim/ra_curve.h. Field order and types
        // are the ABI contract; do not reorder. data is passed as IntPtr
        // because the shim copies it on the C++ side; the C# caller owns
        // the underlying float[] for the duration of ra_curve_create.
        [StructLayout(LayoutKind.Sequential)]
        public struct AccelArgsAbi
        {
            public int Mode;
            public int Gain;
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
            public double CapX;
            public double CapY;
            public int CapMode;
            public int Length;
            public IntPtr Data;
        }

        [DllImport(LibraryName, EntryPoint = "ra_curve_abi_version",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern uint AbiVersion();

        [DllImport(LibraryName, EntryPoint = "ra_curve_create",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr Create(in AccelArgsAbi args);

        [DllImport(LibraryName, EntryPoint = "ra_curve_destroy",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern void Destroy(IntPtr curve);

        [DllImport(LibraryName, EntryPoint = "ra_curve_evaluate",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern double Evaluate(IntPtr curve, double speed);

        private static int resolverRegistered;

        public static void EnsureResolverRegistered()
        {
            if (System.Threading.Interlocked.Exchange(ref resolverRegistered, 1) != 0)
                return;
            NativeLibrary.SetDllImportResolver(
                typeof(RaCurveNative).Assembly, Resolve);
        }

        private static IntPtr Resolve(string name, Assembly asm,
            DllImportSearchPath? path)
        {
            if (name != LibraryName) return IntPtr.Zero;

            // 1. Standard probe (LD_LIBRARY_PATH, ldconfig, RPATH, etc.).
            if (NativeLibrary.TryLoad(LibraryName, asm, path, out var h))
                return h;

            // 2. Explicit override via env var.
            var env = Environment.GetEnvironmentVariable("RAWACCEL_NATIVE_LIB");
            if (!string.IsNullOrEmpty(env) &&
                NativeLibrary.TryLoad(env, out h)) return h;

            // 3. Dev tree: walk up from the running binary to find
            //    linux/build/librawaccel_common.so produced by CMake.
            foreach (var candidate in DevCandidates())
            {
                if (NativeLibrary.TryLoad(candidate, out h)) return h;
            }

            return IntPtr.Zero;
        }

        private static System.Collections.Generic.IEnumerable<string> DevCandidates()
        {
            string filename = RuntimeInformation.IsOSPlatform(OSPlatform.Windows)
                ? "rawaccel_common.dll"
                : "librawaccel_common.so";

            var dir = AppContext.BaseDirectory;
            for (int i = 0; i < 8 && !string.IsNullOrEmpty(dir); i++)
            {
                yield return Path.Combine(dir, "linux", "build", filename);
                yield return Path.Combine(dir, "build", filename);
                yield return Path.Combine(dir, filename);
                dir = Directory.GetParent(dir)?.FullName ?? string.Empty;
            }
        }
    }
}
