using RawAccel.Contracts;

namespace userspace_backend.Driver
{
    // Platform-agnostic apply/read/deactivate surface for the Raw Accel
    // driver (Windows kernel filter via IOCTL) or the Linux userspace agent
    // (HID-BPF + evdev daemon via unix socket). Both implementations consume
    // and produce the same RawAccelConfig POCO; the JSON contract for that
    // POCO is the cross-OS source of truth in RawAccel.Contracts.
    public interface IRawAccelDriver
    {
        // True if this implementation can talk to its backend right now.
        // Windows: driver service installed and reachable. Linux: agent
        // socket exists and is connectable. UI gates Apply on this.
        bool IsAvailable { get; }

        // Push a configuration. Returns true on success, false if the
        // backend rejected the config or the transport failed. Implementations
        // should log the underlying error rather than letting it surface as
        // an exception so callers can render a simple success/fail toast.
        // The 1s WriteDelay anti-abuse mitigation is enforced by the backend
        // (driver / agent), not by this method.
        bool Apply(RawAccelConfig config);

        // Read the currently active configuration from the backend.
        RawAccelConfig Read();

        // Reset the backend to a no-op configuration without uninstalling.
        void Deactivate();

        // Optional telemetry: current input speed split into per-axis X/Y plus
        // the combined (lp-norm / hypot) magnitude, in chart units (normalized
        // in/s). Returns MouseSpeedSample.Zero when unsupported or idle.
        MouseSpeedSample GetCurrentMouseSpeedSample();
    }
}
