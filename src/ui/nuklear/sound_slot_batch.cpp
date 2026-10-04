#include "ui/nuklear/sound_slot_batch.h"

#include "ui/nuklear/theme.h"

#include <algorithm>
#include <cstdio>
#include <ranges>
#include <string_view>

#include "wormedit/nuklear_config.h"

namespace {

auto slot_label(std::size_t value) -> std::string {
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (value == 0 || value >= digits.size() * digits.size()) return "??";
    return {digits[value / 36], digits[value % 36]};
}

auto has_occupants(const worm_ui_sound_slot_batch_t& state) -> bool {
    return std::ranges::any_of(state.items, [](const auto& item) {
        return item.occupied_resource_id != 0;
    });
}

auto replacements_ready(const worm_ui_sound_slot_batch_t& state) -> bool {
    return std::ranges::all_of(state.items, [](const auto& item) {
        return item.occupied_resource_id == 0 || item.replace;
    });
}

void refresh_action_items(worm_ui_sound_slot_batch_t& state) {
    state.action_items.clear();
    state.action_items.reserve(state.items.size());
    for (const auto& item : state.items)
        state.action_items.push_back({item.path.c_str(), item.name.c_str(), 2});
}

auto window_bounds(int width, int height) -> struct nk_rect {
    const auto viewport_width = std::max(1.0f, static_cast<float>(width));
    const auto viewport_height = std::max(1.0f, static_cast<float>(height));
    const auto dialog_width = std::min(850.0f, std::max(1.0f, viewport_width - 24.0f));
    const auto dialog_height = std::min(720.0f, std::max(1.0f, viewport_height - 24.0f));
    return nk_rect(std::max(0.0f, (viewport_width - dialog_width) * 0.5f),
                   std::max(0.0f, (viewport_height - dialog_height) * 0.5f),
                   dialog_width, dialog_height);
}

void draw_batch_header(nk_context* context, const worm_ui_sound_slot_batch_t& state) {
    char label[128];
    std::snprintf(label, sizeof(label), "Assign %zu files from WAV %s", state.items.size(),
                  slot_label(state.first_slot).c_str());
    nk_layout_row_dynamic(context, 26.0f, 1);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28.0f, 1);
    nk_label_wrap(context,
        "Files are ordered by natural filename and assigned to consecutive slots. "
        "Occupied slots keep their sound identity and links when replaced.");
    if (!state.reason.empty()) {
        nk_layout_row_dynamic(context, 30.0f, 1);
        nk_label_wrap(context, state.reason.c_str());
    }
}

void draw_batch_rows(nk_context* context, worm_ui_sound_slot_batch_t& state,
                     float list_height) {
    nk_layout_row_dynamic(context, std::max(70.0f, list_height), 1);
    struct nk_list_view list{};
    const auto total = static_cast<int>(std::max<std::size_t>(state.items.size(), 1));
    if (!nk_list_view_begin(context, &list, "WAV slot batch rows", NK_WINDOW_BORDER, 36, total))
        return;
    for (int row = list.begin; row < std::min<int>(list.end, state.items.size()); ++row) {
        const auto index = static_cast<std::size_t>(row);
        auto& item = state.items[index];
        const auto destination = slot_label(static_cast<std::size_t>(state.first_slot) + index);
        nk_layout_row_begin(context, NK_DYNAMIC, 36.0f, 4);
        nk_layout_row_push(context, 0.10f);
        nk_label(context, destination.c_str(), NK_TEXT_LEFT);
        nk_layout_row_push(context, 0.35f);
        nk_label(context, item.name.c_str(), NK_TEXT_LEFT);
        nk_layout_row_push(context, 0.35f);
        if (item.ambiguous) nk_label(context, "Ambiguous occupant", NK_TEXT_LEFT);
        else if (item.occupied_resource_id != 0) nk_label(context, item.occupied_name.c_str(), NK_TEXT_LEFT);
        else nk_label(context, "(empty)", NK_TEXT_LEFT);
        nk_layout_row_push(context, 0.20f);
        if (item.occupied_resource_id != 0 && !item.ambiguous) {
            auto replacing = static_cast<nk_bool>(item.replace);
            (void)nk_checkbox_label(context, "Replace", &replacing);
            item.replace = replacing != 0;
        }
        else
            nk_label(context, item.ambiguous ? "Resolve first" : "", NK_TEXT_LEFT);
        nk_layout_row_end(context);
    }
    nk_list_view_end(&list);
}

void close_with_reason(worm_ui_sound_slot_batch_t& state, const char* reason) {
    worm_ui_sound_slot_batch_cancel(state);
    state.reason = reason;
}

