#include "nuklear_event_harness.h"
#include "sys/application_defaults.h"
#include "sys/application_command_dispatch.h"
#include "sys/compose_input.h"
#include "sys/compose_view.h"
#include "sys/output_group_export.h"
#include "sys/project_view.h"
#include "sys/resource_job_state.h"
#include "ui/nuklear/compose_state.h"
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/command_registry.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/native_project.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <source_location>
#include <thread>

using namespace wormedit;
using namespace wormedit::sys;
using wormedit::test::click_at;

namespace {
template<class T>
void expect(const T& condition, std::source_location at = std::source_location::current()) {
    INFO("Walkthrough expectation at " << at.line());
    REQUIRE(static_cast<bool>(condition));
}
auto key(SDL_Keycode code, SDL_Scancode scan, bool down = true) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down;
    event.key.key = code;
    event.key.scancode = scan;
    return event;
}
auto click_label(nuklear_event_harness_t& harness, std::string_view label)
    -> std::vector<workspace_action_snapshot_t> {
    (void)harness.frame();
    const auto point = harness.drawn_text_center(label);
    INFO(label << "\n" << harness.drawn_text());
    expect(point);
    return click_at(harness, point->first, point->second);
}
auto song_path() -> std::filesystem::path {
    return std::filesystem::path{WORMEDIT_SOURCE_ROOT} / "tests/fixtures/audio/tagged-project.ogg";
}
auto audio_project() -> song_document_t {
    auto song = make_audio_project_document(song_id_t{41}, song_path(), "Walkthrough", "Author",
        "osu-mania-7k", {.sample_rate = 44100, .channels = 2, .frame_count = 441000, .seekable = true});
    expect(song);
    return std::move(*song);
}
struct charting_t {
    document_history_t history{document_history_t::from(audio_project())};
    workspace_controller_t workspace{*workspace_controller_t::from(history).value};
    application_command_state_t commands;
    notification_sink_t notifications{notification_sink_t::muted()};
    compose_scene_cache_t cache;
    std::optional<editor_interaction_state_t> interaction;
    std::optional<timing_lane_pseudonote_outcome_t> timing_stage;
    command_registry_t registry{command_registry_t::from(
        [this](const command_invocation_context_t& context) { return invoke(context); })};
    frontend_workspace_view_t flattened;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    nuklear_event_harness_t harness{1600, 1000};
    charting_t() {
        workspace.active_tab = workspace_tab_t::compose;
        project();
        expect(interaction);
        commands.project.compose_gesture.note_paint_mode_active = true;
        commands.project.timing_recipe.timing_locked = true;
        project();
        (void)harness.frame();
    }
    void project() {
        flattened = {};
        flattened.view.compose.visible = true;
        populate_compose_view(flattened, history, workspace, cache, 1600, 1000,
                              interaction, commands, nullptr);
        auto& view = harness.view();
        view.project_id.value = history.document.id.value;
        view.document_revision = history.document.revision;
        view.selected_chart_id.value = workspace.selected_chart->value;
        view.compose = flattened.view.compose;
        view.timing_settings_visible = commands.preferences.timing_settings_visible;
    }
    auto invoke(const command_invocation_context_t& context) -> dispatch_outcome_t {
        const auto command = parse_application_command(context.command.value);
        expect(command);
        return invoke_application_command(*command,
            {.document = {.history = history, .workspace = workspace,
                          .notifications = notifications},
             .editor = {.commands = commands, .retained = interaction,
                        .timing_stage = timing_stage}});
    }
    auto focus() const -> command_focus_t {
        return {.workspace_active = true, .active_tab = workspace.active_tab,
                .visible_tabs = {workspace_tab_t::setup, workspace_tab_t::metadata,
                    workspace_tab_t::compose, workspace_tab_t::audio_slices,
                    workspace_tab_t::compare_validate}};
    }
    auto shortcut(const workspace_action_snapshot_t& snapshot) -> command_dispatch_outcome_t {
        const auto& action = snapshot.get();
        expect(action.shortcut_events.size() == 1);
        const auto& input = action.shortcut_events[0];
        return dispatch_shortcut(registry,
            {.chord = {.key = static_cast<shortcut_key_t>(input.hid_usage),
                       .modifiers = input.modifiers}, .auto_repeat = input.repeat},
            focus(), shortcut_primary_modifier_t::control);
    }
    auto send(const SDL_Event& event) -> workspace_action_snapshot_t {
        const auto snapshot = harness.frame({&event, 1});
        auto action = snapshot.get();
        const compose_input_inputs_t environment{
            .document = {.history = history, .workspace = workspace,
                         .notifications = notifications},
            .commands = commands, .controls = {cache, viewport, navigation}};
        INFO(dispatch_compose_input_action(action, environment, *interaction, 0));
        project();
        return snapshot;
    }
};
auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream input{path};
    expect(input);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
