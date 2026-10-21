#include <wormedit/core/project_settings.h>
#include <wormedit/core/command.h>
#include <wormedit/core/preferences.h>
#include <wormedit/core/resource_file_copy.h>
#include "text_util.h"

namespace wormedit {
namespace {
auto is_valid_directory(const std::optional<std::string>& directory) -> bool {
    return !directory || (text_util::is_valid_bounded_text(*directory, maximum_project_path_bytes) &&
        export_asset_directory(*directory, output_destination_kind_t::directory).has_value());
}
}
auto project_settings_t::is_valid() const -> bool {
    return (!default_snap || (*default_snap > 0 && *default_snap <= 1920)) &&
        is_valid_directory(audio_asset_directory) && is_valid_directory(visual_asset_directory);
}
auto project_settings_t::snap_for(std::uint32_t program_default) const -> scoped_setting_t<std::uint32_t> {
    return {default_snap.value_or(program_default), default_snap ? setting_scope_t::project : setting_scope_t::program};
}
auto project_settings_t::audio_directory() const -> std::string_view {
    return audio_asset_directory ? std::string_view{*audio_asset_directory} : default_project_asset_directory;
}
auto project_settings_t::visual_directory() const -> std::string_view {
    return visual_asset_directory ? std::string_view{*visual_asset_directory} : default_project_asset_directory;
}
auto project_settings_t::effective(const program_preferences_t& preferences) const -> effective_project_settings_t {
    return {
        .default_snap = snap_for(preferences.editor.default_snap),
        .audio_asset_directory = {audio_directory(), audio_asset_directory ? setting_scope_t::project : setting_scope_t::program},
        .visual_asset_directory = {visual_directory(), visual_asset_directory ? setting_scope_t::project : setting_scope_t::program}};
}
auto apply_project_settings_command(document_history_t& history, const document_stamp_t& stamp,
                                    project_settings_t settings) -> document_command_result_t {
    document_command_transaction_t transaction{history, stamp, "Project settings"};
    return transaction.finish_execution(transaction.stage(
        [&settings](song_document_t& document) -> attempt_t<document_edit_effect_t> {
            if (!settings.is_valid()) return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                "Project settings require a snap between 1 and 1920 and safe relative asset directories.")}};
            const bool changed = document.settings != settings;
            document.settings = std::move(settings);
            return {.value = document_edit_effect_t{.changed = changed, .command_label = "Project settings",
                .options = {.preserve_retained_source_flags = true}}};
        }));
}
}
