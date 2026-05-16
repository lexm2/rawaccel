#define RA_SHIM_BUILD 1
#include "ra_curve.h"

#include <rawaccel.hpp>

#include <cstring>
#include <new>

namespace {

void copy_args(rawaccel::accel_args& dst, const ra_accel_args& src)
{
    dst.mode = static_cast<rawaccel::accel_mode>(src.mode);
    dst.gain = src.gain != 0;
    dst.input_offset = src.input_offset;
    dst.output_offset = src.output_offset;
    dst.acceleration = src.acceleration;
    dst.decay_rate = src.decay_rate;
    dst.gamma = src.gamma;
    dst.motivity = src.motivity;
    dst.exponent_classic = src.exponent_classic;
    dst.scale = src.scale;
    dst.exponent_power = src.exponent_power;
    dst.limit = src.limit;
    dst.sync_speed = src.sync_speed;
    dst.smooth = src.smooth;
    dst.cap = { src.cap_x, src.cap_y };
    dst.cap_mode = static_cast<rawaccel::cap_mode>(src.cap_mode);

    int len = src.length;
    if (len < 0) len = 0;
    if (static_cast<size_t>(len) > rawaccel::LUT_RAW_DATA_CAPACITY) {
        len = static_cast<int>(rawaccel::LUT_RAW_DATA_CAPACITY);
    }
    dst.length = len;
    if (len > 0 && src.data != nullptr) {
        std::memcpy(dst.data, src.data,
                    static_cast<size_t>(len) * sizeof(float));
    }
}

} // namespace

// modifier_settings is the smallest aggregate in common/ that legally
// holds an accel_union (the union itself has no default constructor
// because its members are non-trivial). The shim owns one and only ever
// reads accel_x; accel_y is initialized to the same args so init_data
// stays well-formed.
struct ra_curve {
    rawaccel::modifier_settings settings;
};

extern "C" {

uint32_t ra_curve_abi_version(void)
{
    return 1u;
}

ra_curve_t* ra_curve_create(const struct ra_accel_args* args)
{
    if (args == nullptr) return nullptr;

    auto* curve = new (std::nothrow) ra_curve_t{};
    if (curve == nullptr) return nullptr;

    copy_args(curve->settings.prof.accel_x, *args);
    copy_args(curve->settings.prof.accel_y, *args);
    rawaccel::init_data(curve->settings);
    return curve;
}

void ra_curve_destroy(ra_curve_t* curve)
{
    delete curve;
}

double ra_curve_evaluate(const ra_curve_t* curve, double speed)
{
    if (curve == nullptr) return 1.0;
    auto& mut = const_cast<rawaccel::accel_union&>(
        curve->settings.data.accel_x);
    const auto& args = curve->settings.prof.accel_x;
    return mut.visit([&](auto& impl) { return impl(speed, args); }, args);
}

} // extern "C"
