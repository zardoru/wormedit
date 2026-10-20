#include "ui/nuklear/sound_import_dialog.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/dialog.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <nuklear.h>

namespace {

static_assert(static_cast<unsigned>(wormedit::sound_kind_t::stem) == WORM_UI_SOUND_IMPORT_STEM);
static_assert(static_cast<unsigned>(wormedit::sound_kind_t::keysound) == WORM_UI_SOUND_IMPORT_KEYSOUND);
static_assert(static_cast<unsigned>(wormedit::sound_kind_t::sample) == WORM_UI_SOUND_IMPORT_SAMPLE);

constexpr float row_height = 26.0F;

auto member_count(const worm_ui_sound_import_state_t& state, std::size_t group) -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count_if(state.items, [group](const auto& item) {
        return item.group_index == std::optional<std::size_t>{group};
    }));
}

void render_kind_choice(nk_context* context, worm_ui_sound_import_item_t& item,
                        const wormedit::ui::kind_catalog_view_t<wormedit::sound_kind_t>& catalog) {
    const auto* label = catalog.label(static_cast<wormedit::sound_kind_t>(item.kind), "Sound");
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, label, nk_vec2(130, 120))) return;
    nk_layout_row_dynamic(context, 24, 1);
    // Keep the import dialog's Keysound, Stem, Sample order; unspecified is not offered.
    constexpr worm_ui_sound_import_kind_t order[]{WORM_UI_SOUND_IMPORT_KEYSOUND,
        WORM_UI_SOUND_IMPORT_STEM, WORM_UI_SOUND_IMPORT_SAMPLE};
    for (const auto kind : order)
        if (nk_combo_item_label(context, catalog.label(static_cast<wormedit::sound_kind_t>(kind), "Sound"), NK_TEXT_LEFT))
            item.kind = kind;
    wormedit::ui::chrome::safe_combo_end(context);
}

void render_import_items(nk_context* context, worm_ui_sound_import_state_t& state,
                         const wormedit::ui::kind_catalog_view_t<wormedit::sound_kind_t>& catalog) {
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Files", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 112, 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Sound import files", NK_WINDOW_BORDER,
                            static_cast<int>(row_height),
                            static_cast<int>(std::max<std::size_t>(state.items.size(), 1)))) return;
    if (state.items.empty()) {
        nk_layout_row_dynamic(context, row_height, 1);
        nk_label(context, "No audio files are staged.", NK_TEXT_LEFT);
    }
    for (int index = list.begin; index < std::min<int>(list.end, state.items.size()); ++index) {
        auto& item = state.items[static_cast<std::size_t>(index)];
        nk_layout_row_template_begin(context, row_height);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_static(context, 145);
        nk_layout_row_template_push_static(context, 34);
        nk_layout_row_template_end(context);
        const auto previous_name = item.name;
        std::array<char, 256> name{};
        std::snprintf(name.data(), name.size(), "%s", item.name.c_str());
        (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
            context, NK_EDIT_FIELD, name.data(), static_cast<int>(name.size()), nk_filter_default);
        if (std::string_view{name.data()} != previous_name)
            item.name = name.data();
        if (state.fixed_kind)
            nk_label(context, catalog.label(static_cast<wormedit::sound_kind_t>(state.fixed_kind == WORM_UI_SOUND_IMPORT_STEM
                    ? WORM_UI_SOUND_IMPORT_STEM : WORM_UI_SOUND_IMPORT_KEYSOUND), "Sound"),
                      NK_TEXT_LEFT);
        else render_kind_choice(context, item, catalog);
        if (nk_button_label(context, "×"))
            worm_ui_nuklear_sound_import_remove(state, static_cast<std::size_t>(index));
    }
    nk_list_view_end(&list);
}

void render_member_move(nk_context* context, worm_ui_sound_import_state_t& state,
                        std::size_t member) {
    const auto group = state.items[member].group_index;
    const auto label = group ? state.groups[*group].name : std::string{"Ungrouped"};
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, label.c_str(), nk_vec2(200, 220))) return;
    nk_layout_row_dynamic(context, 22, 1);
    if (nk_combo_item_label(context, "Ungrouped", NK_TEXT_LEFT))
        worm_ui_nuklear_sound_import_move_member(state, member, std::nullopt);
    for (std::size_t index = 0; index < state.groups.size(); ++index) {
        if (nk_combo_item_label(context, state.groups[index].name.c_str(), NK_TEXT_LEFT))
            worm_ui_nuklear_sound_import_move_member(state, member, index);
    }
    wormedit::ui::chrome::safe_combo_end(context);
}

