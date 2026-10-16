#pragma once

#include <wormedit/audio/transport.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <utility>
#include <variant>

namespace wormedit::audio {

enum class session_render_phase_t {
    mixed,
    priming,
    complete,
    stopped,
};

using session_render_state_t = std::variant<session_render_phase_t, failure_code_t>;
static_assert(sizeof(session_render_state_t) <= sizeof(std::uint64_t));

struct session_render_effect_t {
    [[nodiscard]] static auto from(session_render_state_t status,
                                   std::uint64_t first_timeline_frame = 0,
                                   const transport_mix_result_t& mix = {})
        -> result_t<session_render_effect_t, session_render_effect_t> {
        session_render_effect_t report{
            .status = std::move(status), .first_timeline_frame = first_timeline_frame, .mix = mix};
        if (report.has_render_error())
            return std::unexpected(std::move(report));
        return report;
    }
    session_render_state_t status{session_render_phase_t::stopped};
    std::uint64_t first_timeline_frame{};
    transport_mix_result_t mix;

    [[nodiscard]] auto has_render_error() const noexcept -> bool {
        return std::holds_alternative<failure_code_t>(status);
    }
    [[nodiscard]] auto is_in_phase(session_render_phase_t phase) const noexcept -> bool {
        const auto* current = std::get_if<session_render_phase_t>(&status);
        return current != nullptr && *current == phase;
    }
    [[nodiscard]] auto has_failure(failure_code_t code) const noexcept -> bool {
        const auto* current = std::get_if<failure_code_t>(&status);
        return current != nullptr && *current == code;
    }
    auto operator==(const session_render_effect_t&) const -> bool = default;
};
// Temporary: removed by T-633.
using render_outcome_t = result_t<session_render_effect_t, session_render_effect_t>;

} // namespace wormedit::audio
