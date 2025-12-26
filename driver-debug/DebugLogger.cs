using System;
using System.Diagnostics;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace userspace_backend.Driver.Debug
{
    /// <summary>
    /// Helper class for consistent debug logging with JSON serialization.
    /// </summary>
    public static class DebugLogger
    {
        private const string Prefix = "[DEBUG-DRIVER";

        private static readonly JsonSerializerOptions JsonOptions = new()
        {
            WriteIndented = true,
            DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
            Converters = { new JsonStringEnumConverter() }
        };

        /// <summary>
        /// Logs a message with timestamp to console and debug output.
        /// </summary>
        public static void Log(string message)
        {
            string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss");
            string formatted = $"{Prefix} {timestamp}] {message}";
            Console.WriteLine(formatted);
            System.Diagnostics.Debug.WriteLine(formatted);
        }

        /// <summary>
        /// Logs a section header.
        /// </summary>
        public static void LogHeader(string header)
        {
            Log($"=== {header} ===");
        }

        /// <summary>
        /// Logs an object as formatted JSON.
        /// </summary>
        public static void LogJson(string label, object? obj)
        {
            Log($"{label}:");
            if (obj == null)
            {
                Console.WriteLine("  null");
                System.Diagnostics.Debug.WriteLine("  null");
                return;
            }

            try
            {
                string json = JsonSerializer.Serialize(obj, JsonOptions);
                Console.WriteLine(json);
                System.Diagnostics.Debug.WriteLine(json);
            }
            catch (Exception ex)
            {
                string error = $"  [Serialization error: {ex.Message}]";
                Console.WriteLine(error);
                System.Diagnostics.Debug.WriteLine(error);
            }
        }

        /// <summary>
        /// Logs a collection with count and JSON content.
        /// </summary>
        public static void LogCollection<T>(string label, System.Collections.Generic.IEnumerable<T> items)
        {
            var list = new System.Collections.Generic.List<T>(items);
            Log($"{label} ({list.Count}):");

            try
            {
                string json = JsonSerializer.Serialize(list, JsonOptions);
                Console.WriteLine(json);
                System.Diagnostics.Debug.WriteLine(json);
            }
            catch (Exception ex)
            {
                string error = $"  [Serialization error: {ex.Message}]";
                Console.WriteLine(error);
                System.Diagnostics.Debug.WriteLine(error);
            }
        }
    }
}
