#pragma once

#include "ui/frontend.h"

#include <cstddef>

struct ComposeInspectorContext;

void render_compose_timing_inspector(const ComposeInspectorContext& inspector);
void render_compose_recipe_form(const ComposeInspectorContext& inspector);

void render_compose_label_search(const ComposeInspectorContext& inspector);
void render_compose_measure_section(const ComposeInspectorContext& inspector);
void render_compose_transform_section(const ComposeInspectorContext& inspector);
void render_compose_quantize_section(const ComposeInspectorContext& inspector);
