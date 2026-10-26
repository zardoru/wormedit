#include "ui/frontend/workspace.h"
#include "sys/preferences/options_runtime.h"
#include <wormedit/core/lane_keys.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <limits>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
void expect_options(bool value) { CHECK(value); }
void expect_options_notification(const options_runtime_t& runtime,
                                 notification_severity_t severity,
                                 std::string_view message) {
    REQUIRE(runtime.notification);
    CHECK(runtime.notification->severity == severity);
    CHECK(runtime.notification->source == notification_source_t::system);
    CHECK(runtime.notification->message == message);
}
auto action(const options_runtime_t& r, WormUiOptionsActionKind kind,
    WormUiOptionsCategory category = WORM_UI_OPTIONS_GENERAL) -> wormedit::ui::options_action_t {
    return {.kind = kind, .revision = r.revision, .category = category};
}
void select(options_runtime_t& r, program_preferences_runtime_t& p, WormUiOptionsCategory category) {
    (void)apply_options_action(r, p, action(r, WORM_UI_OPTIONS_SELECT_CATEGORY, category));
}
struct temporary_preferences_t {
    std::filesystem::path directory;
    temporary_preferences_t() {
        static unsigned sequence{};
        directory = std::filesystem::temp_directory_path() /
            ("wormedit-options-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(++sequence));
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
    }
    ~temporary_preferences_t() { std::filesystem::remove_all(directory); }
};
}
TEST_CASE("Default lane key conflicts name the key and reject Apply", "[options][default-lane-keys]") {
    temporary_preferences_t temporary;
    program_preferences_runtime_t program{.path = temporary.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options_category(runtime, program.preferences, WORM_UI_OPTIONS_INPUT);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_INPUT);
    edit.field = WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_DFJK;
    edit.lane_count = 4;
    (void)apply_options_action(runtime, program, edit);
    expect_options(program.preferences.default_lane_keys.bindings.empty());
    edit.revision = runtime.revision;
    edit.field = WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_KEY;
    edit.lane_index = 1;
    edit.integer = physical_key_hid_usage(physical_key_t::key_d);
    (void)apply_options_action(runtime, program, edit);
    expect_options(!runtime.diagnostics.empty());
    expect_options(runtime.diagnostics.front().message.find("key D ") != std::string::npos);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    edit.revision = runtime.revision;
    edit.integer = physical_key_hid_usage(physical_key_t::key_f);
    (void)apply_options_action(runtime, program, edit);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.default_lane_keys.bindings.at(4) == lane_key_bindings_t::dfjk());
    edit.revision = runtime.revision;
    edit.field = WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_NUMBER_ROW;
    (void)apply_options_action(runtime, program, edit);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.default_lane_keys.bindings.at(4) == lane_key_bindings_t::number_row(4));
}

TEST_CASE("Options opening and category switching retain isolated drafts", "[options]") {
    program_preferences_runtime_t program;
    options_runtime_t r;
    open_options(r, program.preferences);
    auto candidate = r.draft;
    candidate.general.autosave_minutes = 12;
    candidate.audio.keep_pitch = true;
    expect_options(edit_options_draft(r, r.revision, candidate));
    expect_options(r.draft.general.autosave_minutes == 12);
    expect_options(!r.draft.audio.keep_pitch);
    expect_options(program.preferences.general.autosave_minutes == 5);
    open_options(r, program.preferences);
    select(r, program, WORM_UI_OPTIONS_AUDIO);
    select(r, program, WORM_UI_OPTIONS_GENERAL);
    expect_options(r.draft.general.autosave_minutes == 12);
    const auto view = wormedit::ui::options_view_t::from(r);
    expect_options(view.changed && view.can_apply && view.autosave_minutes == 12);
}

TEST_CASE("Appearance reduced motion is an isolated accepted display preference", "[options][display-options]") {
    temporary_preferences_t temporary;
    program_preferences_runtime_t program{.path = temporary.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_DISPLAY);
    edit.field = WORM_UI_OPTIONS_FIELD_REDUCED_MOTION;
    edit.boolean = true;
    (void)apply_options_action(runtime, program, edit);
    expect_options(wormedit::ui::options_view_t::from(runtime).reduced_motion);
    expect_options(!program.preferences.display.reduced_motion);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.reduced_motion);
}

