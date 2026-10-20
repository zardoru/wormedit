#pragma once

// Editor interaction state, its view model, and the setup and result types.

#include <wormedit/core/interaction_gestures.h>
#include <wormedit/core/audition_request.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/lane_presentation.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {

struct editor_interaction_state_t {
    // Creates transient, frontend-independent editing state from a validated
    // history snapshot. Snap denominators are ordered by editor preference.
    [[nodiscard]] static auto
    from(const document_history_t& history, chart_id_t chart, lane_id_t active_lane,
         local_position_t cursor, std::span<const std::uint64_t> ordered_snap_denominators,
         std::uint64_t requested_snap_denominator, double viewport_height_pixels = 1.0)
        -> editor_interaction_setup_t;

    chart_id_t chart;
    lane_id_t active_lane;
    editor_scratch_presentation_t scratch_on_right;
    note_kind_t armed_note_kind{note_kind_t::tap};
    // An explicit palette target overrides sound defaults; an empty vector means No layer.
    std::optional<std::vector<layer_id_t>> armed_note_layers;
    std::optional<osu_hitsample_t> armed_hitsample;
    bool hitsound_palette_visible{};
    [[nodiscard]] auto has_hitsound_palette(format_family_t source) const -> bool {
        return hitsound_palette_visible || source == format_family_t::osu_mania ||
               source == format_family_t::quaver;
    }
    // How a moved bar line treats the content after it. Never persisted: propagation is only ever
    // an explicit request, so every new editing session starts by keeping song time.
    bar_line_time_t bar_line_time{bar_line_time_t::keep_song_time};
    local_position_t cursor;
    std::optional<std::uint64_t> virtual_bms_cursor_row;
    // Non-authoring signed offset, in chart rows, from `cursor` to where it is drawn: set while
    // following audio or scrolling with a fixed cursor anchor; other cursor moves clear it.
    double cursor_subrow{};
    std::vector<std::uint64_t> supported_snap_denominators;
    std::uint64_t active_snap_denominator{};
    bool active_snap_was_fallback{};
    double pixels_per_standard_measure{default_pixels_per_standard_measure};
    double viewport_scroll_pixels{};
    // Non-authoring bound retained after a configured playback anchor.
    double viewport_minimum_margin_fraction{editor_cursor_edge_margin_fraction};
    double viewport_end_margin_fraction{editor_cursor_edge_margin_fraction};
    double viewport_height_pixels{1.0};
    std::optional<double> fixed_cursor_screen_y_pixels;
    editor_field_scroll_direction_t scroll_direction{editor_field_scroll_direction_t::up};
    std::optional<active_gesture_t> active_gesture;

