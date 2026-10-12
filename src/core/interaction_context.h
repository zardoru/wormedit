#pragma once

#include <wormedit/core/interaction.h>

namespace wormedit {

// Synchronous dispatch references only. Nested mutations must rebuild the scope.
struct chart_scope_t {
    const chart_document_t& chart;
    const measure_index_t& measures;
};

struct editor_edit_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    chart_scope_t scope;
};

using editor_initial_view_t = std::variant<editor_interaction_view_model_t, diagnostic_list_t>;

} // namespace wormedit
