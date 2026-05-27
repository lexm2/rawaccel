using System;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace userspace_backend.Logging
{
    public static class EditableSettingLog
    {
        private static bool configured;

        public static ILogger Logger { get; private set; } = NullLogger.Instance;

        public static void Configure(ILoggerFactory factory)
        {
            ArgumentNullException.ThrowIfNull(factory);
            if (configured) return;
            Logger = factory.CreateLogger("userspace_backend.EditableSetting");
            configured = true;
        }
    }
}
