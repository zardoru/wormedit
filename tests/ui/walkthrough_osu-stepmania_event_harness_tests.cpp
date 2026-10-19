#include "nuklear_event_harness.h"
#include "sys/application_defaults.h"
#include "sys/compose_input.h"
#include "sys/compose_view.h"
#include "sys/output_group_export.h"
#include "sys/project_view.h"
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
        view.project_id = history.document.id.value;
        view.document_revision = history.document.revision;
        view.selected_chart_id = workspace.selected_chart->value;
        view.compose = flattened.view.compose;
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
    project_chart_runtime_t runtime;
    task_scheduler_t scheduler{1, 1};
    WormUiSetupView view{.visible = true};
    build_project_chart_view(runtime, history.document, view);
    const WormUiProjectOutputGroupAction action{.kind = WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW,
        .project_id = history.document.id.value, .document_revision = history.document.revision,
        .session_generation = runtime.session_generation, .group_index = index};
    (void)apply_project_output_group_ui_action(runtime, history, action, scheduler, policy, root);
    expect(runtime.preview_job());
    for (int attempt = 0; attempt < 1000 && runtime.preview_job()->is_busy(); ++attempt) {
        build_project_chart_view(runtime, history.document, view);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    build_project_chart_view(runtime, history.document, view);
    expect(!runtime.preview_job()->is_busy());
    return view.output_groups[index].preview_status;
}

}

TEST_CASE("Osu StepMania walkthrough creates the chosen audio project and stages Metadata audio",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    nuklear_event_harness_t harness{1280, 1000};
    expect(harness.ready());
    harness.view().pending_new_project_audio_path = "/music/song.ogg";
    (void)harness.frame();
    (void)harness.frame();
    (void)click_label(harness, "4K");
    const auto layout_row = harness.drawn_text_center("6K");
    expect(layout_row);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -3;
    wheel.wheel.mouse_x = layout_row->first;
    wheel.wheel.mouse_y = layout_row->second;
    const std::array scroll{wormedit::test::mouse_motion(layout_row->first, layout_row->second), wheel};
    (void)harness.frame(scroll);
    (void)click_label(harness, "7K");
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
    expect(found->get().compose_resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_MUSIC);
    expect(std::string_view{found->get().compose_import_path} == "/music/song.ogg");
}

