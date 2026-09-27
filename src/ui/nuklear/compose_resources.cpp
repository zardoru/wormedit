#include "ui/nuklear/compose_resources.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>

namespace {

auto filter_role(worm_ui_nuklear_compose_resource_filter_t filter) -> std::uint8_t {
    switch (filter) {
    case worm_ui_nuklear_compose_resource_filter_t::sample:
        return WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
    case worm_ui_nuklear_compose_resource_filter_t::bgm:
        return WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
    case worm_ui_nuklear_compose_resource_filter_t::bga:
        return WORM_UI_COMPOSE_RESOURCE_ROLE_BGA;
    }
    return WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
}

auto filter_name(worm_ui_nuklear_compose_resource_filter_t filter) -> const char* {
    switch (filter) {
    case worm_ui_nuklear_compose_resource_filter_t::sample:
        return "Samples";
    case worm_ui_nuklear_compose_resource_filter_t::bgm:
        return "BGM";
    case worm_ui_nuklear_compose_resource_filter_t::bga:
        return "BGA";
    }
    return "Samples";
}

auto contains_ascii_case_insensitive(std::string_view value, std::string_view query) -> bool {
    if (query.empty()) {
        return true;
    }
    const auto equal = [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) ==
               std::tolower(static_cast<unsigned char>(right));
    };
    return std::search(value.begin(), value.end(), query.begin(), query.end(), equal) !=
           value.end();
}

auto resource_matches(const WormUiComposeResourceView& resource, const char* query) -> bool {
    const std::string_view needle{query ? query : ""};
    return contains_ascii_case_insensitive(resource.target_index, needle) ||
           contains_ascii_case_insensitive(resource.name, needle) ||
           contains_ascii_case_insensitive(resource.source_name, needle);
}

} // namespace

