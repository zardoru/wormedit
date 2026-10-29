#pragma once
#include <wormedit/core/chart_clipboard.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/interaction_state.h>
#include <wormedit/core/notifications.h>
namespace wormedit::sys {
struct compose_paste_session_t {
    wormedit::chart_clipboard_t clipboard;
    wormedit::local_position_t anchor;
    bool anchor_available{true};
    std::string anchor_error;
    std::optional<wormedit::local_position_t> pointer_cursor;
    wormedit::chart_paste_outcome_t proposal;
    wormedit::chart_document_t geometry;
    std::optional<wormedit::editor_field_scene_index_t> index;
};
struct compose_clipboard_workflow_t {
    std::optional<wormedit::chart_clipboard_t> chart_clipboard;
    // External editor text overrides our snapshot only after it differs from this copy.
    std::string system_text_at_copy;
    std::optional<compose_paste_session_t> paste_session;
    auto begin(const wormedit::document_history_t&, const wormedit::editor_interaction_state_t&,
               std::optional<std::string> external, std::string& status) -> bool;
    auto revise(const wormedit::document_history_t&, wormedit::chart_id_t,
                wormedit::local_position_t anchor, std::string* failure = nullptr) -> bool;
    auto apply(wormedit::document_history_t&, wormedit::editor_interaction_state_t&,
               std::string& status, wormedit::notification_severity_t* severity = nullptr) -> bool;
};
auto paste_anchor(compose_paste_session_t&, wormedit::local_position_t) -> wormedit::local_position_t;
// Projection owns detached ghost geometry; workflow owns proposal/admission.
void build_compose_paste_geometry(compose_paste_session_t&, const wormedit::chart_document_t&,
                                 std::uint64_t revision);
} // namespace wormedit::sys
