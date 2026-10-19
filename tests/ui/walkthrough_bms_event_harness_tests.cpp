#include "nuklear_event_harness.h"
#include "sys/application_defaults.h"
#include "sys/compose_input.h"
#include "sys/compose_sound_actions.h"
#include "sys/output_group_export.h"
#include "ui/nuklear/compose_state.h"
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <source_location>

namespace {
using namespace wormedit;
using namespace wormedit::sys;


// Catch assertion macros contribute branches to cognitive complexity. Keep the
// scripted steps readable and attribute failures to their original call sites.
template <typename T>
void require_step(const T& condition,
                  std::source_location where = std::source_location::current()) {
    INFO("walkthrough assertion at " << where.line());
    REQUIRE(static_cast<bool>(condition));
}
void check_step(bool condition,
                std::source_location where = std::source_location::current()) {
    INFO("walkthrough assertion at " << where.line());
    CHECK(condition);
}
void check_false_step(bool condition,
                      std::source_location where = std::source_location::current()) {
    INFO("walkthrough assertion at " << where.line());
    CHECK_FALSE(condition);
}

auto key(SDL_Keycode code, SDL_Scancode scan, bool down = true) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = code;
    event.key.scancode = scan;
    event.key.down = down;
    return event;
}

auto read_file(const std::filesystem::path& path) -> std::string {
    std::ifstream stream{path, std::ios::binary};
    require_step(stream);
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

auto new_bme() -> song_document_t {
    auto document = make_initial_document(song_id_t{1}, "bme-7k-outside-scratch");
    require_step(document);
    return std::move(*document);
}

auto serialized_document(song_document_t document) -> std::string {
    document.revision = 1; // Undo changes the revision; compare all persisted content.
    auto written = write_native_project(document);
    require_step(written.value);
    return std::move(*written.value);
}

auto click_text(nuklear_event_harness_t& harness, std::string_view text, bool twice = false)
    -> std::vector<workspace_action_snapshot_t> {
    const auto point = harness.drawn_text_center(text);
    require_step(point);
    return test::click_at(harness, point->first, point->second, twice);
}

struct walkthrough_t {
    document_history_t history{document_history_t::from(new_bme())};
    decltype(workspace_controller_t::from(history)) workspace{workspace_controller_t::from(history)};
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    frontend_workspace_view_t flattened;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    notification_sink_t notifications{notification_sink_t::muted()};
    nuklear_event_harness_t harness{1600, 1000};

    walkthrough_t() {
        require_step(harness.ready());
        require_step(workspace.value);
        workspace.value->active_tab = workspace_tab_t::compose;
        project();
        (void)harness.frame();
    }
    void project() {
        flattened = {};
        flattened.view.compose.visible = true;
        populate_compose_view(flattened, history, *workspace.value, cache, 1600, 1000,
                              interaction, application, nullptr);
        require_step(interaction);
        require_step(flattened.view.compose.scene_available);
        auto& view = harness.view();
        view.project_id = history.document.id.value;
        view.selected_chart_id = history.document.charts.front().id.value;
        view.document_revision = history.document.revision;
        view.compose = flattened.view.compose;
        view.compose.visible = true;
    }
    auto dispatch(const workspace_action_snapshot_t& snapshot) -> std::string {
        auto action = snapshot.get();
        const compose_input_inputs_t environment{
            .document = {.history = history, .workspace = *workspace.value,
                         .notifications = notifications,
                         .auditions = &application.project.auditions},
            .commands = application,
            .controls = {.cache = cache, .viewport = viewport, .navigation = navigation}};
        return dispatch_compose_input_action(action, environment, *interaction, 0.0);
    }
    auto press(SDL_Keycode code, SDL_Scancode scan, bool down = true)
        -> workspace_action_snapshot_t {
        const auto event = key(code, scan, down);
        return harness.frame(std::span{&event, 1});
    }
};

auto import_review(walkthrough_t& flow) -> resource_id_t {
    const auto path = (std::filesystem::path{WORMEDIT_SOURCE_ROOT} /
                       "tests/fixtures/audio/keysound-c4.ogg").string();
    const std::array paths{path};
    const auto chart = flow.history.document.charts.front().id;
    const auto prepared = prepare_keysound_import(flow.history.document, chart, paths);
    require_step(prepared.entries.size() == 1);
    const WormUiSoundImportItem item{
        .path = path.c_str(), .name = "keysound-c4.ogg", .kind = 2, .kind_fixed = false};
    require_step(worm_ui_frontend_stage_sound_import(1, chart.value, flow.history.document.revision,
                                                &item, 1, nullptr, 0, false, ""));
    (void)flow.harness.frame();
    (void)flow.harness.frame();
    const auto confirmed = click_text(flow.harness, "Confirm import");
    for (const auto& snapshot : confirmed) {
        if (snapshot.get().compose_action != WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT) continue;
        const auto result = dispatch_compose_sound_action(snapshot.get(), flow.history,
            *flow.workspace.value, chart, flow.application);
        require_step(result);
        INFO(*result);
    }
    require_step(flow.history.document.resources.size() == 1);
    require_step(flow.history.undo.size() == 1);
    worm_ui_frontend_sound_import_result(true, "Imported");
    flow.project();
    (void)flow.harness.frame();
    return flow.history.document.resources.front().id;
}
} // namespace

