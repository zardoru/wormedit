#pragma once

#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/resource_job_state.h"

#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/workspace.h>

#include <cstdint>
#include <filesystem>
#include <string>

struct WormWindow;

namespace wormedit::sys {

// Returns false after receiving a window close or quit event.
auto poll_frontend_events(WormWindow& window, metadata_drop_state_t& metadata_drop,
                          const workspace_controller_t& workspace,
                          const document_history_t& history,
                          const std::filesystem::path& asset_base,
                          std::string& status) -> bool;

void poll_application_jobs(runtime::metadata_asset_t& metadata_assets,
                           runtime::resource_copy_t& resource_copy,
                           runtime::output_group_export_t& output_group_export,
                           document_history_t& history,
                           workspace_controller_t& workspace,
                           const std::filesystem::path& asset_base,
                           std::string& status);

auto compose_frame_delta_seconds(std::uint64_t& last_ticks) -> double;

void tick_application_autosave(native_project_autosave_loop_t& autosave_loop,
                               const document_history_t& history,
                               bool loaded_primary,
                               const std::filesystem::path& requested_primary_path,
                               const std::filesystem::path& recovery_path,
                               std::string& status);

void rebind_workspace_after_edit(workspace_controller_t& workspace,
                                 const document_history_t& history,
                                 std::string& status);

void render_application_frame(WormWindow& window);

} // namespace wormedit::sys
