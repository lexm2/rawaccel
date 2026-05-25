using System;
using System.IO;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using RawAccel.Contracts;

namespace userspace_backend.Driver.Linux
{
    // IRawAccelDriver over a unix domain socket. Serializes RawAccelConfig
    // with Newtonsoft (as wrapper.cpp does on Windows) so the wire JSON matches
    // what the agent's nlohmann::json parser expects.
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

        // DEBUG (temporary): prints the agent's per-axis stats reply, throttled.
        private const bool DebugSpeedLines = true;
        private DateTime lastSpeedDebug = DateTime.MinValue;

        public LinuxAgentDriver(ILogger<LinuxAgentDriver>? logger = null)
        {
            this.logger = logger ?? NullLogger<LinuxAgentDriver>.Instance;
            socketPath = ResolveSocketPath();
            client = new AgentClient(socketPath, TimeSpan.FromSeconds(5));
        }

        // RAWACCEL_SOCKET, then the system path, then the dev-launcher path
        // under XDG_RUNTIME_DIR; first existing socket wins (works under both
        // systemd and linux/run-dev-agent.sh).
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

        public MouseSpeedSample GetCurrentMouseSpeedSample()
        {
            try
            {
                var respJson = client.Call("{\"cmd\":\"stats\"}");
                var resp = JObject.Parse(respJson);
                if (!resp.Value<bool>("ok")) return MouseSpeedSample.Zero;
                // current_speed is the combined (hypot) magnitude for the single
                // line; current_speed_x/_y are the genuine per-axis speeds for the
                // two lines. Do NOT fall back x/y to combined: that would draw both
                // per-axis lines on top of the same hypot value (coupled). If a
                // (stale) agent omits the per-axis fields, leave them 0 so the
                // per-axis lines simply hide rather than masquerade as the hypot.
                double combined = resp.Value<double?>("current_speed") ?? 0;
                double x = resp.Value<double?>("current_speed_x") ?? 0;
                double y = resp.Value<double?>("current_speed_y") ?? 0;

                // DEBUG (temporary): show exactly what the agent sent, throttled.
                if (DebugSpeedLines && (DateTime.UtcNow - lastSpeedDebug).TotalMilliseconds > 200)
                {
                    lastSpeedDebug = DateTime.UtcNow;
                    Console.WriteLine($"[speedline] agent x={x:F2} y={y:F2} combined={combined:F2} raw={respJson}");
                }

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
