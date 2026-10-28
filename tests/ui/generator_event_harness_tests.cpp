#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include "sys/compose/actions/compose_actions.h"
#include "sys/compose/generator_runtime.h"
#include "sys/project/workspace_view.h"
#include "../support/temporary_directory.h"
#include <wormedit/core/lane_profiles.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <source_location>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
void check_generator_ui(bool value, std::source_location at = std::source_location::current()) {
    INFO("Generator UI check at " << at.line()); CHECK(value);
}
void require_generator_ui(bool value, std::source_location at = std::source_location::current()) {
    INFO("Generator UI requirement at " << at.line()); REQUIRE(value);
}
auto generator_ui_history() -> document_history_t {
    auto profile = make_bms_7k_profile();
    chart_document_t chart{.id = chart_id_t{1}, .measures = {{.id = measure_id_t{1}}}};
    chart.lane_layouts.push_back({.id = object_id_t{2}, .position = {measure_id_t{1}, 0}, .layout = profile.layout});
    chart.input_profile = profile.input;
    chart.bpms.push_back({.id = object_id_t{3}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}});
    return document_history_t::from({.id = song_id_t{1}, .charts = {std::move(chart)}});
}
struct generator_ui_fixture_t {
    task_scheduler_t scheduler{1, 4};
    notification_sink_t sink{notification_sink_t::muted()};
    runtime::resource_copy_t copy{scheduler, sink};
    runtime::output_group_export_t exporter{sink};
    runtime::metadata_asset_t metadata{sink};
    metadata_drop_state_t drop;
    document_history_t history{generator_ui_history()};
    workspace_controller_t workspace{*workspace_controller_t::from(history).value};
    application_command_state_t commands;
    std::optional<editor_interaction_state_t> interaction;
    compose_scene_cache_t scene;
    frontend_workspace_view_t frontend;
    std::filesystem::path base;
    nuklear_event_harness_t harness{1600, 1200};
    bool generator_open{};
    generator_ui_fixture_t() {
        require_generator_ui(harness.ready());
        workspace.active_tab = workspace_tab_t::compose; workspace.selected_chart = chart_id_t{1};
        auto& layout = worm_ui_nuklear_compose_state().dock_layout.layout;
        layout.show(dock_panel_t::generator, true);
        layout.show(dock_panel_t::sounds, false); layout.show(dock_panel_t::layers, false);
        project();
    }
    void project() {
        frontend.reset(); frontend.view.compose.visible = true;
        populate_compose_view(frontend, history, workspace, scene, 1600, 1200, interaction, commands, nullptr);
        harness.view().compose = frontend.view.compose;
        (void)harness.frame();
    }
    void reveal(std::string_view label) {
        if (harness.drawn_text_center(label)) return;
        // The Generator dock is the lower pane, below the Inspector.
        require_generator_ui(test::scroll_to_text(harness, label, 1500, 1000, -1, 120) ||
                             test::scroll_to_text(harness, label, 1500, 1000, 1, 120));
    }
    void open() {
        if (generator_open) return;
        // The generator has its own dock, so its sections are open as soon as the dock shows.
        reveal("Lua note generator");
        require_generator_ui(harness.drawn_text_center("Lua note generator").has_value());
        generator_open = true;
    }
    void type_path() {
        open();
        reveal("Choose script…");
        const auto label = harness.drawn_text_center("Script file (inside project)");
        require_generator_ui(label.has_value());
        // The caption row is followed by the path field.
        (void)test::click_at(harness, label->first, label->second + 24);
        const char* path = "pattern.lua";
        SDL_Event text{}; text.type = text.text.type = SDL_EVENT_TEXT_INPUT; text.text.text = path;
        (void)harness.frame(std::span{&text, 1});
    }
    auto click(std::string_view label, WormUiComposeActionKind kind) -> workspace_action_snapshot_t {
        reveal(label);
        const auto center = harness.drawn_text_center(label);
        require_generator_ui(center.has_value());
        const auto frames = test::click_at(harness, center->first, center->second);
        auto result = workspace_action_snapshot_t{ui::workspace_action_t{}};
        for (const auto& frame : frames)
            if (frame.get().compose_action == kind) result = frame;
        require_generator_ui(result.get().compose_action == kind);
        return result;
    }
    void dispatch(const workspace_action_snapshot_t& action) {
        require_generator_ui(interaction.has_value());
        const compose_action_inputs_t environment{{history, workspace, sink}, {commands, interaction},
            compose_resource_inputs_t{base, {scheduler, copy, exporter}, {drop, metadata}}};
        (void)dispatch_compose_view_action(action.get(), environment, *interaction, 1200, false, false);
    }
    void run() {
        type_path();
        const auto run = click("Run", WORM_UI_COMPOSE_ACTION_GENERATOR_RUN);
        check_generator_ui(std::string_view{run.get().compose_generator_form.path} == "pattern.lua");
        dispatch(run);
        require_generator_ui(commands.project.generator.runtime != nullptr);
        commands.project.generator.runtime->wait(history, chart_id_t{1});
        project();
    }
};
void write_generator(const std::filesystem::path& base) {
    std::ofstream output{base / "pattern.lua"};
    output << "return {notes={{key='a',measure='1',row=120,lane='101'}}}";
}
}

