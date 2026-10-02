#pragma once

#include "ui/frontend.h"

#include <cstddef>

struct ComposeInspectorContext;

void render_compose_timing_inspector(const ComposeInspectorContext& inspector);

void render_compose_note_label_editor(const ComposeInspectorContext& inspector);
void render_compose_label_search(const ComposeInspectorContext& inspector);
void render_compose_quantize_section(const ComposeInspectorContext& inspector);

auto semantic_layer_target_active(const WormUiComposeView* view) -> bool;

auto render_semantic_layer_palette_panel(const ComposeInspectorContext& inspector) -> bool;

void render_semantic_layer_selected_panel(const ComposeInspectorContext& inspector,
                                                std::size_t layer_count);

void render_semantic_layer_membership_buttons(const ComposeInspectorContext& inspector,
                                                     bool target_active);

void render_semantic_layer_target_buttons(const ComposeInspectorContext& inspector,
                                                 bool target_active);
