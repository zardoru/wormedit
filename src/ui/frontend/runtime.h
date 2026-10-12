#pragma once
#include "actions.h"

typedef struct WormUiTestingDrawnText {
    float x, y, width, height;
    char text[128];
} WormUiTestingDrawnText;
typedef struct WormUiTestingRect {
    float x, y, width, height;
} WormUiTestingRect;
// Copy bounded text command labels and their window-coordinate rectangles for event tests.
size_t worm_ui_frontend_copy_drawn_text_bounds_for_testing(WormUiTestingDrawnText* output,
                                                            size_t capacity);
// Return the current Compose canvas viewport in window coordinates, when one was drawn this frame.
bool worm_ui_frontend_compose_viewport_for_testing(WormUiTestingRect* output);
bool worm_ui_frontend_window_bounds_for_testing(const char* name, WormUiTestingRect* output);
// Hide the real Notifications window in headless tests, without changing its model view.
bool worm_ui_frontend_hide_notification_history_for_testing(void);
void worm_ui_frontend_shutdown(void);
void worm_ui_frontend_begin_input(void);
bool worm_ui_frontend_handle_event(const SDL_Event* event);
WormUiMetadataDropTarget worm_ui_frontend_metadata_drop_target(uint32_t window_id, float x,
                                                                float y);
/* True when a window position lies over the currently rendered Compose or Audio/Slices
   sound listbox, where dropped audio files are added as sounds. */
bool worm_ui_frontend_sound_list_contains(uint32_t window_id, float x, float y);
void worm_ui_frontend_end_input(void);
bool worm_ui_frontend_request_new_project(void);
bool worm_ui_frontend_request_save_project(uint64_t project_id);
bool worm_ui_frontend_stage_sound_import(uint64_t project, uint64_t chart, uint64_t revision,
    const WormUiSoundImportItem* items, size_t item_count,
    const WormUiSoundImportGroup* existing_groups, size_t group_count,
    bool chart_has_bgm, const char* message);
void worm_ui_frontend_sound_import_result(bool applied, const char* message);
WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view);
// Draw a recoverable workspace error when the model could not produce a view.
WormUiWorkspaceAction worm_ui_frontend_workspace_error(
    const char* diagnostic, const WormUiNotificationView* notifications,
    const WormUiCloseConfirmationView* close);
void worm_ui_frontend_resource_placed(uint64_t placed_resource_id, uint64_t next_resource_id);
void worm_ui_frontend_render(int window_width, int window_height);

#define WORM_UI_FRAME_TIMING_CAPACITY 240
#define WORM_UI_FRAME_TIMING_PHASE_COUNT 27
typedef struct WormUiFrameTimingStats {
    size_t count;
    double average_ms;
    double p99_ms;
    /* p99 of the main thread's CPU time; 0 samples where the platform cannot measure it. A wall
       p99 well above it means the frame waited (preempted or blocked) rather than worked. */
    size_t cpu_count;
    double cpu_p99_ms;
} WormUiFrameTimingStats;
#define WORM_UI_FRAME_GPU_WORK_COUNT 5
typedef struct WormUiFrameGpuWorkStats {
    size_t count;
    double average;
    double p99;
} WormUiFrameGpuWorkStats;
typedef struct WormUiFrameGpuWork {
    uint64_t vertices;
    uint64_t indices;
    uint64_t draw_calls;
    uint64_t clip_rect_changes;
    uint64_t texture_switches;
} WormUiFrameGpuWork;
typedef struct WormUiFrameTimingView {
    size_t count;
    double frame_ms[WORM_UI_FRAME_TIMING_CAPACITY];
    WormUiFrameTimingStats frame;
    const char* phase_names[WORM_UI_FRAME_TIMING_PHASE_COUNT];
    WormUiFrameTimingStats phases[WORM_UI_FRAME_TIMING_PHASE_COUNT];
    uint32_t heap_allocations_available;
    uint64_t projection_heap_allocations_latest;
    uint64_t projection_heap_bytes_latest;
    WormUiFrameGpuWorkStats projection_heap_allocations;
    WormUiFrameGpuWorkStats projection_heap_bytes;
    // Application GPU work excludes diagnostic windows (the frame-timing panel),
    // whose own submission is reported separately in the diagnostic_* fields.
    uint32_t gpu_work_available;
    WormUiFrameGpuWork gpu_work_latest;
    const char* gpu_work_names[WORM_UI_FRAME_GPU_WORK_COUNT];
    WormUiFrameGpuWorkStats gpu_work[WORM_UI_FRAME_GPU_WORK_COUNT];
    uint32_t diagnostic_gpu_work_available;
    WormUiFrameGpuWork diagnostic_gpu_work_latest;
    WormUiFrameGpuWorkStats diagnostic_gpu_work[WORM_UI_FRAME_GPU_WORK_COUNT];
} WormUiFrameTimingView;
// Called only while the frontend's diagnostic overlay is visible.
WormUiFrameTimingView worm_ui_frame_timing_view(void);
void worm_ui_frame_timing_record_gpu_work(WormUiFrameGpuWork work);
void worm_ui_frame_timing_record_diagnostic_gpu_work(WormUiFrameGpuWork work);
typedef enum WormUiFrameRenderPhase {
    WORM_UI_FRAME_RENDER_CONVERT,
    WORM_UI_FRAME_RENDER_SUBMIT,
    WORM_UI_FRAME_RENDER_PRESENT
} WormUiFrameRenderPhase;
// The returned token is opaque to the frontend and restores the prior phase.
uint32_t worm_ui_frame_render_phase_begin(WormUiFrameRenderPhase phase);
void worm_ui_frame_render_phase_end(uint32_t previous);

