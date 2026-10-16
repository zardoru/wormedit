#pragma once

#include <wormedit/audio/transport.h>
#include <wormedit/core/operation_state.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace wormedit::audio {

enum class session_render_phase_t {
    mixed,
    priming,
    complete,
    stopped,
};

using session_render_state_t = operation_state_t<session_render_phase_t>;
static_assert(sizeof(session_render_state_t) <= sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<session_render_state_t>);

struct session_render_effect_t {
    [[nodiscard]] static auto from(session_render_state_t status,
                                   std::uint64_t first_timeline_frame = 0,
                                   const transport_mix_result_t& mix = {})
        -> result_t<session_render_effect_t, session_render_effect_t> {
        session_render_effect_t report{
            .status = std::move(status), .first_timeline_frame = first_timeline_frame, .mix = mix};
        if (report.status.has_failure())
            return std::unexpected(std::move(report));
        return report;
    }
    session_render_state_t status{session_render_phase_t::stopped};
    std::uint64_t first_timeline_frame{};
    transport_mix_result_t mix;

    auto operator==(const session_render_effect_t&) const -> bool = default;
};
// Temporary: removed by T-633.
using render_outcome_t = result_t<session_render_effect_t, session_render_effect_t>;

} // namespace wormedit::audio
