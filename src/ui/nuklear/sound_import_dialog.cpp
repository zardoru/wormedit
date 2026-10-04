#include "ui/nuklear/sound_import_dialog.h"
#include "ui/nuklear/chrome.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <nuklear.h>

namespace {

constexpr float row_height = 26.0F;

auto member_count(const worm_ui_sound_import_state_t& state, std::size_t group) -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count_if(state.items, [group](const auto& item) {
        return item.group_index == std::optional<std::size_t>{group};
    }));
}

void render_kind_choice(nk_context* context, worm_ui_sound_import_item_t& item) {
    const char* label = "Sound";
    if (item.kind == WORM_UI_SOUND_IMPORT_STEM) label = "Stem";
    else if (item.kind == WORM_UI_SOUND_IMPORT_SAMPLE) label = "Sample";
    if (!wormedit::ui::chrome::safe_combo_begin_label(context, label, nk_vec2(130, 120))) return;
    nk_layout_row_dynamic(context, 24, 1);
    if (nk_combo_item_label(context, "Sound", NK_TEXT_LEFT)) item.kind = WORM_UI_SOUND_IMPORT_KEYSOUND;
    if (nk_combo_item_label(context, "Stem", NK_TEXT_LEFT)) item.kind = WORM_UI_SOUND_IMPORT_STEM;
    if (nk_combo_item_label(context, "Sample", NK_TEXT_LEFT)) item.kind = WORM_UI_SOUND_IMPORT_SAMPLE;
    nk_combo_end(context);
}

void render_import_items(nk_context* context, worm_ui_sound_import_state_t& state) {
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
        render_kind_choice(context, item);
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
    nk_combo_end(context);
}

auto member_matches(const worm_ui_sound_import_item_t& item,
                    std::optional<std::size_t> group) -> bool {
    return item.group_index == group;
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
                                                int window_width, int window_height)
    -> worm_ui_sound_import_dialog_action_t {
    if (!context || state.phase == worm_ui_sound_import_phase_t::closed)
        return worm_ui_sound_import_dialog_action_t::none;
    const auto width = static_cast<float>(std::clamp(window_width - 32, 1, 720));
    const auto height = static_cast<float>(std::clamp(window_height - 32, 1, 680));
    const auto bounds = nk_rect((static_cast<float>(window_width) - width) / 2.0F,
                                (static_cast<float>(window_height) - height) / 2.0F,
                                width, height);
    if (!nk_begin(context, "Review sound import", bounds,
                   NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE))
        return worm_ui_sound_import_dialog_action_t::none;
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "Review names, kinds, and suggested groups before importing.", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, std::max(140.0F, height - 190.0F), 1);
    if (nk_group_begin(context, "Sound import review body", NK_WINDOW_BORDER)) {
        render_import_items(context, state);
        render_group_rows(context, state);
        nk_group_end(context);
    }
    if (!state.message.empty()) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label_colored(context, state.message.c_str(), NK_TEXT_LEFT, nk_rgb(226, 166, 64));
    }
    nk_layout_row_dynamic(context, 30, 2);
    auto result = worm_ui_sound_import_dialog_action_t::none;
    if (nk_button_label(context, "Cancel")) result = worm_ui_sound_import_dialog_action_t::cancel;
    if (nk_button_label(context, "Confirm import"))
        result = worm_ui_sound_import_dialog_action_t::confirm;
    nk_end(context);
    return result;
}