TEST_CASE("Audio Options validate levels and preserve drafts until Apply", "[options][audio-options]") {
    temporary_preferences_t temporary;
    program_preferences_runtime_t program{.path = temporary.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_AUDIO);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_AUDIO);
    edit.field = WORM_UI_OPTIONS_FIELD_METRONOME_VOLUME;
    edit.integer = 101;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(!wormedit::ui::options_view_t::from(runtime).can_apply);
    edit.revision = runtime.revision;
    edit.integer = 25;
    (void)apply_options_action(runtime, program, edit);
    expect_options(program.preferences.audio.metronome_volume == 100);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY, WORM_UI_OPTIONS_AUDIO)));
    expect_options(program.preferences.audio.metronome_volume == 25);
}
TEST_CASE("Options validates individual draft fields and stale actions atomically", "[options]") {
    program_preferences_runtime_t program;
    options_runtime_t r;
    open_options(r, program.preferences);
    const auto old_revision = r.revision;
    auto candidate = r.draft;
    candidate.general.autosave_minutes = 0;
    expect_options(edit_options_draft(r, old_revision, candidate));
    const auto view = wormedit::ui::options_view_t::from(r);
    expect_options(!view.can_apply && view.diagnostics.size() == 1);
    expect_options(view.diagnostics[0].field == WORM_UI_OPTIONS_FIELD_AUTOSAVE);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options_notification(r, notification_severity_t::warning,
                                "Correct invalid Options fields before applying.");
    expect_options(program.preferences.general.autosave_minutes == 5);
    expect_options(!edit_options_draft(r, old_revision, program.preferences));
    const auto current = r.revision;
    expect_options(!apply_options_action(r, program, {.kind = WORM_UI_OPTIONS_CLOSE, .revision = old_revision}));
    expect_options(r.visible && r.revision == current);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_SELECT_CATEGORY,
        static_cast<WormUiOptionsCategory>(99))));
    expect_options(r.category == WORM_UI_OPTIONS_GENERAL && r.revision == current);
}
TEST_CASE("Options applies every changed page atomically preserving external untouched settings", "[options]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t r;
    open_options(r, program.preferences);
    auto candidate = r.draft;
    candidate.general.autosave_minutes = 12;
    expect_options(edit_options_draft(r, r.revision, candidate));
    select(r, program, WORM_UI_OPTIONS_AUDIO);
    candidate = r.draft;
    candidate.audio.keep_pitch = true;
    expect_options(edit_options_draft(r, r.revision, candidate));
    const auto pending = wormedit::ui::options_view_t::from(r);
    expect_options(pending.changed_categories[WORM_UI_OPTIONS_GENERAL]);
    expect_options(pending.changed_categories[WORM_UI_OPTIONS_AUDIO]);
    expect_options(!pending.changed_categories[WORM_UI_OPTIONS_DISPLAY]);
    program.preferences.display.theme = "external-theme";
    expect_options(apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options_notification(r, notification_severity_t::success, "Options saved.");
    expect_options(program.preferences.audio.keep_pitch);
    expect_options(program.preferences.general.autosave_minutes == 12);
    expect_options(program.preferences.display.theme == "external-theme");
    expect_options(r.draft.general.autosave_minutes == 12);
    const auto loaded = load_program_preferences(program.path);
    expect_options(loaded.contents.succeeded && loaded.contents.preferences == program.preferences);
    select(r, program, WORM_UI_OPTIONS_GENERAL);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.general.autosave_minutes == 12);
}
TEST_CASE("Options conflicts require Revert and defaults remain draft only", "[options]") {
    program_preferences_runtime_t program;
    program.preferences.general.autosave_minutes = 12;
    program.preferences.audio.keep_pitch = true;
    options_runtime_t r;
    open_options(r, program.preferences);
    (void)apply_options_action(r, program, action(r, WORM_UI_OPTIONS_DEFAULTS));
    expect_options(r.draft.general.autosave_minutes == 5 && r.draft.audio.keep_pitch);
    expect_options(program.preferences.general.autosave_minutes == 12);
    program.preferences.general.autosave_minutes = 17;
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.general.autosave_minutes == 17 && !r.status.empty());
    (void)apply_options_action(r, program, action(r, WORM_UI_OPTIONS_REVERT));
    expect_options(r.draft.general.autosave_minutes == 17);
    expect_options(!wormedit::ui::options_view_t::from(r).changed);
    (void)apply_options_action(r, program, action(r, WORM_UI_OPTIONS_CLOSE));
    program.preferences.general.autosave_minutes = 19;
    open_options(r, program.preferences);
    expect_options(r.draft.general.autosave_minutes == 19);
}
TEST_CASE("Options rejects hidden invalid drafts without applying another changed page", "[options]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    auto candidate = runtime.draft;
    candidate.general.autosave_minutes = 0;
    expect_options(edit_options_draft(runtime, runtime.revision, candidate));
    select(runtime, program, WORM_UI_OPTIONS_AUDIO);
    candidate = runtime.draft;
    candidate.audio.keep_pitch = true;
    expect_options(edit_options_draft(runtime, runtime.revision, candidate));
    expect_options(!wormedit::ui::options_view_t::from(runtime).can_apply);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(!program.preferences.audio.keep_pitch);
    expect_options(program.preferences.general.autosave_minutes == 5);
    expect_options(!std::filesystem::exists(program.path));
}

