#include "sys/compose/views/generator_view.h"
#include "sys/compose/generator_runtime.h"
#include <cstdio>
namespace wormedit::ui {
void generator_chart_preview_t::refill(const generator_chart_content_t& content) {
    name=content.name; measures=content.measures.size(); notes=0; bgm=0;
    for (const auto& measure:content.measures) { notes+=measure.notes.size(); bgm+=measure.bgm_events.size(); }
    timing=content.bpms.size()+content.pauses.size()+content.warps.size()+content.scrolls.size()+(content.sv_recipes ? content.sv_recipes->size() : 0);
}
void compose_generator_view_t::refill_preview(const generator_preview_t& preview) {
    notes=preview.notes; layers=preview.layers; charts=preview.charts; sv=preview.sv;
    rationalizations.assign(preview.rationalizations.begin(),preview.rationalizations.end());
    chart_previews.resize(preview.recipe.generated_charts.size());
    for (std::size_t i=0;i<chart_previews.size();++i) chart_previews[i].refill(preview.recipe.generated_charts[i].content);
}
}
namespace wormedit::sys {
void populate_compose_generator(frontend_workspace_view_t& flattened, const document_history_t& history,
    const chart_document_t& chart, const editor_interaction_view_model_t& interaction,
    const application_command_state_t& commands, ui::compose_view_t& view,
    const editor_field_scene_request_t& request) {
    auto& shown = view.generator;
    shown.recipes.resize(history.document.generator_recipes.size());
    std::size_t recipe_count{};
    shown.has_selection = false;
    shown.busy = false; shown.can_apply = false; shown.requires_confirmation = false;
    shown.status = {}; shown.notes = {}; shown.layers = {}; shown.charts = {}; shown.sv = {};

    view.generator_ghosts.clear();
    for (const auto& recipe : history.document.generator_recipes) {
        if (recipe.chart && recipe.chart != chart.id) continue;
        auto& entry = shown.recipes[recipe_count++];
        entry.id = recipe.id;
        char label[64];
        std::snprintf(label, sizeof(label), "Recipe %llu", static_cast<unsigned long long>(recipe.id.value));
        entry.label.assign(label);
        if (recipe.id == commands.project.generator.selected) {
            shown.has_selection = true;
            const auto source = std::tuple{view.project_id, chart.id, view.document_revision, recipe.id,
                flattened.query_session_generation};
            if (flattened.compose.generator_form_source != source) {
                shown.selected_form.refill(recipe);
                flattened.compose.generator_form_source = source;
            }
        }
    }
    shown.recipes.resize(recipe_count);
    if (!shown.has_selection) flattened.compose.generator_form_source.reset();
    const auto& runtime = commands.project.generator.runtime;
    if (!runtime || runtime->stamp() != chart_stamp_t::from(history, chart.id)) {
        shown.chart_previews.resize(0); shown.rationalizations.resize(0); return;
    }
    shown.busy = runtime->is_busy();
    if (const auto* result = runtime->result()) {
        shown.status = result->status;
        shown.requires_confirmation = result->requires_confirmation;
        shown.can_apply = result->proposal.value.has_value() && !shown.busy;
    }
    if (!runtime->preview()) { shown.chart_previews.resize(0); shown.rationalizations.resize(0); }
    if (const auto* preview = runtime->preview()) {
        shown.refill_preview(*preview);
        if (const auto* scene = runtime->preview_scene(interaction, request)) {
            for (const auto& hold : scene->hold_bodies)
                view.generator_ghosts.push_back({.bounds = ui::compose_rect_t::from(hold.bounds),
                    .kind = ui::compose_paste_ghost_kind_t::hold_body});
            for (const auto& note : scene->note_endpoints)
                view.generator_ghosts.push_back({.bounds = ui::compose_rect_t::from(note.bounds),
                    .kind = ui::compose_paste_ghost_kind_t::note_endpoint, .glyph = note.glyph});
        }
    }
}
}
