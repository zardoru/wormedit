#include "nuklear_event_harness.h"
#include "sys/application_defaults.h"
#include "sys/compose_input.h"
#include "sys/compose_sound_actions.h"
#include "sys/sound_import_staging.h"
#include "ui/nuklear/sound_import_runtime.h"
#include <wormedit/core/chart_editor.h>
#include <wormedit/formats/stepmania.h>
#include "sys/output_group_export.h"
#include "ui/nuklear/compose_state.h"
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/bgm_columns.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
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
    INFO("Finding " << text << "\n" << harness.drawn_text());
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

    explicit walkthrough_t(song_document_t document = new_bme())
        : history(document_history_t::from(std::move(document))) {
        require_step(harness.ready());
        require_step(workspace.value);
        workspace.value->active_tab = workspace_tab_t::compose;
        project();
        (void)harness.frame();
    }
    void project() {
        require_step(rebind_workspace(*workspace.value, history.document).value);
        const auto source = workspace_view_t::from(*workspace.value, history.document);
        require_step(source.value);
        WormUiKeysoundDockRequest dock{};
        worm_ui_frontend_keysound_dock_request(&dock);
        flattened = frontend_workspace_view_t::from(*source.value, history.document, "Ready", {}, false, nullptr, dock_sort_t::natural_name, dock);
        flattened.view.compose.visible = true;
        populate_compose_view(flattened, history, *workspace.value, cache, 1600, 1000,
                              interaction, application, nullptr);
        require_step(interaction);
        require_step(flattened.view.compose.scene_available);
        auto& view = harness.view();
        view.project_id = history.document.id.value;
        view.selected_chart_id = workspace.value->selected_chart->value;
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

void check_bms_metadata(walkthrough_t& flow) {
    const auto model = workspace_view_t::from(*flow.workspace.value, flow.history.document,
                                             workspace_tab_t::metadata);
    require_step(model.value);
    auto metadata = frontend_workspace_view_t::from(*model.value, flow.history.document, "", {}, false);
    check_step(metadata.view.metadata.has_chart);
    check_step(metadata.view.metadata.selected_chart_source_family ==
               static_cast<std::uint8_t>(format_family_t::bms));
    const auto targets = std::span{metadata.view.metadata.format_targets,
                                   metadata.view.metadata.format_target_count};
    check_step(std::ranges::any_of(targets, [](const auto& target) {
        return target.chart_scoped && target.family == static_cast<std::uint8_t>(format_family_t::bms);
    }));
}

void check_imported_slots(walkthrough_t& flow, resource_id_t sound) {
    const auto& identities = flow.history.document.resources.front().target_identities;
    require_step(identities.size() == 1);
    check_step(identities.front().family == format_family_t::bms);
    check_step(identities.front().namespace_name == "WAV");
    check_step(identities.front().identifier == "01");
    auto& ui = worm_ui_nuklear_compose_state();
    ui.dock_layout.sounds_panel_visible = true;
    (void)flow.harness.frame();
    check_step(flow.harness.view().compose.keysound_ids_available);
    (void)click_text(flow.harness, "BMS slots");
    flow.project();
    (void)flow.harness.frame();
    const auto& view = flow.harness.view().compose;
    check_step(view.sound_slots_mode);
    require_step(view.sound_slot_count > 0);
    check_step(view.sound_slots[0].resource_id == sound.value);
    (void)click_text(flow.harness, "Automatic IDs");
    flow.project();
    (void)flow.harness.frame();
}

void add_bgm_column(walkthrough_t& flow) {
    check_step(flow.harness.view().compose.bgm_column_count_editable);
    // The toolbar is drawn before the inspector's separate preset-grid "+" button.
    const auto bounds = flow.harness.drawn_text_bounds();
    const auto plus = std::ranges::find_if(bounds, [](const auto& item) {
        return std::string_view{item.text} == "+";
    });
    require_step(plus != bounds.end());
    const auto clicked = test::click_at(flow.harness, plus->x + plus->width / 2,
                                       plus->y + plus->height / 2);
    bool emitted = false;
    for (const auto& snapshot : clicked) {
        if (snapshot.get().compose_action != WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT) continue;
        emitted = true;
        require_step(apply_bgm_column_count(flow.history, flow.interaction->chart,
                                            snapshot.get().compose_bgm_column_count).applied);
    }
    require_step(emitted);
    require_step(flow.history.document.charts.front().bgm_source_lanes.size() == 1);
    require_step(rebind_workspace(*flow.workspace.value, flow.history).value);
    flow.project();
    check_step(flow.harness.view().compose.bgm_lane_count == 1);
    (void)flow.harness.frame();
}

void move_note_and_paste_imported_sound(walkthrough_t& flow, resource_id_t sound) {
    const auto note = flow.history.document.charts.front().measures.front().notes.front().id;
    flow.interaction->selected_notes = {note};
    flow.interaction->selected_objects.clear();
    flow.project();
    check_step(flow.harness.view().compose.move_to_bgm_available);
    require_step(move_editor_selection_to_bgm(flow.history, *flow.interaction).command.applied);
    const auto& measure = flow.history.document.charts.front().measures.front();
    require_step(measure.bgm_events.size() == 1);
    check_step(measure.bgm_events.front().id == note);
    check_step(measure.bgm_events.front().sound == sound);
    flow.application.services.system_clipboard_text = [] {
        return std::string{"iBMSC Clipboard Data\r\n6 0 10000 0 0 0"};
    };
    flow.interaction->cursor = {measure.id, 96};
    std::string status;
    require_step(begin_compose_paste(flow.application, flow.history, *flow.interaction, status));
    require_step(apply_compose_paste(flow.application, flow.history, *flow.interaction, status));
    const auto& pasted = flow.history.document.charts.front().measures.front().notes;
    check_step(std::ranges::any_of(pasted, [&](const auto& entry) {
        return entry.position.row == 96 && entry.sound == sound;
    }));
}
} // namespace

