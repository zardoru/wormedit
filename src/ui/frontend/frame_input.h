#pragma once
#include "compose.h"
#include "request_stamp.h"
#include <wormedit/core/vocabulary.h>
#include <cstdint>
#include <vector>

// What the frontend sampled this frame (keyboard, pointer, hover). Read-only for sys; not a request.
typedef enum WormUiComposeInputKind {
    WORM_UI_COMPOSE_INPUT_NONE = 0,
    WORM_UI_COMPOSE_INPUT_PASTE_UPDATE,
    WORM_UI_COMPOSE_INPUT_PASTE_COMMIT,
    WORM_UI_COMPOSE_INPUT_PASTE_CANCEL,
    WORM_UI_COMPOSE_INPUT_ARM_NOTE_KIND,
    WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD,
    WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD,
    WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE,
    WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_CHART,
    WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
    WORM_UI_COMPOSE_INPUT_MEASURE_SELECT,
    WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE,
    WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE,
    WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK,
    WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE,
    /* A click on empty space in a stem lane adds a continue (or, with Shift, restart) point at the
       snapped row, in the channel of the marker named by object_id. */
    /* The selected playable notes were dropped on the BGM area; lane_id is the BGM source lane
       under the pointer (0 for none). BMSON stem taps return to their stem's lane. */
    /* A BGM event was dropped on the playable field: lane_id is the playfield lane under the
       pointer and object_id the dragged event. It keeps its row and becomes a tap. */
    WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE,
    WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE,
    WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART,
    WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_CONTINUE,
    WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART,
    WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN,
    WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE,
    WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE,
    WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL,
    WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT,
    WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE,
    WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM,
    WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART,
    WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT,
    WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK,
    WORM_UI_COMPOSE_INPUT_DELETE_SELECTION,
    WORM_UI_COMPOSE_INPUT_UNDO,
    WORM_UI_COMPOSE_INPUT_REDO,
    WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS,
    WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE,
    WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION,
    WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE,
    WORM_UI_COMPOSE_INPUT_RESOURCE_PAINT_BEGIN,
    WORM_UI_COMPOSE_INPUT_RESOURCE_PAINT_UPDATE,
    WORM_UI_COMPOSE_INPUT_RESOURCE_PAINT_RELEASE,
    WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
    WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE,
    WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE,
    WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN,
    WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE,
    WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
    WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN,
    WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE,
    WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE,
    WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_BEGIN,
    WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_UPDATE,
    WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_RELEASE,
    WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS,
    WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE,
    WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL,
    WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK,
    WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_CLICK,
    WORM_UI_COMPOSE_INPUT_SET_MINIMAP_PAUSED_BEHAVIOR,
    WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN,
    WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE,
    WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE,
    WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL,
    WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN,
    WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE,
    WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE,
    WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL,
    WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT,
    WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE,
    WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK,
    WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE,
    WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE,
    WORM_UI_COMPOSE_INPUT_KIAI_SPAN_SELECT,
    WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT,
    WORM_UI_COMPOSE_INPUT_RECIPE_SELECT,
    WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT,
    WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND,
    WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP,
    // BGM bar view: audio_fraction anchors a zoom of x_pixels steps, x_pixels pans by a
    // fraction of the song, and reset shows the whole song again.
    WORM_UI_COMPOSE_INPUT_BGM_BAR_ZOOM,
    WORM_UI_COMPOSE_INPUT_BGM_BAR_PAN,
    WORM_UI_COMPOSE_INPUT_BGM_BAR_RESET,
    // Toolbar toggle: show or hide the whole bar row to win vertical space.
    WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE,
    // Speed span drag; admission resolves span_edge before moving the Speed event identified by object_id.
    WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT,
    WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION,
    WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL,
    WORM_UI_COMPOSE_INPUT_DRAW_SOURCE,
    WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW,
    WORM_UI_COMPOSE_INPUT_BMSON_EMPTY_SELECT,
    WORM_UI_COMPOSE_INPUT_BGM_MUTE,
    WORM_UI_COMPOSE_INPUT_BGM_SOLO,
    /* Measure lane: object_id is the measure whose bar line is clicked; selection_mode applies. */
    WORM_UI_COMPOSE_INPUT_BAR_LINE_SELECT,
    /* Measure lane in Draw mode: add a bar line at the snapped row under y_pixels. */
    WORM_UI_COMPOSE_INPUT_BAR_LINE_PLACE,
    /* A song-preview edge or body drag (open question 26), with an admitted span handle. */
    WORM_UI_COMPOSE_INPUT_PREVIEW_SPAN_EDIT,
    WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT,
} WormUiComposeInputKind;

typedef enum WormUiComposeNavigationAction {
    WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD = 0,
    WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD,
    WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD,
    WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD,
    WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_BACKWARD,
    WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_FORWARD,
} WormUiComposeNavigationAction;