TEST_CASE("BMS walkthrough wizard emits the BME identity and creates its chart and output group",
          "[frontend-state][event-harness][walkthrough-bms]") {
    nuklear_event_harness_t harness{1280, 900};
    require_step(harness.ready());
    // Keep the production combo interaction bounded to the default and the chosen catalog entry.
    constexpr std::array<WormUiStringDescriptorView, 2> entries{{
        {.id = "osu-mania-7k", .label = "osu!mania 7K"},
        {.id = "bme-7k-outside-scratch", .label = "BME 7K (outside scratch)"}}};
    const std::array labels{entries[0].label, entries[1].label};
    harness.view().new_project_layouts = {
        .entries = entries.data(), .count = entries.size(), .labels = labels.data()};
    (void)harness.frame();
    require_step(worm_ui_frontend_request_new_project());
    (void)harness.frame();
    (void)harness.frame();
    (void)click_text(harness, "osu!mania 7K");
    (void)click_text(harness, "BME 7K (outside scratch)");
    const auto created = click_text(harness, "Create project");
    require_step(created.front().get().kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT);
    const auto document = make_initial_document(song_id_t{1}, created.front().get().new_project_layout_id);
    require_step(document);
    require_step(document->charts.size() == 1);
    check_step(document->charts.front().input_profile.bindings.size() == 8);
    check_step(document->charts.front().lane_layouts.front().layout.lanes.front().role == lane_role_t::scratch);
    require_step(document->output_groups.size() == 1);
    check_step(document->output_groups.front().family == format_family_t::bms);
}

TEST_CASE("BMS walkthrough reviews a sound then arms and enters a keyboard tap and hold",
          "[frontend-state][event-harness][walkthrough-bms]") {
    walkthrough_t flow;
    const auto sound = import_review(flow);
    // Production Draw state; Alt+2 dispatch is exercised by the existing routing test.
    flow.application.project.compose_gesture.note_paint_mode_active = true;
    flow.project();
    auto& view = flow.harness.view().compose;
    const WormUiComposeKeysoundRowView row{
        .resource_id = sound.value, .slot = "", .name = "keysound-c4.ogg"};
    view.keysounds = &row;
    view.keysound_count = view.keysound_total_count = 1;
    view.keysounds_available = true;
    auto& ui = worm_ui_nuklear_compose_state();
    ui.dock_layout.sounds_panel_visible = true;
    (void)flow.harness.frame();
    const auto armed = click_text(flow.harness, "keysound-c4.ogg", true);
    for (const auto& snapshot : armed) (void)flow.dispatch(snapshot);
    check_step(ui.resource_picker.armed_resource_id == sound.value);
    const auto motion = test::mouse_motion(400, 400);
    (void)flow.harness.frame(std::span{&motion, 1});
    const auto press = flow.press(SDLK_2, SDL_SCANCODE_2);
    require_step(press.get().compose_input_count == 1);
    const auto started = flow.dispatch(press);
    INFO(started);
    const auto released = flow.dispatch(flow.press(SDLK_2, SDL_SCANCODE_2, false));
    INFO(released);
    require_step(flow.history.document.charts.front().measures.front().notes.size() == 1);
    check_step(flow.history.document.charts.front().measures.front().notes.front().sound == sound);
    flow.project();
    (void)flow.harness.frame();
    (void)flow.dispatch(flow.press(SDLK_3, SDL_SCANCODE_3));
    (void)flow.dispatch(flow.press(SDLK_UP, SDL_SCANCODE_UP));
    (void)flow.dispatch(flow.press(SDLK_UP, SDL_SCANCODE_UP, false));
    (void)flow.dispatch(flow.press(SDLK_3, SDL_SCANCODE_3, false));
    const auto& notes = flow.history.document.charts.front().measures.front().notes;
    require_step(notes.size() == 2);
    check_step(notes.back().kind == note_kind_t::hold);
    check_step(notes.back().sound == sound);
    check_step(notes.back().end.has_value());
}

