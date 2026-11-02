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

    std::optional<document_stamp_t> gesture_capture{};

    // Dispatched gestures defer feedback until the outer action boundary has finished cleanup.
    void ensure_failure_view(editor_interaction_result_t& result) const {
        if (!dispatch_capture_revision) result.ensure_view(history, state);
    }

    [[nodiscard]] auto capture_stamp() const -> document_stamp_t {
        auto stamp = gesture_capture.value_or(chart_stamp_t::from(history, state.chart).document);
        stamp.revision = history.document.revision;
        if (!stamp.bms_target) stamp = document_stamp_t::from(stamp.revision);
        return stamp;
    }
    [[nodiscard]] auto result() const -> editor_interaction_result_t {
        return editor_interaction_result_t::from(history, state);
    }

    [[nodiscard]] auto has_validated_capture() const noexcept -> bool {
        return dispatch_capture_revision == history.document.revision;
    }
};

// Song-owned gestures share admission, capture and publication with chart
// gestures, without borrowing chart geometry or constructing a chart view.
struct editor_document_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    std::optional<std::uint64_t> dispatch_capture_revision{};
    std::optional<document_stamp_t> gesture_capture{};

    [[nodiscard]] auto capture_stamp() const -> document_stamp_t {
        return gesture_capture.value_or(document_stamp_t::from(history.document));
    }
    [[nodiscard]] auto has_validated_capture() const noexcept -> bool {
        return dispatch_capture_revision == history.document.revision;
    }
    [[nodiscard]] auto result() const -> editor_interaction_result_t { return {.accepted = true}; }
    // There is no chart projection for song-owned actions. Diagnostics remain
    // on the result; the document surface builds its own failure projection.
    void ensure_failure_view(editor_interaction_result_t&) const {}
};

template<class Context>
concept editor_gesture_context = std::same_as<Context, editor_edit_context_t> ||
    std::same_as<Context, editor_document_context_t>;

using editor_initial_view_t = std::variant<editor_interaction_view_model_t, diagnostic_list_t>;

} // namespace wormedit
