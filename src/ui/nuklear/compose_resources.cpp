#include "ui/nuklear/compose_resources.h"

#include "wormedit/nuklear_config.h"

#include <array>
#include <cstdio>

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

} // namespace

void worm_ui_nuklear_render_compose_resources(struct nk_context* context,
                                              const WormUiComposeView* view,
                                              WormUiWorkspaceAction* action,
                                              worm_ui_nuklear_compose_resource_filter_t* filter) {
    if (!context || !view || !action || !filter) {
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
        const auto selected = *filter == candidate;
        std::array<char, 24> label{};
        std::snprintf(label.data(), label.size(), "%s%s", filter_name(candidate),
                      selected ? " *" : "");
        if (nk_button_label(context, label.data())) {
            *filter = candidate;
        }
    }

    const auto role = filter_role(*filter);
    std::size_t visible_count{};
    for (std::size_t index = 0; index < view->resource_count; ++index) {
        visible_count += (view->resources[index].roles & role) != 0 ? 1 : 0;
    }
    nk_layout_row_dynamic(context, 19, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%s · %zu visible / %zu indexed", filter_name(*filter),
              visible_count, view->resource_total_count);

    if (visible_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_labelf(context, NK_TEXT_LEFT, "No %s resources are registered.", filter_name(*filter));
    }
    for (std::size_t index = 0; index < view->resource_count; ++index) {
        const auto& resource = view->resources[index];
        if ((resource.roles & role) == 0) {
            continue;
        }
        std::array<char, 320> label{};
        std::snprintf(label.data(), label.size(), "%s%s · %s%s%s",
                      resource.selected_audio_asset ? "> " : "", resource.target_index,
                      resource.name, resource.source_name[0] == '\0' ? "" : " · ",
                      resource.source_name);
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
        if (resource.missing) {
            nk_layout_row_dynamic(context, 17, 1);
            nk_label(context, "Missing source", NK_TEXT_LEFT);
        }
    }
    if (view->resources_truncated) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Additional indexed resources are hidden by the view cap.", NK_TEXT_LEFT);
    }
    if (visible_count != 0 && *filter != worm_ui_nuklear_compose_resource_filter_t::bga) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Click audio to select its waveform.", NK_TEXT_LEFT);
    }
    nk_tree_pop(context);
}