TEST_CASE("BMS walkthrough wizard creates BME and BMSON charts with their source families",
          "[frontend-state][event-harness][walkthrough-bms]") {
    const bool bmson = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 900};
    require_step(harness.ready());
    // Keep the production combo interaction bounded to the default and the chosen catalog entry.
    const std::array<WormUiStringDescriptorView, 2> entries{{
        {.id = "osu-mania-7k", .label = "osu!mania 7K"},
        {.id = bmson ? "bmson-generic-24keys" : "bme-7k-outside-scratch",
         .label = bmson ? "BMSON generic 24K" : "BME 7K (outside scratch)"}}};
    const std::array labels{entries[0].label, entries[1].label};
    harness.view().new_project_layouts = {
        .entries = entries.data(), .count = entries.size(), .labels = labels.data()};
    (void)harness.frame();
    require_step(worm_ui_frontend_request_new_project());
    (void)harness.frame();
    (void)harness.frame();
    (void)click_text(harness, "osu!mania 7K");
    (void)click_text(harness, entries[1].label);
    const auto created = click_text(harness, "Create project");
    require_step(created.front().get().kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT);
    const auto document = make_initial_document(song_id_t{1}, created.front().get().new_project_layout_id);
    require_step(document);
    require_step(document->charts.size() == 1);
    check_step(document->charts.front().input_profile.bindings.size() == (bmson ? 0 : 8));
    const auto family = bmson ? format_family_t::bmson : format_family_t::bms;
    check_step(document->charts.front().source_family == family);
    check_step(document->charts.front().lane_layouts.front().layout.lanes.size() == (bmson ? 24 : 8));
    require_step(document->output_groups.size() == 1);
    check_step(document->output_groups.front().family == family);
}

