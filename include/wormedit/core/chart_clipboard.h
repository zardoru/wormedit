#pragma once

#include <wormedit/core/interaction.h>

#include <optional>
#include <string>
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
    song_id_t source_song;
    chart_id_t source_chart;
    std::vector<chart_clipboard_object_t> objects;
};

struct chart_clipboard_result_t {
    std::optional<chart_clipboard_t> clipboard;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return clipboard.has_value(); }
};

[[nodiscard]] auto copy_chart_selection(const song_document_t& document,
    const editor_interaction_state_t& state, std::uint64_t expected_revision,
    object_id_t kiai_head = {}) -> chart_clipboard_result_t;

struct selection_delete_options_t {
    object_id_t kiai_head;
    recipe_id_t recipe;
    bool timing_locked{};
    std::string label{"Delete selection"};
};

// Stage all selected families, then publish once. Failure preserves history and selection.
[[nodiscard]] auto delete_selection_all(document_history_t& history,
    editor_interaction_state_t& state, std::uint64_t expected_revision,
    const selection_delete_options_t& options = {}) -> editor_interaction_result_t;

[[nodiscard]] auto cut_chart_selection(document_history_t& history,
    editor_interaction_state_t& state, std::uint64_t expected_revision,
    selection_delete_options_t options = {}) -> chart_clipboard_result_t;

struct chart_paste_stem_event_t {
    object_id_t channel;
    sound_channel_event_t event;
};

struct chart_paste_proposal_t {
    chart_id_t chart;
    std::uint64_t revision{};
    std::vector<chart_clipboard_payload_t> objects;
    // New retained BMSON events the stem points above are linked to.
    std::vector<chart_paste_stem_event_t> stem_events;
    bool valid{};
    std::size_t missing_lane_count{};
    std::string error;
};

[[nodiscard]] auto propose_chart_paste(const song_document_t& document,
    chart_id_t chart, const chart_clipboard_t& clipboard, local_position_t anchor,
    std::uint64_t expected_revision) -> chart_paste_proposal_t;

[[nodiscard]] auto paste_chart_clipboard(document_history_t& history,
    chart_id_t chart, const chart_clipboard_t& clipboard, local_position_t anchor,
    std::uint64_t expected_revision) -> document_command_result_t;

} // namespace wormedit