TEST_CASE("Closing dirty Options requires explicit discard and Keep editing preserves every draft", "[options]") {
    program_preferences_runtime_t program;
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    auto candidate = runtime.draft;
    candidate.general.autosave_minutes = 12;
    expect_options(edit_options_draft(runtime, runtime.revision, candidate));
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_CLOSE));
    expect_options(runtime.visible);
    expect_options(wormedit::ui::options_view_t::from(runtime).confirm_discard);
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_KEEP_EDITING));
    expect_options(!runtime.confirm_discard);
    expect_options(runtime.draft.general.autosave_minutes == 12);
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_CLOSE));
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DISCARD_AND_CLOSE));
    expect_options(!runtime.visible);
    open_options(runtime, program.preferences);
    expect_options(runtime.draft.general.autosave_minutes == 5);
}
TEST_CASE("Options no-op skips publication and publication failure keeps accepted values", "[options]") {
    temporary_preferences_t file;
    const auto target = file.directory / "existing-directory";
    std::filesystem::create_directory(target);
    program_preferences_runtime_t program{.path = target};
    options_runtime_t r;
    open_options(r, program.preferences);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(r.status == "Options have no changes.");
    expect_options_notification(r, notification_severity_t::info, r.status);
    auto candidate = r.draft;
    candidate.general.autosave_minutes = 12;
    expect_options(edit_options_draft(r, r.revision, candidate));
    expect_options(apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.general.autosave_minutes == 12);
    expect_options(!wormedit::ui::options_view_t::from(r).changed && r.status.find("saving failed") != std::string::npos);
    expect_options_notification(r, notification_severity_t::error, r.status);
    expect_options(std::filesystem::is_directory(target));
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_NONE)));
    expect_options(!r.notification);
    expect_options(program.preferences.general.autosave_minutes == 12);
}
TEST_CASE("Options page validation catches unsupported rates paths scales and player definitions", "[options]") {
    program_preferences_runtime_t program;
    options_runtime_t r;
    open_options(r, program.preferences);
    const std::pair<WormUiOptionsCategory, WormUiOptionsField> fields[]{
        {WORM_UI_OPTIONS_AUDIO, WORM_UI_OPTIONS_FIELD_RATE},
        {WORM_UI_OPTIONS_INPUT, WORM_UI_OPTIONS_FIELD_SHORTCUT_PATH},
        {WORM_UI_OPTIONS_DISPLAY, WORM_UI_OPTIONS_FIELD_SCALE},
        {WORM_UI_OPTIONS_PLAYERS, WORM_UI_OPTIONS_FIELD_PLAYERS}};
    for (const auto [category, field] : fields) {
        select(r, program, category);
        auto candidate = r.draft;
        candidate.audio.playback_rate = {3, 7};
        candidate.general.shortcut_profile_path.clear();
        candidate.display.ui_scale_percent = 99;
        candidate.players.selected = 999;
        expect_options(edit_options_draft(r, r.revision, candidate));
        const auto view = wormedit::ui::options_view_t::from(r);
        expect_options(!view.can_apply && view.diagnostics.size() == 1 && view.diagnostics[0].field == field);
        (void)apply_options_action(r, program, action(r, WORM_UI_OPTIONS_REVERT));
    }
}

TEST_CASE("Options oversized invalid text rejects the active page before preference acceptance", "[options]") {
    program_preferences_runtime_t program;
    options_runtime_t r;
    open_options(r, program.preferences);
    select(r, program, WORM_UI_OPTIONS_DISPLAY);
    auto candidate = r.draft;
    candidate.display.theme = std::string(maximum_preference_string_bytes + 1, 'x');
    expect_options(edit_options_draft(r, r.revision, candidate));
    expect_options(!wormedit::ui::options_view_t::from(r).can_apply);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.theme.empty());
    candidate.display.theme = std::string(1, static_cast<char>(0xff));
    expect_options(edit_options_draft(r, r.revision, candidate));
    const auto view = wormedit::ui::options_view_t::from(r);
    expect_options(!view.can_apply && view.diagnostics.size() == 1 &&
                   view.diagnostics[0].field == WORM_UI_OPTIONS_FIELD_THEME);
}