auto member_matches(const worm_ui_sound_import_item_t& item,
                    std::optional<std::size_t> group) -> bool {
    return item.group_index == group;
}

void render_group_proposals(nk_context* context,
                            worm_ui_keysound_group_review_t& state) {
    nk_layout_row_dynamic(context, 170, 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Suggested existing sound groups", NK_WINDOW_BORDER,
                            static_cast<int>(row_height),
                            static_cast<int>(std::max<std::size_t>(state.groups.size(), 1)))) return;
    for (int row = list.begin; row < std::min<int>(list.end, state.groups.size()); ++row) {
        const auto index = static_cast<std::size_t>(row);
        auto& group = state.groups[index];
        nk_layout_row_template_begin(context, row_height);
        nk_layout_row_template_push_static(context, 28);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_static(context, 80);
        nk_layout_row_template_end(context);
        int accepted = group.accepted ? 1 : 0;
        nk_checkbox_label(context, "", &accepted);
        worm_ui_keysound_group_review_set_accepted(state, index, accepted != 0);
        std::array<char, 65> name{};
        std::snprintf(name.data(), name.size(), "%s", group.name.c_str());
        if (wormedit::ui::chrome::worm_ui_nuklear_text_field(
                context, NK_EDIT_FIELD, name.data(), static_cast<int>(name.size()), nk_filter_default))
            worm_ui_keysound_group_review_rename(state, index, name.data());
        char count[32];
        std::snprintf(count, sizeof(count), "%zu sounds", group.member_ids.size());
        nk_label(context, count, NK_TEXT_LEFT);
    }
    nk_list_view_end(&list);
}

auto candidate_group_index(const worm_ui_keysound_group_review_t& state,
                           std::uint64_t id) -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < state.groups.size(); ++index)
        if (std::ranges::find(state.groups[index].member_ids, id) !=
            state.groups[index].member_ids.end()) return index;
    return std::nullopt;
}

void render_group_candidate(nk_context* context, worm_ui_keysound_group_review_t& state,
                            const worm_ui_keysound_group_candidate_t& candidate) {
    nk_layout_row_template_begin(context, row_height);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 170);
    nk_layout_row_template_end(context);
    nk_label(context, candidate.name.c_str(), NK_TEXT_LEFT);
    const auto current = candidate_group_index(state, candidate.id);
    const char* label = current ? state.groups[*current].name.c_str() : "Ungrouped";
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, label, nk_vec2(200, 200))) return;
    nk_layout_row_dynamic(context, 22, 1);
    if (nk_combo_item_label(context, "Ungrouped", NK_TEXT_LEFT))
        worm_ui_keysound_group_review_move(state, candidate.id, std::nullopt);
    for (std::size_t index = 0; index < state.groups.size(); ++index)
        if (nk_combo_item_label(context, state.groups[index].name.c_str(), NK_TEXT_LEFT))
            worm_ui_keysound_group_review_move(state, candidate.id, index);
    wormedit::ui::chrome::safe_combo_end(context);
}

void render_group_candidates(nk_context* context,
                             worm_ui_keysound_group_review_t& state) {
    nk_layout_row_dynamic(context, 146, 1);
    struct nk_list_view members{};
    const auto member_count = static_cast<int>(state.candidates.size());
    if (!nk_list_view_begin(context, &members, "Suggested members", NK_WINDOW_BORDER,
                            static_cast<int>(row_height), std::max(member_count, 1))) return;
    for (int row = members.begin; row < std::min(members.end, member_count); ++row)
        render_group_candidate(context, state, state.candidates[static_cast<std::size_t>(row)]);
    nk_list_view_end(&members);
}