auto stale_target(const worm_ui_sound_slot_batch_t& state, const WormUiComposeView& view)
    -> bool {
    return !view.sound_slots_mode || view.project_id != state.project_id ||
           view.chart_id != state.chart_id || view.document_revision != state.revision;
}

} // namespace

void worm_ui_sound_slot_batch_stage(worm_ui_sound_slot_batch_t& state,
                                    std::uint64_t project_id, std::uint64_t chart_id,
                                    std::uint64_t revision, std::uint16_t first_slot,
                                    const WormUiSoundSlotImportItem* items,
                                    std::size_t item_count, const char* reason) {
    state = {};
    state.open = true;
    state.project_id = project_id;
    state.chart_id = chart_id;
    state.revision = revision;
    state.first_slot = first_slot;
    if (reason) state.reason = reason;
    if (first_slot == 0 || first_slot > 1295 || item_count == 0 || !items) {
        if (state.reason.empty()) state.reason = "Choose a valid WAV slot and at least one file.";
        return;
    }
    if (item_count > 1295 || item_count - 1 > 1295 - first_slot) {
        state.reason = "The selected files exceed the remaining WAV slots through ZZ.";
    }
    state.items.reserve(item_count);
    for (std::size_t index = 0; index < item_count; ++index) {
        const auto& item = items[index];
        if (!item.path || !item.name || !item.occupied_name) {
            state.reason = "The staged WAV slot list contains an invalid item.";
            state.items.clear();
            return;
        }
        state.items.push_back({.path = item.path, .name = item.name,
            .occupied_resource_id = item.occupied_resource_id,
            .occupied_name = item.occupied_name, .ambiguous = item.ambiguous});
    }
}

void worm_ui_sound_slot_batch_cancel(worm_ui_sound_slot_batch_t& state) {
    state.open = false;
    state.replacement_confirmed = false;
    state.commit_pending = false;
    state.items.clear();
    state.action_items.clear();
}

void worm_ui_sound_slot_batch_result(worm_ui_sound_slot_batch_t& state, bool applied,
                                     const char* message) {
    state.commit_pending = false;
    state.reason = message ? message : "";
    if (applied) worm_ui_sound_slot_batch_cancel(state);
}

void worm_ui_nuklear_render_sound_slot_batch(nk_context* context,
                                            worm_ui_sound_slot_batch_t& state,
                                            const WormUiComposeView& view,
                                            WormUiWorkspaceAction& action,
                                            int window_width, int window_height) {
    if (!state.open) return;
    if (stale_target(state, view)) {
        close_with_reason(state, "The WAV slot target changed; review the files again.");
        return;
    }
    if (nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE) &&
        !state.commit_pending) {
        worm_ui_sound_slot_batch_cancel(state);
        return;
    }
    const auto bounds = window_bounds(window_width, window_height);
    if (!nk_begin(context, "Assign BMS WAV slots", bounds,
                  NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(context);
        return;
    }
    draw_batch_header(context, state);
    draw_batch_rows(context, state, bounds.h - 200.0f);
    nk_layout_row_dynamic(context, 26.0f, 1);
    if (has_occupants(state))
        nk_label(context, "Check each occupied slot you intend to replace.", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 30.0f, 2);
    const auto valid_count = !state.items.empty() && state.items.size() <= 1295 &&
        static_cast<std::size_t>(state.first_slot) + state.items.size() - 1 <= 1295;
    const auto can_confirm = !state.commit_pending && state.reason.empty() && valid_count && replacements_ready(state) &&
        std::ranges::none_of(state.items, [](const auto& item) { return item.ambiguous; }) &&
        action.compose_action == WORM_UI_COMPOSE_ACTION_NONE;
    if (!can_confirm) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Assign files") && can_confirm) {
        refresh_action_items(state);
        action.compose_action = WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH;
        action.compose_input_chart_id = state.chart_id;
        action.compose_input_document_revision = state.revision;
        action.sound_import_project_id = state.project_id;
        action.sound_import_items = state.action_items.data();
        action.sound_import_item_count = state.action_items.size();
        action.compose_sound_slot = state.first_slot;
        action.compose_sound_slot_confirmed = has_occupants(state);
        state.commit_pending = true;
    }
    if (!can_confirm) nk_widget_disable_end(context);
    if (state.commit_pending) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Cancel") && !state.commit_pending)
        worm_ui_sound_slot_batch_cancel(state);
    if (state.commit_pending) nk_widget_disable_end(context);
    nk_end(context);
}