TEST_CASE("BMS walkthrough authors imported sounds with slots BGM and iBMSC paste from a new project",
          "[frontend-state][event-harness][walkthrough-bms]") {
    walkthrough_t flow;
    check_bms_metadata(flow);
    check_step(flow.harness.view().compose.bms_measure_labels);
    const auto& initial_view = flow.harness.view().compose;
    check_step(std::ranges::any_of(std::span{initial_view.grid_lines, initial_view.grid_line_count},
        [](const auto& line) { return line.measure_number == 1 && line.chart_row == 0; }));
    const auto canvas = build_virtual_canvas(flow.history.document.charts.front());
    require_step(std::holds_alternative<virtual_canvas_t>(canvas));
    check_step(std::get<virtual_canvas_t>(canvas).measure_count == bms_addressable_measure_count);
    const auto sound = import_review(flow);
    check_imported_slots(flow, sound);
    add_bgm_column(flow);
    // Production Draw state; Alt+2 dispatch is exercised by the existing routing test.
    flow.application.project.compose_gesture.note_paint_mode_active = true;
    flow.project();
    auto& view = flow.harness.view().compose;
    require_step(view.keysound_count == 1);
    check_step(std::string_view{view.keysounds[0].slot} == "01");
    auto& ui = worm_ui_nuklear_compose_state();
    ui.dock_layout.sounds_panel_visible = true;
    (void)flow.harness.frame();
    // The dock clips long names; the slot cell is part of the same sound row.
    const auto armed = click_text(flow.harness, view.keysounds[0].slot, true);
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
    move_note_and_paste_imported_sound(flow, sound);
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
    const auto output = directory / group.relative_output_path /
                        converted.outputs.front().suggested_filename;
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
        .mode = "BME 7K", .output_groups = "BMS",
        .compatible_output_families = std::uint32_t{1} <<
                                      static_cast<std::uint8_t>(format_family_t::bms),
        .length = "2 s", .diagnostic = ""};
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
    (void)harness.frame();
    (void)click_text(harness, "⋯");
    (void)harness.frame();
    (void)click_text(harness, "Edit");
    (void)harness.frame();
    require_step(harness.drawn_text_center("Save to"));
    const auto saved = click_text(harness, "Save");
    check_step(saved.front().get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE);
    check_step(std::string_view{saved.front().get().project_output_group.path} == "song/normal.bme");
    (void)harness.frame();
    (void)click_text(harness, "⋯");
    (void)harness.frame();
    const auto preview = click_text(harness, "Preview");
    check_step(preview.front().get().project_output_group.kind == WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW);
    (void)harness.frame();
    const auto direct = click_text(harness, "Export");
    check_step(direct.front().get().compose_action == WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP);
    check_step(direct.front().get().compose_existing_files == WORM_UI_EXISTING_FILES_SAVED);
    const auto all = click_text(harness, "Export all");
    check_step(all.front().get().compose_action == WORM_UI_COMPOSE_ACTION_EXPORT_ALL_OUTPUT_GROUPS);
}

namespace {
auto slot_import_paths() -> std::array<std::string, 3> {
    const auto audio = std::filesystem::path{WORMEDIT_SOURCE_ROOT} / "tests/fixtures/audio";
    return {(audio / "keysound-c4.ogg").string(), (audio / "keysound-e4.ogg").string(),
            (audio / "keysound-g4.ogg").string()};
}

void refresh_sound_dock(walkthrough_t& flow) {
    // A changed mode/filter first publishes its virtual-list window, then projects it.
    for (int frame = 0; frame < 2; ++frame) {
        flow.project();
        (void)flow.harness.frame();
    }
}

void show_slot_dock(walkthrough_t& flow) {
    auto& ui = worm_ui_nuklear_compose_state();
    ui.dock_layout.sounds_panel_visible = true;
    ui.dock_layout.sounds_preview_on_click = false;
    refresh_sound_dock(flow);
    (void)click_text(flow.harness, "BMS slots");
    refresh_sound_dock(flow);
}

void select_import_slot(walkthrough_t& flow, const char* label) {
    INFO(flow.harness.drawn_text());
    (void)click_text(flow.harness, label);
    refresh_sound_dock(flow);
    WormUiKeysoundDockRequest dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    require_step(dock.selected_slot_explicit);
}

void apply_sound_action(walkthrough_t& flow, const WormUiWorkspaceAction& action) {
    const auto before = flow.history.document.revision;
    const auto result = dispatch_compose_sound_action(action, flow.history, *flow.workspace.value,
        *flow.workspace.value->selected_chart, flow.application);
    require_step(result);
    INFO(*result);
    require_step(flow.history.document.revision != before);
}

void confirm_sound_review(walkthrough_t& flow, bool slots) {
    (void)flow.harness.frame();
    (void)flow.harness.frame();
    const auto kind = slots ? WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH
                            : WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT;
    const auto frames = click_text(flow.harness, slots ? "Assign files" : "Confirm import");
    const auto found = std::ranges::find_if(frames, [kind](const auto& frame) {
        return frame.get().compose_action == kind;
    });
    require_step(found != frames.end());
    apply_sound_action(flow, found->get());
    if (slots) worm_ui_frontend_sound_slot_batch_result(true, "Imported");
    else worm_ui_frontend_sound_import_result(true, "Imported");
    refresh_sound_dock(flow);
}

auto assign_initial_slot(walkthrough_t& flow, std::uint16_t slot) -> resource_id_t {
    const auto path = slot_import_paths().front();
    const std::array paths{path.c_str()};
    const WormUiWorkspaceAction action{.compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT,
        .compose_input_chart_id = flow.workspace.value->selected_chart->value,
        .compose_input_document_revision = flow.history.document.revision,
        .compose_import_paths = paths.data(), .compose_import_path_count = paths.size(),
        .compose_sound_slot = slot};
    apply_sound_action(flow, action);
    const auto rows = sound_slots_view_t::from(flow.history.document,
        {.first_row = static_cast<std::size_t>(slot - 1), .row_limit = 1});
    require_step(rows.rows.size() == 1);
    return rows.rows.front().resource;
}

auto sound_drop_origin(walkthrough_t& flow) -> sound_drop_origin_t {
    WormUiKeysoundDockRequest dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    return {.project_id = flow.history.document.id.value,
        .chart_id = flow.workspace.value->selected_chart->value,
        .revision = flow.history.document.revision, .tab = WORM_UI_WORKSPACE_TAB_COMPOSE,
        .slots_mode = dock.slots_mode, .first_slot = dock.selected_slot,
        .slot_explicit = dock.selected_slot_explicit, .target = resource_drop_target_t::sounds};
}
}