TEST_CASE("BMS walkthrough pastes all four editor formats through preview commit and Undo",
          "[frontend-state][event-harness][walkthrough-bms]") {
    const auto* name = GENERATE("bmse-3.0-mixed.clip", "bmse-legacy-mixed.clip",
        "ibmsc-3.5.5.16-mixed.clip", "ibmsc-3.5.5.16-xnt-mixed.clip");
    const std::string source = "#PLAYER 1\r\n#BPM 120\r\n#WAV01 a.ogg\r\n#WAV02 b.ogg\r\n"
        "#WAV03 c.ogg\r\n#WAV04 d.ogg\r\n#WAV05 e.ogg\r\n#WAV06 f.ogg\r\n"
        "#00311:01\r\n#00316:01\r\n#00318:01\r\n#00319:01\r\n#00301:01\r\n";
    auto parsed = parse_bms_bytes(std::as_bytes(std::span{source}),
        {.file_extension = ".bme", .lane_profile_id = "bme-7k-outside-scratch"});
    require_step(parsed.value.song);
    auto history = document_history_t::from(std::move(*parsed.value.song));
    history.document.charts.front().divisions_per_standard_measure = 1344;
    editor_interaction_state_t state;
    state.chart = history.document.charts.front().id;
    state.cursor = {history.document.charts.front().measures[1].id, 0};
    application_command_state_t application;
    const auto text = read_file(std::filesystem::path{WORMEDIT_SOURCE_ROOT} /
                                "tests/fixtures/bms/clipboard" / name);
    application.services.system_clipboard_text = [&text] { return text; };
    std::string status;
    require_step(begin_compose_paste(application, history, state, status));
    check_step(status.starts_with("Pasting BMS editor data"));
    require_step(application.project.clipboard.paste_session);
    check_step(application.project.clipboard.paste_session->clipboard.objects.size() == 9);
    const auto before = history.document;
    INFO(status);
    require_step(apply_compose_paste(application, history, state, status));
    check_step(status == "Selection pasted.");
    check_step(history.undo.size() == 1);
    require_step(undo_document_command(history));
    check_step(serialized_document(history.document) == serialized_document(before));
}

