#pragma once

#include <wormedit/core/notifications.h>

#include <wormedit/core/document.h>
#include <wormedit/core/interaction.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::sys {

enum class label_search_step_t { first, next, previous };

struct label_search_hit_t {
    std::vector<object_id_t> matches;
    local_position_t target;
};

// Notes whose label contains `text` and the match a step lands on: `first` is the earliest match,
// `next` the first one strictly after the cursor and `previous` the last one strictly before it,
// both wrapping around. Never mutates the chart.
[[nodiscard]] auto find_label_search_hit(const chart_document_t& chart, std::string_view text,
                                         local_position_t cursor, label_search_step_t step)
    -> std::optional<label_search_hit_t>;

// Applies a search step to the editor: `first` selects every match, all steps move the cursor
// to the landing match. Returns the status line.
[[nodiscard]] auto run_label_search(document_history_t& history, editor_interaction_state_t& state,
                                    std::string_view text, label_search_step_t step,
                                    notification_severity_t* severity = nullptr) -> std::string;

} // namespace wormedit::sys
