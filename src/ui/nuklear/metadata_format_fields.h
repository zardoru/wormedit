#pragma once

#include "ui/nuklear/metadata_panel_internal.h"

void initialize_bms_field_buffers(const wormedit::ui::metadata_view_t* metadata);
void initialize_bmson_field_buffers(const wormedit::ui::metadata_view_t* metadata);
void initialize_typed_field_buffers(const wormedit::ui::metadata_view_t* metadata);
void render_format_section(const MetadataPanel& panel);
