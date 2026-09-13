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

// Applies one structural or definition edit through shared document history.
// Insertions preserve existing measure IDs and local positions. Deletion is
// deliberately conservative: any remaining object that names the measure makes
// the complete proposal invalid and leaves history untouched.
[[nodiscard]] auto apply_measure_edit(document_history_t& history, std::uint64_t expected_revision,
                                      const measure_edit_action_t& action,
                                      measure_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
