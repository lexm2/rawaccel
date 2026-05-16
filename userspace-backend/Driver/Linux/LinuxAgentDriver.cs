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
            socketPath = Environment.GetEnvironmentVariable(EnvSocketPath)
                         ?? AgentClient.DefaultSocketPath;
            client = new AgentClient(socketPath, TimeSpan.FromSeconds(5));
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

        public double GetCurrentMouseSpeed()
        {
            try
            {
                var respJson = client.Call("{\"cmd\":\"stats\"}");
                var resp = JObject.Parse(respJson);
                if (!resp.Value<bool>("ok")) return 0;
                return resp.Value<double?>("current_speed") ?? 0;
            }
            catch (Exception ex)
            {
                logger.LogDebug(ex, "agent stats probe failed");
                return 0;
            }
        }
    }
}
