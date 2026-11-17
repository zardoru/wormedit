#pragma once
#include "ui/frontend/metadata_audio.h"
struct MetadataPanel;
struct nk_context;
namespace wormedit::ui { struct audio_ui_window_t; }
void render_metadata_timing_links_section(const MetadataPanel&);
void synchronize_metadata_timing_links(nk_context*, const wormedit::ui::metadata_view_t&);
auto metadata_timing_links_own_input() -> bool;
void render_metadata_timing_link_confirmation(nk_context*, const wormedit::ui::metadata_view_t&,
    wormedit::ui::timing_link_action_t&);