TEST_CASE("BMS walkthrough saves then publishes a folder chart and refuses replacement",
          "[frontend-state][event-harness][walkthrough-bms]") {
    walkthrough_t flow;
    const auto sound = import_review(flow);
    const auto chart = flow.history.document.charts.front().id;
    const auto measure = flow.history.document.charts.front().measures.front().id;
    flow.interaction->active_lane = flow.history.document.charts.front().input_profile.bindings[1].lane;
    require_step(dispatch_editor_action(flow.history, *flow.interaction,
        place_tap_action_t{.position = {measure, 0}, .sound = sound}));
    const auto directory = std::filesystem::path{WORMEDIT_SOURCE_ROOT} / ".agent/bms-export-walkthrough";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto saved = create_native_project_file(directory / "song.wormedit", flow.history.document);
    require_step(saved.value);
    const auto reopened = parse_native_project(read_file(directory / "song.wormedit"));
    require_step(reopened.value);
    check_step(serialized_document(*reopened.value) == serialized_document(flow.history.document));
    auto group = flow.history.document.output_groups.front();
    require_step(group.charts == std::vector{chart});
    group.relative_output_path = "song/normal.bme";
    const output_group_export_request_t request{
        .document = flow.history.document,
        .plan = output_group_export_plan_t::from(flow.history.document, group),
        .asset_base = directory};
    const auto prepared = output_group_export_preparation_t::from(request);
    require_step(prepared);
    check_step(prepared->pending_assets.size() == 1);
    check_step(!std::filesystem::exists(directory / "song"));
    check_step(std::ranges::any_of(prepared->converted.diagnostics, [](const auto& diagnostic) {
        return diagnostic.loss && !diagnostic.message.empty();
    }));
    const auto exported = run_output_group_export(request, {});
    INFO(exported.message);
    require_step(exported.published);
    const auto converted = convert_document(request.document, request.plan.request);
    require_step(converted);
    require_step(converted.outputs.size() == 1);
    const auto output = directory / group.relative_output_path;
    const auto bytes = read_file(output);
    check_step(bytes.find("#WAV01 ") != std::string::npos);
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{bytes}),
        {.file_extension = output.extension().string()});
    require_step(parsed.value.song);
    require_step(parsed.value.song->audio_assets.size() == 1);
    const auto copied = output.parent_path() / parsed.value.song->audio_assets.front().source_path;
    check_step(std::filesystem::is_regular_file(copied));
    check_step(read_file(copied) == read_file(flow.history.document.audio_assets.front().source_path));
    // Pure conversion remains available without publication.
    const auto preview = convert_document(request.document, request.plan.request);
    require_step(preview);
    check_step(preview.outputs.size() == 1);
    const auto second = run_output_group_export(request, {});
    check_false_step(second.published);
    check_step(read_file(output) == bytes);
    std::filesystem::remove_all(directory);
}

TEST_CASE("BMS walkthrough Project group edit preview and export emit their production actions",
          "[frontend-state][event-harness][walkthrough-bms]") {
    nuklear_event_harness_t harness{1600, 1200};
    require_step(harness.ready());
    const std::uint64_t chart_id = 2;
    const WormUiProjectChartRow chart{
        .chart_id = chart_id, .name = "Normal", .author = "Author", .rating = "4",
        .mode = "BME 7K", .output_groups = "BMS", .length = "2 s", .diagnostic = ""};
    const WormUiProjectOutputGroupView group{
        .document_index = 0, .name = "BMS", .family = static_cast<std::uint8_t>(format_family_t::bms),
        .path = "song/normal.bme", .chart_ids = &chart_id, .chart_count = 1,
        .preview_status = "Preview not run"};
    auto& view = harness.view();
    view.active_tab = WORM_UI_WORKSPACE_TAB_SETUP;
    view.compose.visible = false;
    view.setup.visible = true;
    view.setup.song_id = 1;
    view.setup.document_revision = 1;
    // Group export remains available without a selected chart row.
    view.setup.selected_chart_id = 0;
    view.setup.charts = &chart;
    view.setup.chart_count = view.setup.chart_row_count = 1;
    view.setup.output_groups = &group;
    view.setup.output_group_count = 1;
    view.setup.stepmania_policy_status = "Ready";
    (void)harness.frame();
    (void)click_text(harness, "Edit");
    (void)harness.frame();
    require_step(harness.drawn_text_center("Output path"));
    const auto saved = click_text(harness, "Save group");
    check_step(saved.front().get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE);
    check_step(std::string_view{saved.front().get().project_output_group.path} == "song/normal.bme");
    const auto preview = click_text(harness, "Preview");
    check_step(preview.front().get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW);
    const auto direct = click_text(harness, "Export");
    check_step(direct.front().get().compose_action == WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP);
    check_false_step(direct.front().get().compose_export_overwrite);
    const auto dialog = click_text(harness, "Export…");
    check_step(std::string_view{dialog.front().get().menu_command_id} == "document.export_output_group");
}