void worm_ui_frontend_bga_dock_request(WormUiBgaDockRequest* request);
void worm_ui_frontend_sound_usage_request(WormUiSoundUsageRequest* request);
void worm_ui_frontend_audio_waveform_viewport_request(WormUiAudioWaveformViewportRequest* request);
void worm_ui_frontend_select_sound_usage_source(uint64_t project_id, uint64_t resource_id,
                                              uint64_t channel_id, uint64_t clip_id);
void worm_ui_frontend_bga_move_preview(uint64_t* chart_id, uint64_t* document_revision,
                                       uint64_t* event_id, double* y_pixels);
void worm_ui_frontend_keysound_dock_request(WormUiKeysoundDockRequest* request);
void worm_ui_frontend_bookmark_chooser_request(WormUiBookmarkChooserRequest* request);
bool worm_ui_frontend_open_bookmark_chooser(uint64_t project_id, uint64_t chart_id, uint64_t revision);
bool worm_ui_frontend_open_sound_picker(uint64_t project_id, uint64_t chart_id, uint64_t revision);
bool worm_ui_frontend_open_project_export(uint64_t project_id, uint64_t chart_id, uint64_t revision);
typedef enum WormUiComposeDockCommand {
    WORM_UI_COMPOSE_DOCK_OPEN_LAYERS,
    WORM_UI_COMPOSE_DOCK_CREATE_LAYER,
    WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS,
} WormUiComposeDockCommand;
bool worm_ui_frontend_compose_dock_command(WormUiComposeDockCommand command,
    uint64_t project_id, uint64_t chart_id, uint64_t revision);
/* Width in logical pixels the BMS branch dock took from the left of the Compose field last frame;
   the Compose projection narrows its viewport by it. Zero while the dock is hidden. */
int worm_ui_frontend_compose_left_dock_pixels(void);
double worm_ui_frontend_compose_bgm_lane_width(uint64_t project_id, uint64_t chart_id,
                                             uint64_t lane_id);
/* Queues one dropped file for exact slot assignment; false when automatic mode is active.
   In slots mode invalid/stale requests are consumed without falling back to allocation. */
bool worm_ui_frontend_stage_sound_slot_batch(uint64_t project, uint64_t chart, uint64_t revision,
    uint16_t first_slot, const WormUiSoundSlotImportItem* items, size_t count, const char* reason);

bool worm_ui_frontend_stage_sound_slot_file(uint64_t project, uint64_t chart, uint64_t revision,
                                            const char* path, size_t file_count);
void worm_ui_frontend_sound_slot_batch_result(bool applied, const char* message);
bool worm_ui_frontend_stage_sound_slot_file_at(uint64_t project, uint64_t chart,
    uint64_t revision, uint16_t slot, uint64_t occupied_resource, const char* path);
bool worm_ui_frontend_bga_list_contains(uint32_t window_id, float x, float y);

// Opens the searchable registry command menu when workspace input is available.
bool worm_ui_frontend_open_command_menu(void);