TEST_CASE("Options registered page field actions update only the current draft", "[options]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t r;
    open_options(r, program.preferences);
    auto edit = action(r, WORM_UI_OPTIONS_EDIT_FIELD);
    edit.field = WORM_UI_OPTIONS_FIELD_AUTOSAVE;
    edit.integer = 12;
    expect_options(!apply_options_action(r, program, edit));
    expect_options(r.draft.general.autosave_minutes == 12);
    expect_options(program.preferences.general.autosave_minutes == 5);
    expect_options(!std::filesystem::exists(program.path));
    const auto revision = r.revision;
    edit.integer = 13; // Prior frame's payload cannot replace the draft.
    expect_options(!apply_options_action(r, program, edit));
    expect_options(r.revision == revision && r.draft.general.autosave_minutes == 12);
    edit = action(r, WORM_UI_OPTIONS_EDIT_FIELD);
    edit.field = WORM_UI_OPTIONS_FIELD_SCALE; edit.integer = 150;
    (void)apply_options_action(r, program, edit);
    expect_options(r.revision == revision && r.draft.display.ui_scale_percent == 0);
    edit.field = WORM_UI_OPTIONS_FIELD_AUTOSAVE; edit.integer = 0;
    (void)apply_options_action(r, program, edit);
    expect_options(!wormedit::ui::options_view_t::from(r).can_apply && r.draft.general.autosave_minutes == 0);
    edit.revision = r.revision; edit.integer = 15;
    (void)apply_options_action(r, program, edit);
    expect_options(wormedit::ui::options_view_t::from(r).can_apply);
    expect_options(apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(load_program_preferences(program.path).contents.preferences.general.autosave_minutes == 15);
}
TEST_CASE("Options bounded text and player actions reject malformed payloads without mutation", "[options]") {
    program_preferences_runtime_t program;
    options_runtime_t r;
    open_options(r, program.preferences);
    select(r, program, WORM_UI_OPTIONS_INPUT);
    auto edit = action(r, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_INPUT);
    edit.field = WORM_UI_OPTIONS_FIELD_SHORTCUT_PATH;
    edit.text.assign(wormedit::maximum_preference_string_bytes + 1, 'x');
    const auto revision = r.revision;
    (void)apply_options_action(r, program, edit);
    expect_options(r.revision == revision && r.draft.general.shortcut_profile_path == "shortcuts.json");
    edit.text.clear();
    (void)apply_options_action(r, program, edit);
    expect_options(!wormedit::ui::options_view_t::from(r).can_apply);
    select(r, program, WORM_UI_OPTIONS_PLAYERS);
    edit = action(r, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_PLAYERS);
    edit.field = WORM_UI_OPTIONS_FIELD_PLAYER_PATH; edit.player_index = 999;
    const auto before = r.draft.players;
    (void)apply_options_action(r, program, edit);
    expect_options(r.draft.players == before);
    edit.field = WORM_UI_OPTIONS_FIELD_PLAYER_NAME; edit.player_index = 0;
    edit.text = "Test player";
    (void)apply_options_action(r, program, edit);
    expect_options(r.draft.players.players[0].name == "Test player");
    expect_options(program.preferences.players == before);
}

TEST_CASE("Editor Options typed edits retain page isolation and reject invalid family bounds", "[options]") {
    program_preferences_runtime_t program;
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_EDITOR);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_EDITOR);
    edit.field = WORM_UI_OPTIONS_FIELD_SCROLL_DIRECTION;
    edit.family_index = 2;
    edit.integer = 2;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(runtime.draft.editor.scroll_directions[2] == 2);
    expect_options(program.preferences.editor.scroll_directions[2] == 0);
    edit.revision = runtime.revision;
    edit.family_index = 6;
    const auto before = runtime.draft;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(runtime.draft == before);
    edit.family_index = 2;
    edit.integer = 3;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(!wormedit::ui::options_view_t::from(runtime).can_apply);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS));
    expect_options(runtime.draft.editor == editor_preferences_t{});
    expect_options(wormedit::ui::options_view_t::from(runtime).can_apply);
    expect_options(program.preferences.general.autosave_minutes == 5);
}

TEST_CASE("Editor options save the note click audition toggle", "[options][audition]") {
    temporary_preferences_t temporary;
    program_preferences_runtime_t program;
    program.path = temporary.directory / "preferences.json";
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_EDITOR);
    expect_options(wormedit::ui::options_view_t::from(runtime).audition_on_click);
    const wormedit::ui::options_action_t edit{.kind = WORM_UI_OPTIONS_EDIT_FIELD,
        .revision = runtime.revision, .category = WORM_UI_OPTIONS_EDITOR,
        .field = WORM_UI_OPTIONS_FIELD_AUDITION_ON_CLICK, .boolean = false};
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(!wormedit::ui::options_view_t::from(runtime).audition_on_click);
    expect_options(apply_options_action(runtime, program,
        action(runtime, WORM_UI_OPTIONS_APPLY, WORM_UI_OPTIONS_EDITOR)));
    expect_options(!program.preferences.editor.audition_on_click);
    expect_options(!load_program_preferences(program.path).contents.preferences.editor.audition_on_click);
}

