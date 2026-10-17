#pragma once

#include <wormedit/core/interaction_state.h>
#include <wormedit/core/proposal.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit {

// Offsets and durations are measured in standard measures, never rounded rows.
// Payloads retain labels, resources, layers, branches and source provenance.
// Timing objects ride along with notes: a warp's duration is its span, every other timing object
// has a zero duration and lane index 0.
using chart_clipboard_payload_t =
    std::variant<note_t, bgm_event_t, bpm_event_t, pause_event_t, warp_event_t, scroll_event_t,
                 speed_event_t, kiai_event_t, timing_anchor_t>;

// A BMSON stem point: the retained sound-channel event a semantic note or BGM marker stands for.
// Pasting creates a fresh retained event in the same channel (by ID) with the same flags.
struct chart_clipboard_stem_t {
    object_id_t channel;
    bool continuation{false};
    bool playable{true};
};

struct chart_clipboard_object_t {
    rational_t offset;
    rational_t duration;
    std::size_t lane_index{};
    chart_clipboard_payload_t payload;
    // Empty for ordinary objects, so older payloads keep their meaning.
    std::optional<chart_clipboard_stem_t> stem;
};

struct chart_clipboard_t {
    [[nodiscard]] static auto from(const song_document_t& document,
                                   const editor_interaction_state_t& state,
                                   std::uint64_t expected_revision, object_id_t kiai_head = {})
        -> result_t<chart_clipboard_t, std::string>;

    song_id_t source_song;
    chart_id_t source_chart;
    std::vector<chart_clipboard_object_t> objects;
    // Copied bar lines, as offsets from the same anchor as the objects. Pasting splits the measure
    // each lands in without moving any object; empty in older payloads and other editors' text.
    std::vector<rational_t> bar_lines;
};

using chart_clipboard_outcome_t = result_t<chart_clipboard_t, std::string>;

struct selection_delete_options_t {
    object_id_t kiai_head;
    recipe_id_t recipe;
    bool timing_locked{};
    std::string label{"Delete selection"};
};

// Stage all selected families, then publish once. Failure preserves history and selection.
[[nodiscard]] auto delete_selection_all(document_history_t& history,
    editor_interaction_state_t& state,
    const selection_delete_options_t& options = {}) -> editor_interaction_result_t;

[[nodiscard]] auto create_cut_chart_clipboard(document_history_t& history,
    editor_interaction_state_t& state,
    selection_delete_options_t options = {}) -> chart_clipboard_outcome_t;

struct chart_paste_stem_event_t {
    object_id_t channel;
    sound_channel_event_t event;
};

// Rejected candidates retain only read-only geometry/counts for the ghost preview.
struct chart_paste_report_t {
    std::vector<chart_clipboard_payload_t> objects;
    // Rows from the start of the song where the paste adds a bar line; object positions above
    // already name the measures those splits create.
    std::vector<std::uint64_t> bar_line_rows;
    std::size_t missing_lane_count{};
    auto operator==(const chart_paste_report_t&) const -> bool = default;
};

struct chart_paste_diagnostic_t : diagnostic_t {
    chart_stamp_t stamp;
    chart_paste_report_t report;
    auto operator==(const chart_paste_diagnostic_t&) const -> bool = default;

    [[nodiscard]] static auto from(std::string message, chart_stamp_t stamp,
                                   chart_paste_report_t report) -> chart_paste_diagnostic_t {
        return {error(diagnostic_code_t::collision, std::move(message)), stamp, std::move(report)};
    }
};

struct chart_paste_proposal_t;
using chart_paste_diagnostics_t = std::vector<chart_paste_diagnostic_t>;
using chart_paste_outcome_t =
    proposal_outcome_t<chart_paste_proposal_t, chart_stamp_t, chart_paste_diagnostics_t>;

struct chart_paste_proposal_t {
    static constexpr proposal_capture_policy_t apply_policy{
        .stale_label = "Chart paste proposal", .identity = document_identity_policy_t::revision_only};
    static constexpr proposal_capture_policy_t revision_policy{
        .stale_label = apply_policy.stale_label, .identity = apply_policy.identity,
        .stale_code = diagnostic_code_t::collision, .uses_current_project_diagnostics = true};
    [[nodiscard]] static auto from(const proposal_read_scope_t<chart_stamp_t>&,
        const chart_clipboard_t&, local_position_t anchor)
        -> result_t<chart_paste_proposal_t, chart_paste_diagnostic_t>;
    // Consumes a revision-bound object batch and publishes it once.
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>& scope) &&
        -> diagnosed_t<std::optional<std::string>>;
    [[nodiscard]] static auto propose(const song_document_t& document, chart_id_t chart,
        const chart_clipboard_t& clipboard, local_position_t anchor,
        std::uint64_t expected_revision) -> chart_paste_outcome_t;
    [[nodiscard]] auto revise(const proposal_read_scope_t<chart_stamp_t>&,
        const chart_clipboard_t& clipboard, local_position_t anchor) const -> chart_paste_outcome_t;

    chart_paste_report_t report;
    // New retained BMSON events the stem points above are linked to.
    std::vector<chart_paste_stem_event_t> stem_events;
    auto operator==(const chart_paste_proposal_t&) const -> bool = default;
};

static_assert(staged_proposal<chart_paste_proposal_t,
    std::tuple<const song_document_t&, chart_id_t, const chart_clipboard_t&, local_position_t, std::uint64_t>,
    std::tuple<const proposal_read_scope_t<chart_stamp_t>&, const chart_clipboard_t&, local_position_t>,
    chart_stamp_t, chart_paste_diagnostics_t>);

using chart_paste_preview_view_t =
    proposal_preview_view_t<chart_paste_proposal_t, chart_stamp_t, chart_paste_diagnostics_t>;

[[nodiscard]] auto paste_chart_clipboard(document_history_t& history,
    chart_id_t chart, const chart_clipboard_t& clipboard, local_position_t anchor) -> document_command_result_t;

} // namespace wormedit
