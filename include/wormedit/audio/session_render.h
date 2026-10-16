#pragma once

#include <wormedit/audio/transport.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <utility>

namespace wormedit::audio {

enum class session_render_status_t {
    mixed,
    priming,
    complete,
    stopped,
    invalid_buffer,
    stale_generation,
    underrun,
    transport_failed,
};

struct session_render_effect_t {
    [[nodiscard]] static auto from(session_render_status_t status,
                                   std::uint64_t first_timeline_frame = 0,
                                   const transport_mix_result_t& mix = {})
        -> result_t<session_render_effect_t, session_render_effect_t> {
        session_render_effect_t report{
            .status = status, .first_timeline_frame = first_timeline_frame, .mix = mix};
        if (report.has_render_error())
            return std::unexpected(std::move(report));
        return report;
    }
    session_render_status_t status{session_render_status_t::stopped};
    std::uint64_t first_timeline_frame{};
    transport_mix_result_t mix;

    [[nodiscard]] auto has_render_error() const noexcept -> bool {
        return status == session_render_status_t::invalid_buffer ||
               status == session_render_status_t::stale_generation ||
               status == session_render_status_t::underrun ||
               status == session_render_status_t::transport_failed;
    }
    auto operator==(const session_render_effect_t&) const -> bool = default;
};
// Temporary: removed by T-633.
using render_outcome_t = result_t<session_render_effect_t, session_render_effect_t>;

} // namespace wormedit::audio
