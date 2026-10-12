#pragma once

#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_pointer_chords.h"

#include <array>

inline constexpr std::array<wormedit::signed_rational_t, 6> beat_fit_snap_steps{{
    {1, 1}, {3, 4}, {1, 2}, {1, 3}, {1, 4}, {1, 10},
}};

void cancel_compose_navigation(WormUiWorkspaceAction* action);
void append_compose_cancel_once(WormUiWorkspaceAction* action);
bool compose_text_owned(const nk_context* context);

// Razor slicing needs a selected BGM source lane and no competing tool. Input handlers enforce this
// themselves, so it holds whether or not the BMSON tab that shows the toggle is drawn.
inline auto bmson_razor_eligible(const WormUiComposeView& view) -> bool {
    return view.scene_available && view.selected_bmson_bgm_event_id != 0 &&
           view.selected_bmson_source_lane_id != 0 && !view.note_paint_mode_active &&
           !view.semantic_layer_paint_mode_active && !view.timing_stage_active;
}

void reset_bmson_marker_move_capture();
void set_waveform_offset_drag_action(WormUiWorkspaceAction* action,
                                    WormUiAudioOffsetDragAction kind,
                                    std::int64_t delta_frames);
void reset_waveform_offset_drag();
void cancel_beat_fit_drag(WormUiWorkspaceAction* action);
bool compose_shortcut_modifier(std::uint16_t modifiers);
void cancel_offset_fit_drag(WormUiWorkspaceAction* action);
void append_beat_fit_input(WormUiWorkspaceAction* action,
                           WormUiComposeInputKind kind, double y, bool ctrl_held, bool onset_snap_held = false);
auto compose_timing_target(const WormUiComposeView& view, double screen_y)
    -> WormUiTimingTarget;
bool compose_point_hits_note(const WormUiComposeView* view, double x, double y);
// Starts a note paint stroke at the press position, with the armed Sounds sample if any.
void append_note_paint_begin(const ComposePointerEventContext& input);
std::uint64_t compose_bgm_event_at(const WormUiComposeView* view, double x, double y);
std::uint64_t compose_bga_event_at(const WormUiComposeView* view, double x, double y);
auto compose_bga_lane_kind_at(const WormUiComposeView& view, double x, double y)
    -> std::optional<std::uint8_t>;
bool compose_point_hits_selected_note(const WormUiComposeView* view, double x, double y);
WormUiComposeSelectionMode compose_selection_mode(std::uint16_t modifiers);

bool process_draw_right_pointer(const ComposePointerEventContext& input);