void render_keysound_group_rows(nk_context* context,
                                worm_ui_keysound_group_review_t& state) {
    nk_layout_row_dynamic(context, wormedit::ui::metrics::control_default, 1);
    const auto leave_bounds = nk_widget_bounds(context);
    if (nk_button_label(context, "Leave all ungrouped"))
        worm_ui_keysound_group_review_leave_all_ungrouped(state);
    if (wormedit::ui::nuklear::tooltip::hovered(context, leave_bounds))
        wormedit::ui::nuklear::tooltip::text(context,
            "Edit group names or move sounds between groups, or leave them all ungrouped.");
    render_group_proposals(context, state);
    render_group_candidates(context, state);
}

void render_member_list(nk_context* context, worm_ui_sound_import_state_t& state,
                        std::optional<std::size_t> group, const char* title) {
    const auto count = group ? member_count(state, *group)
                             : static_cast<std::size_t>(std::ranges::count_if(
                                   state.items, [](const auto& item) {
                                       return !item.group_index.has_value();
                                   }));
    nk_layout_row_dynamic(context, 104, 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, title, NK_WINDOW_BORDER,
                            static_cast<int>(row_height), static_cast<int>(count))) return;
    std::size_t member_row = 0;
    for (std::size_t index = 0; index < state.items.size(); ++index) {
        if (!member_matches(state.items[index], group)) continue;
        if (member_row >= static_cast<std::size_t>(list.begin) &&
            member_row < static_cast<std::size_t>(list.end)) {
            nk_layout_row_template_begin(context, row_height);
            nk_layout_row_template_push_dynamic(context);
            nk_layout_row_template_push_static(context, 200);
            nk_layout_row_template_end(context);
            nk_label(context, state.items[index].name.c_str(), NK_TEXT_LEFT);
            render_member_move(context, state, index);
        }
        ++member_row;
    }
    nk_list_view_end(&list);
}

void render_group_rows(nk_context* context, worm_ui_sound_import_state_t& state) {
    if (state.groups.empty()) return;
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Groups", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 1);
    if (nk_button_label(context, "Leave all ungrouped"))
        worm_ui_nuklear_sound_import_leave_all_ungrouped(state);
    nk_layout_row_dynamic(context, 138, 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Suggested sound groups", NK_WINDOW_BORDER,
                            static_cast<int>(row_height),
                            static_cast<int>(std::max<std::size_t>(state.groups.size(), 1)))) return;
    for (int visible = list.begin; visible < std::min<int>(list.end, state.groups.size()); ++visible) {
        const auto index = static_cast<std::size_t>(visible);
        auto& group = state.groups[index];
        nk_layout_row_template_begin(context, row_height);
        nk_layout_row_template_push_static(context, 34);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_static(context, 110);
        nk_layout_row_template_push_static(context, 84);
        nk_layout_row_template_end(context);
        int accepted = group.accepted ? 1 : 0;
        nk_checkbox_label(context, "", &accepted);
        worm_ui_nuklear_sound_import_set_group_accepted(state, index, accepted != 0);
        std::array<char, 65> name{};
        std::snprintf(name.data(), name.size(), "%s", group.name.c_str());
        if (wormedit::ui::chrome::worm_ui_nuklear_text_field(
                context, NK_EDIT_FIELD, name.data(), static_cast<int>(name.size()), nk_filter_default))
            worm_ui_nuklear_sound_import_rename_group(state, index, name.data());
        char count[40];
        std::snprintf(count, sizeof(count), "%zu files", member_count(state, index));
        nk_label(context, count, NK_TEXT_LEFT);
        if (nk_button_label(context, group.expanded ? "Hide files" : "Show files"))
            worm_ui_nuklear_sound_import_set_group_expanded(state, index, !group.expanded);
    }
    nk_list_view_end(&list);
    const auto expanded = std::ranges::find(state.groups, true,
                                             &worm_ui_sound_import_group_t::expanded);
    if (expanded != state.groups.end())
        render_member_list(context, state,
                           static_cast<std::size_t>(expanded - state.groups.begin()),
                           "Group members");
    const auto ungrouped_count = static_cast<std::size_t>(std::ranges::count_if(
        state.items, [](const auto& item) { return !item.group_index.has_value(); }));
    char ungrouped[80];
    std::snprintf(ungrouped, sizeof(ungrouped), "Ungrouped · %zu files", ungrouped_count);
    nk_layout_row_dynamic(context, 24, 1);
    if (nk_button_label(context, state.ungrouped_expanded ? "Hide ungrouped files" : ungrouped))
        worm_ui_nuklear_sound_import_set_ungrouped_expanded(state,
                                                            !state.ungrouped_expanded);
    if (state.ungrouped_expanded)
        render_member_list(context, state, std::nullopt, "Ungrouped sound files");
}

} // namespace