TEST_CASE("BMS walkthrough imports three picked files starting at an explicitly clicked 0A",
          "[frontend-state][event-harness][walkthrough-bms][selected-slot-import]") {
    // The production BMS session factory starts a fresh chart; layout family is T-707.
    walkthrough_t flow{make_initial_document(song_id_t{1})};
    show_slot_dock(flow);
    check_false_step(worm_ui_nuklear_compose_state().keysound_catalog.keysound_selected_slot_explicit);
    select_import_slot(flow, "0A  (empty)");
    const auto picked = flow.harness.click_widget("Choose files for consecutive BMS WAV slots");
    require_step(std::ranges::any_of(picked, [](const auto& frame) {
        return frame.get().sound_import_pick_requested;
    }));
    const auto paths = slot_import_paths();
    const auto staged = stage_sound_import(flow.history.document, *flow.workspace.value, paths);
    require_step(staged.accepted);
    check_step(worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch.first_slot == 10);
    confirm_sound_review(flow, true);
    const auto slots = sound_slots_view_t::from(flow.history.document, {.first_row = 9, .row_limit = 3});
    require_step(slots.rows.size() == 3);
    for (std::size_t index = 0; index < paths.size(); ++index) {
        check_step(static_cast<bool>(slots.rows[index].resource));
        check_step(slots.rows[index].source_path == paths[index]);
    }
    check_step(slots.rows.front().identifier == "0A");
    check_step(slots.rows.back().identifier == "0C");
}

TEST_CASE("BMS walkthrough arming slot 05 does not redirect automatic picked or dropped imports",
          "[frontend-state][event-harness][walkthrough-bms][selected-slot-import]") {
    const auto dropped = GENERATE(false, true);
    const auto file_count = GENERATE(std::size_t{1}, std::size_t{2});
    walkthrough_t flow{make_initial_document(song_id_t{1})};
    const auto armed = assign_initial_slot(flow, 5);
    worm_ui_nuklear_compose_state().dock_layout.sounds_panel_visible = true;
    refresh_sound_dock(flow);
    const auto clicks = click_text(flow.harness, "05", true);
    for (const auto& snapshot : clicks) (void)flow.dispatch(snapshot);
    require_step(worm_ui_nuklear_compose_state().resource_picker.armed_resource_id == armed.value);
    show_slot_dock(flow);
    // Exercise the production armed-sound reveal sync without clicking its slot.
    worm_ui_nuklear_compose_state().keysound_catalog.keysound_reveal_primary = true;
    refresh_sound_dock(flow);
    refresh_sound_dock(flow);
    require_step(sound_drop_origin(flow).first_slot == 5);
    require_step(!worm_ui_nuklear_compose_state().keysound_catalog.keysound_selected_slot_explicit);
    const auto all_paths = slot_import_paths();
    const auto paths = std::span{all_paths}.subspan(1, file_count);
    const sound_drop_batch_t batch{.origin = sound_drop_origin(flow), .paths = {paths.begin(), paths.end()}};
    const auto staged = dropped ? stage_dropped_sound_import(flow.history.document, batch)
        : stage_sound_import(flow.history.document, *flow.workspace.value, paths);
    require_step(staged.accepted);
    check_false_step(worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch.open);
    confirm_sound_review(flow, false);
    const auto slots = sound_slots_view_t::from(flow.history.document, {.row_limit = 5});
    for (std::size_t index = 0; index < file_count; ++index)
        check_step(static_cast<bool>(slots.rows[index].resource));
    check_step(slots.rows[4].resource == armed);
}

