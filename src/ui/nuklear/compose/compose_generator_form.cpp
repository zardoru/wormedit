#include "ui/nuklear/compose/compose_generator_form.h"
#include "ui/nuklear/compose/compose_panel_internal.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/file_dialog_slot.h"
#include "ui/nuklear/chrome/ui_metrics.h"
#include "ui/nuklear/render/theme.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <algorithm>
#include <optional>
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
namespace chrome = wormedit::ui::chrome;
namespace metrics = wormedit::ui::metrics;
using wormedit::ui::theme::UiColor;
void muted_label(nk_context* context, const char* text, nk_text_alignment alignment = NK_TEXT_LEFT) {
    nk_label_colored(context, text, alignment, chrome::color(UiColor::text_muted));
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
    nk_layout_row_dynamic(inspector.context, metrics::control_default, 1);
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
    if (chrome::Button{"Choose script…"}.render(inspector.context)) {
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
// One removable text row: name and value fields plus a small remove button. True when removed.
auto remove_cell(nk_context* context) -> bool {
    return chrome::Button{"×", chrome::ButtonKind::ghost, "Remove"}.render(context);
}
void parameters_card(nk_context* context) {
    char title[48];
    std::snprintf(title, sizeof(title), "Parameters (%zu)", editor.form.parameters.size());
    // The count changes the title, so the section keeps one identity across frames.
    if (!chrome::section_push(context, NK_TREE_TAB, title, NK_MAXIMIZED, "Generator parameters", 20, 0)) return;
    if (!editor.form.parameters.empty()) {
        nk_layout_row_template_begin(context, metrics::control_compact);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_static(context, metrics::control_default);
        nk_layout_row_template_end(context);
        muted_label(context, "Name"); muted_label(context, "Value (JSON)"); nk_spacer(context);
    }
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < editor.form.parameters.size(); ++index) {
        auto& parameter = editor.form.parameters[index];
        nk_layout_row_template_begin(context, metrics::control_default);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_static(context, metrics::control_default);
        nk_layout_row_template_end(context);
        (void)chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, parameter.name, sizeof(parameter.name), nk_filter_default);
        (void)chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, parameter.value, sizeof(parameter.value), nk_filter_default);
        if (remove_cell(context)) removed = index;
    }
    if (removed) editor.form.parameters.erase(editor.form.parameters.begin() + static_cast<std::ptrdiff_t>(*removed));
    nk_layout_row_dynamic(context, metrics::control_default, 1);
    const auto full = editor.form.parameters.size() >= wormedit::maximum_generator_parameters;
    if (chrome::Button{"+ Add parameter", chrome::ButtonKind::ghost, nullptr, !full}.render(context))
        editor.form.parameters.emplace_back();
    chrome::section_pop(context);
}
void inputs_card(nk_context* context) {
    char title[48];
    std::snprintf(title, sizeof(title), "MIDI inputs (%zu)", editor.form.inputs.size());
    if (!chrome::section_push(context, NK_TREE_TAB, title, NK_MAXIMIZED, "Generator MIDI inputs", 21, 0)) return;
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < editor.form.inputs.size(); ++index) {
        nk_layout_row_template_begin(context, metrics::control_default);
        nk_layout_row_template_push_static(context, 64.0f);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_static(context, metrics::control_default);
        nk_layout_row_template_end(context);
        muted_label(context, "Asset ID");
        auto& input = editor.form.inputs[index];
        (void)chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, input.asset, sizeof(input.asset), nk_filter_decimal);
        if (remove_cell(context)) removed = index;
    }
    if (removed) editor.form.inputs.erase(editor.form.inputs.begin() + static_cast<std::ptrdiff_t>(*removed));
    nk_layout_row_dynamic(context, metrics::control_default, 1);
    const auto full = editor.form.inputs.size() >= wormedit::maximum_generator_parameters;
    if (chrome::Button{"+ Add MIDI input", chrome::ButtonKind::ghost, nullptr, !full}.render(context))
        editor.form.inputs.emplace_back();
    chrome::section_pop(context);
}
// Added / changed / removed counts of one kind of output; zeros fade so changes stand out.
void count_row(nk_context* context, const char* name, const wormedit::generator_diff_t& counts) {
    constexpr float count_width = 56.0f;
    nk_layout_row_template_begin(context, metrics::control_compact);
    nk_layout_row_template_push_dynamic(context);
    for (int i = 0; i < 3; ++i) nk_layout_row_template_push_static(context, count_width);
    nk_layout_row_template_end(context);
    nk_label(context, name, NK_TEXT_LEFT);
    char text[24];
    const std::size_t values[]{counts.added, counts.changed, counts.removed};
    const char marks[]{'+', '~', '-'};
    for (int i = 0; i < 3; ++i) {
        std::snprintf(text, sizeof(text), "%c%zu", marks[i], values[i]);
        nk_label_colored(context, text, NK_TEXT_RIGHT,
                         values[i] ? chrome::color(UiColor::text) : metrics::text_faint);
    }
}
void count_header(nk_context* context) {
    constexpr float count_width = 56.0f;
    nk_layout_row_template_begin(context, metrics::control_compact);
    nk_layout_row_template_push_dynamic(context);
    for (int i = 0; i < 3; ++i) nk_layout_row_template_push_static(context, count_width);
    nk_layout_row_template_end(context);
    nk_spacer(context);
    muted_label(context, "added", NK_TEXT_RIGHT); muted_label(context, "changed", NK_TEXT_RIGHT);
    muted_label(context, "removed", NK_TEXT_RIGHT);
}
void preview_summary(nk_context* context,const wormedit::ui::compose_generator_view_t& view) {
    if (!chrome::section_push(context, NK_TREE_TAB, "Preview", NK_MAXIMIZED, "Generator preview", 17, 0)) return;
    count_header(context);
    if (view.chart_previews.empty()) {
        count_row(context, "Notes", view.notes);
        count_row(context, "Layers", view.layers);
        count_row(context, "SV outputs", view.sv);
    }
    if (!view.chart_previews.empty() || view.charts.removed) {
        count_row(context, "Difficulties", view.charts);
        for (const auto& chart:view.chart_previews) {
            nk_layout_row_dynamic(context, metrics::control_compact, 1); nk_label(context,chart.name.c_str(),NK_TEXT_LEFT);
            nk_layout_row_dynamic(context,36,1);
            char summary[160];
            std::snprintf(summary,sizeof(summary),"%zu measures, %zu notes, %zu BGM, %zu timing events",
                chart.measures,chart.notes,chart.bgm,chart.timing);
            nk_text_wrap(context,summary,static_cast<int>(std::char_traits<char>::length(summary)));
        }
    }
    if (!view.rationalizations.empty() &&
        chrome::section_push(context, NK_TREE_NODE, "Exact values", NK_MINIMIZED)) {
        nk_layout_row_dynamic(context, metrics::control_default, 1);
        if (chrome::Button{"Copy exact-value report"}.render(context)) {
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
            nk_layout_row_dynamic(context,22,1); muted_label(context,"Copy the report to read all exact values.");
        }
        chrome::section_pop(context);
    }
    chrome::section_pop(context);
}
void actions(const ComposeInspectorContext& inspector) {
    auto* context = inspector.context;
    const auto& view = inspector.view->generator;
    nk_layout_row_dynamic(context, metrics::control_primary, 2);
    if (chrome::Button{editor.selected ? "Regenerate" : "Run", chrome::ButtonKind::primary}.render(context))
        queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_RUN);
    if (chrome::Button{"Cancel"}.render(context)) queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_CANCEL);
    if (view.requires_confirmation) {
        nk_layout_row_dynamic(context, metrics::control_default, 1);
        if (chrome::Button{"Run changed script", chrome::ButtonKind::destructive}.render(context))
            queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_CONFIRM);
    }
    nk_layout_row_dynamic(context, metrics::control_default, editor.selected ? 2 : 1);
    if (chrome::Button{"Apply", chrome::ButtonKind::primary, nullptr, view.can_apply}.render(context)) {
        editor.apply_pending = true; queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY);
    }
    if (editor.selected && chrome::Button{"Detach recipe"}.render(context))
        queue(inspector, WORM_UI_COMPOSE_ACTION_GENERATOR_DETACH);
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
    nk_layout_row_dynamic(context, metrics::control_compact, 1);
    nk_label(context, "Lua note generator", NK_TEXT_LEFT);
    sync(*inspector.view); selector(inspector);
    if (chrome::section_push(context, NK_TREE_TAB, "Script", NK_MAXIMIZED, "Generator script", 16, 0)) {
        nk_layout_row_dynamic(context, metrics::control_compact, 1);
        muted_label(context, "Script file (inside project)");
        nk_layout_row_template_begin(context, metrics::control_default);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_push_static(context, 116.0f);
        nk_layout_row_template_end(context);
        (void)chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, editor.form.path,
            static_cast<int>(sizeof(editor.form.path)), nk_filter_default);
        picker(inspector);
        nk_layout_row_template_begin(context, metrics::control_default);
        nk_layout_row_template_push_static(context, 116.0f);
        nk_layout_row_template_push_dynamic(context);
        nk_layout_row_template_end(context);
        muted_label(context, "Seed");
        (void)chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, editor.form.seed,
            static_cast<int>(sizeof(editor.form.seed)), nk_filter_decimal);
        chrome::section_pop(context);
    }
    parameters_card(context);
    inputs_card(context);
    if (editor.selected) editor.form.keep_recipe = true;
    nk_layout_row_dynamic(context, metrics::control_default, 1);
    const auto keep_state = editor.form.keep_recipe ? chrome::CheckState::on : chrome::CheckState::off;
    if (chrome::Checkbox{"Keep recipe", keep_state, nullptr, !editor.selected}.render(context))
        editor.form.keep_recipe = !editor.form.keep_recipe;
    if (editor.selected) {
        nk_layout_row_dynamic(context, metrics::control_default, 1);
        if (chrome::Checkbox{"Run saved source snapshot",
                editor.form.use_saved_source ? chrome::CheckState::on : chrome::CheckState::off}.render(context))
            editor.form.use_saved_source = !editor.form.use_saved_source;
    }
    const auto& view = inspector.view->generator;
    preview_summary(context,view);
    const auto status = !editor.error.empty() ? std::string_view{editor.error} : view.status;
    if (!status.empty()) { nk_layout_row_dynamic(context, 44, 1); nk_text_wrap(context, status.data(), static_cast<int>(status.size())); }
    if (view.busy) { nk_layout_row_dynamic(context, metrics::control_compact, 1); muted_label(context, "Running…"); }
    actions(inspector);
}
