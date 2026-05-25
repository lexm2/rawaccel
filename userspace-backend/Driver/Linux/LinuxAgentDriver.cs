using System;
using System.IO;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using RawAccel.Contracts;

namespace userspace_backend.Driver.Linux
{
    // IRawAccelDriver implementation that talks to the rawaccel-agent over
    // a unix domain socket. Serializes RawAccelConfig with Newtonsoft (the
    // same library wrapper.cpp uses on Windows) so the on-the-wire JSON
    // shape is identical to what the agent's nlohmann::json parser expects.
    public sealed class LinuxAgentDriver : IRawAccelDriver
    {
        private const string EnvSocketPath = "RAWACCEL_SOCKET";

        private static readonly JsonSerializerSettings JsonSettings =
            new JsonSerializerSettings
            {
                Formatting = Formatting.None,
                NullValueHandling = NullValueHandling.Include,
            };

        private readonly AgentClient client;
        private readonly string socketPath;
        private readonly ILogger<LinuxAgentDriver> logger;

        public LinuxAgentDriver(ILogger<LinuxAgentDriver>? logger = null)
        {
            this.logger = logger ?? NullLogger<LinuxAgentDriver>.Instance;
            socketPath = ResolveSocketPath();
            client = new AgentClient(socketPath, TimeSpan.FromSeconds(5));
        }

        // Try RAWACCEL_SOCKET first, then the production system path, then
        // the dev-launcher path under XDG_RUNTIME_DIR. The first existing
        // socket wins so the GUI works whether the agent was started by
        // systemd or by linux/run-dev-agent.sh.
        private static string ResolveSocketPath()
        {
            var envPath = Environment.GetEnvironmentVariable(EnvSocketPath);
            if (!string.IsNullOrEmpty(envPath)) return envPath;

            if (File.Exists(AgentClient.DefaultSocketPath))
            {
                return AgentClient.DefaultSocketPath;
            }

            var runtimeDir =
                Environment.GetEnvironmentVariable("XDG_RUNTIME_DIR");
            if (!string.IsNullOrEmpty(runtimeDir))
            {
                var devPath = Path.Combine(runtimeDir, "rawaccel.sock");
                if (File.Exists(devPath)) return devPath;
            }

            return AgentClient.DefaultSocketPath;
        }

        public bool IsAvailable
        {
            get
            {
                if (!File.Exists(socketPath)) return false;
                try
                {
                    var resp = client.Call("{\"cmd\":\"status\"}");
                    var jo = JObject.Parse(resp);
                    return jo.Value<bool>("ok");
                }
                catch (Exception ex)
                {
                    logger.LogDebug(ex, "agent status probe failed");
                    return false;
                }
            }
        }

        public bool Apply(RawAccelConfig config)
        {
            try
            {
                var configToken = JObject.FromObject(config, JsonSerializer.Create(JsonSettings));
                var request = new JObject
                {
                    ["cmd"] = "apply",
                    ["config"] = configToken,
                };
                var respJson = client.Call(request.ToString(Formatting.None));
                var resp = JObject.Parse(respJson);
                if (!resp.Value<bool>("ok"))
                {
                    var error = resp.Value<string>("error") ?? "unknown agent error";
                    logger.LogError("agent apply failed: {Error}", error);
                    return false;
                }
                var deferredMs = resp.Value<int?>("deferred_ms") ?? 0;
                logger.LogInformation(
                    "agent apply scheduled (deferred {DeferredMs} ms)", deferredMs);
                return true;
            }
            catch (Exception ex)
            {
                logger.LogError(ex, "agent apply failed");
                return false;
            }
        }

        public RawAccelConfig Read()
        {
            var respJson = client.Call("{\"cmd\":\"get\"}");
            var resp = JObject.Parse(respJson);
            if (!resp.Value<bool>("ok"))
            {
                var error = resp.Value<string>("error") ?? "unknown agent error";
                throw new InvalidOperationException($"agent get failed: {error}");
            }
            var config = resp["config"] ?? throw new InvalidDataException(
                "agent get response missing 'config' field");
            return config.ToObject<RawAccelConfig>(JsonSerializer.Create(JsonSettings))
                ?? throw new InvalidDataException("agent get returned null config");
        }

        public void Deactivate()
        {
            var respJson = client.Call("{\"cmd\":\"deactivate\"}");
            var resp = JObject.Parse(respJson);
            if (!resp.Value<bool>("ok"))
            {
                var error = resp.Value<string>("error") ?? "unknown agent error";
                throw new InvalidOperationException(
                    $"agent deactivate failed: {error}");
            }
        }

        // DEBUG AID: when true, GetCurrentMouseSpeedSample returns random speeds
        // instead of querying the agent, so the chart's current-speed indicator
        // lines can be verified before the HID-BPF agent exposes real per-axis
        // telemetry (current_speed_x/current_speed_y from the ra_state map).
        // Leave false in committed code; flip to true to visually test the lines.
        private static readonly bool DebugRandomSpeed = false;

        public double GetCurrentMouseSpeed() => GetCurrentMouseSpeedSample().Combined;

        public MouseSpeedSample GetCurrentMouseSpeedSample()
        {
            if (DebugRandomSpeed)
            {
                // Random per-axis speeds in a typical mouse-speed range; Combined
                // is their honest hypot so combined-mode shows a consistent value.
                double rx = Random.Shared.NextDouble() * 100.0;
                double ry = Random.Shared.NextDouble() * 100.0;
                return new MouseSpeedSample(rx, ry, Math.Sqrt(rx * rx + ry * ry));
            }

            try
            {
                var respJson = client.Call("{\"cmd\":\"stats\"}");
                var resp = JObject.Parse(respJson);
                if (!resp.Value<bool>("ok")) return MouseSpeedSample.Zero;
                // current_speed is the combined magnitude; the agent may also
                // emit per-axis current_speed_x/current_speed_y. Until it does,
                // fall back to the combined value so the single-line case keeps
                // working and the two-line case degrades gracefully.
                double combined = resp.Value<double?>("current_speed") ?? 0;
                double x = resp.Value<double?>("current_speed_x") ?? combined;
                double y = resp.Value<double?>("current_speed_y") ?? combined;
                return new MouseSpeedSample(x, y, combined);
            }
            catch (Exception ex)
            {
                logger.LogDebug(ex, "agent stats probe failed");
                return MouseSpeedSample.Zero;
            }
        }
    }
}
