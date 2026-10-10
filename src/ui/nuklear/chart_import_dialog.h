#pragma once

#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"

// The Project tab's "Import chart…" dialog: a path field with a native-picker button, the
// difficulties of the chart file to tick, the resource and timing preview, and Cancel / Import.
// Returns true when the user pressed Browse; the frontend then opens its file picker.
[[nodiscard]] auto worm_ui_nuklear_render_chart_import_dialog(
    struct nk_context* context, const WormUiChartImportView& view, WormUiWorkspaceAction& action,
    int window_width, int window_height) -> bool;
