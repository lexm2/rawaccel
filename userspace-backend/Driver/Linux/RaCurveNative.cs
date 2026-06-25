using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace userspace_backend.Driver.Linux
{
    // P/Invoke surface for shim/ra_curve.h. A custom DllImportResolver finds
    // librawaccel_common.so / rawaccel_common.dll in the CMake build tree, so
    // dev builds need no LD_LIBRARY_PATH or system install.
    internal static class RaCurveNative
    {
        public const string LibraryName = "rawaccel_common";

        // shim ABI this binding targets
        // ra_curve_abi_version must agree
        public const uint ExpectedAbiVersion = 3;

        [DllImport(LibraryName, EntryPoint = "ra_curve_abi_version",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern uint AbiVersion();

        // Builds a curve handle from a RawAccelConfig JSON string (the shim
        // parses the first profile). Returns IntPtr.Zero on failure.
        [DllImport(LibraryName, EntryPoint = "ra_curve_create_from_config_json",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr CreateFromConfigJson(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string configJson);

        [DllImport(LibraryName, EntryPoint = "ra_curve_destroy",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern void Destroy(IntPtr curve);

        // one input sample (x, y) in, post-acceleration (outX, outY) out
        // (mirrors rawaccel::modifier::modify)
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

            // 1. standard probe (LD_LIBRARY_PATH, ldconfig, RPATH)
            if (NativeLibrary.TryLoad(LibraryName, asm, path, out var h))
                return h;

            // 2. explicit override via env var
            var env = Environment.GetEnvironmentVariable("RAWACCEL_NATIVE_LIB");
            if (!string.IsNullOrEmpty(env) &&
                NativeLibrary.TryLoad(env, out h)) return h;

            // 3. dev tree: walk up from the binary to the CMake artifact
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
