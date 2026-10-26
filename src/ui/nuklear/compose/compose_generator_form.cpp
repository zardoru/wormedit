#include "ui/nuklear/compose/compose_generator_form.h"
#include "ui/nuklear/compose/compose_panel_internal.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/file_dialog_slot.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <algorithm>
namespace {
struct generator_editor_t {
    wormedit::ui::generator_form_t form;
    wormedit::song_id_t project;
    wormedit::chart_id_t chart;
    wormedit::recipe_id_t selected;
    std::uint64_t revision{};
    bool apply_pending{};
    wormedit::ui::file_dialog_slot_t picker;
    wormedit::song_id_t picker_project;
    wormedit::chart_id_t picker_chart;
    wormedit::recipe_id_t picker_recipe;
    std::uint64_t picker_revision{};
    std::string error;
} editor;
void queue(const ComposeInspectorContext& inspector, WormUiComposeActionKind kind) {
    auto& action = *inspector.action;
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = kind;
    action.compose_input_project_id = inspector.view->project_id;
    action.compose_input_chart_id = inspector.view->chart_id;
    action.compose_input_document_revision = inspector.view->document_revision;
    action.compose_generator_form = editor.form;
}
void field(nk_context* context, const char* name, char* buffer, std::size_t bytes, nk_plugin_filter filter = nk_filter_default) {
    nk_layout_row_dynamic(context, 20, 1); nk_label(context, name, NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, buffer,
        static_cast<int>(bytes), filter);
}
void sync(const wormedit::ui::compose_view_t& view) {
    const bool has_recipe = view.generator.has_selection;
    const auto selected = has_recipe ? view.generator.selected_form.recipe : wormedit::recipe_id_t{};
    if (editor.project != view.project_id || editor.chart != view.chart_id || editor.selected != selected ||
        (editor.apply_pending && editor.revision != view.document_revision)) {
        editor.form = has_recipe ? view.generator.selected_form : wormedit::ui::generator_form_t{};
        editor.apply_pending = false;
        editor.error.clear();
    }
    editor.project = view.project_id; editor.chart = view.chart_id;
    editor.selected = selected; editor.revision = view.document_revision;
}
void selector(const ComposeInspectorContext& inspector) {
    static std::vector<const char*> names;
    names.clear(); names.push_back("New generator");
    int selected{};
    for (const auto& recipe : inspector.view->generator.recipes) {
        if (recipe.id == editor.selected) selected = static_cast<int>(names.size());
        names.push_back(recipe.label.c_str());
    }
    nk_layout_row_dynamic(inspector.context, 24, 1);
    const int choice = wormedit::ui::chrome::safe_combo(inspector.context, names.data(),
        static_cast<int>(names.size()), selected, 24, nk_vec2(260, 220));
    if (choice == selected || choice < 0 || static_cast<std::size_t>(choice) >= names.size()) return;
    editor.form.recipe = choice ? inspector.view->generator.recipes[static_cast<std::size_t>(choice - 1)].id : wormedit::recipe_id_t{};
    queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_SELECT);
}
void SDLCALL picked(void* userdata, const char* const* paths, int) {
    const auto generation = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userdata));
    editor.picker.complete(generation, paths && paths[0] ? std::string{paths[0]} : std::string{},
        paths ? std::string{} : std::string{SDL_GetError()}, paths && !paths[0]);
}
void picker(const ComposeInspectorContext& inspector) {
    const auto& view = *inspector.view;
    nk_layout_row_dynamic(inspector.context, 24, 1);
    if (nk_button_label(inspector.context, "Choose script…")) {
        if (const auto generation = editor.picker.begin()) {
            editor.picker_project = view.project_id; editor.picker_chart = view.chart_id;
            editor.picker_revision = view.document_revision; editor.picker_recipe = editor.selected;
            static constexpr SDL_DialogFileFilter filter{"Lua generator", "lua"};
            SDL_ShowOpenFileDialog(picked, reinterpret_cast<void*>(static_cast<std::uintptr_t>(*generation)),
                SDL_GetKeyboardFocus(), &filter, 1, nullptr, false);
        }
    }
    const auto result = editor.picker.take();
    if (!result || editor.picker_project != view.project_id || editor.picker_chart != view.chart_id ||
        editor.picker_recipe != editor.selected || editor.picker_revision != view.document_revision) return;
    if (!*result) editor.error = result->error().message;
    else if (!result->value().canceled) std::snprintf(editor.form.path, sizeof(editor.form.path), "%s", result->value().path.c_str());
}
void parameter_fields(nk_context* context) {
    for (auto& parameter : editor.form.parameters) {
        field(context, "Parameter name", parameter.name, sizeof(parameter.name));
        field(context, "Value (JSON)", parameter.value, sizeof(parameter.value));
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Add parameter") && editor.form.parameters.size() < wormedit::maximum_generator_parameters)
        editor.form.parameters.emplace_back();
    if (nk_button_label(context, "Remove parameter") && !editor.form.parameters.empty()) editor.form.parameters.pop_back();
    for (auto& input : editor.form.inputs) field(context, "MIDI asset ID", input.asset, sizeof(input.asset), nk_filter_decimal);
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Add MIDI input") && editor.form.inputs.size() < wormedit::maximum_generator_parameters)
        editor.form.inputs.emplace_back();
    if (nk_button_label(context, "Remove input") && !editor.form.inputs.empty()) editor.form.inputs.pop_back();
}
void preview_summary(nk_context* context,const wormedit::ui::compose_generator_view_t& view) {
    if (view.chart_previews.empty()) {
        nk_layout_row_dynamic(context,24,1);
        nk_labelf(context,NK_TEXT_LEFT,"Notes +%zu / ~%zu / -%zu",view.notes.added,view.notes.changed,view.notes.removed);
        nk_layout_row_dynamic(context,24,1);
        nk_labelf(context,NK_TEXT_LEFT,"Layers +%zu / ~%zu / -%zu",view.layers.added,view.layers.changed,view.layers.removed);
        nk_layout_row_dynamic(context,24,1);
        nk_labelf(context,NK_TEXT_LEFT,"SV outputs +%zu / ~%zu / -%zu",view.sv.added,view.sv.changed,view.sv.removed);
    }
    if (!view.chart_previews.empty() || view.charts.removed) {
        nk_layout_row_dynamic(context,24,1);
        nk_labelf(context,NK_TEXT_LEFT,"Difficulties +%zu / ~%zu / -%zu",view.charts.added,view.charts.changed,view.charts.removed);
        for (const auto& chart:view.chart_previews) {
            nk_layout_row_dynamic(context,22,1); nk_label(context,chart.name.c_str(),NK_TEXT_LEFT);
            nk_layout_row_dynamic(context,36,1);
            char summary[160];
            std::snprintf(summary,sizeof(summary),"%zu measures, %zu notes, %zu BGM, %zu timing events",
                chart.measures,chart.notes,chart.bgm,chart.timing);
            nk_text_wrap(context,summary,static_cast<int>(std::char_traits<char>::length(summary)));
        }
    }
    if (view.rationalizations.empty()) return;
    if (!nk_tree_push(context,NK_TREE_NODE,"Exact values",NK_MINIMIZED)) return;
    nk_layout_row_dynamic(context,24,1);
    if (nk_button_label(context,"Copy exact-value report")) {
        std::string report;
        for (const auto& value:view.rationalizations) report+=value+'\n';
        (void)SDL_SetClipboardText(report.c_str());
    }
    const auto count=std::min<std::size_t>(view.rationalizations.size(),32);
    for (std::size_t i=0;i<count;++i) {
        nk_layout_row_dynamic(context,36,1);
        const auto& line=view.rationalizations[i]; nk_text_wrap(context,line.c_str(),static_cast<int>(line.size()));
    }
    if (count<view.rationalizations.size()) {
        nk_layout_row_dynamic(context,22,1); nk_label(context,"Copy the report to read all exact values.",NK_TEXT_LEFT);
    }
    nk_tree_pop(context);
}
void actions(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    const auto& view = inspector.view->generator;
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, editor.selected ? "Regenerate" : "Run")) queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_RUN);
    if (nk_button_label(context, "Cancel")) queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_CANCEL);
    if (view.requires_confirmation) {
        nk_layout_row_dynamic(context, 24, 1);
        if (nk_button_label(context, "Run changed script")) queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_CONFIRM);
    }
    nk_layout_row_dynamic(context, 24, 2);
    if (!view.can_apply) nk_widget_disable_begin(context);
    if (nk_button_label(context, "Apply")) { editor.apply_pending = true; queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY); }
    if (!view.can_apply) nk_widget_disable_end(context);
    if (editor.selected && nk_button_label(context, "Detach recipe")) queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_DETACH);
}
}
void reset_compose_generator_form() {
    editor.picker.reset();
    editor.form = {};
    editor.project = {}; editor.chart = {}; editor.selected = {};
    editor.revision = 0; editor.apply_pending = false; editor.error.clear();
}
void render_compose_generator_form(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    if (!nk_tree_push(context, NK_TREE_TAB, "Lua note generator", NK_MINIMIZED)) return;
    sync(*inspector.view); selector(inspector);
    field(context, "Script file (inside project)", editor.form.path, sizeof(editor.form.path));
    picker(inspector);
    field(context, "Seed", editor.form.seed, sizeof(editor.form.seed), nk_filter_decimal);
    parameter_fields(context);
    if (editor.selected) editor.form.keep_recipe = true;
    nk_bool keep = editor.form.keep_recipe;
    if (editor.selected) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 24, 1); nk_checkbox_label(context, "Keep recipe", &keep);
    if (editor.selected) nk_widget_disable_end(context);
    editor.form.keep_recipe = keep;
    if (editor.selected) {
        nk_bool saved = editor.form.use_saved_source;
        nk_layout_row_dynamic(context, 24, 1); nk_checkbox_label(context, "Run saved source snapshot", &saved);
        editor.form.use_saved_source = saved;
    }
    const auto& view = inspector.view->generator;
    preview_summary(context,view);
    const auto status = !editor.error.empty() ? std::string_view{editor.error} : view.status;
    if (!status.empty()) { nk_layout_row_dynamic(context, 44, 1); nk_text_wrap(context, status.data(), static_cast<int>(status.size())); }
    if (view.busy) { nk_layout_row_dynamic(context, 20, 1); nk_label(context, "Running…", NK_TEXT_LEFT); }
    actions(inspector);
    nk_tree_pop(context);
}
