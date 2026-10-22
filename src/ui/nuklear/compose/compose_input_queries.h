#pragma once

#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/compose/compose_area_menu.h"
#include "ui/nuklear/compose/compose_recipe_menu.h"
#include "ui/nuklear/compose/compose_note_menu.h"
#include "ui/nuklear/compose/compose_inspector.h"

#include <algorithm>
#include <array>

// Borrow live state: callers query again after handlers that can acquire input.
struct compose_input_queries_t {
    const worm_ui_nuklear_compose_state_t& state;

    auto is_canvas_input_blocked() const -> bool {
        return is_canvas_popup_blocked() || has_bookmark_rename();
    }

    auto is_canvas_hover_blocked() const -> bool {
        return has_canvas_editor_input() || has_gesture_input(canvas_consumer_t::hover);
    }

    auto is_paint_blocked() const -> bool {
        return has_canvas_editor_input() || has_gesture_input(canvas_consumer_t::paint);
    }

    auto has_popup_input() const -> bool {
        return is_canvas_popup_blocked() || compose_area_menu_open() ||
               compose_area_menu_consumed() || compose_recipe_menu_open() ||
               compose_recipe_menu_consumed() || compose_note_menu_open() ||
               state.dialogs.export_dialog_open || state.dialogs.export_confirmation_dialog.open ||
               state.dialogs.compose_dialog_release_pending ||
               state.layer_editor.layer_creation_context.has_value() ||
               state.keysound_slots.keysound_slot_confirmation_open ||
               state.keysound_slots.keysound_slot_batch.open ||
               state.keysound_filters.is_popup_open() ||
               state.keysound_filters.keysound_filter_release_pending;
    }

    auto is_editor_modal_blocked() const -> bool {
        return has_popup_input() || has_inline_editor_input() || has_bookmark_rename();
    }

    auto is_context_invocation_blocked() const -> bool {
        return has_popup_input() || has_inline_editor_input();
    }

private:
    auto is_canvas_popup_blocked() const -> bool {
        return compose_quantize_owns_input() || state.keysound_catalog.keysound_group_review.open ||
            wormedit::ui::chrome::context_menu_owns_input(state.keysound_catalog.keysound_group_menu) ||
            wormedit::ui::chrome::context_menu_owns_input(state.layer_editor.layers_menu) ||
            state.has_palette_context_menu() ||
            state.bga_interaction.bga_stack_open || state.bga_interaction.bga_stack_release_pending ||
            state.bga_catalog.bga_renumber_confirmation_open || state.bga_catalog.bga_renumber_release_pending ||
            compose_note_menu_consumed() ||
            worm_ui_nuklear_bookmark_chooser_owns_input(state.bookmark_ui.bookmark_chooser) ||
            compose_sound_picker_owns_input(state.sound_picker_ui.sound_picker);
    }

    auto has_inline_editor_input() const -> bool {
        return state.inline_timing.inline_timing_open || state.inline_offset.inline_offset_open ||
            state.dialogs.go_to_measure_open;
    }

    auto has_bookmark_rename() const -> bool {
        return state.bookmark_ui.bookmark_rename.active || state.bookmark_ui.bookmark_rename.release_pending;
    }

    auto has_canvas_editor_input() const -> bool {
        return state.inline_timing.inline_timing_open || has_bookmark_rename();
    }

    enum class canvas_consumer_t { hover, paint };

    auto has_gesture_input(canvas_consumer_t consumer) const -> bool {
        struct participation_t {
            bool active;
            bool hover;
            bool paint;
            auto operator==(const participation_t&) const -> bool = default;
        };
        // One declaration per capture flag that reserves either consumer. Other
        // captures keep their ordered handler's admission policy. Rebuild on each
        // query: an earlier handler may have acquired or released a capture.
        const auto participation = std::to_array<participation_t>({
            {.active = state.pointer_capture.note_paint_pointer_active, .hover = true, .paint = true},
            {.active = state.bmson_input.bmson_razor_pointer_active, .hover = true, .paint = true},
            {.active = state.bmson_input.bmson_marker_move_active, .hover = true, .paint = true},
            {.active = state.pointer_capture.semantic_layer_paint_pointer_active, .hover = true, .paint = true},
            {.active = state.timing_drag.timing_stage_drag_active, .hover = true, .paint = true},
            {.active = state.timing_drag.kiai_drag_active, .hover = true, .paint = true},
            {.active = state.timing_drag.recipe_drag_active, .hover = true, .paint = true},
            {.active = state.pointer_capture.pointer_hold_active, .hover = true, .paint = false},
            {.active = state.pointer_capture.pointer_hold_begin_pending, .hover = true, .paint = false},
            {.active = state.range_selection.lane_hold_active, .hover = true, .paint = false},
            {.active = state.note_move.note_move_active, .hover = true, .paint = false},
            {.active = state.note_move.note_move_candidate_active, .hover = true, .paint = false},
            {.active = state.bar_line_move.active, .hover = true, .paint = false},
            {.active = state.bar_line_move.capture.active, .hover = true, .paint = false},
            {.active = state.area_selection.area_selection_active, .hover = true, .paint = false},
            {.active = state.timing_marker.timing_marker_candidate_active, .hover = true, .paint = false},
            {.active = state.timing_marker.timing_marker_drag_active, .hover = true, .paint = false},
            {.active = state.waveform_drag.waveform_beat_zero_drag_active, .hover = true, .paint = false},
            {.active = state.waveform_drag.waveform_overview_drag_active, .hover = true, .paint = false},
            {.active = state.bmson_input.bmson_marker_move_candidate_active, .hover = false, .paint = true},
        });
        return std::ranges::any_of(participation, [consumer](const auto& capture) {
            return capture.active && (consumer == canvas_consumer_t::hover ? capture.hover : capture.paint);
        });
    }
};
