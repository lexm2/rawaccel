#pragma once

// Backend interface for rawaccel-agentd: the agent owns the math/state,
// backends own the per-packet transport.

#include "rawaccel.hpp"

namespace rawaccel_agent {

namespace ra = rawaccel;

struct Backend {
    virtual ~Backend() = default;
    virtual void on_settings_changed(const ra::modifier_settings&) = 0;

    // Most recent smoothed input speed across all connected devices, in the
    // same units the curve sees (DPI-normalized magnitude per ms). Returns
    // 0 when the backend has no per-packet visibility (e.g. BPF, which
    // runs in-kernel and does not surface samples to userspace).
    virtual double current_speed() const { return 0.0; }
};

// Used by tests and as the default until a real backend is selected.
struct NoopBackend : Backend {
    int settings_changes = 0;
    ra::modifier_settings last_settings{};

    void on_settings_changed(const ra::modifier_settings& s) override {
        last_settings = s;
        ++settings_changes;
    }
};

} // namespace rawaccel_agent
