#include "common/file_dialog.h"
#include "ui/frontend/workspace.h"
#include "ui/nuklear/resources/sound_import_runtime.h"
#include "ui/nuklear/resources/sound_import_dialog.h"

#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <memory>
#include <mutex>

namespace {
worm_ui_sound_import_state_t pending;
struct picker_target_t { std::uint64_t project{}, chart{}, revision{}, generation{}; wormedit::ui::sound_import_kind_t kind{}; };
struct picker_state_t {
    std::mutex mutex;
    bool open{}, ready{};
    std::uint64_t generation{};
    picker_target_t target;
    std::vector<std::string> paths;
    void invalidate() {
        std::lock_guard lock{mutex};
        ++generation;
        open = false;
        ready = false;
        paths.clear();
    }
} picker;

void SDLCALL picker_callback(void* userdata, const char* const* paths, int) {
    const std::unique_ptr<picker_target_t> target{static_cast<picker_target_t*>(userdata)};
    std::lock_guard lock{picker.mutex};
    if (target->generation != picker.generation) return;
    picker.open = false;
    picker.ready = true;
    picker.target = *target;
    picker.paths.clear();
    for (std::size_t index = 0; paths && paths[index]; ++index)
        picker.paths.emplace_back(paths[index]);
}

void submit_import(wormedit::ui::workspace_action_t& action) {
    auto submitted = worm_ui_nuklear_sound_import_confirm(pending);
    if (!submitted) return;
    for (const auto& item : submitted->items)
        action.sound_import_items.push_back({item.path, item.name,
            item.kind, pending.fixed_kind != wormedit::ui::sound_import_kind_t::generic});
    for (const auto& group : submitted->groups)
        action.sound_import_groups.push_back({group.name,
            group.existing_group.value_or(wormedit::sound_group_id_t{}), group.member_indexes});
    action.compose_action = WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT;
    action.sound_import_project_id = wormedit::song_id_t{pending.project_id};
    action.sound_import_kind = pending.fixed_kind;
    action.compose_input_chart_id.value = pending.chart_id;
    action.compose_input_document_revision = pending.document_revision;
}
} // namespace

auto worm_ui_nuklear_pending_sound_import() -> worm_ui_sound_import_state_t& { return pending; }
bool worm_ui_nuklear_sound_import_active() {
    std::lock_guard lock{picker.mutex};
    return pending.phase != worm_ui_sound_import_phase_t::closed || picker.open;
}

void worm_ui_nuklear_reset_sound_import() {
    pending = {};
    picker.invalidate();
}

bool worm_ui_frontend_stage_sound_import(std::uint64_t project, std::uint64_t chart,
    std::uint64_t revision, const wormedit::ui::sound_import_item_t* items, std::size_t item_count,
    const wormedit::ui::sound_import_group_t* existing_groups, std::size_t group_count,
    bool chart_has_bgm, const char* message) {
    if (!items || item_count == 0 || chart == 0)
        return false;
    const bool same_target = pending.project_id == project && pending.chart_id == chart &&
                             pending.document_revision == revision;
    if (!same_target) pending = {};
    std::vector<std::string> paths;
    for (std::size_t index = 0; index < item_count; ++index) {
        if (items[index].path.empty() || items[index].name.empty()) return false;
        paths.emplace_back(items[index].path);
    }
    worm_ui_nuklear_sound_import_set_target(pending, project, chart, revision);
    worm_ui_nuklear_sound_import_fix_kind(pending, items[0].kind_fixed ? static_cast<wormedit::ui::sound_import_kind_t>(items[0].kind) : wormedit::ui::sound_import_kind_t::generic);
    worm_ui_nuklear_sound_import_add(pending, paths, chart_has_bgm);
    for (auto& item : pending.items) {
        const auto found = std::find_if(items, items + item_count, [&](const auto& candidate) {
            return item.path == candidate.path;
        });
        if (found != items + item_count) { item.name = found->name; item.kind = pending.fixed_kind != wormedit::ui::sound_import_kind_t::generic ? static_cast<wormedit::sound_kind_t>(pending.fixed_kind) : found->kind; }
    }
    std::vector<worm_ui_sound_import_existing_group_t> groups;
    for (std::size_t index = 0; existing_groups && index < group_count; ++index)
        groups.push_back({wormedit::sound_group_id_t{existing_groups[index].existing_group_id},
                          existing_groups[index].name});
    worm_ui_nuklear_sound_import_suggest_groups(pending, groups);
    if (pending.phase == worm_ui_sound_import_phase_t::ready)
        pending.phase = worm_ui_sound_import_phase_t::review_list;
    if (message && message[0]) pending.message = message;
    return !pending.items.empty();
}

void worm_ui_frontend_sound_import_result(bool applied, const char* message) {
    if (applied) worm_ui_nuklear_sound_import_commit_succeeded(pending);
    else worm_ui_nuklear_sound_import_commit_failed(pending, message ? message : "Import failed.");
}

void worm_ui_nuklear_sound_import_pick(SDL_Window* window, const wormedit::ui::workspace_view_t& view, wormedit::ui::sound_import_kind_t kind) {
    if (!view.project_loaded || !view.selected_chart_id.value) return;
    picker_target_t* target{};
    {
        std::lock_guard lock{picker.mutex};
        if (picker.open) return;
        picker.open = true;
        picker.ready = false;
        picker.paths.clear();
        target = new picker_target_t{view.project_id.value, view.selected_chart_id.value,
                                     view.document_revision, picker.generation, kind};
        picker.target = *target;
    }
    wormedit::platform::show_open_file_dialog(picker_callback, target, window, nullptr, 0, nullptr, true);
}

void worm_ui_nuklear_consume_sound_picker(const wormedit::ui::workspace_view_t& view,
                                         wormedit::ui::workspace_action_t& action) {
    std::lock_guard lock{picker.mutex};
    if (!picker.ready || action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    picker.ready = false;
    const bool same_target = picker.target.project == view.project_id.value &&
        picker.target.chart == view.selected_chart_id.value && picker.target.revision == view.document_revision;
    if (!same_target || picker.paths.empty()) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES;
    action.sound_import_project_id = view.project_id;
    action.sound_import_kind = picker.target.kind;
    action.compose_input_chart_id.value = view.selected_chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    action.compose_import_paths = std::move(picker.paths);
}

void worm_ui_nuklear_render_pending_sound_import(nk_context* context,
    const wormedit::ui::workspace_view_t& view, wormedit::ui::workspace_action_t& action, int width, int height) {
    if (pending.phase == worm_ui_sound_import_phase_t::closed) return;
    if (!view.project_loaded || pending.project_id != view.project_id.value ||
        pending.chart_id != view.selected_chart_id.value) {
        worm_ui_nuklear_sound_import_cancel(pending);
        return;
    }
    const auto intent = worm_ui_nuklear_render_sound_import_dialog(context, pending, width, height, view.sound_kinds);
    if (intent == worm_ui_sound_import_dialog_action_t::cancel)
        worm_ui_nuklear_sound_import_cancel(pending);
    else if (intent == worm_ui_sound_import_dialog_action_t::confirm &&
             action.compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        submit_import(action);
}