auto preview_group(document_history_t& history, std::size_t index,
                   const std::filesystem::path& root, stepmania_export_policy_t policy = {}) -> std::string {
    if (history.document.output_groups[index].family == format_family_t::stepmania)
        history.document.output_groups[index].stepmania = policy;
    project_chart_runtime_t runtime;
    task_scheduler_t scheduler{1, 1};
    wormedit::ui::setup_view_t view{.visible = true};
    view.refill(runtime, history.document);
    const wormedit::ui::project_output_group_action_t action{.kind = WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW,
        .project_id = history.document.id, .document_revision = history.document.revision,
        .session_generation = runtime.session_generation, .group_index = index};
    (void)apply_project_output_group_ui_action(runtime, history, action, scheduler, root);
    expect(runtime.preview_job());
    for (int attempt = 0; attempt < 1000 && runtime.preview_job()->is_busy(); ++attempt) {
        view.refill(runtime, history.document);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    view.refill(runtime, history.document);
    expect(!runtime.preview_job()->is_busy());
    return std::string{view.output_groups[index].preview_status};
}

}

TEST_CASE("Osu StepMania walkthrough opens Timing focus and visible settings from a new project",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    charting_t flow;
    const auto revision = flow.history.document.revision;
    const auto f3 = key(SDLK_F3, SDL_SCANCODE_F3);
    expect(flow.shortcut(flow.harness.frame({&f3, 1})).value.is_invoked());
    flow.project();
    (void)flow.harness.frame();
    expect(flow.workspace.active_tab == workspace_tab_t::compose);
    expect(flow.harness.view().compose.timing_focus);

    const auto* settings = find_command(flow.registry, {"timing.open_settings"});
    expect(settings);
    auto& view = flow.harness.view();
    view.command_menu_rows.resize(1);
    view.command_menu_rows[0] = {.id = settings->definition.id.value.c_str(),
        .label = settings->definition.display_name.c_str(), .enabled = settings->definition.enabled};
    expect(worm_ui_frontend_open_command_menu());
    (void)flow.harness.frame();
    (void)flow.harness.frame();
    const auto enter = key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto chosen = flow.harness.frame({&enter, 1});
    expect(chosen.get().command_menu_invoked);
    expect(std::string_view{chosen.get().command_menu_command_id} == "timing.open_settings");
    expect(invoke_command(flow.registry, {chosen.get().command_menu_command_id}, flow.focus()).value.is_invoked());
    flow.project();
    (void)flow.harness.frame();
    (void)flow.harness.frame();
    expect(flow.harness.drawn_text_center("Metronome"));
    expect(flow.harness.drawn_text_center("Timing lanes"));
    expect(flow.history.document.revision == revision);
    expect(flow.history.undo.empty());
}

TEST_CASE("Osu StepMania walkthrough returns from Timing focus with F4 and opens settings in Compose",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    charting_t flow;
    expect(invoke_command(flow.registry, {"workspace.open_timing"}, flow.focus()).value.is_invoked());
    flow.project();
    const auto f4 = key(SDLK_F4, SDL_SCANCODE_F4);
    expect(flow.shortcut(flow.harness.frame({&f4, 1})).value.is_invoked());
    flow.project();
    expect(!flow.harness.view().compose.timing_focus);
    expect(invoke_command(flow.registry, {"timing.open_settings"}, flow.focus()).value.is_invoked());
    flow.project();
    (void)flow.harness.frame();
    (void)flow.harness.frame();
    expect(flow.harness.drawn_text_center("Metronome"));
}

