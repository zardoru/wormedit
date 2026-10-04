#pragma once

#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/resource_job_state.h"
#include "sys/sound_drop_transaction.h"
#include "sys/workspace_edit_feedback.h"

#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/workspace.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct WormWindow;

namespace wormedit::sys {

// Returns false after receiving a window close or quit event.
auto poll_frontend_events(WormWindow& window, metadata_drop_state_t& metadata_drop,
                          const workspace_controller_t& workspace,
                          const document_history_t& history,
                          const std::filesystem::path& asset_base,
                          bool offer_audio_project_drop,
                          std::string& pending_audio_project_path,
                          std::string& pending_chart_project_path,
                          sound_drop_transaction_t& sound_drop_transaction,
                          std::vector<sound_drop_batch_t>& pending_sound_batches,
                          std::vector<std::string>& pending_bga_paths,
                          notification_sink_t& notifications) -> bool;

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
                               notification_sink_t& notifications);

void rebind_workspace_after_edit(workspace_controller_t& workspace,
                                 const document_history_t& history,
                                 notification_sink_t notifications, workspace_edit_feedback_t& feedback);

void render_application_frame(WormWindow& window);

} // namespace wormedit::sys
