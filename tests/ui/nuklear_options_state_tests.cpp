#include "sys/options_runtime.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
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
    WormUiOptionsCategory category = WORM_UI_OPTIONS_GENERAL) -> WormUiOptionsAction {
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
    const auto view = options_view(r);
    expect_options(view.changed && view.can_apply && view.autosave_minutes == 12);
}
TEST_CASE("Options validates individual draft fields and stale actions atomically", "[options]") {
    program_preferences_runtime_t program;
    options_runtime_t r;
    open_options(r, program.preferences);
    const auto old_revision = r.revision;
    auto candidate = r.draft;
    candidate.general.autosave_minutes = 0;
    expect_options(edit_options_draft(r, old_revision, candidate));
    const auto view = options_view(r);
    expect_options(!view.can_apply && view.diagnostic_count == 1);
    expect_options(view.diagnostics[0].field == WORM_UI_OPTIONS_FIELD_AUTOSAVE);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options_notification(r, notification_severity_t::warning,
                                "Correct this page's invalid fields before applying.");
    expect_options(program.preferences.general.autosave_minutes == 5);
    expect_options(!edit_options_draft(r, old_revision, program.preferences));
    const auto current = r.revision;
    expect_options(!apply_options_action(r, program, {.kind = WORM_UI_OPTIONS_CLOSE, .revision = old_revision}));
    expect_options(r.visible && r.revision == current);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_SELECT_CATEGORY,
        static_cast<WormUiOptionsCategory>(99))));
    expect_options(r.category == WORM_UI_OPTIONS_GENERAL && r.revision == current);
}
TEST_CASE("Options applies one page atomically preserving other applied sections and drafts", "[options]") {
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
    program.preferences.display.theme = "external-theme";
    expect_options(apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options_notification(r, notification_severity_t::success, "Options saved.");
    expect_options(program.preferences.audio.keep_pitch);
    expect_options(program.preferences.general.autosave_minutes == 5);
    expect_options(program.preferences.display.theme == "external-theme");
    expect_options(r.draft.general.autosave_minutes == 12);
    const auto loaded = load_program_preferences(program.path);
    expect_options(loaded.contents.parsed && loaded.contents.preferences == program.preferences);
    select(r, program, WORM_UI_OPTIONS_GENERAL);
    expect_options(apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
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
    expect_options(!options_view(r).changed);
    (void)apply_options_action(r, program, action(r, WORM_UI_OPTIONS_CLOSE));
    program.preferences.general.autosave_minutes = 19;
    open_options(r, program.preferences);
    expect_options(r.draft.general.autosave_minutes == 19);
}
TEST_CASE("Options no-op skips publication and publication failure keeps accepted values", "[options]") {
    temporary_preferences_t file;
    const auto target = file.directory / "existing-directory";
    std::filesystem::create_directory(target);
    program_preferences_runtime_t program{.path = target};
    options_runtime_t r;
    open_options(r, program.preferences);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(r.status == "This page has no changes.");
    expect_options_notification(r, notification_severity_t::info, r.status);
    auto candidate = r.draft;
    candidate.general.autosave_minutes = 12;
    expect_options(edit_options_draft(r, r.revision, candidate));
    expect_options(apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.general.autosave_minutes == 12);
    expect_options(!options_view(r).changed && r.status.find("saving failed") != std::string::npos);
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
        const auto view = options_view(r);
        expect_options(!view.can_apply && view.diagnostic_count == 1 && view.diagnostics[0].field == field);
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
    expect_options(!options_view(r).can_apply);
    expect_options(!apply_options_action(r, program, action(r, WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.theme.empty());
    candidate.display.theme = std::string(1, static_cast<char>(0xff));
    expect_options(edit_options_draft(r, r.revision, candidate));
    const auto view = options_view(r);
    expect_options(!view.can_apply && view.diagnostics[0].field == WORM_UI_OPTIONS_FIELD_PAGE);
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
    expect_options(!options_view(r).can_apply && r.draft.general.autosave_minutes == 0);
    edit.revision = r.revision; edit.integer = 15;
    (void)apply_options_action(r, program, edit);
    expect_options(options_view(r).can_apply);
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
    std::fill(std::begin(edit.text), std::end(edit.text), 'x');
    const auto revision = r.revision;
    (void)apply_options_action(r, program, edit);
    expect_options(r.revision == revision && r.draft.general.shortcut_profile_path == "shortcuts.json");
    std::fill(std::begin(edit.text), std::end(edit.text), '\0');
    (void)apply_options_action(r, program, edit);
    expect_options(!options_view(r).can_apply);
    select(r, program, WORM_UI_OPTIONS_PLAYERS);
    edit = action(r, WORM_UI_OPTIONS_EDIT_FIELD, WORM_UI_OPTIONS_PLAYERS);
    edit.field = WORM_UI_OPTIONS_FIELD_PLAYER_PATH; edit.player_index = 999;
    const auto before = r.draft.players;
    (void)apply_options_action(r, program, edit);
    expect_options(r.draft.players == before);
    edit.field = WORM_UI_OPTIONS_FIELD_PLAYER_NAME; edit.player_index = 0;
    std::copy_n("Test player", 12, edit.text);
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
    expect_options(!options_view(runtime).can_apply);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS));
    expect_options(runtime.draft.editor == editor_preferences_t{});
    expect_options(options_view(runtime).can_apply);
    expect_options(program.preferences.general.autosave_minutes == 5);
}

TEST_CASE("Editor options save the note click audition toggle", "[options][audition]") {
    temporary_preferences_t temporary;
    program_preferences_runtime_t program;
    program.path = temporary.directory / "preferences.json";
    options_runtime_t runtime;
    open_options(runtime, program.preferences);
    select(runtime, program, WORM_UI_OPTIONS_EDITOR);
    expect_options(options_view(runtime).audition_on_click);
    const WormUiOptionsAction edit{.kind = WORM_UI_OPTIONS_EDIT_FIELD,
        .revision = runtime.revision, .category = WORM_UI_OPTIONS_EDITOR,
        .field = WORM_UI_OPTIONS_FIELD_AUDITION_ON_CLICK, .boolean = false};
    expect_options(!apply_options_action(runtime, program, edit));
    expect_options(!options_view(runtime).audition_on_click);
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
    expect_options(options_view(runtime).keysound_budget_mib == 8192);
    expect_options(program.preferences.audio.keysound_budget_mib == 512);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(load_program_preferences(program.path).contents.preferences.audio.keysound_budget_mib == 8192);
    edit.revision = runtime.revision;
    edit.integer = 0;
    (void)apply_options_action(runtime, program, edit);
    expect_options(options_view(runtime).keysound_budget_mib == 256);
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
    expect_options(options_view(runtime).move_collision_policy == 1 && options_view(runtime).can_apply);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(load_program_preferences(program.path).contents.preferences.editor.move_collision_policy ==
        move_collision_policy_t::displace_to_bgm);
    (void)apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS));
    expect_options(options_view(runtime).move_collision_policy == 0);
}

