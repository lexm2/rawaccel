using System;
using System.Buffers.Binary;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace userspace_backend.Driver.Linux
{
    // Thrown when the agent socket file is missing (daemon not running);
    // distinct from transport errors so callers can hint "start the daemon".
    public sealed class AgentUnavailableException : Exception
    {
        public AgentUnavailableException(string message) : base(message) { }
    }

    // Unix-domain-socket client for the rawaccel-agent control protocol.
    // Wire format (mirrors linux/agent/control_server.cpp and the Rust CLI in
    // linux/cli/src/client.rs): 4-byte big-endian length + UTF-8 JSON, one
    // request per connection. Keep all three clients in agreement.
    internal sealed class AgentClient
    {
        public const int MaxFrameBytes = 16 * 1024 * 1024;
        public const string DefaultSocketPath = "/run/rawaccel/control.sock";

        private readonly string socketPath;
        private readonly TimeSpan timeout;

        public AgentClient(string socketPath, TimeSpan timeout)
        {
            this.socketPath = socketPath;
            this.timeout = timeout;
        }

        public string Call(string requestJson)
        {
            var requestBytes = Encoding.UTF8.GetBytes(requestJson);
            if (requestBytes.Length > MaxFrameBytes)
            {
                throw new InvalidOperationException(
                    $"request frame too large: {requestBytes.Length} > {MaxFrameBytes}");
            }

            // Fast-fail when missing: an AF_UNIX connect to a nonexistent path
            // otherwise surfaces a misleading EADDRNOTAVAIL.
            if (!File.Exists(socketPath))
            {
                throw new AgentUnavailableException(
                    $"rawaccel-agent socket not found at {socketPath}. "
                    + "Start rawaccel-agentd (linux/run-dev-agent.sh) or "
                    + "set RAWACCEL_SOCKET to the active socket path.");
            }

            using var socket = new Socket(
                AddressFamily.Unix, SocketType.Stream, ProtocolType.Unspecified);
            socket.SendTimeout = (int)timeout.TotalMilliseconds;
            socket.ReceiveTimeout = (int)timeout.TotalMilliseconds;

            var endpoint = new UnixDomainSocketEndPoint(socketPath);
            try
            {
                using var connectCts = new CancellationTokenSource(timeout);
                socket.ConnectAsync(endpoint, connectCts.Token)
                      .AsTask()
                      .GetAwaiter().GetResult();
            }
            catch (OperationCanceledException)
            {
                throw new TimeoutException(
                    $"connect to {socketPath} timed out after {timeout.TotalSeconds:0.##}s");
            }

            using var stream = new NetworkStream(socket, ownsSocket: false);

            var lenBuf = new byte[4];
            BinaryPrimitives.WriteUInt32BigEndian(lenBuf, (uint)requestBytes.Length);
            stream.Write(lenBuf, 0, 4);
            stream.Write(requestBytes, 0, requestBytes.Length);

            ReadExact(stream, lenBuf, 4);
            uint respLen = BinaryPrimitives.ReadUInt32BigEndian(lenBuf);
            if (respLen > MaxFrameBytes)
            {
                throw new InvalidDataException(
                    $"response frame too large: {respLen} > {MaxFrameBytes}");
            }

            var respBytes = new byte[respLen];
            if (respLen > 0) ReadExact(stream, respBytes, (int)respLen);
            return Encoding.UTF8.GetString(respBytes);
        }

        private static void ReadExact(Stream s, byte[] buf, int n)
        {
            int read = 0;
            while (read < n)
            {
                int got = s.Read(buf, read, n - read);
                if (got <= 0) throw new EndOfStreamException(
                    $"agent closed connection after {read}/{n} bytes");
                read += got;
            }
        }
    }
}