TEST_CASE("Options Audio clamps wide keysound actions and applies persisted budgets", "[options][budget]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_AUDIO);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_AUDIO);
    edit.field = WORM_UI_OPTIONS_FIELD_KEYSOUND_MEMORY;
    edit.integer = std::numeric_limits<std::uint64_t>::max();
    (void)apply_options_action(runtime, program, edit);
    expect_options(wormedit::ui::options_view_t::from(runtime).keysound_budget_mib == 8192);
    expect_options(program.preferences.audio.keysound_budget_mib == 512);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(load_program_preferences(program.path).contents.preferences.audio.keysound_budget_mib == 8192);
    edit.revision = runtime.revision;
    edit.integer = 0;
    (void)apply_options_action(runtime, program, edit);
    expect_options(wormedit::ui::options_view_t::from(runtime).keysound_budget_mib == 256);
}

TEST_CASE("Editing collision policy applies to disk and Defaults restores Reject", "[options][bgm-move]") {
    temporary_preferences_t temporary;
    program_preferences_runtime_t program;
    program.path = temporary.directory / "preferences.json";
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_EDITOR);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_EDITOR);
    edit.field = WORM_UI_OPTIONS_FIELD_MOVE_COLLISION_POLICY;
    edit.integer = 1;
    (void)apply_options_action(runtime, program, edit);
    expect_options(wormedit::ui::options_view_t::from(runtime).move_collision_policy == move_collision_policy_t::displace_to_bgm && wormedit::ui::options_view_t::from(runtime).can_apply);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(load_program_preferences(program.path).contents.preferences.editor.move_collision_policy ==
        move_collision_policy_t::displace_to_bgm);
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS));
    expect_options(wormedit::ui::options_view_t::from(runtime).move_collision_policy == move_collision_policy_t::reject);
}

TEST_CASE("Field theme Options edits stay draft until Apply and persist independently of chrome", "[options][theme]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path=file.directory/"preferences.json"};
    options_runtime_t runtime;
    open_options(runtime,program.preferences);
    select(runtime,program,WORM_UI_OPTIONS_DISPLAY);
    auto edit=action(runtime,WORM_UI_OPTIONS_EDIT_FIELD,WORM_UI_OPTIONS_DISPLAY);
    edit.field=WORM_UI_OPTIONS_FIELD_FIELD_SCHEME;
    edit.text = "ibmsc-o2mania";
    expect_options(!(apply_options_action(runtime,program,edit)));
    expect_options(program.preferences.display.field_scheme=="wormedit");
    expect_options(std::string(wormedit::ui::options_view_t::from(runtime).field_scheme)=="ibmsc-o2mania");
    expect_options(apply_options_action(runtime,program,action(runtime,WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.field_scheme=="ibmsc-o2mania");
    expect_options(program.preferences.display.theme.empty());
    const auto loaded=load_program_preferences(program.path);
    expect_options(loaded.contents.succeeded);
    expect_options(loaded.contents.preferences.display.field_scheme=="ibmsc-o2mania");
    expect_options(!(apply_options_action(runtime,program,action(runtime,WORM_UI_OPTIONS_DEFAULTS))));
    expect_options(runtime.draft.display.field_scheme=="wormedit");
    expect_options(program.preferences.display.field_scheme=="ibmsc-o2mania");
    expect_options(!(apply_options_action(runtime,program,action(runtime,WORM_UI_OPTIONS_REVERT))));
    expect_options(runtime.draft.display.field_scheme=="ibmsc-o2mania");
}

TEST_CASE("Appearance snap grid opt-out stays draft until Apply and supports Defaults and Revert", "[options][snap-grid]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_DISPLAY);
    edit.field = WORM_UI_OPTIONS_FIELD_SNAP_COLORED_GRID;
    edit.boolean = false;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(!wormedit::ui::options_view_t::from(runtime).snap_colored_grid);
    expect_options(program.preferences.display.snap_colored_grid);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(!load_program_preferences(program.path).contents.preferences.display.snap_colored_grid);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS)));
    expect_options(wormedit::ui::options_view_t::from(runtime).snap_colored_grid);
    expect_options(!program.preferences.display.snap_colored_grid);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_REVERT)));
    expect_options(!wormedit::ui::options_view_t::from(runtime).snap_colored_grid);
}

TEST_CASE("Grid opacity draft Apply Revert Defaults and wide values stay bounded", "[options][snap-grid][opacity]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_DISPLAY);
    edit.field = WORM_UI_OPTIONS_FIELD_SNAP_GRID_OPACITY;
    edit.integer = 60;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(wormedit::ui::options_view_t::from(runtime).snap_grid_opacity_percent == 60);
    expect_options(program.preferences.display.snap_grid_opacity_percent == 35);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(load_program_preferences(program.path).contents.preferences.display.snap_grid_opacity_percent == 60);
    edit.revision = runtime.revision;
    edit.integer = std::numeric_limits<std::uint64_t>::max();
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(wormedit::ui::options_view_t::from(runtime).snap_grid_opacity_percent == 100);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_REVERT)));
    expect_options(wormedit::ui::options_view_t::from(runtime).snap_grid_opacity_percent == 60);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS)));
    expect_options(wormedit::ui::options_view_t::from(runtime).snap_grid_opacity_percent == 35);
    expect_options(program.preferences.display.snap_grid_opacity_percent == 60);
}

