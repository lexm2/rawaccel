#include "hid_descriptor.hpp"

#include <cstring>

namespace rawaccel_agent {

namespace {

// HID spec item-prefix decoding.
constexpr std::uint8_t TYPE_MAIN   = 0;
constexpr std::uint8_t TYPE_GLOBAL = 1;
constexpr std::uint8_t TYPE_LOCAL  = 2;

// Main item tags.
constexpr std::uint8_t TAG_INPUT      = 0x8;
constexpr std::uint8_t TAG_COLLECTION = 0xA;
constexpr std::uint8_t TAG_END_COLLEC = 0xC;

// Global item tags.
constexpr std::uint8_t TAG_USAGE_PAGE   = 0x0;
constexpr std::uint8_t TAG_LOG_MIN      = 0x1;
constexpr std::uint8_t TAG_LOG_MAX      = 0x2;
constexpr std::uint8_t TAG_REPORT_SIZE  = 0x7;
constexpr std::uint8_t TAG_REPORT_ID    = 0x8;
constexpr std::uint8_t TAG_REPORT_COUNT = 0x9;
constexpr std::uint8_t TAG_PUSH         = 0xA;
constexpr std::uint8_t TAG_POP          = 0xB;

// Local item tags.
constexpr std::uint8_t TAG_USAGE     = 0x0;
constexpr std::uint8_t TAG_USAGE_MIN = 0x1;
constexpr std::uint8_t TAG_USAGE_MAX = 0x2;

// Usage page / usage values we care about.
constexpr std::uint32_t UP_GENERIC_DESKTOP = 0x01;
constexpr std::uint32_t USAGE_POINTER = 0x01;
constexpr std::uint32_t USAGE_MOUSE   = 0x02;
constexpr std::uint32_t USAGE_X       = 0x30;
constexpr std::uint32_t USAGE_Y       = 0x31;

struct GlobalState {
    std::uint32_t usage_page = 0;
    std::int32_t  logical_min = 0;
    std::int32_t  logical_max = 0;
    std::uint32_t report_size = 0;
    std::uint32_t report_count = 0;
    bool          has_report_id = false;
    std::uint8_t  report_id = 0;
};

struct PerReport {
    std::uint8_t report_id = 0;
    bool         has_report_id = false;
    std::uint32_t bit_offset = 0;  // running bit cursor within payload.
    MouseAxis x;
    MouseAxis y;
};

// Decode a variable-width signed/unsigned data field from item data.
std::int32_t read_signed(const std::uint8_t* p, std::size_t n)
{
    if (n == 0) return 0;
    std::int32_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        v |= std::int32_t(p[i]) << (8 * i);
    }
    // Sign-extend.
    if (n < 4) {
        std::int32_t mask = 1 << (8 * n - 1);
        if (v & mask) v |= ~((1 << (8 * n)) - 1);
    }
    return v;
}

std::uint32_t read_unsigned(const std::uint8_t* p, std::size_t n)
{
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        v |= std::uint32_t(p[i]) << (8 * i);
    }
    return v;
}

bool is_relative(std::uint32_t input_flags)
{
    return (input_flags & 0x04) != 0;  // bit 2 = Relative.
}

bool is_variable(std::uint32_t input_flags)
{
    return (input_flags & 0x02) != 0;  // bit 1 = Variable.
}

bool is_data(std::uint32_t input_flags)
{
    return (input_flags & 0x01) == 0;  // bit 0 = Constant when set.
}

