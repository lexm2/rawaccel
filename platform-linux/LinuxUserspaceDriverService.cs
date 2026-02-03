using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using userspace_backend.Common;
using userspace_backend.Common.AccelFormulas;
using userspace_backend.Driver;
using userspace_backend.Driver.Types;
using userspace_backend.Model;

namespace userspace_backend.Platform.Linux
{
    /// <summary>
    /// Linux userspace driver service that communicates with the rawaccel daemon via Unix socket.
    /// </summary>
    public class LinuxUserspaceDriverService : IDriverService
    {
        private const string SocketPath = "/tmp/rawaccel.sock";
        private const string DaemonPath = "/usr/local/bin/rawaccel-daemon";
        private const uint IPC_MAGIC = 0x52415743;  // "RAWC"
        private const uint IPC_VERSION = 1;
        private const int MAX_LUT_POINTS = 256;

        private Socket? _socket;
        private Process? _daemonProcess;
        private readonly ILutComputer _lutComputer;

        public LinuxUserspaceDriverService()
        {
            _lutComputer = new LutComputer();
        }

        public bool IsAvailable => File.Exists(DaemonPath);

        public void Activate(MappingModel mapping, IEnumerable<IDeviceModel> devices)
        {
            // Start daemon if not running
            EnsureDaemonRunning();

            // Get profiles from mapping
            var profiles = DriverMapper.MapProfilesFromMapping(mapping);

            foreach (var profile in profiles)
            {
                // Convert to binary format
                var config = ConvertToDeviceConfig(profile);

                // Send via Unix socket
                SendConfigUpdate(config);
            }

            Console.WriteLine($"Activated {profiles.Count()} profile(s)");
        }

        public void Deactivate()
        {
            if (_socket == null || !_socket.Connected)
            {
                return;
            }

            try
            {
                // Send disable command
                var message = new RawaccelIpcMessage
                {
                    Magic = IPC_MAGIC,
                    Version = IPC_VERSION,
                    Command = (uint)IpcCommand.Disable,
                    PayloadSize = 0
                };

                SendMessage(message, null);
                Console.WriteLine("Deactivated driver");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error deactivating driver: {ex.Message}");
            }
        }

        private RawaccelDeviceConfig ConvertToDeviceConfig(DriverProfile profile)
        {
            var config = new RawaccelDeviceConfig
            {
                Enabled = true,
                SeparateAxes = false,  // Use combined speed for both axes
                Dpi = (uint)profile.OutputDPI
            };

            // Convert X axis LUT
            config.LutX = ConvertLut(profile.ArgsX);

            // If Y axis is different, use separate LUTs
            if (!ArgsEqual(profile.ArgsX, profile.ArgsY))
            {
                config.SeparateAxes = true;
                config.LutY = ConvertLut(profile.ArgsY);
            }
            else
            {
                config.LutY = config.LutX;  // Reuse same LUT
            }

            return config;
        }

        private RawaccelLut ConvertLut(DriverAccelArgs args)
        {
            var lut = new RawaccelLut();

            if (args.Mode == AccelMode.Lut && args.LutData != null && args.LutLength > 0)
            {
                // Use existing LUT data
                // LutData is interleaved (speed, multiplier, speed, multiplier, ...)
                int numPoints = args.LutLength / 2;
                numPoints = Math.Min(numPoints, MAX_LUT_POINTS);

                lut.NumPoints = (uint)numPoints;
                lut.Speeds = new float[MAX_LUT_POINTS];
                lut.Multipliers = new float[MAX_LUT_POINTS];

                for (int i = 0; i < numPoints; i++)
                {
                    lut.Speeds[i] = args.LutData[i * 2];
                    lut.Multipliers[i] = args.LutData[i * 2 + 1];
                }
            }
            else if (args.Mode != AccelMode.NoAccel)
            {
                // Generate LUT from formula
                var formula = CreateFormulaFromArgs(args);
                var lutResult = _lutComputer.ComputeLut(formula);

                int numPoints = lutResult.Length / 2;
                numPoints = Math.Min(numPoints, MAX_LUT_POINTS);

                lut.NumPoints = (uint)numPoints;
                lut.Speeds = new float[MAX_LUT_POINTS];
                lut.Multipliers = new float[MAX_LUT_POINTS];

                for (int i = 0; i < numPoints; i++)
                {
                    lut.Speeds[i] = lutResult.Data[i * 2];
                    lut.Multipliers[i] = lutResult.Data[i * 2 + 1];
                }
            }
            else
            {
                // No acceleration - linear 1:1 mapping
                lut.NumPoints = 2;
                lut.Speeds = new float[MAX_LUT_POINTS];
                lut.Multipliers = new float[MAX_LUT_POINTS];
                lut.Speeds[0] = 0.0f;
                lut.Multipliers[0] = 1.0f;
                lut.Speeds[1] = 200.0f;
                lut.Multipliers[1] = 1.0f;
            }

            return lut;
        }