TEST_CASE("Field waveform opacity previews as a draft and supports Apply Revert Defaults", "[options][feedback]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_DISPLAY);
    edit.field = WORM_UI_OPTIONS_FIELD_FIELD_WAVEFORM_OPACITY;
    edit.integer = 40;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(wormedit::ui::options_view_t::from(runtime).field_waveform_opacity_percent == 40);
    expect_options(program.preferences.display.field_waveform_opacity_percent == 75);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(load_program_preferences(program.path).contents.preferences.display.field_waveform_opacity_percent == 40);
    edit.revision = runtime.revision;
    edit.integer = std::numeric_limits<std::uint64_t>::max();
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(wormedit::ui::options_view_t::from(runtime).field_waveform_opacity_percent == 100);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_REVERT)));
    expect_options(wormedit::ui::options_view_t::from(runtime).field_waveform_opacity_percent == 40);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS)));
    expect_options(wormedit::ui::options_view_t::from(runtime).field_waveform_opacity_percent == 75);
}

TEST_CASE("Wheel inversion stays an Editor draft until applied", "[options][feedback]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_EDITOR);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_EDITOR);
    edit.field = WORM_UI_OPTIONS_FIELD_INVERT_SCROLL_WHEEL;
    edit.boolean = true;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(wormedit::ui::options_view_t::from(runtime).invert_scroll_wheel);
    expect_options(!program.preferences.editor.invert_scroll_wheel);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(load_program_preferences(program.path).contents.preferences.editor.invert_scroll_wheel);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS)));
    expect_options(!wormedit::ui::options_view_t::from(runtime).invert_scroll_wheel);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_REVERT)));
    expect_options(wormedit::ui::options_view_t::from(runtime).invert_scroll_wheel);
}

TEST_CASE("Scratch presentation precedence preserves chart layout and independent overrides", "[options][scratch-presentation]") {
    display_preferences_t preferences;
    const std::array<std::optional<bool>, 2> skin{false, true};
    expect_options(resolve_scratch_positions(preferences, skin) == skin);
    preferences.scratch_positions = {1, 2};
    const auto mixed = resolve_scratch_positions(preferences, skin);
    expect_options(!mixed[0].has_value());
    expect_options(mixed[1] == false);
    preferences.scratch_positions = {3, 1};
    const auto single_right = resolve_scratch_positions(preferences, skin);
    expect_options(single_right[0] == true);
    expect_options(!single_right[1].has_value());
    preferences.scratch_positions = {0, 0};
    expect_options(resolve_scratch_positions(preferences, {}) == std::array<std::optional<bool>, 2>{});
    preferences.scratch_positions = {4, std::numeric_limits<std::uint32_t>::max()};
    expect_options(resolve_scratch_positions(preferences, skin) == std::array<std::optional<bool>, 2>{});
}

TEST_CASE("Appearance scratch edits preview independently and Apply persists the accepted values", "[options][scratch-presentation]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    auto edit = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_DISPLAY);
    edit.field = WORM_UI_OPTIONS_FIELD_P1_SCRATCH_POSITION;
    edit.integer = 3;
    expect_options(!apply_options_action(runtime, program, edit));
    auto view = wormedit::ui::options_view_t::from(runtime);
    expect_options(view.p1_scratch_position == 3 && view.p2_scratch_position == 0);
    expect_options(program.preferences.display.scratch_positions[0] == 0);
    edit.revision = runtime.revision;
    edit.field = WORM_UI_OPTIONS_FIELD_P2_SCRATCH_POSITION;
    edit.integer = 1;
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.scratch_positions == std::array<std::uint32_t, 2>{3, 1});
    expect_options(load_program_preferences(program.path).contents.preferences.display.scratch_positions ==
                   program.preferences.display.scratch_positions);
    edit.revision = runtime.revision;
    edit.integer = std::numeric_limits<std::uint64_t>::max();
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(!wormedit::ui::options_view_t::from(runtime).can_apply);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.scratch_positions[1] == 1);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_REVERT)));
    expect_options(wormedit::ui::options_view_t::from(runtime).p2_scratch_position == 1);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS)));
    view = wormedit::ui::options_view_t::from(runtime);
    expect_options(view.p1_scratch_position == 0 && view.p2_scratch_position == 0);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_CLOSE)));
    expect_options(runtime.visible && runtime.confirm_discard);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DISCARD_AND_CLOSE)));
    expect_options(!runtime.visible);
    expect_options(program.preferences.display.scratch_positions == std::array<std::uint32_t, 2>{3, 1});
}

namespace {
void require_recent(bool value) { REQUIRE(value); }
}

