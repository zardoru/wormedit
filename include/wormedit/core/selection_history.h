#pragma once

#include <wormedit/core/interaction_types.h>

namespace wormedit {
struct editor_interaction_state_t;
struct document_history_t;

struct editor_selection_snapshot_t {
    chart_id_t chart;
    std::vector<object_id_t> notes;
    std::vector<editor_selection_object_t> objects;
    std::optional<position_span_t> range;
    std::optional<canvas_row_span_t> canvas_range;
    std::optional<std::size_t> canvas_measure;
    [[nodiscard]] static auto from(const editor_interaction_state_t&) -> editor_selection_snapshot_t;
    void restore(editor_interaction_state_t&) const;
    auto operator==(const editor_selection_snapshot_t&) const -> bool = default;
};

struct editor_selection_record_t {
    std::uint64_t sequence{};
    editor_selection_snapshot_t before;
    editor_selection_snapshot_t after;
};

// Session-only history, never part of a song snapshot or its saved/dirty revision.
struct editor_selection_history_t {
    std::vector<editor_selection_record_t> undo;
    std::vector<editor_selection_record_t> redo;
    void record(document_history_t&, editor_selection_snapshot_t before,
                editor_selection_snapshot_t after);
};

struct editor_history_result_t {
    bool changed{};
    bool document_changed{};
};
[[nodiscard]] auto apply_editor_history(document_history_t&, editor_interaction_state_t&, bool undo)
    -> editor_history_result_t;
} // namespace wormedit