TEST_CASE("Osu StepMania walkthrough places plain taps and keyboard holds through production input",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    charting_t flow;
    expect(flow.harness.ready());
    const auto tap = flow.send(key(SDLK_1, SDL_SCANCODE_1));
    expect(tap.get().compose_input_count == 1);
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

TEST_CASE("Osu StepMania walkthrough saves and publishes all three groups with song audio",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    charting_t flow;
    (void)flow.send(key(SDLK_1, SDL_SCANCODE_1));
    (void)flow.send(key(SDLK_1, SDL_SCANCODE_1, false));
    auto& history = flow.history;
    expect(history.document.output_groups.size() == 3);
    const auto duplicate = apply_duplicate_chart_command(history, history.document.charts.front().id);
    expect(duplicate.command.applied);
    expect(duplicate.payload.chart);
    const auto added = apply_add_chart_command(history, history.document.charts.front().id);
    expect(added.command.applied);
    expect(added.payload.chart);
    for (std::size_t index = 0; index < 3; ++index) {
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
        expect(preview.find("\nWrite: " + group.relative_output_path.generic_string() + "/") != std::string::npos);
        expect(preview.find("\nCopy: " + group.relative_output_path.generic_string() + "/assets/") != std::string::npos);
        expect(!std::filesystem::exists(root / group.relative_output_path));
        const auto result = run_output_group_export({.document = history.document,
            .plan = output_group_export_plan_t::from(history.document, group), .asset_base = root}, {});
        INFO(group.name << ": " << result.message);
        expect(result.published);
        const project_asset_copy_request_t music{.source_path = song_path(),
            .asset = history.document.audio_assets.front().id};
        expect(std::filesystem::is_regular_file(root / group.relative_output_path /
                                                "assets" / music.destination_filename()));
    }
    expect(std::filesystem::is_regular_file(root / "sm/walkthrough-41.sm"));
    expect(std::filesystem::is_regular_file(root / "ssc/walkthrough-41.ssc"));
    expect(read_text(root / "sm/walkthrough-41.sm").find("#MUSIC:assets/") != std::string::npos);
    expect(read_text(root / "ssc/walkthrough-41.ssc").find("#MUSIC:assets/") != std::string::npos);
}

TEST_CASE("Osu StepMania walkthrough palette searches registry labels and previews conversion",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    nuklear_event_harness_t harness{1280, 1000};
    auto registry = command_registry_t::from({});
    // The application owns command availability; use its registry labels with an enabled chart.
    const auto* command = find_command(registry, {"timing.tap_assistant"});
    expect(command);
    harness.view().command_menu_count = 1;
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
    expect(status.starts_with("Preview: 1 file(s), 0 conversion warning(s)."));
    expect(status.find("\nWrite: osu/") != std::string::npos);
    expect(status.find("\nCopy: osu/assets/") != std::string::npos);
    expect(std::filesystem::is_empty(root));
    // Render the production preview text through the Project tab surface.
    const auto chart = history.document.charts.front().id.value;
    const WormUiProjectOutputGroupView group{.name = "osu!mania", .path = "osu",
        .chart_ids = &chart, .chart_count = 1, .preview_status = status.c_str()};
    auto& setup = harness.view().setup;
    harness.view().active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    harness.view().compose.visible = false;
    setup.visible = true;
    setup.output_groups = &group;
    setup.output_group_count = 1;
    setup.stepmania_policy_status = "Ready";
    (void)harness.frame();
    expect(harness.drawn_text().find("Copy: osu/assets/") != std::string::npos);
    const output_group_export_request_t request{.document = history.document,
        .plan = output_group_export_plan_t::from(history.document, history.document.output_groups.front()),
        .asset_base = root};
    expect(run_output_group_export(request, {}).published);
    const auto refused = run_output_group_export(request, {});
    expect(preview_group(history, 0, root) == "Cannot export: " + refused.message);
    history.document.audio_assets.front().source_path = "/missing/walkthrough-song.ogg";
    history.document.output_groups.front().relative_output_path = "missing-output";
    ++history.document.revision;
    auto missing_request = request;
    missing_request.document = history.document;
    missing_request.plan = output_group_export_plan_t::from(history.document, history.document.output_groups.front());
    const auto missing = run_output_group_export(missing_request, {});
    expect(!missing.published);
    expect(missing.message.find("walkthrough-song.ogg") != std::string::npos);
    expect(preview_group(history, 0, root) == "Cannot export: " + missing.message);
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

TEST_CASE("Osu StepMania walkthrough preview receives incompatible StepMania target and syntax choices",
          "[frontend-state][event-harness][walkthrough-osu-stepmania]") {
    auto document = make_initial_document(song_id_t{1}, "osu-mania-4k");
    expect(document.has_value());
    auto history = document_history_t::from(std::move(*document));
    const stepmania_export_policy_t policy{
        .timing_syntax = stepmania_timing_syntax_t::legacy_negative_stop,
        .implementation_profile = stepmania_implementation_profile_t::etterna_0_75_1};
    const auto status = preview_group(history, 2, std::filesystem::temp_directory_path(), policy);
    expect(status.find("audited only with native WARPS") != std::string::npos);
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
    expect(status.find("\nWrite: ") != std::string::npos);
    expect(status.find("\nWarning: BMS measure ratios") != std::string::npos);
    expect(std::filesystem::is_empty(root));
    nuklear_event_harness_t harness{1280, 1000};
    expect(harness.ready());
    const WormUiProjectOutputGroupView group{.name = "BMS", .path = "bms",
        .preview_status = status.c_str()};
    harness.view().active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    harness.view().compose.visible = false;
    harness.view().setup.visible = true;
    harness.view().setup.output_groups = &group;
    harness.view().setup.output_group_count = 1;
    harness.view().setup.stepmania_policy_status = "Ready";
    (void)harness.frame();
    expect(harness.drawn_text().find("Warning: BMS measure ratios") != std::string::npos);
    std::filesystem::remove_all(root);
}