auto worm_ui_nuklear_render_sound_import_dialog(nk_context* context,
                                                worm_ui_sound_import_state_t& state,
                                                int window_width, int window_height,
                                                const wormedit::ui::kind_catalog_view_t<wormedit::sound_kind_t>& catalog)
    -> worm_ui_sound_import_dialog_action_t {
    if (!context || state.phase == worm_ui_sound_import_phase_t::closed)
        return worm_ui_sound_import_dialog_action_t::none;
    namespace chrome = wormedit::ui::chrome;
    static chrome::Dialog dialog;
    auto result = worm_ui_sound_import_dialog_action_t::none;
    // The review body scrolls inside a fixed-height group; the dialog fits around it.
    constexpr float body_height = 400.0F;
    if (chrome::dialog_begin(dialog, context, "Review sound import", "Review sound import",
                             {.modal = true, .width = 720.0F,
                              .area = nk_vec2(static_cast<float>(window_width),
                                              static_cast<float>(window_height))})) {
        nk_layout_row_dynamic(context, body_height, 1);
        if (nk_group_begin(context, "Sound import review body", NK_WINDOW_BORDER)) {
            render_import_items(context, state, catalog);
            render_group_rows(context, state);
            nk_group_end(context);
        }
        if (!state.message.empty())
            chrome::dialog_text(dialog, context, state.message.c_str(), chrome::color(chrome::UiColor::conflict));
        chrome::dialog_footer_begin(dialog, context, 2, false, 120.0F);
        if (chrome::dialog_button(dialog, context, "Cancel", chrome::DialogButtonRole::secondary))
            result = worm_ui_sound_import_dialog_action_t::cancel;
        if (chrome::dialog_button(dialog, context, "Confirm import", chrome::DialogButtonRole::primary))
            result = worm_ui_sound_import_dialog_action_t::confirm;
    }
    if (chrome::dialog_end(dialog, context) == chrome::DialogResult::cancel)
        result = worm_ui_sound_import_dialog_action_t::cancel;
    return result;
}

auto& group_review_dialog() {
    static wormedit::ui::chrome::Dialog dialog;
    return dialog;
}

auto group_review_stale(const WormUiComposeView& view,
                        const worm_ui_keysound_group_review_t& state) -> bool {
    return state.project_id != view.project_id || state.chart_id != view.chart_id ||
           state.document_revision != view.document_revision;
}

void render_manual_group_contents(nk_context* context, const WormUiComposeView& view,
                                  worm_ui_keysound_group_review_t& state) {
    nk_layout_row_dynamic(context, wormedit::ui::metrics::control_default, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, NK_EDIT_FIELD, state.manual_group_name.data(),
        static_cast<int>(state.manual_group_name.size()), nk_filter_default);
    nk_layout_row_dynamic(context, wormedit::ui::metrics::control_default, 1);
    const char* destination = "Create new group";
    if (state.target_group_id != 0 && view.compose_sound_groups) {
        const auto* end = view.compose_sound_groups + view.compose_sound_group_count;
        const auto group = std::ranges::find(view.compose_sound_groups, end,
            state.target_group_id, &WormUiComposeSoundGroupView::group_id);
        if (group != end) destination = group->name;
    }
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, destination, nk_vec2(280, 250))) return;
    nk_layout_row_dynamic(context, 22, 1);
    if (nk_combo_item_label(context, "Create new group", NK_TEXT_LEFT)) state.target_group_id = 0;
    for (std::size_t index = 0; index < view.compose_sound_group_count; ++index)
        if (nk_combo_item_label(context, view.compose_sound_groups[index].name, NK_TEXT_LEFT))
            state.target_group_id = view.compose_sound_groups[index].group_id;
    wormedit::ui::chrome::safe_combo_end(context);
}

