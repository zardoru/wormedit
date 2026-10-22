#include "ui/nuklear/resources/sound_slot_batch.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/dialog.h"
#include "ui/nuklear/theme.h"

#include <algorithm>
#include <cstdio>
#include <ranges>
#include <string_view>

#include "wormedit/nuklear_config.h"

namespace chrome = wormedit::ui::chrome;
namespace nuklear = wormedit::ui::nuklear;

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

void draw_batch_header(nk_context* context, chrome::Dialog& dialog,
                       const worm_ui_sound_slot_batch_t& state) {
    char label[128];
    std::snprintf(label, sizeof(label), "Assign %zu files from WAV %s", state.items.size(),
                  slot_label(state.first_slot).c_str());
    nk_layout_row_dynamic(context, 15.0f, 1);
    const auto bounds = nk_widget_bounds(context);
    nk_label_colored(context, label, NK_TEXT_LEFT, chrome::color(chrome::UiColor::text_bright));
    if (nuklear::tooltip::hovered(context, bounds))
        nuklear::tooltip::text(context,
            "Files are ordered by natural filename and assigned to consecutive slots. "
            "Occupied slots keep their sound identity and links when replaced.");
    if (!state.reason.empty())
        chrome::dialog_text(dialog, context, state.reason.c_str(), chrome::color(chrome::UiColor::conflict));
}

void draw_batch_rows(nk_context* context, worm_ui_sound_slot_batch_t& state,
                     float list_height) {
    nk_layout_row_dynamic(context, list_height, 1);
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

auto stale_target(const worm_ui_sound_slot_batch_t& state, const wormedit::ui::compose_view_t& view)
    -> bool {
    return !view.sound_slots_mode || view.project_id.value != state.project_id ||
           view.chart_id.value != state.chart_id || view.document_revision != state.revision;
}
} // namespace

void worm_ui_sound_slot_batch_stage(worm_ui_sound_slot_batch_t& state,
                                    std::uint64_t project_id, std::uint64_t chart_id,
                                    std::uint64_t revision, std::uint16_t first_slot,
                                    const wormedit::ui::sound_slot_import_item_t* items,
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
    if (item_count > 1295 || item_count - 1 > static_cast<std::size_t>(1295 - first_slot)) {
        state.reason = "The selected files exceed the remaining WAV slots through ZZ.";
    }
    state.items.reserve(item_count);
    for (std::size_t index = 0; index < item_count; ++index) {
        const auto& item = items[index];
        state.items.push_back({.path = item.path, .name = item.name,
            .occupied_resource_id = item.occupied_resource_id.value,
            .occupied_name = item.occupied_name, .ambiguous = item.ambiguous});
    }
}

void worm_ui_sound_slot_batch_cancel(worm_ui_sound_slot_batch_t& state) {
    state.open = false;
    state.replacement_confirmed = false;
    state.commit_pending = false;
    state.items.clear();
}

void worm_ui_sound_slot_batch_result(worm_ui_sound_slot_batch_t& state, bool applied,
                                     const char* message) {
    state.commit_pending = false;
    state.reason = message ? message : "";
    if (applied) worm_ui_sound_slot_batch_cancel(state);
}

void worm_ui_nuklear_render_sound_slot_batch(nk_context* context,
                                            worm_ui_sound_slot_batch_t& state,
                                            const wormedit::ui::compose_view_t& view,
                                            wormedit::ui::workspace_action_t& action,
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
    static chrome::Dialog dialog;
    // The list shows up to ten rows and scrolls beyond that, so the dialog fits its content.
    constexpr float row_height = 36.0f;
    const auto list_height = std::clamp(static_cast<float>(state.items.size()) * row_height + 4.0f,
                                        70.0f, 10.0f * row_height + 4.0f);
    const auto valid_count = !state.items.empty() && state.items.size() <= 1295 &&
        static_cast<std::size_t>(state.first_slot) + state.items.size() - 1 <= 1295;
    const auto can_confirm = !state.commit_pending && state.reason.empty() && valid_count && replacements_ready(state) &&
        std::ranges::none_of(state.items, [](const auto& item) { return item.ambiguous; }) &&
        action.compose_action == WORM_UI_COMPOSE_ACTION_NONE;
    if (chrome::dialog_begin(dialog, context, "Assign BMS WAV slots", "Assign BMS WAV slots",
                             {.modal = true, .width = 850.0f,
                              .area = nk_vec2(static_cast<float>(window_width),
                                              static_cast<float>(window_height))})) {
        draw_batch_header(context, dialog, state);
        draw_batch_rows(context, state, list_height);
        if (has_occupants(state)) {
            chrome::dialog_text(dialog, context, "Check each occupied slot you intend to replace.",
                                chrome::color(chrome::UiColor::text_muted));
        }
        chrome::dialog_footer_begin(dialog, context, 2, false, 120.0f);
        if (chrome::dialog_button(dialog, context, "Cancel", chrome::DialogButtonRole::secondary,
                                  !state.commit_pending))
            worm_ui_sound_slot_batch_cancel(state);
        if (chrome::dialog_button(dialog, context, "Assign files", chrome::DialogButtonRole::primary,
                                  can_confirm)) {
            action.compose_action = WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH;
            action.compose_input_chart_id.value = state.chart_id;
            action.compose_input_document_revision = state.revision;
            action.sound_import_project_id = wormedit::song_id_t{state.project_id};
            for (const auto& item : state.items)
                action.sound_import_items.push_back({.path = item.path, .name = item.name,
                    .kind = wormedit::sound_kind_t::keysound});

            action.compose_sound_slot = state.first_slot;
            action.compose_sound_slot_confirmed = has_occupants(state);
            state.commit_pending = true;
        }
    }
    (void)chrome::dialog_end(dialog, context);
}
