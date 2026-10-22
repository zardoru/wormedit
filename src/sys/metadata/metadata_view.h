#pragma once

#include "sys/metadata/metadata_asset_runtime.h"
#include "sys/metadata/metadata_drop_state.h"
#include "sys/workspace_view.h"

#include <filesystem>

namespace wormedit::sys {

// Refill owned metadata presentation data in place. The unconverted Compose output-row
// labels still borrow frame-arena text until T-698.
void populate_metadata_view_projection(
    frontend_workspace_view_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base, metadata_drop_state_t& metadata_drop,
    runtime::metadata_asset_t& metadata_asset_runtime);

} // namespace wormedit::sys