TEST_CASE("BMS walkthrough explicitly selected drop ranges retain occupied-slot replacement review",
          "[frontend-state][event-harness][walkthrough-bms][selected-slot-import]") {
    walkthrough_t flow{make_initial_document(song_id_t{1})};
    const auto occupied = assign_initial_slot(flow, 11);
    show_slot_dock(flow);
    select_import_slot(flow, "0A  (empty)");
    const auto paths = slot_import_paths();
    const sound_drop_batch_t batch{.origin = sound_drop_origin(flow), .paths = {paths.begin(), paths.end()}};
    require_step(stage_dropped_sound_import(flow.history.document, batch).accepted);
    auto& review = worm_ui_nuklear_compose_state().keysound_slots.keysound_slot_batch;
    require_step(review.items.size() == 3);
    check_step(review.items[1].occupied_resource_id == occupied.value);
    check_false_step(review.items[1].replace);
    (void)flow.harness.frame();
    (void)flow.harness.frame();
    const auto refused = click_text(flow.harness, "Assign files");
    check_step(std::ranges::none_of(refused, [](const auto& frame) {
        return frame.get().compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH;
    }));
    (void)click_text(flow.harness, "Replace");
    confirm_sound_review(flow, true);
    const auto slots = sound_slots_view_t::from(flow.history.document, {.first_row = 10, .row_limit = 1});
    check_step(slots.rows.front().resource == occupied);
    check_step(slots.rows.front().source_path == paths[1]);
}

TEST_CASE("BMS walkthrough refuses a selected range beyond ZZ before staging and names remaining slots",
          "[frontend-state][event-harness][walkthrough-bms][selected-slot-import]") {
    walkthrough_t flow{make_initial_document(song_id_t{1})};
    show_slot_dock(flow);
    auto& ui = worm_ui_nuklear_compose_state();
    // Filter the virtual catalog to expose its final two rows, then click the real row.
    std::snprintf(ui.keysound_catalog.keysound_slots_search.data(),
                  ui.keysound_catalog.keysound_slots_search.size(), "%s", "ZY");
    refresh_sound_dock(flow);
    select_import_slot(flow, "ZY  (empty)");
    const auto revision = flow.history.document.revision;
    const auto staged = stage_sound_import(flow.history.document, *flow.workspace.value, slot_import_paths());
    check_false_step(staged.accepted);
    check_step(staged.message.find("Remaining WAV slots through ZZ: 2.") != std::string::npos);
    check_false_step(ui.keysound_slots.keysound_slot_batch.open);
    check_step(worm_ui_nuklear_pending_sound_import().phase == worm_ui_sound_import_phase_t::closed);
    check_step(flow.history.document.revision == revision);
}

TEST_CASE("BMS walkthrough clears explicit slot imports on mode chart and project changes",
          "[frontend-state][event-harness][walkthrough-bms][selected-slot-import]") {
    walkthrough_t flow{make_initial_document(song_id_t{1})};
    show_slot_dock(flow);
    select_import_slot(flow, "0A  (empty)");
    (void)click_text(flow.harness, "Automatic IDs");
    refresh_sound_dock(flow);
    check_false_step(sound_drop_origin(flow).slot_explicit);
    (void)click_text(flow.harness, "BMS slots");
    refresh_sound_dock(flow);
    select_import_slot(flow, "0A  (empty)");
    const auto duplicate = apply_duplicate_chart_command(flow.history, *flow.workspace.value->selected_chart);
    require_step(duplicate.command.applied);
    worm_ui_nuklear_compose_state().dock_layout.sounds_panel_visible = false;
    flow.workspace.value->selected_chart = duplicate.payload.chart;
    refresh_sound_dock(flow);
    check_false_step(sound_drop_origin(flow).slot_explicit);
    show_slot_dock(flow);
    select_import_slot(flow, "0A  (empty)");
    worm_ui_frontend_begin_project_session();
    check_false_step(worm_ui_nuklear_compose_state().keysound_catalog.keysound_selected_slot_explicit);
}

