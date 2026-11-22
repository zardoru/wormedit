#include "frame_timing_support.h"
#include <wormedit/visual/image_job.h>
#include <wormedit/window.h>
#include "sys/compose/compose_onset_snap.h"
#include "ui/field/color.h"
#include "ui/frontend/compose.h"
#include <cstddef>
#include <cstdlib>
#include <new>

// The real Compose input selector is linked below. Its unrelated UI-only
// resource-placement notification has no recipient in this headless harness.
void worm_ui_frontend_resource_placed(uint64_t placed, uint64_t) {
    placed_resource_notifications.push_back(placed);
}

void* operator new(std::size_t size) {
    wormedit::sys::record_frame_heap_allocation(size);
    if (count_allocations) ++allocations;
    if (auto* value = std::malloc(size == 0 ? 1 : size)) return value;
    throw std::bad_alloc();
}

void operator delete(void* value) noexcept { std::free(value); }

void operator delete(void* value, std::size_t) noexcept { std::free(value); }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

// Headless platform adapter: production frame projection reads the test window
// dimensions without creating an SDL window or touching the desktop.
void worm_window_get_size(WormWindow* window, int* width, int* height) {
    if (width) *width = window ? window->width : 0;
    if (height) *height = window ? window->height : 0;
}

// Deterministic frontend input boundary for the application projection driver.
void worm_ui_frontend_keysound_dock_request(wormedit::ui::keysound_dock_request_t* request) {
    if (request) *request = {.row_limit = 64, .include_ungrouped = true,
        .automatic_row_limit = 64, .filter_layer_row_limit = 64};
}

void worm_ui_frontend_sound_usage_request(wormedit::ui::sound_usage_request_t* request) {
    if (request) *request = {};
}

void worm_ui_frontend_bookmark_chooser_request(wormedit::ui::bookmark_chooser_request_t* request) {
    if (request) *request = {};
}

void worm_ui_frontend_audio_waveform_viewport_request(wormedit::ui::audio_waveform_viewport_request_t* request) {
    if (request) *request = {};
}

int worm_ui_frontend_title_bar_height() { return 36; }
#endif