TEST_CASE("Recent files persist normalized successful paths newest first", "[options][recent-files]") {
    temporary_preferences_t file;
    program_preferences_runtime_t runtime{.path = file.directory / "preferences.json"};
    const auto chart = file.directory / "music" / "chart.bms";
    const auto project = file.directory / "project.wormedit";
    require_recent(remember_recent_file(runtime, chart).empty());
    require_recent(remember_recent_file(runtime, project).empty());
    require_recent(remember_recent_file(runtime, file.directory / "." / "music" / "chart.bms").empty());
    require_recent(runtime.recent_files.size() == 2);
    expect_options(runtime.recent_files[0].label == "chart.bms");
    program_preferences_runtime_t reloaded{.path = runtime.path};
    load_recent_files(reloaded);
    require_recent(reloaded.recent_files.size() == 2);
    expect_options(reloaded.recent_files[0].path == runtime.recent_files[0].path);
    expect_options(reloaded.recent_files[1].path == runtime.recent_files[1].path);
    expect_options(reloaded.warnings.empty());
    reloaded.preferences.general.recent_files_count = 1;
    wormedit::ui::workspace_view_t view{};
    build_recent_files_view(reloaded, view);
    require_recent(view.recent_files.size() == 1);
    expect_options(std::string_view{view.recent_files[0].path} == runtime.recent_files[0].path);
    reloaded.preferences.general.recent_files_count = 100;
    build_recent_files_view(reloaded, view);
    expect_options(view.recent_files.size() == 2);
}

TEST_CASE("Recent files cap history and retain missing entries for normal open diagnostics", "[options][recent-files]") {
    temporary_preferences_t file;
    program_preferences_runtime_t runtime{.path = file.directory / "preferences.json"};
    for (unsigned index = 0; index < 103; ++index)
        require_recent(remember_recent_file(runtime, file.directory / (std::to_string(index) + ".bms")).empty());
    require_recent(runtime.recent_files.size() == 100);
    expect_options(runtime.recent_files.front().label == "102.bms");
    expect_options(runtime.recent_files.back().label == "3.bms");
    const auto previous = runtime.recent_files.front().path;
    expect_options(!(remember_recent_file(runtime, {}).empty()));
    expect_options(runtime.recent_files.front().path == previous);
    load_recent_files(runtime);
    expect_options(runtime.recent_files.size() == 100);
    expect_options(runtime.recent_files.front().path == previous);
}

TEST_CASE("Corrupt recent files do not prevent preference startup", "[options][recent-files]") {
    temporary_preferences_t file;
    program_preferences_runtime_t runtime{.path = file.directory / "preferences.json"};
    require_recent(static_cast<bool>(create_file_atomic(file.directory / "recent-files.json", "{broken", maximum_preferences_bytes)));
    load_recent_files(runtime);
    expect_options(runtime.recent_files.empty());
    expect_options(!(runtime.warnings.empty()));
}

namespace {
auto layout_skin_action(const options_runtime_t& runtime, const char* scheme) -> wormedit::ui::options_action_t {
    auto result = action(runtime, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_DISPLAY);
    result.field = WORM_UI_OPTIONS_FIELD_LAYOUT_FIELD_SCHEME;
    result.text = scheme;
    return result;
}
}

TEST_CASE("Options layout skin overrides apply revert and inherit without changing default", "[options][layout-skins]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    program.preferences.display.field_scheme = "classic";
    options_runtime_t runtime;
    set_options_field_layout(runtime, "beat-7k", "Beat 7 keys");
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    expect_options(!apply_options_action(runtime, program, layout_skin_action(runtime, "bmse")));
    expect_options(program.preferences.display.field_scheme_overrides.empty());
    expect_options(resolve_field_scheme(runtime.draft.display, "beat-7k") == "bmse");
    const auto view = wormedit::ui::options_view_t::from(runtime);
    expect_options(std::string_view{view.current_field_layout_key} == "beat-7k");
    expect_options(std::string_view{view.current_field_layout_name} == "Beat 7 keys");
    expect_options(std::string_view{view.current_layout_field_scheme} == "bmse");
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.field_scheme == "classic");
    expect_options(load_program_preferences(program.path).contents.preferences.display.field_scheme_overrides.at("beat-7k") == "bmse");
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, ""));
    expect_options(runtime.draft.display.field_scheme_overrides.empty());
    expect_options(std::string_view{wormedit::ui::options_view_t::from(runtime).current_layout_field_scheme}.empty());
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_REVERT));
    expect_options(resolve_field_scheme(runtime.draft.display, "beat-7k") == "bmse");
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, ""));
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(resolve_field_scheme(program.preferences.display, "beat-7k") == "classic");
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, "bmse"));
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS));
    expect_options(runtime.draft.display.field_scheme_overrides.empty());
    expect_options(runtime.draft.display.field_scheme == "wormedit");
}

