#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

struct note_label_edit_limits_t {
    std::size_t maximum_selected_notes{50'000};
    std::size_t maximum_label_bytes{4U * 1024U};
    std::size_t maximum_total_label_bytes{32U * 1024U * 1024U};
    std::size_t maximum_chart_objects{default_maximum_chart_objects};
};

struct set_note_label_action_t {
    chart_id_t chart;
    std::vector<object_id_t> notes;
    // nullopt clears the label. An empty string is a retained authored label.
    std::optional<std::string> label;
};

// Assigns one exact UTF-8 label to every explicitly named note, or clears it
// from all of them, as one revision-checked command. Note identities must be
// unique both in the request and chart-wide. Gameplay properties and source
// provenance are left untouched. A semantic no-op creates no history entry.
[[nodiscard]] auto
apply_note_label_edit(document_history_t& history, std::uint64_t expected_revision,
                      const set_note_label_action_t& action, note_label_edit_limits_t limits = {})
    -> document_command_result_t;

// Notes whose label contains `text`, compared ASCII case-insensitively, in chart
// order (measure order, then row, then id). An empty search matches nothing.
// Never mutates the document.
[[nodiscard]] auto find_notes_by_label(const chart_document_t& chart, std::string_view text)
    -> std::vector<object_id_t>;

} // namespace wormedit
