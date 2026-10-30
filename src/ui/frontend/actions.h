#pragma once
#include "bga_usage.h"
#include "generator.h"
#include "bms_program.h"
#include <wormedit/core/metadata_inheritance_types.h>
#include "timing_links.h"
#include "audio_slicer.h"
#include <wormedit/core/timing_edit_policy.h>
#include <wormedit/core/preview_mix.h>
#include "workspace_types.h"
#include "file_jobs.h"
#include "lane_keys.h"
#include "options_notifications.h"
#include "sound_import.h"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui {
struct metadata_channel_map_lane_t {
    lane_id_t lane;
    std::optional<std::string> visible;
    std::optional<std::string> invisible;
    std::optional<std::string> long_note;
    std::optional<std::string> mine;

    auto operator==(const metadata_channel_map_lane_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct sound_import_item_t {
    std::string path;
    std::string name;
    sound_kind_t kind{sound_kind_t::unspecified};
    bool kind_fixed{};
    auto operator==(const sound_import_item_t&) const -> bool = default;
};
struct sound_slot_import_item_t {
    std::string path;
    std::string name;
    resource_id_t occupied_resource_id{};
    std::string occupied_name;
    bool ambiguous{};
    auto operator==(const sound_slot_import_item_t&) const -> bool = default;
};
struct sound_group_assignment_t {
    sound_group_id_t group_id{};
    std::string name;
    std::vector<resource_id_t> sound_ids;
    auto operator==(const sound_group_assignment_t&) const -> bool = default;
};
struct sound_import_group_t {
    std::string name;
    sound_group_id_t existing_group_id{};
    std::vector<std::size_t> member_indices;
    auto operator==(const sound_import_group_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiComposeActionKind {
    WORM_UI_COMPOSE_ACTION_NONE = 0,
    WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET,
    WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE,
    WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR,
    WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE,
    WORM_UI_COMPOSE_ACTION_AREA_DELETE,
    WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE,
    /* Adds a SPEEDS event at the area start whose transition length is the area length in beats. */
    WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN,
    WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP,
    WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY,
    WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP_RESIZE,
    WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY_RESIZE,
    /* Sets the cursor measure's time signature from timing_meter_numerator_text and
       timing_meter_denominator_text. */
    WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER,
    /* Sets the meter from timing_meter_*_text on every measure in compose_measure_ids as one undo
       step. */
    WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER,
    WORM_UI_COMPOSE_ACTION_SET_MEASURES_LENGTH,
    /* Shift+Alt bar line drag: compose_measure_ids[0] is the measure the line ends; it takes the
       timing_meter_*_text meter and the next measure takes compose_second_meter_*. */
    WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE,
    /* The selected SV recipe form (compose_recipe_form, compose_recipe_id): preview shows the
       candidate in the lane without writing, apply stores it as one undo step, clear drops the
       preview. */
    WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM,
    WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM,
    WORM_UI_COMPOSE_ACTION_CLEAR_SV_RECIPE_PREVIEW,
    WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE,
    WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT,
    WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE,
    WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE,
    WORM_UI_COMPOSE_ACTION_ZOOM_IN,
    WORM_UI_COMPOSE_ACTION_ZOOM_OUT,
    WORM_UI_COMPOSE_ACTION_SCROLL_UP,
    WORM_UI_COMPOSE_ACTION_SCROLL_DOWN,
    WORM_UI_COMPOSE_ACTION_RESET_VIEW,
    WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE,
    WORM_UI_COMPOSE_ACTION_RESOURCE_PAINT_TOOL,
    WORM_UI_COMPOSE_ACTION_SELECT_TOOL,
    WORM_UI_COMPOSE_ACTION_DRAW_TOOL,
    WORM_UI_COMPOSE_ACTION_LAYER_TOOL,
    WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY,
    WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_FOCUS,
    WORM_UI_COMPOSE_ACTION_TOGGLE_EDIT_SNAP,
    WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP,
    WORM_UI_COMPOSE_ACTION_FIND_FITTING_SNAP,
    WORM_UI_COMPOSE_ACTION_TOGGLE_Y_LOCK,
    WORM_UI_COMPOSE_ACTION_RATE_SLOWER,
    WORM_UI_COMPOSE_ACTION_RATE_FASTER,
    WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES,
    WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION,
    WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE,
    WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET,
    WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT,
    WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT,
    WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP,
    WORM_UI_COMPOSE_ACTION_EXPORT_ALL_OUTPUT_GROUPS,
    WORM_UI_COMPOSE_ACTION_CONFIRM_OUTPUT_EXPORT,
    WORM_UI_COMPOSE_ACTION_CANCEL_OUTPUT_EXPORT,
    WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR,
    WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM,
    WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM,
    WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER,
    WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE,
    WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM,
    WORM_UI_COMPOSE_ACTION_MAKE_BMSON_STEM,
    WORM_UI_COMPOSE_ACTION_PACK_BMSON_CHANNEL,
    WORM_UI_COMPOSE_ACTION_RESTART_AND_PACK_BMSON_CHANNEL,
    WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE,
    /* Relinks compose_resource_id's audio file to fallback candidate compose_fallback_candidate. */
    WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK,
    /* Keysound listbox actions. The sound list is compose_sound_resource_ids. */
    WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES,
    WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT,
    WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH,
    WORM_UI_COMPOSE_ACTION_STAGE_SOUND_SLOT_BATCH,
    WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY,
    WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT,
    WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES,
    WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA,
    WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS,
    WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS,
    WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS,
    WORM_UI_COMPOSE_ACTION_SUGGEST_SOUND_GROUPS,
    WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS,
    WORM_UI_COMPOSE_ACTION_AUDITION_SOUND,
    WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER,
    WORM_UI_COMPOSE_ACTION_REMOVE_SOUND_LAYER,
    WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS,
    WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET,
    WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK,
    WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL,
    // Search text travels in compose_note_label.
    WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_BEFORE,
    WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_AFTER,
    WORM_UI_COMPOSE_ACTION_REMOVE_EMPTY_MEASURE,
    // Confirmed removal of the cursor measure together with its contents.
    WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_LEFT,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_RIGHT,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_EARLIER,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER,
    WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION,
    WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_REORDER_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION,
    WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL,
    WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN,
    WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END,
    WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION,
    WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION,
    WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION,
    WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION,
    WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER,
    WORM_UI_COMPOSE_ACTION_NAVIGATE_NEXT_AUDIO_MARKER,
    WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW,
    WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK,
    WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES,
    WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE,
    WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE,
    WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR,
    WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS,
    WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP,
    WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY,
    WORM_UI_COMPOSE_ACTION_TOGGLE_LAYER_ASSIST_TICK,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER,
    WORM_UI_COMPOSE_ACTION_BMS_PROGRAM,
    WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART,
    WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_CONTINUE,
    WORM_UI_COMPOSE_ACTION_SELECT_QUANTIZE_PRESET,
    WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS,
    WORM_UI_COMPOSE_ACTION_TOGGLE_QUANTIZE_PREVIEW,
    WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET,
    WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET,
    WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU,
    WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW,
    WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU,
    WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER,
    WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION,
    WORM_UI_COMPOSE_ACTION_OPEN_SELECTED_SOUND_LAYER_MENU,
    WORM_UI_COMPOSE_ACTION_ADD_SELECTED_SOUNDS_TO_LAYER,
    /* Session-only: the measure lane shows bar lines as selectable objects. */
    WORM_UI_COMPOSE_ACTION_TOGGLE_MEASURE_LANE,
    /* Session-only: moved bar lines carry the content after them (off by default). */
    WORM_UI_COMPOSE_ACTION_TOGGLE_BAR_LINE_TIME,
    /* The authored song preview (open question 26). SET reads compose_marker_audio_begin and an
       optional compose_marker_audio_end in milliseconds; an empty end keeps it point-only. */
    WORM_UI_COMPOSE_ACTION_SET_PREVIEW,
    WORM_UI_COMPOSE_ACTION_SET_PREVIEW_FROM_SELECTION,
    WORM_UI_COMPOSE_ACTION_SET_PREVIEW_START_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_CLEAR_PREVIEW,
    WORM_UI_COMPOSE_ACTION_USE_SONG_PREVIEW,
    WORM_UI_COMPOSE_ACTION_ADD_EXPORT_CUT_FROM_SELECTION,
    WORM_UI_COMPOSE_ACTION_UPDATE_EXPORT_CUT,
    WORM_UI_COMPOSE_ACTION_DELETE_EXPORT_CUT,
    /* Not an action: the number of action kinds, for tables indexed by kind. Keep it last. */
    WORM_UI_COMPOSE_ACTION_GENERATOR_SELECT,
    WORM_UI_COMPOSE_ACTION_GENERATOR_RUN,
    WORM_UI_COMPOSE_ACTION_GENERATOR_CONFIRM,
    WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY,
    WORM_UI_COMPOSE_ACTION_GENERATOR_CANCEL,
    WORM_UI_COMPOSE_ACTION_GENERATOR_DETACH,
    WORM_UI_COMPOSE_ACTION_COUNT,
} WormUiComposeActionKind;

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

typedef enum WormUiMetadataEditKind {
    WORM_UI_METADATA_EDIT_NONE = 0,
    WORM_UI_METADATA_EDIT_SONG,
    WORM_UI_METADATA_EDIT_CHART,
    WORM_UI_METADATA_EDIT_INHERITANCE,
    WORM_UI_METADATA_EDIT_AUDIO_OFFSET,
    WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_OUTPUT_PATH,
    WORM_UI_METADATA_EDIT_REDIVIDE_CHART,
    WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP,
    WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP,
    // Author and/or rating written to metadata_batch_chart_ids in one command. The author is
    // metadata_chart_author; the rating is metadata_has_chart_rating plus the numerator/denominator
    // texts (no rating clears it).
    WORM_UI_METADATA_EDIT_BATCH_CHART,
    // Key-count preview and apply: the index names a choice in the chart's family list.
    WORM_UI_METADATA_EDIT_PREVIEW_LANE_PROFILE,
    WORM_UI_METADATA_EDIT_APPLY_LANE_PROFILE,
} WormUiMetadataEditKind;

typedef enum WormUiClipboardAction {
    WORM_UI_CLIPBOARD_NONE = 0,
    WORM_UI_CLIPBOARD_COPY,
    WORM_UI_CLIPBOARD_CUT,
    WORM_UI_CLIPBOARD_PASTE,
} WormUiClipboardAction;

/* A timing pointer target captures its source-document fraction from the drawn axis when available.
   Raw viewport coordinates and the visible slice also support surfaces without chart-axis geometry. */
namespace wormedit::ui {
struct timing_target_t {
    double screen_y;
    double viewport_height_pixels;
    double document_begin_fraction;
    double document_end_fraction;
    bool downscroll;
    std::optional<double> source_document_fraction;
    auto operator==(const timing_target_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct workspace_action_t {
    timing_link_action_t timing_link;
    wormedit::ui::file_jobs_action_t file_jobs;
    std::string menu_command_id;
    bool compose_menu_command;
    WormUiClipboardAction clipboard;
    wormedit::ui::notification_action_t notifications;
    wormedit::ui::options_action_t options;
    wormedit::ui::lane_keys_action_t lane_keys;
    WormUiCloseDecision close_decision;
    song_id_t close_project_id;
    uint64_t close_revision;
    wormedit::ui::project_chart_action_t project_chart;
    wormedit::ui::project_settings_action_t project_settings;
    wormedit::ui::project_output_group_action_t project_output_group;
    wormedit::ui::chart_import_action_t chart_import;
    wormedit::ui::tap_timing_action_t tap_timing;
    wormedit::ui::timing_settings_action_t timing_settings;
    // The complete next playback mix requested by the Mixer dock.
    std::optional<wormedit::preview_mix_t> preview_mix;
    WormUiWorkspaceActionKind kind;
    struct wormedit::ui::external_player_action_t external_player;
    std::string project_path;
    // Unknown requests automatic detection.
    text_encoding_t bms_encoding_id;
    std::string bms_lane_profile_id;
    // Stable core layout identity.
    std::string new_project_layout_id;
    WormUiWorkspaceTab tab;
    uint64_t recovery_scan_generation;
    uint64_t transport_seek_frame;
    // Current field span, stamped onto BGM-bar view inputs for centered zoom/pan retention.
    double minimap_visible_begin_fraction;
    double minimap_visible_end_fraction;
    bool minimap_window_capture_active;
    double minimap_window_capture_begin_fraction;
    double minimap_window_capture_end_fraction;
    int64_t hover_source_frame;
    bool hover_source_frame_valid;
    uint64_t hover_source_total_frames;
    uint32_t hover_source_sample_rate;
    uint8_t transport_rate_index;
    wormedit::audio::playback_rate_id_t transport_rate_id;
    bool transport_keep_pitch;
    std::optional<audio_slicer_action_t> audio_slicer;
    wormedit::asset_id_t audio_asset_id;
    wormedit::resource_id_t audio_resource_id;
    wormedit::ui::bga_usage_activation_t bga_usage;
    wormedit::song_id_t sound_usage_song_id;
    uint64_t sound_usage_document_revision;
    wormedit::resource_id_t sound_usage_resource_id;
    wormedit::asset_id_t sound_usage_asset_id;
    wormedit::chart_id_t sound_usage_chart_id;
    wormedit::measure_id_t sound_usage_measure_id;
    uint64_t sound_usage_row;
    wormedit::object_id_t sound_usage_object_id;
    wormedit::object_id_t sound_usage_channel_id;
    wormedit::clip_id_t sound_usage_clip_id;
    wormedit::resource_id_t sound_usage_referenced_resource_id;
    wormedit::asset_id_t sound_usage_referenced_asset_id;
    sound_usage_kind_t sound_usage_kind;
    sound_usage_scope_t sound_usage_scope;
    bool sound_usage_position_valid;
    wormedit::asset_id_t visual_asset_id;
    wormedit::player_preview_skin_t player_preview_skin;
    WormUiAudioOffsetDragAction audio_offset_drag_action;
    chart_id_t audio_offset_drag_chart_id;
    uint64_t audio_offset_drag_document_revision;
    uint32_t audio_offset_drag_sample_rate;
    int64_t audio_offset_drag_delta_frames;
    size_t recovery_row_index;
    chart_id_t compare_chart_id;
    uint64_t compare_diagnostic_row;
    wormedit::measure_id_t compare_diagnostic_measure;
    chart_id_t compare_diagnostic_chart;
    double compare_scroll_delta;
    size_t recovery_candidate_index;
    std::string recovery_save_as_destination;
    // Metadata payloads own their text and lists across frontend calls.
    WormUiMetadataEditKind metadata_edit;
    uint64_t metadata_document_revision;
    chart_id_t metadata_chart_id;
    bool metadata_has_chart_rating;
    std::optional<std::string> metadata_chart_rating_numerator_text;
    std::optional<std::string> metadata_chart_rating_denominator_text;
    std::optional<std::string> metadata_audio_offset_numerator_text;
    std::optional<std::string> metadata_audio_offset_denominator_text;
    std::optional<std::string> metadata_title;
    std::optional<std::string> metadata_artist;
    std::optional<std::string> metadata_chart_name;
    std::optional<std::string> metadata_chart_author;
    metadata_overrides_t metadata_overrides;
    std::vector<std::optional<std::string>> metadata_search_tags;
    bool metadata_inspect_copy;
    bool metadata_inspect_export;
    bool metadata_inspect_import;
    bool metadata_import_copy_project;
    bool metadata_import_copy_export;
    bool metadata_format_chart_scoped;
    format_family_t metadata_format_family;
    size_t metadata_format_index;
    std::optional<std::string> metadata_format_name;
    std::optional<std::string> metadata_format_value;
    size_t metadata_output_group_index;
    std::optional<std::string> metadata_output_path;
    // REDIVIDE_CHART: target divisions per standard measure (exact, 0 when unset).
    uint64_t metadata_redivision_resolution;
    // Channel-map preview owns one record per lane; empty channel text means unset.
    object_id_t metadata_channel_map_layout_segment_id;
    std::vector<metadata_channel_map_lane_t> metadata_channel_map_lanes;
    std::vector<chart_id_t> metadata_batch_chart_ids;
    bool metadata_batch_edit_author;
    bool metadata_batch_edit_rating;
    size_t metadata_lane_profile_index;
    WormUiComposeActionKind compose_action;
    uint64_t compose_area_begin_row;
    uint64_t compose_area_end_row;
    bool compose_area_virtual;
    bool compose_area_begin_edge;
    bool compose_area_clear_requested;
    uint64_t compose_measure_number;
    uint32_t compose_bgm_column_count{};
    uint32_t compose_lane_profile_index;
    std::string compose_lane_layout_profile_id;
    int64_t compose_audio_offset_numerator;
    uint64_t compose_audio_offset_denominator;
    wormedit::chart_id_t compose_input_chart_id;
    wormedit::song_id_t compose_input_project_id;
    uint64_t compose_input_document_revision;
    size_t compose_quantize_preset_index;
    bool compose_quantize_preset_selected;
    uint64_t compose_snap_denominator;
    uint64_t compose_quantize_divisions;
    quantization_space_t compose_quantize_space;
    quantization_tie_t compose_quantize_tie;
    quantization_hold_t compose_quantize_holds;
    std::string compose_quantize_preset_name;
    double compose_input_viewport_width_pixels;
    // Uncovered editor right edge in the same source coordinates as the viewport.
    double compose_input_editor_right_pixels;
    wormedit::object_id_t compose_sound_hover_object_id;
    bool compose_has_pointer_hover;
    double compose_pointer_hover_x_pixels;
    double compose_pointer_hover_y_pixels;
    bool compose_viewport_pointer_valid;
    double compose_viewport_pointer_x_pixels;
    double compose_viewport_pointer_y_pixels;
    bool compose_viewport_middle_click;
    double compose_viewport_middle_click_x_pixels;
    double compose_viewport_middle_click_y_pixels;
    bool compose_note_label_focus_consumed;
    std::string compose_note_label;
    wormedit::object_id_t compose_bookmark_id;
    wormedit::bookmark_reorder_t compose_bookmark_reorder;
    wormedit::object_id_t compose_section_id;
    wormedit::object_id_t compose_export_cut_id;
    std::string compose_marker_label;
    std::string compose_marker_audio_begin;
    std::string compose_marker_audio_end;
    wormedit::layer_id_t compose_semantic_layer_id;
    wormedit::object_id_t compose_note_context_id;
    size_t compose_semantic_layer_first_row;
    size_t compose_semantic_layer_row_count{};
    wormedit::resource_id_t compose_resource_id;
    uint32_t compose_fallback_candidate;
    wormedit::object_id_t compose_bmson_bgm_event_id;
    wormedit::asset_id_t compose_resource_asset_id;
    std::vector<wormedit::resource_id_t> compose_sound_resource_ids;
    std::vector<wormedit::ui::sound_group_assignment_t> compose_sound_group_assignments;
    bool compose_sound_group_keep_ids_together;
    bool compose_sound_overwrite_note_layers;
    /* Renumber WAV or BMP identifiers for the selected resources, or all in the namespace. */
    bool compose_renumber_all;
    bool compose_renumber_bga;
    std::vector<std::string> compose_import_paths;
    // Chart positions, including virtual tail measures; these are not stable measure IDs.
    std::vector<uint64_t> compose_measure_ids;
    uint32_t compose_second_meter_numerator, compose_second_meter_denominator;
    bool sound_properties_apply;
    wormedit::song_id_t sound_properties_project_id{};
    wormedit::chart_id_t sound_properties_chart_id{};
    wormedit::resource_id_t sound_properties_resource_id{};
    uint64_t sound_properties_document_revision;
    std::string sound_properties_name;
    wormedit::sound_kind_t sound_properties_kind{wormedit::sound_kind_t::unspecified};
    // A tag-only edit; name and authored kind remain unchanged when present.
    std::optional<std::vector<std::string>> sound_properties_tags;
    std::optional<wormedit::hitsound_change_t> compose_hitsound;
    std::optional<wormedit::ui::preview_audio_export_action_t> preview_audio_export;
    bool sound_import_pick_requested;
    wormedit::ui::sound_import_kind_t sound_import_kind{};
    wormedit::song_id_t sound_import_project_id{};
    std::vector<wormedit::ui::sound_import_item_t> sound_import_items;
    std::vector<wormedit::ui::sound_import_group_t> sound_import_groups;
    uint16_t compose_sound_slot;
    wormedit::object_id_t compose_bmson_channel_id;
    bool compose_sound_slot_confirmed;
    size_t compose_output_group_index;
    WormUiExistingFilesChoice compose_existing_files;
    std::optional<resource_role_t> compose_resource_role;
    std::string compose_import_path;
    timing_lane_kind_t compose_timing_lane;
    bms_program_action_t bms_program;

    std::string compose_semantic_layer_name;
    uint8_t compose_semantic_layer_red;
    uint8_t compose_semantic_layer_green;
    uint8_t compose_semantic_layer_blue;
    uint8_t compose_semantic_layer_alpha;
    std::vector<wormedit::ui::compose_input_action_t> compose_inputs;
    std::vector<wormedit::ui::compose_viewport_input_t> compose_viewport_inputs;
    wormedit::ui::timing_recipe_form_t compose_recipe_form;
    wormedit::ui::generator_form_t compose_generator_form;
    wormedit::recipe_id_t compose_recipe_id;
    std::string compose_measure_length_numerator_text;
    std::string compose_measure_length_denominator_text;
    std::string timing_meter_numerator_text;
    std::string timing_meter_denominator_text;
    std::optional<timing_anchoring_t> timing_anchoring;
    wormedit::ui::timing_stage_action_t timing_stage_action;
    wormedit::chart_id_t timing_chart_id;
    uint64_t timing_document_revision;
    wormedit::object_id_t timing_object_id;
    wormedit::timing_lane_kind_t timing_lane;
    bool timing_has_target_y;
    wormedit::ui::timing_target_t timing_target;
    double timing_value;
    double timing_secondary_value;
    bool timing_transition_is_seconds;
    bool timing_toggle_primary;
    wormedit::pause_unit_t timing_pause_unit;
    std::string timing_bpm_decimal_text;
    bool timing_bpm_decimal_edited;
    int timing_bpm_step;
    std::string timing_primary_numerator_text;
    std::string timing_primary_denominator_text;
    std::string timing_secondary_numerator_text;
    std::string timing_secondary_denominator_text;
    std::vector<wormedit::ui::shortcut_event_t> shortcut_events;
    bool shortcut_overflowed;
    bool shortcut_window_focused;
    bool shortcut_notification_history_focused;
    bool shortcut_text_entry_active;
    // Compose ownership transition: 1 = modal, 2 = text/IME.
    uint8_t compose_input_ownership;
    bool shortcut_ime_composition_active;
    bool shortcut_compose_lane_focused;
    wormedit::lane_id_t shortcut_focused_lane_id;
    bool command_menu_invoked;
    std::string command_menu_command_id;

    void set_sound_resources(std::span<const std::uint64_t> ids) {
        compose_sound_resource_ids.clear();
        for (const auto id : ids) compose_sound_resource_ids.push_back(resource_id_t{id});
    }
};
} // namespace wormedit::ui

// Private application/frontend boundary. Implementations own all widget
// toolkit state and translate semantic views into typed actions.
bool worm_ui_frontend_init(SDL_Window* window, SDL_Renderer* renderer);
// Select a loaded field palette; empty or unknown IDs use Wormedit. Chrome is unaffected.
bool worm_ui_frontend_select_field_scheme(const char* id);
// Resolve a layout override, the default, then Wormedit against semantic compatibility.
bool worm_ui_frontend_configure_field_scheme(const char* requested, const char* fallback,
    const wormedit::ui::compose_lane_view_t* lanes, size_t count);
// Core stage none/left/right; -1 uses chart layout, 0 left, 1 right.
int worm_ui_frontend_field_scratch_side(uint8_t stage);
void worm_ui_frontend_set_snap_colored_grid(bool enabled);
void worm_ui_frontend_set_snap_grid_opacity(uint32_t percent);
void worm_ui_frontend_set_field_waveform_opacity(uint32_t percent);
// Applies accepted program settings; theme errors leave the previous palette untouched.
std::string worm_ui_frontend_apply_display_settings(std::string_view theme, uint32_t scale_percent,
                                                  bool timing_overlay);
bool worm_ui_frontend_set_interface_scale(uint32_t percent);
// The scale in use: the requested scale, capped so the window keeps the minimum layout.
uint32_t worm_ui_frontend_interface_scale_percent(void);
bool worm_ui_frontend_is_title_bar_drag_position(float x, float y, int window_width);
// Initialize the same event/action frontend without a window or renderer for integration tests.
bool worm_ui_frontend_init_headless(int width, int height);
// Actual in-window chrome height; headless initialization keeps the fallback layout.
int worm_ui_frontend_title_bar_height(void);
// Called by frontend producers on the UI thread; the callback copies text before
// returning and must not throw. Shutdown clears the non-owning callback.
void worm_ui_frontend_set_notification_sink(void* context, WormUiNotificationPostCallback callback);
void worm_ui_frontend_notify(WormUiNotificationSeverity severity,
    WormUiNotificationProducerSource source, const char* message);
// Exercise the real dialog callback handoff without opening a native picker.
uint64_t worm_ui_frontend_begin_project_dialog_for_testing(WormUiWorkspaceActionKind kind);
void worm_ui_frontend_project_dialog_callback_for_testing(uint64_t generation,
    const char* const* files);
wormedit::ui::workspace_action_t worm_ui_frontend_consume_project_dialog_for_testing(void);
// Read queued draw text before render clears it; returns required bytes (no terminator).
size_t worm_ui_frontend_copy_drawn_text_for_testing(char* output, size_t capacity);
