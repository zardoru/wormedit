#pragma once
#include "ui/frontend/metadata_audio.h"
struct MetadataPanel;
struct nk_context;
namespace wormedit::ui { struct workspace_action_t; }
void render_metadata_timing_links_section(const MetadataPanel&);
void synchronize_metadata_timing_links(nk_context*, const wormedit::ui::metadata_view_t&);
auto metadata_timing_links_own_input() -> bool;
void render_metadata_timing_link_confirmation(nk_context*, const wormedit::ui::metadata_view_t&,
    wormedit::ui::workspace_action_t&);