void worm_ui_nuklear_render_compose_resources(struct nk_context* context,
                                              const WormUiComposeView* view,
                                              WormUiWorkspaceAction* action,
                                              worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) {
        return;
    }
    if (!nk_tree_push(context, NK_TREE_TAB, "Samples / BGM / BGA", NK_MAXIMIZED)) {
        return;
    }

    nk_layout_row_dynamic(context, 24, 3);
    constexpr std::array filters{
        worm_ui_nuklear_compose_resource_filter_t::sample,
        worm_ui_nuklear_compose_resource_filter_t::bgm,
        worm_ui_nuklear_compose_resource_filter_t::bga,
    };
    for (const auto candidate : filters) {
        const auto selected = state->resource_filter == candidate;
        std::array<char, 24> label{};
        std::snprintf(label.data(), label.size(), "%s%s", filter_name(candidate),
                      selected ? " *" : "");
        if (nk_button_label(context, label.data())) {
            if (state->resource_filter != candidate) {
                state->resource_pointer_mode =
                    worm_ui_nuklear_compose_resource_pointer_mode_t::off;
            }
            state->resource_filter = candidate;
        }
    }

    nk_layout_row_dynamic(context, 19, 1);
    nk_label(context, "Search", NK_TEXT_LEFT);
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(context, 0.76f);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state->resource_search.data(),
                                         state->resource_search.size(), nk_filter_default);
    nk_layout_row_push(context, 0.24f);
    if (nk_button_label(context, "Clear")) {
        state->resource_search.fill('\0');
    }
    nk_layout_row_end(context);

    const WormUiComposeResourceView* armed = nullptr;
    for (std::size_t index = 0; index < view->resource_count; ++index) {
        if (view->resources[index].resource_id == state->armed_resource_id) {
            armed = &view->resources[index];
            break;
        }
    }
    nk_layout_row_dynamic(context, 19, 1);
    if (state->armed_resource_id == 0) {
        nk_label(context, "Armed: none", NK_TEXT_LEFT);
    } else if (armed) {
        nk_labelf(context, NK_TEXT_LEFT, "Armed: %s · %s · %zu selected",
                  armed->target_index, armed->name, view->selected_object_count);
    } else {
        nk_labelf(context, NK_TEXT_LEFT, "Armed resource #%llu is unavailable",
                  static_cast<unsigned long long>(state->armed_resource_id));
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto can_assign = view->scene_available && view->selected_object_count != 0 &&
                            armed && !armed->missing;
    if (!can_assign) {
        nk_widget_disable_begin(context);
    }
    const auto assign_clicked = nk_button_label(context, "Assign selected");
    if (!can_assign) {
        nk_widget_disable_end(context);
    }
    if (assign_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_id = state->armed_resource_id;
        action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION;
    }
    if (state->armed_resource_id == 0) {
        nk_widget_disable_begin(context);
    }
    const auto disarm_clicked = nk_button_label(context, "Disarm");
    if (state->armed_resource_id == 0) {
        nk_widget_disable_end(context);
    }
    if (disarm_clicked) {
        state->armed_resource_id = 0;
        state->resource_pointer_mode =
            worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    }

    nk_layout_row_dynamic(context, 24, 1);
    const auto placement_role = filter_role(state->resource_filter);
    const auto can_place = view->scene_available && armed && !armed->missing &&
                           (armed->roles & placement_role) != 0;
    if (!can_place) {
        nk_widget_disable_begin(context);
    }
    const auto place_clicked =
        nk_button_label(context, state->resource_filter ==
                                     worm_ui_nuklear_compose_resource_filter_t::sample
                                 ? "Place Sample at cursor"
                                 : state->resource_filter ==
                                           worm_ui_nuklear_compose_resource_filter_t::bgm
                                       ? "Place BGM at cursor"
                                       : "Place BGA background at cursor");
    if (!can_place) {
        nk_widget_disable_end(context);
    }
    if (place_clicked && can_place &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_resource_id = armed->resource_id;
        action->compose_resource_role = placement_role;
        action->compose_action = WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE;
    }
    const auto can_pointer_place = can_place && !view->note_paint_mode_active &&
                                   !view->semantic_layer_paint_mode_active &&
                                   !view->timing_stage_active &&
                                   (state->resource_filter !=
                                        worm_ui_nuklear_compose_resource_filter_t::bgm ||
                                    view->bgm_lane_count != 0);
    if (!can_pointer_place) {
        state->resource_pointer_mode =
            worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        nk_widget_disable_begin(context);
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto place_mode_clicked = nk_button_label(
        context, state->resource_pointer_mode ==
                         worm_ui_nuklear_compose_resource_pointer_mode_t::place
                     ? "Place: On"
                     : "Place: Off");
    const auto replace_mode_clicked = nk_button_label(
        context, state->resource_pointer_mode ==
                         worm_ui_nuklear_compose_resource_pointer_mode_t::replace
                     ? "Replace: On"
                     : "Replace: Off");
    if (!can_pointer_place) {
        nk_widget_disable_end(context);
    }
    if (place_mode_clicked && can_pointer_place) {
        state->resource_pointer_mode =
            state->resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place
                ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
                : worm_ui_nuklear_compose_resource_pointer_mode_t::place;
    } else if (replace_mode_clicked && can_pointer_place) {
        state->resource_pointer_mode =
            state->resource_pointer_mode ==
                    worm_ui_nuklear_compose_resource_pointer_mode_t::replace
                ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
                : worm_ui_nuklear_compose_resource_pointer_mode_t::replace;
    }

    if (state->resource_filter != worm_ui_nuklear_compose_resource_filter_t::bga) {
        nk_layout_row_dynamic(context, 24, 1);
        const auto can_create = view->selected_audio_asset_id != 0;
        if (!can_create) {
            nk_widget_disable_begin(context);
        }
        const auto create_clicked = nk_button_label(
            context, state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample
                         ? "Create Sample from waveform"
                         : "Create BGM from waveform");
        if (!can_create) {
            nk_widget_disable_end(context);
        }
        if (create_clicked && can_create &&
            action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_resource_asset_id = view->selected_audio_asset_id;
            action->compose_resource_role =
                state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::sample
                    ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                    : WORM_UI_COMPOSE_RESOURCE_ROLE_BGM;
            action->compose_action = WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE;
        }
    }
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "External file (absolute path)", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 1);
    (void)nk_edit_string_zero_terminated(
        context, NK_EDIT_FIELD, state->resource_import_path.data(),
        state->resource_import_path.size(), nk_filter_default);
    nk_layout_row_dynamic(context, 24, 1);
    const auto can_import = state->resource_import_path[0] != '\0';
    if (!can_import) {
        nk_widget_disable_begin(context);
    }
    const auto import_clicked = nk_button_label(
        context, state->resource_filter == worm_ui_nuklear_compose_resource_filter_t::bga
                     ? "Link image as BGA"
                     : state->resource_filter ==
                               worm_ui_nuklear_compose_resource_filter_t::bgm
                           ? "Link audio as BGM"
                           : "Link audio as Sample");
    if (!can_import) {
        nk_widget_disable_end(context);
    }
    if (import_clicked && can_import &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        std::snprintf(action->compose_import_path, sizeof(action->compose_import_path),
                      "%s", state->resource_import_path.data());
        action->compose_resource_role = filter_role(state->resource_filter);
        action->compose_action = WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET;
    }

    const auto role = filter_role(state->resource_filter);
    std::size_t role_count{};
    std::size_t matched_count{};
    for (std::size_t index = 0; index < view->resource_count; ++index) {
        if ((view->resources[index].roles & role) == 0) {
            continue;
        }
        ++role_count;
        matched_count +=
            resource_matches(view->resources[index], state->resource_search.data()) ? 1 : 0;
    }
    nk_layout_row_dynamic(context, 19, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%s · %zu/%zu match · %zu indexed",
              filter_name(state->resource_filter), matched_count, role_count,
              view->resource_total_count);

    if (role_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_labelf(context, NK_TEXT_LEFT, "No %s resources are registered.",
                  filter_name(state->resource_filter));
    } else if (matched_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "No resources match the search.", NK_TEXT_LEFT);
    }
    for (std::size_t index = 0; index < view->resource_count; ++index) {
        const auto& resource = view->resources[index];
        if ((resource.roles & role) == 0 ||
            !resource_matches(resource, state->resource_search.data())) {
            continue;
        }
        std::array<char, 320> label{};
        std::snprintf(label.data(), label.size(), "%s%s%s · %s",
                      resource.selected_audio_asset ? "> " : "", resource.target_index,
                      state->armed_resource_id == resource.resource_id ? " *" : "",
                      resource.name);
        nk_layout_row_dynamic(context, 25, 1);
        if (resource.audio_asset) {
            if (resource.missing) {
                nk_widget_disable_begin(context);
            }
            const auto clicked = nk_button_label(context, label.data());
            if (resource.missing) {
                nk_widget_disable_end(context);
            }
            if (clicked && !resource.missing && !resource.selected_audio_asset &&
                action->kind == WORM_UI_WORKSPACE_ACTION_NONE) {
                action->kind = WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET;
                action->audio_asset_id = resource.asset_id;
            }
        } else {
            nk_label(context, label.data(), NK_TEXT_LEFT);
        }
        if (resource.source_name[0] != '\0') {
            nk_layout_row_dynamic(context, 18, 1);
            nk_label(context, resource.source_name, NK_TEXT_LEFT);
        }
        nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
        nk_layout_row_push(context, 0.50f);
        if (!view->scene_available) {
            nk_widget_disable_begin(context);
        }
        const auto uses_clicked = nk_button_label(context, "Uses");
        if (!view->scene_available) {
            nk_widget_disable_end(context);
        }
        if (uses_clicked && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
            action->compose_resource_id = resource.resource_id;
            action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES;
        }
        nk_layout_row_push(context, 0.50f);
        if (resource.missing) {
            nk_widget_disable_begin(context);
        }
        const auto arm_clicked = nk_button_label(
            context, state->armed_resource_id == resource.resource_id ? "Armed" : "Arm");
        if (resource.missing) {
            nk_widget_disable_end(context);
        }
        if (arm_clicked) {
            state->armed_resource_id = resource.resource_id;
            state->resource_pointer_mode =
                worm_ui_nuklear_compose_resource_pointer_mode_t::off;
        }
        nk_layout_row_end(context);
        if (resource.missing) {
            nk_layout_row_dynamic(context, 17, 1);
            nk_label(context, "Missing source", NK_TEXT_LEFT);
        }
    }
    if (view->resources_truncated) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Additional indexed resources are hidden by the view cap.", NK_TEXT_LEFT);
    }
    if (matched_count != 0 &&
        state->resource_filter != worm_ui_nuklear_compose_resource_filter_t::bga) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Click audio to select its waveform.", NK_TEXT_LEFT);
    }
    if (matched_count != 0) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Arm a resource, then Assign selected.", NK_TEXT_LEFT);
    }
    nk_tree_pop(context);
}