TEST_CASE("Osu StepMania walkthrough explains refused Draw and follows F4 back to note editing",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    charting_t flow;
    flow.commands.project.compose_gesture.note_paint_mode_active = false;
    const auto revision = flow.history.document.revision;
    const auto f3 = key(SDLK_F3, SDL_SCANCODE_F3);
    expect(flow.shortcut(flow.harness.frame({&f3, 1})).value.is_invoked());
    flow.project();
    // Availability belongs to the application; exercise its disabled registry
    // path and the same production feedback formatter used by the input boundary.
    expect(update_command_state(flow.registry, {"edit.tool_draw"}, false, false).value);
    const auto draw = key(SDLK_2, SDL_SCANCODE_2);
    const auto refused = flow.shortcut(flow.harness.frame({&draw, 1}, SDL_KMOD_LALT));
    expect(refused.value.status == command_dispatch_status_t::disabled);
    expect(unavailable_application_command_message(flow.registry, refused.value.command, flow.commands) ==
           "Timing focus is on: press F4 to edit notes.");
    expect(!flow.commands.project.compose_gesture.note_paint_mode_active);
    expect(flow.history.document.revision == revision);

    const auto f4 = key(SDLK_F4, SDL_SCANCODE_F4);
    expect(flow.shortcut(flow.harness.frame({&f4, 1})).value.is_invoked());
    flow.project();
    expect(!flow.harness.view().compose.timing_focus);
    expect(update_command_state(flow.registry, {"edit.tool_draw"}, true, false).value);
    expect(flow.shortcut(flow.harness.frame({&draw, 1}, SDL_KMOD_LALT)).value.is_invoked());
    flow.project();
    (void)flow.send(key(SDLK_1, SDL_SCANCODE_1));
    (void)flow.send(key(SDLK_1, SDL_SCANCODE_1, false));
    expect(flow.history.document.charts.front().measures.front().notes.size() == 1);
}

TEST_CASE("Osu StepMania walkthrough creates the chosen audio project and stages Metadata audio",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    nuklear_event_harness_t harness{1280, 1000};
    expect(harness.ready());
    harness.view().pending_new_project_audio_path = "/music/song.ogg";
    (void)harness.frame();
    (void)harness.frame();
    (void)click_label(harness, "7 keys");
    const auto frames = click_label(harness, "Create project");
    const auto created = std::ranges::find_if(frames, [](const auto& frame) {
        return frame.get().kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT;
    });
    expect(created != frames.end());
    expect(std::string_view{created->get().new_project_layout_id} == "osu-mania-7k");
    expect(std::string_view{created->get().project_path} == "/music/song.ogg");
    (void)harness.frame();
    auto& view = harness.view();
    view.pending_new_project_audio_path = "";
    view.active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
    view.compose.visible = false;
    view.metadata.visible = view.metadata.has_chart = true;
    view.metadata.chart_id = view.selected_chart_id;
    view.metadata.document_revision = view.document_revision;
    view.metadata.dropped_music_path = "/music/song.ogg";
    (void)harness.frame();
    (void)click_label(harness, "Audio and export");
    const auto linked = click_label(harness, "Use as song audio");
    const auto found = std::ranges::find_if(linked, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET;
    });
    expect(found != linked.end());
    expect(found->get().compose_resource_role == wormedit::resource_role_t::music);
    expect(std::string_view{found->get().compose_import_path} == "/music/song.ogg");
}

