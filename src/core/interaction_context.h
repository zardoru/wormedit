#pragma once

#include <wormedit/core/interaction_state.h>

#include <cstdint>
#include <optional>

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
    // Borrowed only during dispatch; view-dependent gestures never store this pointer.
    const editor_interaction_view_model_t* initial_view{};
    // Only dispatch supplies this boundary result. Direct callers remain uncached.
    std::optional<std::uint64_t> dispatch_capture_revision{};

    // Dispatched gestures defer feedback until the outer action boundary has finished cleanup.
    void ensure_failure_view(editor_interaction_result_t& result) const {
        if (!dispatch_capture_revision) result.ensure_view(history, state);
    }

    [[nodiscard]] auto has_validated_capture() const noexcept -> bool {
        return dispatch_capture_revision == history.document.revision;
    }
};

using editor_initial_view_t = std::variant<editor_interaction_view_model_t, diagnostic_list_t>;

} // namespace wormedit
