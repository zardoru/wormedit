#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/setup_state.h"

#include "wormedit/nuklear_config.h"

#include <array>

namespace {

void publish_policy_action(WormUiWorkspaceAction* action, int file_profile, int timing_syntax,
                           int implementation) {
    if (!action || action->kind != WORM_UI_WORKSPACE_ACTION_NONE)
        return;
    const auto file = static_cast<WormUiStepmaniaFileProfile>(file_profile);
    const auto timing = static_cast<WormUiStepmaniaTimingSyntax>(timing_syntax);
    const auto target = static_cast<WormUiStepmaniaImplementation>(implementation);
    if (!worm_ui_nuklear_setup_policy_values_valid(file, timing, target))
        return;
    action->kind = WORM_UI_WORKSPACE_ACTION_SET_STEPMANIA_EXPORT_POLICY;
    action->stepmania_file_profile = file;
    action->stepmania_timing_syntax = timing;
    action->stepmania_implementation = target;
}

} // namespace

void worm_ui_nuklear_render_setup_panel(struct nk_context* context,
                                        const WormUiSetupView* view,
                                        WormUiWorkspaceAction* action) {
    if (!context || !view || !action || !view->visible)
        return;

    nk_layout_row_dynamic(context, 22, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Charts: %zu · Audio assets: %zu · Visual assets: %zu",
              view->chart_count, view->audio_asset_count, view->visual_asset_count);
    nk_layout_row_dynamic(context, 26, 1);
    nk_label(context, "StepMania export policy", NK_TEXT_LEFT);

    constexpr std::array file_labels{"SM", "SSC"};
    constexpr std::array timing_labels{"Native WARPS", "Legacy negative BPM",
                                       "Legacy negative STOP"};
    constexpr std::array implementation_labels{"Syntax only", "StepMania 5.1",
                                                "Etterna 0.75.1"};
    int file_profile = static_cast<int>(view->stepmania_file_profile);
    int timing_syntax = static_cast<int>(view->stepmania_timing_syntax);
    int implementation = static_cast<int>(view->stepmania_implementation);

    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "File family", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.72f);
    const auto next_file = nk_combo(context, file_labels.data(), file_labels.size(), file_profile,
                                    24, nk_vec2(280, 100));
    nk_layout_row_end(context);

    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Timing syntax", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.72f);
    const auto next_timing =
        nk_combo(context, timing_labels.data(), timing_labels.size(), timing_syntax, 24,
                 nk_vec2(280, 120));
    nk_layout_row_end(context);

    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Implementation target", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.72f);
    const auto next_implementation =
        nk_combo(context, implementation_labels.data(), implementation_labels.size(),
                 implementation, 24, nk_vec2(280, 120));
    nk_layout_row_end(context);

    if (next_file != file_profile || next_timing != timing_syntax ||
        next_implementation != implementation) {
        publish_policy_action(action, next_file, next_timing, next_implementation);
    }

    nk_layout_row_dynamic(context, 22, 1);
    nk_label_colored(context, view->stepmania_policy_status, NK_TEXT_LEFT,
                     view->stepmania_policy_valid ? nk_rgb(120, 210, 150)
                                                  : nk_rgb(235, 150, 120));
    nk_layout_row_dynamic(context, 38, 1);
    nk_label_wrap(context,
                  "These choices stage a typed conversion request. Export destination and "
                  "publication remain separate transactional steps.");
}