TEST_CASE("BMS walkthrough exports explicit extensions independently of new chart layouts",
          "[frontend-state][event-harness][walkthrough-bms]") {
    const auto* layout = GENERATE("bme-7k-outside-scratch", "pms-standard-9button");
    const auto* path = GENERATE("song/normal.bme", "song/normal.bms", "song/normal.pms", "song/normal.osu", "song");
    auto document = make_initial_document(song_id_t{1}, layout);
    require_step(document);
    auto group = document->output_groups.front();
    group.relative_output_path = path;
    const auto directory = std::filesystem::temp_directory_path() / "wormedit-bms-extension-walkthrough";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto plan = output_group_export_plan_t::from(*document, group);
    check_step(plan.request.target_profile.empty());
    const auto converted = convert_document(*document, plan.request);
    const auto exported = run_output_group_export(
        {.document = *document, .plan = plan, .asset_base = directory}, {});
    INFO(exported.message);
    if (group.relative_output_path.extension() == ".osu") {
        check_false_step(exported.published);
        check_false_step(std::filesystem::exists(directory / "song"));
    } else {
        require_step(converted);
        require_step(exported.published);
        const auto output = group.relative_output_path.has_extension()
            ? directory / group.relative_output_path
            : directory / "song" / converted.outputs.front().suggested_filename;
        check_step(std::filesystem::is_regular_file(output));
        const auto expected = std::string_view{layout}.starts_with("pms") ? ".pms" : ".bme";
        if (!group.relative_output_path.has_extension()) check_step(output.extension() == expected);
        const auto bytes = read_file(output);
        const auto parsed = parse_bms_bytes(std::as_bytes(std::span{bytes}),
            {.file_extension = expected, .lane_profile_id = layout});
        require_step(parsed.value.song);
        check_step(parsed.value.song->charts.front().lane_layouts.front().layout.profile_id == layout);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE("BMS walkthrough packages declared sounds and images and names missing sources",
          "[frontend-state][event-harness][walkthrough-bms]") {
    const auto family = GENERATE(format_family_t::bms, format_family_t::bmson);
    walkthrough_t flow;
    (void)import_review(flow);
    const auto image = std::filesystem::path{WORMEDIT_SOURCE_ROOT} / "docs/manual/images/start-screen.png";
    flow.history.document.visual_assets.push_back({.id = asset_id_t{500}, .source_path = image});
    flow.history.document.resources.push_back({.id = resource_id_t{501}, .name = "background.png",
        .asset = asset_id_t{500}, .roles = {resource_role_t::bga}});
    auto group = flow.history.document.output_groups.front();
    group.family = family;
    group.relative_output_path = "song";
    const auto root = std::filesystem::temp_directory_path() / "wormedit-bms-packaging-walkthrough";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    output_group_export_request_t request{.document = flow.history.document,
        .plan = output_group_export_plan_t::from(flow.history.document, group), .asset_base = root};
    const auto exported = run_output_group_export(request, {});
    INFO(exported.message);
    require_step(exported.published);
    for (const auto& asset : request.document.audio_assets) {
        const project_asset_copy_request_t copy{.source_path = asset.source_path, .asset = asset.id};
        check_step(read_file(root / "song/assets" / copy.destination_filename()) == read_file(asset.source_path));
    }
    const project_asset_copy_request_t image_copy{.source_path = image, .asset = asset_id_t{500}};
    check_step(read_file(root / "song/assets" / image_copy.destination_filename()) == read_file(image));
    std::filesystem::remove_all(root / "song");
    request.document.visual_assets.front().source_path = "/missing/walkthrough-background.png";
    const auto missing = run_output_group_export(request, {});
    check_false_step(missing.published);
    check_step(missing.message.find("walkthrough-background.png") != std::string::npos);
    check_false_step(std::filesystem::exists(root / "song"));
    std::filesystem::remove_all(root);
}
