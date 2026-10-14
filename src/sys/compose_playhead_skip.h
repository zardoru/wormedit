#pragma once

#include "sys/bookmark_commands.h"
#include "sys/compose_view.h"

#include <cstdint>
#include <optional>

namespace wormedit::sys {

enum class playhead_skip_unit_t { snap, beat, measure, page, rows };

// Target row of a playhead skip while playing. One snap goes to the next or previous active-snap
// row strictly past the playhead; one beat uses quarter-note boundaries independently of snap.
// Fractional boundaries round to an integer row, with at least one row of progress. One measure keeps the playhead's offset in the neighboring
// measure, clamped to that measure's length; rows moves by a rounded row count.
// Page uses measure stepping with a two-beat minimum per measure requested. `amount` is
// signed (negative goes back) and the result is clamped to the chart's rows.
[[nodiscard]] auto playhead_skip_target_row(const measure_index_t& measures, double playhead_row,
                                            playhead_skip_unit_t unit, double amount,
                                            std::uint64_t divisions_per_measure,
                                            std::uint64_t snap_denominator)
    -> std::optional<std::uint64_t>;

// Moves the playing preview to the skip target with a single seek. Playback continues from the
// new point. A failed seek keeps playing from the prior position and reports the seek message.
[[nodiscard]] auto skip_playing_playhead(application_command_state_t& application,
                                         const chart_document_t& chart,
                                         std::uint64_t snap_denominator, playhead_skip_unit_t unit,
                                         double amount) -> compose_action_outcome_t;

[[nodiscard]] auto is_preview_playing(const application_command_state_t& application) -> bool;

} // namespace wormedit::sys