TEST_CASE("Options layout changes reject stale choices and retain other draft overrides", "[options][layout-skins]") {
    program_preferences_runtime_t program;
    options_runtime_t runtime;
    set_options_field_layout(runtime, "beat-7k", "Beat 7 keys");
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, "bmse"));
    const auto stale = layout_skin_action(runtime, "classic");
    set_options_field_layout(runtime, "pump-single", "Pump single");
    expect_options(runtime.revision != stale.revision);
    (void)apply_options_action(runtime, program, stale);
    expect_options(runtime.draft.display.field_scheme_overrides.size() == 1);
    expect_options(runtime.draft.display.field_scheme_overrides.at("beat-7k") == "bmse");
    expect_options(std::string_view{wormedit::ui::options_view_t::from(runtime).current_layout_field_scheme}.empty());
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, "custom-unknown"));
    expect_options(resolve_field_scheme(runtime.draft.display, "pump-single") == "custom-unknown");
    const auto revision = runtime.revision;
    set_options_field_layout(runtime, "pump-single", "Updated label");
    expect_options(runtime.revision == revision);
    set_options_field_layout(runtime, "", "");
    const auto before = runtime.draft;
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, "bmse"));
    expect_options(runtime.draft == before);
}

TEST_CASE("Options rejects overflow and unterminated layout skin edits without polluting drafts", "[options][layout-skins]") {
    program_preferences_runtime_t program;
    for (unsigned index = 0; index < maximum_field_scheme_overrides; ++index)
        program.preferences.display.field_scheme_overrides.emplace(std::to_string(index), "classic");
    options_runtime_t runtime;
    set_options_field_layout(runtime, "overflow", "Overflow");
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    const auto before = runtime.draft;
    const auto revision = runtime.revision;
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, "bmse"));
    expect_options(runtime.draft == before && runtime.revision == revision);
    auto bad_text = layout_skin_action(runtime, "bmse");
    std::fill(std::begin(bad_text.text), std::end(bad_text.text), 'x');
    (void)apply_options_action(runtime, program, bad_text);
    expect_options(runtime.draft == before && runtime.revision == revision);
    set_options_field_layout(runtime, "0", "Existing layout");
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, "bmse"));
    expect_options(runtime.draft.display.field_scheme_overrides.size() == maximum_field_scheme_overrides);
    expect_options(runtime.draft.display.field_scheme_overrides.at("0") == "bmse");
    set_options_field_layout(runtime, std::string(maximum_preference_string_bytes + 1, 'k'), "Invalid key");
    expect_options(runtime.current_field_layout_key.empty());
}

TEST_CASE("Options layout skin validation and category isolation preserve accepted preferences", "[options][layout-skins]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path = file.directory / "preferences.json"};
    options_runtime_t runtime;
    set_options_field_layout(runtime, "beat-7k", "Beat 7 keys");
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    auto invalid = runtime.draft;
    invalid.display.field_scheme_overrides.emplace("beat-7k", std::string(maximum_preference_string_bytes + 1, 'x'));
    expect_options(edit_options_draft(runtime, runtime.revision, invalid));
    expect_options(!wormedit::ui::options_view_t::from(runtime).can_apply);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.field_scheme_overrides.empty());
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_REVERT));
    expect_options(wormedit::ui::options_view_t::from(runtime).can_apply);
    (void)apply_options_action(runtime, program, layout_skin_action(runtime, "bmse"));
    select(runtime, program, WORM_UI_OPTIONS_GENERAL);
    auto general = runtime.draft;
    general.general.recent_files_count = 20;
    expect_options(edit_options_draft(runtime, runtime.revision, general));
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(resolve_field_scheme(program.preferences.display, "beat-7k") == "bmse");
    select(runtime, program, WORM_UI_OPTIONS_DISPLAY);
    expect_options(resolve_field_scheme(runtime.draft.display, "beat-7k") == "bmse");
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(resolve_field_scheme(program.preferences.display, "beat-7k") == "bmse");
}

TEST_CASE("Options preflight rejects conflicts before exposing a display candidate",
          "[options][display-options][preflight]") {
    program_preferences_runtime_t program;
    options_runtime_t runtime;
    open_options_category(runtime, program.preferences, WORM_UI_OPTIONS_DISPLAY);
    auto display = runtime.draft;
    display.display.ui_scale_percent = 150;
    expect_options(edit_options_draft(runtime, runtime.revision, display));
    select(runtime, program, WORM_UI_OPTIONS_GENERAL);
    auto general = runtime.draft;
    general.general.autosave_minutes = 12;
    expect_options(edit_options_draft(runtime, runtime.revision, general));
    program.preferences.general.autosave_minutes = 8;
    const auto previous = program.preferences;
    expect_options(!prepare_options_apply(runtime, program.preferences));
    expect_options(program.preferences == previous);
    expect_options(runtime.draft.display.ui_scale_percent == 150);
    expect_options(runtime.notification.has_value());
    expect_options(runtime.status.find("changed outside Options") != std::string::npos);
}