TEST_CASE("BMS walkthrough single-file slot drops retain their exact row target",
          "[frontend-state][event-harness][walkthrough-bms][selected-slot-import]") {
    walkthrough_t flow{make_initial_document(song_id_t{1})};
    show_slot_dock(flow);
    select_import_slot(flow, "0A  (empty)");
    const sound_drop_batch_t batch{.origin = sound_drop_origin(flow),
        .paths = {slot_import_paths().front()}};
    require_step(stage_dropped_sound_import(flow.history.document, batch).accepted);
    const auto assigned = flow.harness.frame();
    require_step(assigned.get().compose_action == WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT);
    check_step(assigned.get().compose_sound_slot == 10);
    apply_sound_action(flow, assigned.get());
    const auto slots = sound_slots_view_t::from(flow.history.document, {.first_row = 9, .row_limit = 1});
    check_step(static_cast<bool>(slots.rows.front().resource));
}


TEST_CASE("BMS walkthrough treats dotted and extensionless destinations as folders",
          "[frontend-state][event-harness][walkthrough-bms]") {
    const auto* layout = GENERATE("bme-7k-outside-scratch", "pms-standard-9button");
    const auto* path = GENERATE("song/normal.bme", "song");
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
    require_step(converted);
    require_step(exported.published);
    const auto output = directory / group.relative_output_path /
                        converted.outputs.front().suggested_filename;
    check_step(std::filesystem::is_regular_file(output));
    const auto expected = std::string_view{layout}.starts_with("pms") ? ".pms" : ".bme";
    check_step(output.extension() == expected);
    const auto bytes = read_file(output);
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{bytes}),
        {.file_extension = expected, .lane_profile_id = layout});
    require_step(parsed.value.song);
    check_step(parsed.value.song->charts.front().lane_layouts.front().layout.profile_id == layout);
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
    const auto alternate_directory = root / "alternate";
    std::filesystem::create_directories(alternate_directory);
    const auto original_sound = flow.history.document.audio_assets.front().source_path;
    const auto same_basename_sound = alternate_directory / original_sound.filename();
    std::ofstream(same_basename_sound, std::ios::binary) << "different sound bytes";
    flow.history.document.audio_assets.push_back(
        {.id = asset_id_t{600}, .source_path = same_basename_sound});
    flow.history.document.resources.push_back({.id = resource_id_t{601}, .name = "alternate.wav",
        .asset = asset_id_t{600}});
    output_group_export_request_t request{.document = flow.history.document,
        .plan = output_group_export_plan_t::from(flow.history.document, group), .asset_base = root};
    const auto prepared = output_group_export_preparation_t::from(request);
    require_step(prepared);
    check_step(prepared->pending_assets.size() == 3);
    std::set<std::filesystem::path> preview_paths;
    for (const auto& copy : prepared->pending_assets) {
        check_step(preview_paths.insert(copy.destination_filename()).second);
        const auto mapping = std::ranges::find(prepared->request.staged_assets, copy.asset,
                                               &conversion_request_t::staged_asset_t::asset);
        require_step(mapping != prepared->request.staged_assets.end());
        check_step(mapping->relative_path == std::filesystem::path{"assets"} /
                                                 copy.destination_filename());
    }
    const auto source_name = original_sound.filename();
    check_step(preview_paths.contains(source_name));
    check_step(preview_paths.contains(std::filesystem::path{"600-" + source_name.string()}));
    const auto exported = run_output_group_export(request, {});
    INFO(exported.message);
    require_step(exported.published);
    for (const auto& asset : request.document.audio_assets) {
        const auto copied = root / "song/assets" /
            (asset.id == asset_id_t{600}
                 ? std::filesystem::path{"600-" + asset.source_path.filename().string()}
                 : asset.source_path.filename());
        check_step(read_file(copied) == read_file(asset.source_path));
    }
    check_step(read_file(root / "song/assets" / image.filename()) == read_file(image));
    for (const auto& copy : prepared->pending_assets)
        check_step(std::filesystem::is_regular_file(root / "song/assets" /
                                                    copy.destination_filename()));
    std::filesystem::remove_all(root / "song");
    request.document.visual_assets.front().source_path = "/missing/walkthrough-background.png";
    const auto missing = run_output_group_export(request, {});
    check_false_step(missing.published);
    check_step(missing.message.find("walkthrough-background.png") != std::string::npos);
    check_false_step(std::filesystem::exists(root / "song"));
    std::filesystem::remove_all(root);
}
