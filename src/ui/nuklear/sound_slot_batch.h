#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ui/frontend.h"

struct nk_context;
struct worm_ui_sound_slot_batch_item_t {
    std::string path;
    std::string name;
    std::uint64_t occupied_resource_id{};
    std::string occupied_name;
    bool ambiguous{};
    bool replace{};
};

// Retained while the slot review is open; every pointer exposed to the action
// remains valid until the next frame after dispatch.
struct worm_ui_sound_slot_batch_t {
    bool open{};
    bool replacement_confirmed{};
    bool commit_pending{};
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t revision{};
    std::uint16_t first_slot{};
    std::vector<worm_ui_sound_slot_batch_item_t> items;
    std::string reason;
    std::vector<WormUiSoundImportItem> action_items;
};

void worm_ui_sound_slot_batch_stage(worm_ui_sound_slot_batch_t& state,
                                    std::uint64_t project_id, std::uint64_t chart_id,
                                    std::uint64_t revision, std::uint16_t first_slot,
                                    const WormUiSoundSlotImportItem* items,
                                    std::size_t item_count, const char* reason);
void worm_ui_sound_slot_batch_cancel(worm_ui_sound_slot_batch_t& state);
void worm_ui_sound_slot_batch_result(worm_ui_sound_slot_batch_t& state, bool applied,
                                     const char* message);
void worm_ui_nuklear_render_sound_slot_batch(nk_context* context,
                                            worm_ui_sound_slot_batch_t& state,
                                            const WormUiComposeView& view,
                                            WormUiWorkspaceAction& action,
                                            int window_width, int window_height);