        private IAccelFormula CreateFormulaFromArgs(DriverAccelArgs args)
        {
            // Create the appropriate formula based on mode
            // Constructor signatures:
            // ClassicFormula(accel, exponent, inputOffset, capY, gain)
            // PowerFormula(scale, exponent, outputOffset, capY, gain)
            // NaturalFormula(decayRate, inputOffset, limit, gain)
            // JumpFormula(input, output, smooth, gain)
            // SynchronousFormula(syncSpeed, motivity, gamma, smoothness, gain)
            // LinearFormula(accel, inputOffset, capY, gain)

            return args.Mode switch
            {
                AccelMode.Classic => new ClassicFormula(
                    args.Acceleration * args.Scale,
                    args.ExponentClassic,
                    args.InputOffset,
                    args.Limit,
                    args.Gain
                ),
                AccelMode.Power => new PowerFormula(
                    args.Scale,
                    args.ExponentPower,
                    args.OutputOffset,
                    args.Limit,
                    args.Gain
                ),
                AccelMode.Natural => new NaturalFormula(
                    args.DecayRate,
                    args.InputOffset,
                    args.Limit,
                    args.Gain
                ),
                AccelMode.Jump => new JumpFormula(
                    args.InputOffset,
                    args.Acceleration,
                    args.Smooth,
                    args.Gain
                ),
                AccelMode.Synchronous => new SynchronousFormula(
                    args.SyncSpeed,
                    args.Motivity,
                    args.Gamma,
                    args.Smooth,
                    args.Gain
                ),
                _ => new LinearFormula(0, 0, 1, false)  // No acceleration = linear 1:1
            };
        }

        private bool ArgsEqual(DriverAccelArgs a, DriverAccelArgs b)
        {
            // Simple comparison - in practice, X and Y are usually the same
            return a.Mode == b.Mode &&
                   Math.Abs(a.Acceleration - b.Acceleration) < 0.0001 &&
                   Math.Abs(a.InputOffset - b.InputOffset) < 0.0001 &&
                   Math.Abs(a.OutputOffset - b.OutputOffset) < 0.0001;
        }

        private void SendConfigUpdate(RawaccelDeviceConfig config)
        {
            var message = new RawaccelIpcMessage
            {
                Magic = IPC_MAGIC,
                Version = IPC_VERSION,
                Command = (uint)IpcCommand.UpdateConfig,
                PayloadSize = (uint)Marshal.SizeOf<RawaccelDeviceConfig>()
            };

            SendMessage(message, config);
        }

        private void SendMessage(RawaccelIpcMessage message, RawaccelDeviceConfig? config)
        {
            if (_socket == null || !_socket.Connected)
            {
                throw new InvalidOperationException("Socket not connected");
            }

            // Send header
            byte[] headerBytes = StructToBytes(message);
            _socket.Send(headerBytes);

            // Send payload if present
            if (config.HasValue)
            {
                byte[] configBytes = ConfigToBytes(config.Value);
                _socket.Send(configBytes);
            }

            // Wait for ACK
            byte[] ack = new byte[4];
            _socket.Receive(ack);

            uint ackValue = BitConverter.ToUInt32(ack, 0);
            if (ackValue != 1)
            {
                throw new Exception("Daemon did not acknowledge config update");
            }
        }

