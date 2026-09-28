#pragma once

#include "sys/resource_job_state.h"

#include <wormedit/core/workspace.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct WormUiWorkspaceAction;

namespace wormedit::sys {

struct metadata_copy_preview_key_t {
    wormedit::song_id_t song;
    std::uint64_t revision{};
    wormedit::chart_id_t chart;
    wormedit::asset_id_t asset;
    std::filesystem::path source_path;
    std::filesystem::path output_path;
    std::size_t output_group_index{};
    bool export_copy{};
    bool import_copy{};
};

namespace runtime {

class metadata_asset_t {
public:
    metadata_asset_t();
    ~metadata_asset_t();

    metadata_asset_t(const metadata_asset_t&) = delete;
    auto operator=(const metadata_asset_t&) -> metadata_asset_t& = delete;
    metadata_asset_t(metadata_asset_t&&) = delete;
    auto operator=(metadata_asset_t&&) -> metadata_asset_t& = delete;

    auto start_copy_preview(const WormUiWorkspaceAction& action,
                            const wormedit::document_history_t& history,
                            const wormedit::workspace_controller_t& workspace,
                            const std::filesystem::path& asset_base) -> std::string;
    auto start_import_copy(
        const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
        const wormedit::workspace_controller_t& workspace,
        const std::filesystem::path& asset_base, wormedit::task_scheduler_t& scheduler,
        bool resource_copy_busy, bool export_busy) -> std::string;

    auto poll(wormedit::document_history_t& history,
              wormedit::workspace_controller_t& workspace,
              const std::filesystem::path& asset_base) -> std::optional<std::string>;
    void cancel();
    // Call after the scheduler passed to start_import_copy() has shut down.
    void cleanup(const std::filesystem::path& asset_base);

    [[nodiscard]] auto import_pending() const -> bool;
    [[nodiscard]] auto copy_preview_pending() const -> bool;
    [[nodiscard]] auto copy_preview_key() const
        -> const std::optional<metadata_copy_preview_key_t>&;
    [[nodiscard]] auto copy_preview_message() const -> const std::string&;
    void clear_copy_preview_key();

private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
};

} // namespace runtime
} // namespace wormedit::sys
