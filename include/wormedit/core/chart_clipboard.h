#pragma once

#include <wormedit/core/interaction.h>
#include <wormedit/core/result.h>

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

struct chart_paste_proposal_t {
    chart_stamp_t stamp;
    std::vector<chart_clipboard_payload_t> objects;
    // New retained BMSON events the stem points above are linked to.
    std::vector<chart_paste_stem_event_t> stem_events;
    std::size_t missing_lane_count{};
    diagnostic_list_t diagnostics;

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return static_cast<bool>(stamp.chart) && !objects.empty() && diagnostics.empty();
    }
    [[nodiscard]] auto message() const noexcept -> std::string_view {
        return diagnostics.empty() ? std::string_view{""} : std::string_view{diagnostics.front().message};
    }
    void reject(std::string message) & {
        diagnostics.clear();
        diagnostics.push_back(wormedit::error(diagnostic_code_t::collision, std::move(message)));
    }
    [[nodiscard]] auto reject(std::string message) && -> chart_paste_proposal_t {
        this->reject(std::move(message));
        return std::move(*this);
    }
};

[[nodiscard]] auto propose_chart_paste(const song_document_t& document, chart_id_t chart,
                                       const chart_clipboard_t& clipboard, local_position_t anchor,
                                       std::uint64_t expected_revision) -> chart_paste_proposal_t;

// Consumes a revision-bound object batch and publishes it once.
[[nodiscard]] auto apply_chart_paste(document_history_t& history, chart_paste_proposal_t&& proposal)
    -> document_command_result_t;

[[nodiscard]] auto paste_chart_clipboard(document_history_t& history,
    chart_id_t chart, const chart_clipboard_t& clipboard, local_position_t anchor) -> document_command_result_t;

} // namespace wormedit