TEST_CASE("Field theme Options edits stay draft until Apply and persist independently of chrome", "[options][theme]") {
    temporary_preferences_t file;
    program_preferences_runtime_t program{.path=file.directory/"preferences.json"};
    options_runtime_t runtime;
    open_options(runtime,program.preferences);
    select(runtime,program,WORM_UI_OPTIONS_DISPLAY);
    auto edit=action(runtime,WORM_UI_OPTIONS_EDIT_FIELD,WORM_UI_OPTIONS_DISPLAY);
    edit.field=WORM_UI_OPTIONS_FIELD_FIELD_SCHEME;
    std::strcpy(edit.text,"ibmsc-o2mania");
    expect_options(!(apply_options_action(runtime,program,edit)));
    expect_options(program.preferences.display.field_scheme=="wormedit");
    expect_options(std::string(options_view(runtime).field_scheme)=="ibmsc-o2mania");
    expect_options(apply_options_action(runtime,program,action(runtime,WORM_UI_OPTIONS_APPLY)));
    expect_options(program.preferences.display.field_scheme=="ibmsc-o2mania");
    expect_options(program.preferences.display.theme.empty());
    const auto loaded=load_program_preferences(program.path);
    expect_options(loaded.contents.parsed);
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
    expect_options(!options_view(runtime).snap_colored_grid);
    expect_options(program.preferences.display.snap_colored_grid);
    expect_options(apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_APPLY)));
    expect_options(!load_program_preferences(program.path).contents.preferences.display.snap_colored_grid);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_DEFAULTS)));
    expect_options(options_view(runtime).snap_colored_grid);
    expect_options(!program.preferences.display.snap_colored_grid);
    expect_options(!apply_options_action(runtime, program, action(runtime, WORM_UI_OPTIONS_REVERT)));
    expect_options(!options_view(runtime).snap_colored_grid);
}
