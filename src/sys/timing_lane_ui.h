#pragma once

#include "ui/frontend.h"
#include <wormedit/core/timing_lane_catalog.h>

namespace wormedit::sys {

template <typename Ui> struct ui_codec;

// The remaining timing-lane enum boundary. Labels and selection identities
// come from the core catalog; domain editing and geometry policies stay named.
template <> struct ui_codec<WormUiTimingLaneKind> {
    [[nodiscard]] static constexpr auto from(timing_lane_kind_t lane) -> WormUiTimingLaneKind {
        for (const auto& binding : bindings)
            if (binding.lane == lane) return binding.kind;
        return WORM_UI_TIMING_LANE_BPM; // Preserve the existing projection fallback.
    }

    [[nodiscard]] static constexpr auto into(WormUiTimingLaneKind lane)
        -> std::optional<timing_lane_kind_t> {
        for (const auto& binding : bindings)
            if (binding.kind == lane) return binding.lane;
        return std::nullopt;
    }

    [[nodiscard]] static constexpr auto descriptor(WormUiTimingLaneKind lane)
        -> const timing_lane_descriptor_t* {
        const auto kind = into(lane);
        return kind ? find_catalog_entry(timing_lanes, *kind) : nullptr;
    }

  private:
    struct binding_t {
        WormUiTimingLaneKind kind;
        timing_lane_kind_t lane;
    };
    inline static constexpr std::array bindings{
        binding_t{.kind = WORM_UI_TIMING_LANE_BPM, .lane = timing_lane_kind_t::bpm},
        binding_t{.kind = WORM_UI_TIMING_LANE_SCROLL, .lane = timing_lane_kind_t::scroll},
        binding_t{.kind = WORM_UI_TIMING_LANE_SPEED, .lane = timing_lane_kind_t::speed},
        binding_t{.kind = WORM_UI_TIMING_LANE_STOP, .lane = timing_lane_kind_t::stop},
        binding_t{.kind = WORM_UI_TIMING_LANE_DELAY, .lane = timing_lane_kind_t::delay},
        binding_t{.kind = WORM_UI_TIMING_LANE_WARP, .lane = timing_lane_kind_t::warp},
        binding_t{.kind = WORM_UI_TIMING_LANE_KIAI, .lane = timing_lane_kind_t::kiai},
        binding_t{.kind = WORM_UI_TIMING_LANE_ANCHOR, .lane = timing_lane_kind_t::anchor},
    };
    static_assert(is_catalog_exhaustive(bindings, WORM_UI_TIMING_LANE_COUNT));
    static_assert(WORM_UI_TIMING_LANE_COUNT == timing_lane_kind_count);
};

} // namespace wormedit::sys
