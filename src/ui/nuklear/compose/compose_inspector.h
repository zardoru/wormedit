#pragma once

#include "ui/frontend.h"

#include <cstddef>

struct ComposeInspectorContext;
struct nk_context;

void reset_compose_quantize_popup();
void abandon_compose_quantize_popup();
void prepare_compose_quantize_input(nk_context* context, const wormedit::ui::compose_view_t& view,
                                    bool inspector_visible, bool window_focused);
auto compose_quantize_owns_input() -> bool;
void reset_compose_quantize_section_visibility();
auto compose_quantize_section_visible() -> bool;

void render_compose_timing_inspector(const ComposeInspectorContext& inspector);
void render_compose_recipe_form(const ComposeInspectorContext& inspector);

void render_compose_label_search(const ComposeInspectorContext& inspector);
void render_compose_measure_section(const ComposeInspectorContext& inspector);
void render_compose_transform_section(const ComposeInspectorContext& inspector);
void render_compose_quantize_section(const ComposeInspectorContext& inspector);
