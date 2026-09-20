#include <wormedit/core/timing_discontinuity.h>

namespace wormedit {

auto valid_timing_discontinuity_profile(timing_discontinuity_profile_t profile) noexcept -> bool {
    return profile == timing_discontinuity_profile_t::canonical ||
           profile == timing_discontinuity_profile_t::bms_legacy ||
           profile == timing_discontinuity_profile_t::bmson_1_0 ||
           profile == timing_discontinuity_profile_t::stepmania_5_1;
}

auto resolve_timing_discontinuity_profile(
    format_family_t source_family, std::optional<timing_discontinuity_profile_t> requested) noexcept
    -> timing_discontinuity_profile_t {
    if (requested && valid_timing_discontinuity_profile(*requested)) {
        return *requested;
    }
    switch (source_family) {
    case format_family_t::bms:
        return timing_discontinuity_profile_t::bms_legacy;
    case format_family_t::bmson:
        return timing_discontinuity_profile_t::bmson_1_0;
    case format_family_t::stepmania:
        return timing_discontinuity_profile_t::stepmania_5_1;
    default:
        return timing_discontinuity_profile_t::canonical;
    }
}

auto timing_discontinuity_order(timing_discontinuity_profile_t profile,
                                preview_discontinuity_kind_t kind) noexcept -> std::uint8_t {
    if (profile == timing_discontinuity_profile_t::bms_legacy ||
        profile == timing_discontinuity_profile_t::bmson_1_0) {
        switch (kind) {
        case preview_discontinuity_kind_t::delay:
            return 0;
        case preview_discontinuity_kind_t::playable:
        case preview_discontinuity_kind_t::bgm:
        case preview_discontinuity_kind_t::bga:
        case preview_discontinuity_kind_t::metronome:
            return 1;
        case preview_discontinuity_kind_t::bpm:
            return 2;
        case preview_discontinuity_kind_t::stop:
            return 3;
        }
    }
    switch (kind) {
    case preview_discontinuity_kind_t::bpm:
        return 0;
    case preview_discontinuity_kind_t::delay:
        return 1;
    case preview_discontinuity_kind_t::playable:
    case preview_discontinuity_kind_t::bgm:
    case preview_discontinuity_kind_t::bga:
    case preview_discontinuity_kind_t::metronome:
        return 2;
    case preview_discontinuity_kind_t::stop:
        return 3;
    }
    return 4;
}

} // namespace wormedit
