#pragma once

#include <wormedit/core/interaction.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {

// Offsets and durations are measured in standard measures, never rounded rows.
// Payloads retain labels, resources, layers, branches and source provenance.
struct chart_clipboard_object_t {
    rational_t offset;
    rational_t duration;
    std::size_t lane_index{};
    std::variant<note_t, bgm_event_t> payload;
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
    const editor_interaction_state_t& state, std::uint64_t expected_revision)
    -> chart_clipboard_result_t;

[[nodiscard]] auto cut_chart_selection(document_history_t& history,
    editor_interaction_state_t& state, std::uint64_t expected_revision)
    -> chart_clipboard_result_t;

struct chart_paste_proposal_t {
    chart_id_t chart;
    std::uint64_t revision{};
    std::vector<std::variant<note_t, bgm_event_t>> objects;
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