auto manual_assignment(const worm_ui_keysound_group_review_t& state)
    -> std::optional<worm_ui_keysound_group_assignment_t> {
    auto assignment = worm_ui_keysound_group_assignment_t{
        .name = state.target_group_id == 0 ? std::string{state.manual_group_name.data()} : std::string{},
        .existing_group_id = state.target_group_id};
    for (const auto& candidate : state.candidates) assignment.sound_ids.push_back(candidate.id);
    if (assignment.sound_ids.empty() || (state.target_group_id == 0 && assignment.name.empty()))
        return std::nullopt;
    return assignment;
}

auto reviewed_assignments(worm_ui_keysound_group_review_t& state)
    -> std::vector<worm_ui_keysound_group_assignment_t> {
    if (state.manual) {
        const auto assignment = manual_assignment(state);
        if (assignment) return {*assignment};
        if (state.target_group_id == 0) state.message = "Enter a group name.";
        return {};
    }
    return worm_ui_keysound_group_review_confirm(state);
}

void submit_group_review(wormedit::ui::workspace_action_t& action, const WormUiComposeView& view,
                         worm_ui_keysound_group_review_t& state,
                         bool& release_pending) {
    const auto assignments = reviewed_assignments(state);
    if (!assignments.empty()) {
        worm_ui_keysound_group_review_stage_payload(state, assignments, state.keep_ids_together);
        action.compose_action = WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS;
        action.compose_input_chart_id = view.chart_id;
        action.compose_input_document_revision = view.document_revision;
        action.compose_sound_group_assignments = state.staged_payload.data();
        action.compose_sound_group_assignment_count = state.staged_payload.size();
        action.compose_sound_group_keep_ids_together = state.keep_ids_together;
        state.open = false;
        release_pending = true;
    } else if (!state.manual && state.message.empty()) {
        worm_ui_keysound_group_review_cancel(state);
        release_pending = true;
    }
}

void render_group_review_footer(nk_context* context, const WormUiComposeView& view,
                                wormedit::ui::workspace_action_t& action,
                                worm_ui_keysound_group_review_t& state,
                                bool& release_pending, bool stale) {
    namespace chrome = wormedit::ui::chrome;
    auto& dialog = group_review_dialog();
    int keep = state.keep_ids_together ? 1 : 0;
    nk_layout_row_dynamic(context, wormedit::ui::metrics::control_default, 1);
    nk_checkbox_label(context, "Keep IDs together", &keep);
    state.keep_ids_together = keep != 0;
    if (!state.message.empty())
        chrome::dialog_text(dialog, context, state.message.c_str(), chrome::color(chrome::UiColor::conflict));
    chrome::dialog_footer_begin(dialog, context, 2);
    if (chrome::dialog_button(dialog, context, "Cancel", chrome::DialogButtonRole::secondary)) {
        worm_ui_keysound_group_review_cancel(state);
        release_pending = true;
    }
    if (chrome::dialog_button(dialog, context, "Confirm", chrome::DialogButtonRole::primary) && !stale &&
        action.compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        submit_group_review(action, view, state, release_pending);
}

void worm_ui_nuklear_render_keysound_group_review(
    nk_context* context, const WormUiComposeView& view, wormedit::ui::workspace_action_t& action,
    worm_ui_keysound_group_review_t& state, bool& release_pending,
    int window_width, int window_height) {
    namespace chrome = wormedit::ui::chrome;
    if (!context || !state.open) return;
    auto& dialog = group_review_dialog();
    if (chrome::dialog_begin(dialog, context, "Group selected sounds", "Group selected sounds",
                             {.modal = true, .width = 700.0F,
                              .area = nk_vec2(static_cast<float>(window_width),
                                              static_cast<float>(window_height)),
                              .no_input = release_pending})) {
        const auto stale = group_review_stale(view, state);
        if (stale) {
            state.message = "The sound list changed. Close this review and suggest again.";
        } else if (state.manual) {
            render_manual_group_contents(context, view, state);
        } else {
            render_keysound_group_rows(context, state);
        }
        render_group_review_footer(context, view, action, state, release_pending, stale);
    }
    // Escape cancels like the Cancel button.
    if (chrome::dialog_end(dialog, context) == chrome::DialogResult::cancel && state.open) {
        worm_ui_keysound_group_review_cancel(state);
        release_pending = true;
    }
}
