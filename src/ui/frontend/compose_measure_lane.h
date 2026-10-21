#pragma once

#include <wormedit/core/vocabulary.h>

#include <algorithm>
#include <vector>

namespace wormedit::ui {

// The measure lane: the measure rail shown as a lane of selectable bar lines (grid lines with a
// measure_id). Sys fills it in place on compose_view_t each frame; the frontend reads it there.
struct compose_measure_lane_view_t {
    bool visible{};
    // What moving a bar line does to the content after it.
    bar_line_time_t bar_line_time{bar_line_time_t::keep_song_time};
    // The selected bar lines, each named by the measure it starts.
    std::vector<measure_id_t> selected_bar_lines;

    [[nodiscard]] auto is_selected(measure_id_t measure) const -> bool {
        return std::ranges::find(selected_bar_lines, measure) != selected_bar_lines.end();
    }
};

} // namespace wormedit::ui
