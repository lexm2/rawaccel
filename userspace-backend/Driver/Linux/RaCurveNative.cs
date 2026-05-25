using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace userspace_backend.Driver.Linux
{
    // P/Invoke surface for shim/ra_curve.h. Resolves librawaccel_common.so
    // (Linux) / rawaccel_common.dll (Windows) via NativeLibrary.SetDllImport-
    // Resolver so dev builds can find the .so next to the CMake artifact
    // tree without requiring LD_LIBRARY_PATH or a system install.
    internal static class RaCurveNative
    {
        public const string LibraryName = "rawaccel_common";

        // The shim ABI this binding targets; ra_curve_abi_version must agree.
        public const uint ExpectedAbiVersion = 2;

        [DllImport(LibraryName, EntryPoint = "ra_curve_abi_version",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern uint AbiVersion();

        // Builds a curve handle from a driver-config JSON string (the same
        // RawAccelConfig shape the apply path sends to the agent). The shim
        // parses the first profile and runs the full modifier pipeline, so no
        // per-profile math is reimplemented on the managed side. Returns
        // IntPtr.Zero on parse/allocation failure.
        [DllImport(LibraryName, EntryPoint = "ra_curve_create_from_config_json",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr CreateFromConfigJson(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string configJson);

        [DllImport(LibraryName, EntryPoint = "ra_curve_destroy",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern void Destroy(IntPtr curve);

        // Mirrors ManagedAccel.Accelerate / rawaccel::modifier::modify: one
        // input sample (x, y) in, post-acceleration (outX, outY) out.
        [DllImport(LibraryName, EntryPoint = "ra_curve_modify",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern void Modify(IntPtr curve, double x, double y,
            double dpiFactor, double timeMs, out double outX, out double outY);

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