#if defined(WORMEDIT_HAS_LUA)
TEST_CASE("real generator inspector Run previews ghosts and Apply commits one Undo", "[frontend-state][event-harness][generator]") {
    auto directory = test::temporary_directory_t::from(); require_generator_ui(directory.has_value());
    write_generator(directory->path());
    generator_ui_fixture_t fixture; fixture.base = directory->path(); fixture.run();
    check_generator_ui(fixture.history.undo.empty());
    check_generator_ui(fixture.history.document.charts.front().measures.front().notes.empty());
    check_generator_ui(!fixture.harness.view().compose.generator_ghosts.empty());
    const auto apply = fixture.click("Apply", WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY);
    fixture.dispatch(apply); fixture.project();
    check_generator_ui(fixture.history.undo.size() == 1 && fixture.history.document.generator_recipes.size() == 1);
    check_generator_ui(fixture.history.document.charts.front().measures.front().notes.size() == 1);
    require_generator_ui(undo_document_command(fixture.history)); fixture.project();
    check_generator_ui(fixture.history.document.charts.front().measures.front().notes.empty());
    check_generator_ui(fixture.history.document.generator_recipes.empty());
}
TEST_CASE("real generator inspector previews and applies SV output", "[frontend-state][event-harness][generator][sv]") {
    auto directory = test::temporary_directory_t::from(); require_generator_ui(directory.has_value());
    {
        std::ofstream output{directory->path() / "pattern.lua"};
        output << "return {sv_points={sv_point{key='base',at=at_measure('1',0),value=2}},"
                  "sv_spans={sv_span{key='rise',from=at_measure('1',0),to=at_measure('1',480),value_from=1,value_to=2}}}";
    }
    generator_ui_fixture_t fixture; fixture.base = directory->path(); fixture.run();
    check_generator_ui(fixture.history.undo.empty());
    check_generator_ui(fixture.harness.view().compose.generator.sv.added == 2);
    fixture.dispatch(fixture.click("Apply", WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY)); fixture.project();
    check_generator_ui(fixture.history.undo.size() == 1);
    check_generator_ui(fixture.history.document.charts.front().scrolls.size() == 1);
    check_generator_ui(fixture.history.document.charts.front().sv_recipes.size() == 1);
}
TEST_CASE("real generator inspector Cancel discards ghosts and stale Apply cannot publish", "[frontend-state][event-harness][generator]") {
    auto directory = test::temporary_directory_t::from(); require_generator_ui(directory.has_value());
    write_generator(directory->path());
    generator_ui_fixture_t fixture; fixture.base = directory->path(); fixture.run();
    const auto apply = fixture.click("Apply", WORM_UI_COMPOSE_ACTION_GENERATOR_APPLY);
    fixture.dispatch(fixture.click("Cancel", WORM_UI_COMPOSE_ACTION_GENERATOR_CANCEL)); fixture.project();
    check_generator_ui(fixture.harness.view().compose.generator_ghosts.empty());
    ++fixture.history.document.revision;
    fixture.dispatch(apply);
    check_generator_ui(fixture.history.undo.empty());
    check_generator_ui(fixture.history.document.charts.front().measures.front().notes.empty());
}
#endif
TEST_CASE("same identity project session discards generator drafts and keeps action snapshots owned", "[frontend-state][event-harness][generator]") {
    generator_ui_fixture_t fixture; fixture.type_path();
    const auto original = fixture.click("Run", WORM_UI_COMPOSE_ACTION_GENERATOR_RUN);
    check_generator_ui(std::string_view{original.get().compose_generator_form.path} == "pattern.lua");
    worm_ui_frontend_begin_project_session(); (void)fixture.harness.frame();
    const auto reset = fixture.click("Run", WORM_UI_COMPOSE_ACTION_GENERATOR_RUN);
    check_generator_ui(std::string_view{reset.get().compose_generator_form.path}.empty());
    check_generator_ui(std::string_view{original.get().compose_generator_form.path} == "pattern.lua");
}