TEST_CASE("Osu StepMania walkthrough places plain taps and keyboard holds through production input",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    charting_t flow;
    expect(flow.harness.ready());
    const auto tap = flow.send(key(SDLK_1, SDL_SCANCODE_1));
    expect(tap.get().compose_inputs.size() == 1);
    expect(tap.get().compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    (void)flow.send(key(SDLK_1, SDL_SCANCODE_1, false));
    (void)flow.send(key(SDLK_2, SDL_SCANCODE_2));
    (void)flow.send(key(SDLK_UP, SDL_SCANCODE_UP));
    (void)flow.send(key(SDLK_UP, SDL_SCANCODE_UP, false));
    (void)flow.send(key(SDLK_2, SDL_SCANCODE_2, false));
    const auto& notes = flow.history.document.charts.front().measures.front().notes;
    expect(notes.size() == 2);
    expect(notes[0].kind == note_kind_t::tap);
    expect(!notes[0].sound);
    expect(notes[1].kind == note_kind_t::hold);
    expect(notes[1].end.has_value());
    expect(!notes[1].sound);
    expect(flow.history.document.charts.front().bpms.front().beats_per_minute == signed_rational_t{120, 1});
}

TEST_CASE("Osu StepMania walkthrough saves and publishes both groups with song audio",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    charting_t flow;
    (void)flow.send(key(SDLK_1, SDL_SCANCODE_1));
    (void)flow.send(key(SDLK_1, SDL_SCANCODE_1, false));
    auto& history = flow.history;
    expect(history.document.output_groups.size() == 2);
    const auto duplicate = apply_duplicate_chart_command(history, history.document.charts.front().id);
    expect(duplicate.command.applied);
    expect(duplicate.payload.chart);
    const auto added = apply_add_chart_command(history, history.document.charts.front().id);
    expect(added.command.applied);
    expect(added.payload.chart);
    for (std::size_t index = 0; index < 2; ++index) {
        auto group = history.document.output_groups[index];
        group.charts = {history.document.charts.front().id, *duplicate.payload.chart, *added.payload.chart};
        expect(apply_set_output_group_command(history, index, std::move(group)).applied);
    }
    const auto root = std::filesystem::path{WORMEDIT_SOURCE_ROOT} / ".agent/osu-stepmania-publication";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    expect(create_native_project_file(root / "song.wormedit", history.document).value);
    for (std::size_t index = 0; index < history.document.output_groups.size(); ++index) {
        const auto& group = history.document.output_groups[index];
        const auto preview = preview_group(history, index, root);
        INFO(preview);
        expect(preview.starts_with("Preview: "));
        const output_group_export_request_t request{.document = history.document,
            .plan = output_group_export_plan_t::from(history.document, group), .asset_base = root};
        const auto prepared = output_group_export_preparation_t::from(request, {});
        expect(prepared);
        expect(std::ranges::any_of(prepared->files, [](const auto& file) {
            return file.action == output_group_export_file_action_t::copy;
        }));
        expect(!std::filesystem::exists(root / group.relative_output_path));
        const auto result = run_output_group_export(request, {});
        INFO(group.name << ": " << result.message);
        expect(result.published);
        const project_asset_copy_request_t music{.source_path = song_path(),
            .asset = history.document.audio_assets.front().id};
        const auto asset_directory = export_asset_directory(group.relative_output_path,
                                                             group.destination_kind);
        expect(asset_directory);
        expect(std::filesystem::is_regular_file(root / *asset_directory /
                                                music.source_path.filename()));
    }
    const auto stepmania = std::ranges::find(history.document.output_groups,
                                              format_family_t::stepmania,
                                              &output_group_t::family);
    expect(stepmania != history.document.output_groups.end());
    const auto stepmania_path = root / stepmania->relative_output_path;
    expect(std::filesystem::is_regular_file(stepmania_path));
    expect(read_text(stepmania_path).find("#MUSIC:assets/") != std::string::npos);
}

TEST_CASE("Osu StepMania walkthrough palette searches registry labels and previews conversion",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    nuklear_event_harness_t harness{1280, 1000};
    auto registry = command_registry_t::from({});
    // The application owns command availability; use its registry labels with an enabled chart.
    const auto* command = find_command(registry, {"timing.tap_assistant"});
    expect(command);
    harness.view().command_menu_rows.resize(1);
    harness.view().command_menu_rows[0] = {.id = command->definition.id.value.c_str(), .label = command->definition.display_name.c_str(), .enabled = true};
    (void)harness.frame();
    expect(worm_ui_frontend_open_command_menu());
    (void)harness.frame();
    (void)harness.frame();
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "TAP";
    (void)harness.frame({&text, 1});
    const auto enter = key(SDLK_RETURN, SDL_SCANCODE_RETURN);
    const auto selected = harness.frame({&enter, 1});
    expect(selected.get().command_menu_invoked);
    expect(std::string_view{selected.get().command_menu_command_id} == "timing.tap_assistant");
    auto history = document_history_t::from(audio_project());
    const auto root = std::filesystem::temp_directory_path() / "wormedit-audio-preview-walkthrough";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto status = preview_group(history, 0, root);
    INFO(status);
    expect(status.starts_with("Preview: 2 file(s), 0 conversion warning(s), 0 conversion error(s)."));
    expect(std::filesystem::is_empty(root));
    // Render the production preview text through the Project tab surface.
    const auto chart = history.document.charts.front().id;
    const wormedit::ui::project_output_group_view_t group{.name = "osu!mania", .path = "osu",
        .chart_ids = {chart}, .preview_status = status.c_str()};
    auto& setup = harness.view().setup;
    harness.view().active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    harness.view().compose.visible = false;
    setup.visible = true;
    setup.output_groups = {group};
    (void)harness.frame();
    expect(harness.drawn_text().find("Preview: 2 file(s)") != std::string::npos);
    const output_group_export_request_t request{.document = history.document,
        .plan = output_group_export_plan_t::from(history.document, history.document.output_groups.front()),
        .asset_base = root};
    expect(run_output_group_export(request, {}).published);
    const auto asked = run_output_group_export(request, {});
    expect(!asked.published);
    expect(asked.confirmation_required);
    expect(preview_group(history, 0, root).starts_with("Preview: 2 file(s)"));
    history.document.audio_assets.front().source_path = "/missing/walkthrough-song.ogg";
    history.document.output_groups.front().relative_output_path = "missing-output";
    ++history.document.revision;
    auto missing_request = request;
    missing_request.document = history.document;
    missing_request.plan = output_group_export_plan_t::from(history.document, history.document.output_groups.front());
    const auto missing = run_output_group_export(missing_request, {});
    expect(!missing.published);
    expect(missing.message.find("walkthrough-song.ogg") != std::string::npos);
    const auto missing_preview = preview_group(history, 0, root);
    expect(missing_preview.starts_with("Cannot export: "));
    expect(missing_preview.find("walkthrough-song.ogg") != std::string::npos);
    expect(!std::filesystem::exists(root / "missing-output"));
    std::filesystem::remove_all(root);
}

TEST_CASE("Osu StepMania walkthrough refuses multiple music resources without publishing",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    auto song = audio_project();
    song.resources.push_back({.id = resource_id_t{100}, .name = "Second music",
        .asset = song.audio_assets.front().id, .roles = {resource_role_t::music}});
    const auto root = std::filesystem::path{WORMEDIT_SOURCE_ROOT} / ".agent/osu-stepmania-refusal";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    for (const auto& group : song.output_groups) {
        const auto result = run_output_group_export({.document = song,
            .plan = output_group_export_plan_t::from(song, group), .asset_base = root}, {});
        INFO(result.message);
        expect(!result.published);
        expect(result.message.find("music resource") != std::string::npos);
        expect(!std::filesystem::exists(root / group.relative_output_path));
    }
}

namespace {
struct walkthrough_save_directory_t {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("wormedit-walkthrough-save-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    walkthrough_save_directory_t() { std::filesystem::create_directories(path); }
    ~walkthrough_save_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

auto wait_for_saved_project(runtime::resource_copy_t& runtime)
    -> std::optional<project_save_job_t::completion_t> {
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        if (auto completed = runtime.poll_project_save()) return completed;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return std::nullopt;
}
}

TEST_CASE("Osu StepMania walkthrough Save copies linked song audio and repeat Save copies nothing",
          "[frontend-state][event-harness][walkthrough-osu-stepmania][project-save]") {
    walkthrough_save_directory_t directory;
    charting_t flow;
    const auto save_key = key(SDLK_S, SDL_SCANCODE_S);
    const auto action = flow.harness.frame({&save_key, 1}, SDL_KMOD_CTRL);
    expect(action.get().shortcut_events.size() == 1);
    expect(action.get().shortcut_events[0].hid_usage == static_cast<wormedit::shortcut_key_t>(SDL_SCANCODE_S));
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t runtime{scheduler};
    const auto destination = directory.path / "song.wormedit";
    expect(runtime.start_project_save(flow.history.document, song_path().parent_path(), destination));
    expect(runtime.is_project_save_busy());
    const auto completed = wait_for_saved_project(runtime);
    expect(completed);
    expect(*completed);
    const auto& report = *completed->value().payload;
    INFO(report.status_message());
    expect(report.storage.value);
    expect(report.copy_errors.empty());
    expect(report.copied_files.size() == 1);
    report.publish_paths(flow.history, song_path().parent_path());
    const auto saved_path = flow.history.document.audio_assets.front().source_path;
    expect(saved_path.is_relative());
    expect(saved_path.parent_path() == "assets");
    expect(read_text(directory.path / saved_path) == read_text(song_path()));
    expect(report.status_message().find(saved_path.generic_string()) != std::string::npos);
    const auto reopened = parse_native_project(read_text(destination));
    expect(reopened.value);
    expect(reopened.value->audio_assets.front().source_path == saved_path);
    const auto modified = std::filesystem::last_write_time(directory.path / saved_path);
    expect(runtime.start_project_save(flow.history.document, directory.path, destination));
    const auto repeated = wait_for_saved_project(runtime);
    expect(repeated);
    expect(*repeated);
    expect(repeated->value().payload->storage.value);
    expect(repeated->value().payload->copied_files.empty());
    expect(repeated->value().payload->copy_errors.empty());
    expect(std::filesystem::last_write_time(directory.path / saved_path) == modified);
}

TEST_CASE("Osu StepMania walkthrough preview receives incompatible StepMania target and syntax choices",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    auto document = make_initial_document(song_id_t{1}, "osu-mania-4k");
    expect(document.has_value());
    auto history = document_history_t::from(std::move(*document));
    const stepmania_export_policy_t policy{
        .timing_syntax = stepmania_timing_syntax_t::legacy_negative_stop,
        .implementation_profile = stepmania_implementation_profile_t::etterna_0_75_1};
    const auto status = preview_group(history, 1, std::filesystem::temp_directory_path(), policy);
    expect(status.starts_with("Preview: "));
    const output_group_export_request_t request{.document = history.document,
        .plan = output_group_export_plan_t::from(history.document,
                                                 history.document.output_groups[1]),
        .asset_base = std::filesystem::temp_directory_path()};
    const auto prepared = output_group_export_preparation_t::from(request, {});
    expect(prepared);
    expect(std::ranges::any_of(prepared->converted.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("audited only with native WARPS") != std::string::npos;
    }));
}

TEST_CASE("Export preview lists a new BME chart and its conversion warnings",
          "[frontend-state][event-harness][walkthrough-bms]") {
    auto document = make_initial_document(song_id_t{1}, "bme-7k-outside-scratch");
    expect(document);
    auto history = document_history_t::from(std::move(*document));
    const auto root = std::filesystem::temp_directory_path() / "wormedit-preview-warnings";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto status = preview_group(history, 0, root);
    INFO(status);
    expect(status.find("1 conversion warning(s)") != std::string::npos);
    const output_group_export_request_t request{.document = history.document,
        .plan = output_group_export_plan_t::from(history.document,
                                                 history.document.output_groups.front()),
        .asset_base = root};
    const auto prepared = output_group_export_preparation_t::from(request, {});
    expect(prepared);
    expect(!prepared->files.empty());
    expect(std::ranges::any_of(prepared->converted.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("BMS measure ratios") != std::string::npos;
    }));
    expect(std::filesystem::is_empty(root));
    nuklear_event_harness_t harness{1280, 1000};
    expect(harness.ready());
    const wormedit::ui::project_output_group_view_t group{.name = "BMS", .path = "bms",
        .preview_status = status.c_str()};
    harness.view().active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    harness.view().compose.visible = false;
    harness.view().setup.visible = true;
    harness.view().setup.output_groups = {group};
    (void)harness.frame();
    expect(harness.drawn_text().find(
               "Preview: 1 file(s), 1 conversion warning(s), 0 conversion error(s).") !=
           std::string::npos);
    expect(harness.drawn_text().find("Warning: BMS measure ratios") == std::string::npos);
    std::filesystem::remove_all(root);
}
