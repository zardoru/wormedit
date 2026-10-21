#pragma once
#include <wormedit/core/vocabulary.h>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit {
struct program_preferences_t;
struct document_history_t;
struct document_stamp_t;
struct document_command_result_t;
enum class setting_scope_t { program, project };
template <typename T> struct scoped_setting_t {
    T value;
    setting_scope_t source{setting_scope_t::program};
};
struct effective_project_settings_t {
    scoped_setting_t<std::uint32_t> default_snap;
    scoped_setting_t<std::string_view> audio_asset_directory, visual_asset_directory;
};
struct project_settings_t {
    std::optional<std::uint32_t> default_snap;
    std::optional<std::string> audio_asset_directory, visual_asset_directory;
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] auto snap_for(std::uint32_t program_default) const -> scoped_setting_t<std::uint32_t>;
    [[nodiscard]] auto audio_directory() const -> std::string_view;
    [[nodiscard]] auto visual_directory() const -> std::string_view;
    [[nodiscard]] auto effective(const program_preferences_t&) const -> effective_project_settings_t;
    auto operator==(const project_settings_t&) const -> bool = default;
};
[[nodiscard]] auto apply_project_settings_command(document_history_t&, const document_stamp_t&,
    project_settings_t) -> document_command_result_t;
}
