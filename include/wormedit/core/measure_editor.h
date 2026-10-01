#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace wormedit {

struct measure_edit_limits_t {
    std::size_t maximum_measures{1'000'000};
    std::uint64_t maximum_rows_per_measure{100'000'000};
};

struct insert_measure_action_t {
    chart_id_t chart;
    std::size_t before_index{};
    measure_definition_t definition{{1, 1}, meter_t{4, 4}};
};

struct update_measure_definition_action_t {
    chart_id_t chart;
    measure_id_t measure;
    measure_definition_t definition;
};

struct delete_measure_action_t {
    chart_id_t chart;
    measure_id_t measure;
};

using measure_edit_action_t =
    std::variant<insert_measure_action_t, update_measure_definition_action_t,
                 delete_measure_action_t>;

// Append default 4/4 measures through the source audio's end using the chart's
// final BPM and exact existing timing. Existing IDs and positions stay intact.
// Unknown stream lengths are a no-op; invalid or excessive extents fail before
// modifying the chart. Callers commit the resulting draft through history.
[[nodiscard]] auto extend_chart_to_audio_end(chart_document_t& chart,
                                             const audio_stream_info_t& stream,
                                             measure_edit_limits_t limits = {}) -> bool;

// Extend native charts to the longest linked music/BGM source after a timing
// edit. Apply to a document draft so a failed extent rejects the whole command.
[[nodiscard]] auto extend_native_charts_to_audio_end(song_document_t& document) -> bool;

// Applies one structural or definition edit through shared document history.
// Insertions preserve existing measure IDs and local positions. Deletion is
// deliberately conservative: any remaining object that names the measure makes
// the complete proposal invalid and leaves history untouched.
[[nodiscard]] auto apply_measure_edit(document_history_t& history, std::uint64_t expected_revision,
                                      const measure_edit_action_t& action,
                                      measure_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