void process_input(const GlobalState& g, std::vector<std::uint32_t>& usages,
                   std::uint32_t input_flags, PerReport& rep)
{
    // Walk ReportCount fields, each ReportSize bits. Consume one Local
    // Usage per field; if the local-usage list is shorter than the field
    // count, repeat the last (per HID 1.11 Section 6.2.2.7).
    for (std::uint32_t i = 0; i < g.report_count; ++i) {
        std::uint32_t usage = 0;
        if (!usages.empty()) {
            usage = (i < usages.size()) ? usages[i] : usages.back();
        }

        const bool var = is_variable(input_flags);
        const bool data = is_data(input_flags);
        const bool rel = is_relative(input_flags);

        if (var && data && rel && g.usage_page == UP_GENERIC_DESKTOP) {
            if (usage == USAGE_X && !rep.x.present) {
                rep.x.present = true;
                rep.x.bit_offset_in_payload = rep.bit_offset;
                rep.x.bit_size = g.report_size;
                rep.x.is_signed = (g.logical_min < 0);
            } else if (usage == USAGE_Y && !rep.y.present) {
                rep.y.present = true;
                rep.y.bit_offset_in_payload = rep.bit_offset;
                rep.y.bit_size = g.report_size;
                rep.y.is_signed = (g.logical_min < 0);
            }
        }

        rep.bit_offset += g.report_size;
    }
}

} // namespace

std::optional<MouseDescriptor> parse_mouse_descriptor(
    const std::uint8_t* descriptor, std::size_t len)
{
    GlobalState g{};
    std::vector<GlobalState> push_stack;
    std::vector<std::uint32_t> usages;
    std::int32_t usage_min = 0;
    std::int32_t usage_max = 0;
    bool usage_min_set = false;
    bool usage_max_set = false;

    std::vector<std::uint32_t> coll_usages;  // Outer collection usage stack.
    bool in_mouse_collection = false;
    PerReport rep{};
    std::optional<MouseDescriptor> result;

    auto flush_locals = [&]{
        // Per HID 1.11 6.2.2.8: Local items are reset by Main items.
        usages.clear();
        usage_min_set = usage_max_set = false;
        usage_min = usage_max = 0;
    };

    auto commit_report_if_complete = [&]{
        if (rep.x.present && rep.y.present && !result) {
            MouseDescriptor md;
            md.has_report_id = rep.has_report_id;
            md.report_id = rep.report_id;
            md.x = rep.x;
            md.y = rep.y;
            md.report_bits = rep.bit_offset;
            result = md;
        }
    };

    std::size_t i = 0;
    while (i < len) {
        const std::uint8_t prefix = descriptor[i++];
        if (prefix == 0xFE) {
            // Long item: bSize (1 byte) + bLongItemTag (1 byte) + data.
            if (i + 1 >= len) return std::nullopt;
            std::size_t dsize = descriptor[i++];
            ++i;  // long tag
            i += dsize;
            continue;
        }
        const std::uint8_t bsize_code = prefix & 0x03;
        const std::uint8_t btype = (prefix >> 2) & 0x03;
        const std::uint8_t btag  = (prefix >> 4) & 0x0F;
        const std::size_t dsize = (bsize_code == 3) ? 4 : bsize_code;
        if (i + dsize > len) return std::nullopt;
        const std::uint8_t* dp = descriptor + i;
        i += dsize;

        if (btype == TYPE_GLOBAL) {
            const std::int32_t  sv = read_signed(dp, dsize);
            const std::uint32_t uv = read_unsigned(dp, dsize);
            switch (btag) {
                case TAG_USAGE_PAGE:   g.usage_page  = uv; break;
                case TAG_LOG_MIN:      g.logical_min = sv; break;
                case TAG_LOG_MAX:      g.logical_max = sv; break;
                case TAG_REPORT_SIZE:  g.report_size = uv; break;
                case TAG_REPORT_COUNT: g.report_count = uv; break;
                case TAG_REPORT_ID:
                    g.has_report_id = true;
                    g.report_id = static_cast<std::uint8_t>(uv);
                    // A new Report ID resets the cursor within that report.
                    if (rep.report_id != g.report_id ||
                        rep.has_report_id != g.has_report_id) {
                        // Commit anything from the prior report first.
                        commit_report_if_complete();
                        rep = PerReport{};
                        rep.has_report_id = g.has_report_id;
                        rep.report_id = g.report_id;
                    }
                    break;
                case TAG_PUSH:         push_stack.push_back(g); break;
                case TAG_POP:
                    if (!push_stack.empty()) {
                        g = push_stack.back();
                        push_stack.pop_back();
                    }
                    break;
                default: break;
            }
        } else if (btype == TYPE_LOCAL) {
            const std::uint32_t uv = read_unsigned(dp, dsize);
            switch (btag) {
                case TAG_USAGE:     usages.push_back(uv); break;
                case TAG_USAGE_MIN: usage_min = uv; usage_min_set = true; break;
                case TAG_USAGE_MAX:
                    usage_max = uv;
                    usage_max_set = true;
                    if (usage_min_set) {
                        // Expand the range into individual usages so the
                        // Input handler can pull one per field.
                        for (std::int32_t u = usage_min; u <= usage_max; ++u) {
                            usages.push_back(static_cast<std::uint32_t>(u));
                        }
                        usage_min_set = usage_max_set = false;
                    }
                    break;
                default: break;
            }
        } else if (btype == TYPE_MAIN) {
            const std::uint32_t flags = read_unsigned(dp, dsize);
            if (btag == TAG_COLLECTION) {
                std::uint32_t coll_usage = usages.empty() ? 0 : usages.front();
                coll_usages.push_back(coll_usage);
                if (g.usage_page == UP_GENERIC_DESKTOP &&
                    (coll_usage == USAGE_MOUSE || coll_usage == USAGE_POINTER)) {
                    in_mouse_collection = true;
                }
                flush_locals();
            } else if (btag == TAG_END_COLLEC) {
                if (!coll_usages.empty()) coll_usages.pop_back();
                if (coll_usages.empty()) {
                    commit_report_if_complete();
                    if (in_mouse_collection && result) return result;
                    in_mouse_collection = false;
                }
                flush_locals();
            } else if (btag == TAG_INPUT) {
                if (in_mouse_collection) {
                    process_input(g, usages, flags, rep);
                } else {
                    rep.bit_offset += g.report_size * g.report_count;
                }
                flush_locals();
            } else {
                // Output / Feature: do not contribute to Input layout but
                // consume Local state per spec.
                flush_locals();
            }
        }
    }

    commit_report_if_complete();
    return result;
}