        private void EnsureDaemonRunning()
        {
            // Check if daemon is running
            if (_daemonProcess == null || _daemonProcess.HasExited)
            {
                // Try to start it
                if (!File.Exists(DaemonPath))
                {
                    throw new FileNotFoundException($"Daemon not found at {DaemonPath}. Run 'make install' in linux-driver/userspace/");
                }

                Console.WriteLine("Starting rawaccel daemon...");

                _daemonProcess = Process.Start(new ProcessStartInfo
                {
                    FileName = "sudo",
                    Arguments = $"{DaemonPath} --socket {SocketPath}",
                    UseShellExecute = false,
                    RedirectStandardOutput = false,
                    RedirectStandardError = false
                });

                // Wait for socket to be ready
                for (int i = 0; i < 50; i++)
                {
                    if (File.Exists(SocketPath))
                    {
                        Thread.Sleep(100);  // Give daemon time to initialize
                        break;
                    }
                    Thread.Sleep(100);
                }

                if (!File.Exists(SocketPath))
                {
                    throw new Exception("Daemon failed to create socket");
                }
            }

            // Connect socket if needed
            if (_socket == null || !_socket.Connected)
            {
                var endpoint = new UnixDomainSocketEndPoint(SocketPath);
                _socket = new Socket(AddressFamily.Unix, SocketType.Stream, ProtocolType.Unspecified);

                try
                {
                    _socket.Connect(endpoint);
                    Console.WriteLine("Connected to daemon");
                }
                catch (Exception ex)
                {
                    throw new Exception($"Failed to connect to daemon: {ex.Message}");
                }
            }
        }

        private byte[] StructToBytes<T>(T structure) where T : struct
        {
            int size = Marshal.SizeOf(structure);
            byte[] bytes = new byte[size];
            IntPtr ptr = Marshal.AllocHGlobal(size);

            try
            {
                Marshal.StructureToPtr(structure, ptr, false);
                Marshal.Copy(ptr, bytes, 0, size);
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }

            return bytes;
        }

        private byte[] ConfigToBytes(RawaccelDeviceConfig config)
        {
            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms);

            // Write config fields in order matching C struct
            bw.Write(config.Enabled ? (byte)1 : (byte)0);
            bw.Write(new byte[3]);  // Padding

            // Write lut_x
            WriteLut(bw, config.LutX);

            // Write lut_y
            WriteLut(bw, config.LutY);

            // Write separate_axes
            bw.Write(config.SeparateAxes ? (byte)1 : (byte)0);
            bw.Write(new byte[3]);  // Padding

            // Write dpi
            bw.Write(config.Dpi);

            return ms.ToArray();
        }

        private void WriteLut(BinaryWriter bw, RawaccelLut lut)
        {
            // Write speeds array (256 floats)
            for (int i = 0; i < MAX_LUT_POINTS; i++)
            {
                bw.Write(lut.Speeds[i]);
            }

            // Write multipliers array (256 floats)
            for (int i = 0; i < MAX_LUT_POINTS; i++)
            {
                bw.Write(lut.Multipliers[i]);
            }

            // Write num_points
            bw.Write(lut.NumPoints);
        }

        #region IPC Data Structures

        private enum IpcCommand : uint
        {
            UpdateConfig = 1,
            Disable = 2,
            Enable = 3,
            GetStatus = 4
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        private struct RawaccelIpcMessage
        {
            public uint Magic;
            public uint Version;
            public uint Command;
            public uint PayloadSize;
        }

        private struct RawaccelLut
        {
            public float[] Speeds;        // 256 floats
            public float[] Multipliers;   // 256 floats
            public uint NumPoints;
        }

        private struct RawaccelDeviceConfig
        {
            public bool Enabled;
            public RawaccelLut LutX;
            public RawaccelLut LutY;
            public bool SeparateAxes;
            public uint Dpi;
        }

        #endregion
    }
}