    template<class Gesture> [[nodiscard]] auto gesture() -> Gesture* {
        return gesture_payload<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    template<class Gesture> [[nodiscard]] auto gesture() const -> const Gesture* {
        return gesture_payload<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    template<class Gesture> [[nodiscard]] auto gesture_stamp() const -> const document_stamp_t* {
        return wormedit::gesture_stamp<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    [[nodiscard]] auto hold() -> pending_hold_t* {
        return hold_payload(active_gesture ? &*active_gesture : nullptr);
    }
    [[nodiscard]] auto hold() const -> const pending_hold_t* {
        return hold_payload(active_gesture ? &*active_gesture : nullptr);
    }
    [[nodiscard]] auto additional_keyboard_holds() const -> std::span<const pending_hold_t> {
        return additional_hold_payload(active_gesture ? &*active_gesture : nullptr);
    }
    [[nodiscard]] auto cursor_canvas_row(const measure_index_t& index) const -> std::optional<std::uint64_t> {
        return virtual_bms_cursor_row ? virtual_bms_cursor_row : index.row_of(cursor);
    }
    [[nodiscard]] auto is_pending() const -> bool { return active_gesture.has_value(); }
    void clear() { active_gesture.reset(); }
    [[nodiscard]] auto is_stale(std::uint64_t revision) const -> bool {
        return active_gesture && std::visit([revision](const auto& proposal) {
            return !proposal.stamp.is_revision_current(revision);
        }, *active_gesture);
    }
    [[nodiscard]] auto gesture_kind() const -> std::optional<editor_gesture_kind_t> {
        if (!active_gesture) return std::nullopt;
        return std::visit([](const auto& proposal) { return proposal.payload.kind; }, *active_gesture);
    }
    // The most recently committed non-empty area/Space interval. This is
    // controller state rather than renderer state so marker authoring and
    // future frontends can consume the same exact chart span.
    std::optional<position_span_t> retained_selection_range;
    std::optional<canvas_row_span_t> retained_canvas_selection_range;
    std::optional<std::size_t> selected_canvas_measure;
    std::vector<object_id_t> selected_notes;
    std::vector<editor_selection_object_t> selected_objects;
    // Document revision the selection was last checked against (see prune_stale_selection).
    std::optional<std::uint64_t> selection_checked_revision;
    // Edit snapping applies to every pointer-driven object class; while off, edits land on the
    // chart's finest row instead of the active grid (see effective_edit_snap_denominator).
    bool snap_enabled{true};
    // Y-lock (as in iBMSC and BMSE): pointer drags of notes change lanes only and never rows.
    bool y_lock{false};
    // View-only BGA event lane visibility; hidden lanes have no hit geometry.
    bool bga_lane_visible{true};
    std::uint8_t bga_revealed_lane_mask{};
};

// The denominator pointer-driven edits of any object class should snap to.
[[nodiscard]] inline auto edit_snap_denominator(const chart_document_t& chart,
                                                const editor_interaction_state_t& state) noexcept
    -> std::uint64_t {
    return effective_edit_snap_denominator(chart, state.active_snap_denominator, state.snap_enabled);
}

struct editor_interaction_view_model_t {
    chart_id_t chart;
    lane_id_t active_lane;
    editor_scratch_presentation_t scratch_on_right;
    std::string active_lane_name;
    local_position_t cursor;
    std::optional<std::uint64_t> virtual_bms_cursor_row;
    double cursor_subrow{};
    std::uint64_t active_snap_denominator{};
    bool snap_enabled{true};
    bool y_lock{false};
    bool bga_lane_visible{true};
    std::uint8_t bga_revealed_lane_mask{};
    std::vector<std::uint64_t> supported_snap_denominators;
    std::uint64_t document_revision{};
    double pixels_per_standard_measure{default_pixels_per_standard_measure};
    double viewport_scroll_pixels{};
    double viewport_height_pixels{1.0};
    double viewport_minimum_margin_fraction{editor_cursor_edge_margin_fraction};
    double viewport_end_margin_fraction{editor_cursor_edge_margin_fraction};
    std::optional<double> fixed_cursor_screen_y_pixels;
    editor_field_scroll_direction_t scroll_direction{editor_field_scroll_direction_t::up};
    struct virtual_hold_preview_t {
        std::uint64_t anchor_row{};
        std::uint64_t end_row{};
        std::uint64_t head_row{};
        std::optional<std::uint64_t> tail_row;
        lane_id_t lane;
        std::uint64_t document_revision{};
        std::shared_ptr<const std::vector<object_id_t>> removals;
        bool adds_note{};
    };
    std::optional<virtual_hold_preview_t> pending_virtual_hold;
    std::vector<virtual_hold_preview_t> pending_canvas_keyboard_holds;
    std::optional<active_gesture_preview_t> active_gesture;

    template<class Gesture> [[nodiscard]] auto gesture() -> Gesture* {
        return gesture_payload<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    template<class Gesture> [[nodiscard]] auto gesture() const -> const Gesture* {
        return gesture_payload<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    template<class Gesture> [[nodiscard]] auto gesture_stamp() const -> const document_stamp_t* {
        return wormedit::gesture_stamp<Gesture>(active_gesture ? &*active_gesture : nullptr);
    }
    [[nodiscard]] auto hold() -> pending_hold_t* {
        return hold_payload(active_gesture ? &*active_gesture : nullptr);
    }
    [[nodiscard]] auto hold() const -> const pending_hold_t* {
        return hold_payload(active_gesture ? &*active_gesture : nullptr);
    }
    [[nodiscard]] auto additional_keyboard_holds() const -> std::span<const pending_hold_t> {
        return additional_hold_payload(active_gesture ? &*active_gesture : nullptr);
    }
    std::optional<position_span_t> retained_selection_range;
    std::optional<canvas_row_span_t> retained_canvas_selection_range;
    std::optional<std::size_t> selected_canvas_measure;
    std::vector<object_id_t> selected_notes;
    std::vector<editor_selection_object_t> selected_objects;
    bool can_move_backward{};
    bool can_move_forward{};
};

struct editor_interaction_setup_t {
    std::optional<editor_interaction_state_t> state;
    std::optional<editor_interaction_view_model_t> view;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return state.has_value() && view.has_value();
    }
};

struct editor_interaction_result_t {
    [[nodiscard]] static auto from(const document_history_t& history,
        const editor_interaction_state_t& state, bool document_changed = false)
        -> editor_interaction_result_t;
    [[nodiscard]] static auto from(
        std::variant<editor_interaction_view_model_t, diagnostic_list_t> view,
        bool document_changed = false) -> editor_interaction_result_t;

    // Attach failure feedback only after state rollback and terminal cleanup.
    void ensure_view(const document_history_t& history, const editor_interaction_state_t& state);

    bool accepted{};
    bool document_changed{};
    std::optional<editor_interaction_view_model_t> view;
    diagnostic_list_t diagnostics;
    std::optional<audition_request_t> audition;
    // Distinguishes placement from deletion/merging for advance-after-place.
    std::optional<object_id_t> created_note;

    [[nodiscard]] explicit operator bool() const noexcept {
        return accepted;
    }
};

// A borrowed measure index is valid only for this chart and document revision.
struct editor_measure_geometry_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    const measure_index_t* index{};
};

} // namespace wormedit