BpfDecision validate_for_bpf(const MouseDescriptor& d)
{
    BpfDecision out;
    auto reject = [&](const char* why) {
        out.reject = BpfRejection{why};
    };

    if (!d.x.present || !d.y.present) { reject("X or Y axis missing"); return out; }
    if (d.x.bit_size != 8 && d.x.bit_size != 16) { reject("X is not 8 or 16 bits"); return out; }
    if (d.y.bit_size != 8 && d.y.bit_size != 16) { reject("Y is not 8 or 16 bits"); return out; }
    if (d.x.bit_offset_in_payload % 8 != 0) { reject("X is not byte-aligned"); return out; }
    if (d.y.bit_offset_in_payload % 8 != 0) { reject("Y is not byte-aligned"); return out; }
    if (!d.x.is_signed) { reject("X is unsigned"); return out; }
    if (!d.y.is_signed) { reject("Y is unsigned"); return out; }

    BpfMouseLayout layout{};
    const std::uint8_t prefix = d.has_report_id ? 1 : 0;
    layout.report_id      = d.has_report_id ? d.report_id : 0;
    layout.dx_byte_offset = static_cast<std::uint8_t>(prefix + d.x.bit_offset_in_payload / 8);
    layout.dx_byte_size   = static_cast<std::uint8_t>(d.x.bit_size / 8);
    layout.dy_byte_offset = static_cast<std::uint8_t>(prefix + d.y.bit_offset_in_payload / 8);
    layout.dy_byte_size   = static_cast<std::uint8_t>(d.y.bit_size / 8);
    out.layout = layout;
    return out;
}

} // namespace rawaccel_agent
