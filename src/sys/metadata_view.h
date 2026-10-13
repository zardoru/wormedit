#pragma once

#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/workspace_view.h"

#include <filesystem>

namespace wormedit::sys {

// Populate pointer-backed metadata UI fields. All strings referenced by the view
// are stored in `flattened` and remain valid for its lifetime.
void populate_metadata_view_projection(
    flattened_workspace_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base, metadata_drop_state_t& metadata_drop,
    runtime::metadata_asset_t& metadata_asset_runtime);

} // namespace wormedit::sys
