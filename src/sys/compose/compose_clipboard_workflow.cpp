#include "sys/compose/compose_clipboard_workflow.h"
#include <wormedit/core/command.h>
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/timing.h>
#include <wormedit/formats/bms_clipboard.h>
namespace wormedit::sys {
auto paste_anchor(compose_paste_session_t& session,
                  wormedit::local_position_t cursor) -> wormedit::local_position_t {
    if (session.pointer_cursor && *session.pointer_cursor == cursor) return session.anchor;
    session.pointer_cursor.reset();
    return cursor;
}

namespace {
// Pasted playable heads obey the same Warp rule as placed ones; fakes, mines and tails may land.
void block_paste_heads_in_warps(const wormedit::chart_document_t& chart,
                                const wormedit::measure_index_t& index,
                                wormedit::chart_paste_outcome_t& proposal) {
    for (const auto& object : proposal.value->payload.report.objects) {
        const auto* note = std::get_if<wormedit::note_t>(&object);
        const auto row = note ? index.row_of(note->position) : std::nullopt;
        if (!row || !wormedit::is_head_blocked_by_warp(chart, index, *row, note->kind)) continue;
        proposal.diagnostics.push_back(wormedit::chart_paste_diagnostic_t::from(
            "Playable note heads cannot sit inside a warp.", proposal.value->stamp,
            std::move(proposal.value->payload.report)));
        proposal.value.reset();
        return;
    }
}

void build_compose_paste_preview(compose_paste_session_t& session,
    const wormedit::document_history_t& history, const wormedit::chart_document_t& source,
    wormedit::local_position_t anchor) {
    const auto& document = history.document;
    session.anchor = anchor;
    session.anchor_available = true;
    session.proposal = wormedit::chart_paste_proposal_t::propose(document, source.id,
        session.clipboard, anchor, document.revision);
    if (session.proposal.value && !source.warps.empty()) {
        const auto* indices = history.indices(source.id);
        if (indices && indices->measures)
            block_paste_heads_in_warps(source, *indices->measures, session.proposal);
    }
    build_compose_paste_geometry(session, source, document.revision);
}
}
auto compose_clipboard_workflow_t::revise(
    const wormedit::document_history_t& history, wormedit::chart_id_t chart,
    wormedit::local_position_t anchor, std::string* failure) -> bool {
    const auto& document = history.document;
    auto& session = paste_session;
    if (!session) {
        if (failure) *failure = "The paste preview is stale.";
        return false;
    }
    const auto preview = wormedit::chart_paste_preview_view_t::from(session->proposal);
    if (!preview.stamp || !preview.stamp->is_current(document, chart,
            wormedit::document_identity_policy_t::revision_only)) {
        if (failure) *failure = wormedit::stale_action_message("Paste preview");
        session.reset();
        return false;
    }
    if (session->anchor == anchor && session->index) return true;
    const auto source = history.read_chart(chart);
    if (source == nullptr) {
        if (failure) *failure = "The paste preview is stale.";
        session.reset();
        return false;
    }
    build_compose_paste_preview(*session, history, *source, anchor);
    return true;
}

namespace {
// Decodes other editors' text against the lane layout at the cursor.
auto decode_external_clipboard(std::string_view text, const wormedit::song_document_t& document,
    const wormedit::chart_document_t& chart, wormedit::local_position_t cursor)
    -> wormedit::chart_clipboard_outcome_t {
    const auto* segment = wormedit::active_lane_layout_segment(chart, cursor);
    if (!segment && chart.lane_layouts.empty())
        return std::unexpected(std::string{"Choose a lane profile before pasting BMS editor data."});
    const auto& layout = segment ? segment->layout : chart.lane_layouts.front().layout;
    return wormedit::parse_bms_chart_clipboard(text, document, chart, layout);
}
} // namespace

auto compose_clipboard_workflow_t::begin(
    const wormedit::document_history_t& history, const wormedit::editor_interaction_state_t& state,
    std::optional<std::string> external, std::string& status) -> bool {
    const auto& document = history.document;
    const auto* source = history.read_chart(state.chart);
    if (!source) {
        paste_session.reset();
        status = "The paste chart is unavailable.";
        return false;
    }
    if (external && (!wormedit::is_bms_editor_clipboard_text(*external) ||
        (chart_clipboard && *external == system_text_at_copy))) external.reset();
    if ((!external && !chart_clipboard) || state.is_pending()) {
        status = "Paste requires a copied selection and no pending gesture.";
        return false;
    }
    compose_paste_session_t session;
    if (external) {
        auto decoded = decode_external_clipboard(*external, document, *source, state.cursor);
        if (!decoded) {
            status = "BMS editor clipboard was not pasted: " + decoded.error();
            return false;
        }
        session.clipboard = std::move(*decoded);
    } else {
        session.clipboard = *chart_clipboard;
    }
    build_compose_paste_preview(session, history, *source, state.cursor);
    paste_session = std::move(session);
    status = external ? "Pasting BMS editor data: click or Enter to commit; Escape to cancel."
                      : "Paste preview: click or Enter to commit; Escape to cancel.";
    return true;
}

auto compose_clipboard_workflow_t::apply(
    wormedit::document_history_t& history, wormedit::editor_interaction_state_t& state,
    std::string& status, wormedit::notification_severity_t* severity) -> bool {
    if (severity) *severity = wormedit::notification_severity_t::warning;
    const auto anchor = paste_session
        ? paste_anchor(*paste_session, state.cursor) : state.cursor;
    if (!revise(history, state.chart, anchor, &status))
        return false;
    auto& session = *paste_session;
    if (!session.anchor_available) { status = session.anchor_error; return false; }
    if (!session.proposal.value) {
        const auto preview = wormedit::chart_paste_preview_view_t::from(session.proposal);
        status = preview.diagnostic ? preview.diagnostic->message : "";
        return false;
    }
    const auto applied = wormedit::paste_chart_clipboard(history, state.chart, session.clipboard,
                                                       session.anchor);
    if (!applied.applied) {
        status = applied.diagnostics.empty() ? "Paste failed." : applied.diagnostics.front().message;
        if (severity && !applied.diagnostics.empty()) {
            *severity = wormedit::to_notification_severity(
                applied.diagnostics.front().severity, wormedit::notification_severity_t::warning);
        }
        return false;
    }
    paste_session.reset();
    status = "Selection pasted.";
    if (severity) *severity = wormedit::notification_severity_t::success;
    return true;
}

} // namespace wormedit::sys