namespace wormedit::ui {
struct compose_input_action_t {
    WormUiComposeInputKind kind;
    note_kind_t note_kind;
    wormedit::lane_id_t lane_id;
    wormedit::object_id_t object_id;
    wormedit::chart_id_t timing_chart_id;
    uint64_t timing_document_revision;
    wormedit::timing_lane_kind_t timing_lane;
    uint64_t timing_anchor_chart_row;
    std::optional<resource_role_t> resource_role;
    bool successive;
    bool razor_stroke_start;
    bool note_move_to_bgm;
    WormUiComposeSelectionMode selection_mode;
    WormUiComposeNavigationAction navigation_action;
    double x_pixels;
    double y_pixels;
    double audio_fraction;
    bool overview_shift;
    bool overview_primary;
    bool beat_fit_ctrl_held;
    bool beat_fit_onset_snap_held;
    int64_t beat_fit_snap_step_numerator;
    uint64_t beat_fit_snap_step_denominator;
    /* Raw Compose transport selector. Admission maps RECIPE_COMMAND to compose_recipe_command_t;
       span edits to their bounded handle type before the handler runs. object_id names the recipe
       or span head; Kiai create uses zero. */
    uint8_t span_edge;
    double span_anchor_y_pixels;
    /* NOTE_PAINT_BEGIN: sample resource written into every note of the stroke; 0 paints silent notes. */
    wormedit::resource_id_t resource_id;
    visual_event_kind_t bga_lane_kind;
    bool bga_lane_kind_valid;
    /* Raw Compose transport selector. Admission preserves note-paint 0 as the legacy toggle,
       while semantic paint accepts only 1 add and 2 remove. */
    uint8_t paint_intent;
    auto operator==(const compose_input_action_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* Legacy wire values for span selectors. The sys input boundary maps these to family-specific
   handles before dispatch. CREATE only applies to Kiai. */
typedef enum WormUiSpanHandle {
    WORM_UI_SPAN_HANDLE_BEGIN = 0,
    WORM_UI_SPAN_HANDLE_END = 1,
    WORM_UI_SPAN_HANDLE_BODY = 2,
    WORM_UI_SPAN_HANDLE_CREATE = 3,
} WormUiSpanHandle;

typedef enum WormUiComposeViewportInputKind {
    WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL = 0,
    WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
    WORM_UI_COMPOSE_VIEWPORT_INPUT_PINCH_ZOOM,
} WormUiComposeViewportInputKind;

namespace wormedit::ui {
struct compose_viewport_input_t {
    WormUiComposeViewportInputKind kind;
    bool zoom_modifier;
    double delta_x;
    double delta_y;
    double cursor_x_pixels;
    double cursor_y_pixels;
    double zoom_factor;
    auto operator==(const compose_viewport_input_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
/* Input the frontend sampled this frame. Read by the shortcut and Compose input dispatch; not a request. */
/* The shell's keyboard state this frame: events and what currently owns text input. */
struct shortcut_input_t {
    std::vector<wormedit::ui::shortcut_event_t> events;
    bool overflowed;
    bool window_focused;
    bool notification_history_focused;
    bool text_entry_active;
    bool ime_composition_active;
    bool compose_lane_focused;
    wormedit::lane_id_t focused_lane_id;
};
/* The seek/minimap bar: current field span, stamped onto BGM-bar view inputs for centered zoom/pan retention. */
struct minimap_input_t {
    double visible_begin_fraction;
    double visible_end_fraction;
    bool window_capture_active;
    double window_capture_begin_fraction;
    double window_capture_end_fraction;
};
/* Source frame under the pointer, for hover read-outs and audition. */
struct hover_source_input_t {
    int64_t frame;
    bool frame_valid;
    uint64_t total_frames;
    uint32_t sample_rate;
};
/* Input the frontend sampled this frame. Read by the shortcut and Compose input dispatch; not a request. */
struct frame_input_t {
    shortcut_input_t shortcuts;
    minimap_input_t minimap;
    hover_source_input_t hover_source;
    double compose_input_viewport_width_pixels;
    // Uncovered editor right edge in the same source coordinates as the viewport.
    double compose_input_editor_right_pixels;
    bool compose_has_pointer_hover;
    double compose_pointer_hover_x_pixels;
    double compose_pointer_hover_y_pixels;
    bool compose_viewport_pointer_valid;
    double compose_viewport_pointer_x_pixels;
    double compose_viewport_pointer_y_pixels;
    bool compose_viewport_middle_click;
    double compose_viewport_middle_click_x_pixels;
    double compose_viewport_middle_click_y_pixels;
    std::vector<wormedit::ui::compose_input_action_t> compose_inputs;
    std::vector<wormedit::ui::compose_viewport_input_t> compose_viewport_inputs;
    // Compose ownership transition: 1 = modal, 2 = text/IME.
    uint8_t compose_input_ownership;
};
} // namespace wormedit::ui
