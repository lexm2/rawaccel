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

        // Push a configuration. Throws on transport or validation failure.
        // The 1s WriteDelay anti-abuse mitigation is enforced by the backend
        // (driver / agent), not by this method.
        void Apply(RawAccelConfig config);

        // Read the currently active configuration from the backend.
        RawAccelConfig Read();

        // Reset the backend to a no-op configuration without uninstalling.
        void Deactivate();

        // Optional telemetry: current input speed (counts/ms or in/s,
        // implementation-defined). Returns 0 when unsupported.
        double GetCurrentMouseSpeed();
    }
}
