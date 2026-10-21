#include <wormedit/core/project_settings.h>
#include <wormedit/core/preferences.h>
#include <wormedit/core/command.h>
#include <wormedit/core/chart_editor.h>
#include "sys/project_view.h"
#include <catch2/catch_test_macros.hpp>
#include <source_location>

using namespace wormedit;
namespace {
void check_setting(bool value, std::source_location at = std::source_location::current()) {
    INFO("settings assertion at " << at.line());
    CHECK(value);
}
}
TEST_CASE("Project settings resolve explicit overrides over program defaults", "[core][project-settings]") {
    auto preferences = program_preferences_t::with_defaults();
    project_settings_t settings;
    auto effective = settings.effective(preferences);
    check_setting(effective.default_snap.value == 4 && effective.default_snap.source == setting_scope_t::program);
    check_setting(effective.audio_asset_directory.value == "assets");
    settings = {.default_snap = 12, .audio_asset_directory = "media/audio",
        .visual_asset_directory = "media/images"};
    effective = settings.effective(preferences);
    check_setting(settings.is_valid());
    check_setting(effective.default_snap.value == 12 && effective.default_snap.source == setting_scope_t::project);
    check_setting(effective.audio_asset_directory.value == "media/audio" && effective.visual_asset_directory.value == "media/images");
}
TEST_CASE("Project settings reject unsafe values", "[core][project-settings]") {
    project_settings_t settings;
    settings.default_snap = 0;
    check_setting(!settings.is_valid());
    settings.default_snap = 1921;
    check_setting(!settings.is_valid());
    settings.default_snap.reset();
    for (const auto* path : {"../outside", "/absolute", ""}) {
        settings.audio_asset_directory = path;
        check_setting(!settings.is_valid());
    }
}
TEST_CASE("Project settings are stamped dirty and undoable in one transaction", "[core][project-settings]") {
    auto seed = document_history_t::from(song_document_t{.id = song_id_t{1}});
    REQUIRE(apply_new_chart_command(seed));
    seed.document.metadata.format_groups.push_back({.family = format_family_t::bms,
        .encoding = {.detected = text_encoding_t::ascii,
            .original_bytes = std::make_shared<const std::vector<std::byte>>(), .unchanged_since_import = true}});
    seed.document.charts.front().metadata.format_groups.push_back({.family = format_family_t::bmson,
        .encoding = {.unchanged_since_import = false}});
    auto history = document_history_t::from(seed.document);
    const auto stamp = document_stamp_t::from(history.document);
    const project_settings_t settings{.default_snap = 8, .audio_asset_directory = "audio"};
    check_setting(!is_document_dirty(history));
    check_setting(apply_project_settings_command(history, stamp, settings).applied);
    check_setting(is_document_dirty(history) && history.undo.size() == 1);
    check_setting(history.document.settings == settings);
    check_setting(history.document.metadata.format_groups.front().encoding.unchanged_since_import);
    check_setting(!history.document.charts.front().metadata.format_groups.front().encoding.unchanged_since_import);
    check_setting(!apply_project_settings_command(history, stamp, {}).applied);
    check_setting(undo_document_command(history));
    check_setting(history.document.settings == project_settings_t{} && !is_document_dirty(history));
    check_setting(redo_document_command(history));
    check_setting(history.document.settings == settings);
    const auto current = document_stamp_t::from(history.document);
    check_setting(!apply_project_settings_command(history, current, settings).applied);
    check_setting(history.undo.size() == 1);
    check_setting(!apply_project_settings_command(history, current, {.default_snap = 0}).applied);
    check_setting(history.document.settings == settings);
}
TEST_CASE("Project settings UI action preserves other fields and projects Undo immediately", "[core][project-settings]") {
    auto seed = document_history_t::from(song_document_t{.id = song_id_t{1}});
    REQUIRE(apply_new_chart_command(seed));
    seed.document.settings.audio_asset_directory = "audio";
    auto history = document_history_t::from(seed.document);
    ui::project_settings_action_t action{.requested = true, .project = history.document.id,
        .revision = history.document.revision, .field = project_setting_field_t::default_snap,
        .use_program_default = false, .integer = 8};
    check_setting(sys::apply_project_settings_ui_action(history, action).applied);
    check_setting(history.document.settings.audio_asset_directory == "audio");
    ui::project_settings_view_t view;
    auto preferences = program_preferences_t::with_defaults();
    view.refill(history.document, preferences);
    check_setting(view.snap_overridden && view.default_snap == 8 && view.program_default_snap == 4);
    preferences.editor.default_snap = 12;
    view.refill(history.document, preferences);
    check_setting(view.default_snap == 8 && view.program_default_snap == 12);
    check_setting(undo_document_command(history));
    view.refill(history.document, preferences);
    check_setting(!view.snap_overridden && view.default_snap == 12);
    action.revision = history.document.revision;
    action.field = project_setting_field_t::audio_directory;
    action.use_program_default = true;
    check_setting(sys::apply_project_settings_ui_action(history, action).applied);
    check_setting(!history.document.settings.audio_asset_directory);
}
