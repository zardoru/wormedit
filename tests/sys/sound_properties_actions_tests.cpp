#include "sys/compose_sound_actions.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>

namespace {
using namespace wormedit;
void check_properties(bool condition) { CHECK(condition); }
struct properties_fixture_t {
    document_history_t history;
    workspace_controller_t workspace;
    WormUiWorkspaceAction action{};
    properties_fixture_t() {
        song_document_t song{.id = song_id_t{1}, .charts = {{.id = chart_id_t{2},
            .measures = {{.id = measure_id_t{1}}},
            .lane_layouts = {{.id = object_id_t{10}, .position = {measure_id_t{1}, 0},
                .layout = {.profile_id = "test", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
            .bpms = {{.id = object_id_t{11}, .position = {measure_id_t{1}, 0},
                      .beats_per_minute = {120, 1}}}}}};
        song.audio_assets = {{.id = asset_id_t{3}, .source_path = "voice.wav"}};
        song.resources = {{.id = resource_id_t{4}, .name = "Voice", .asset = asset_id_t{3},
            .roles = {resource_role_t::sample}, .sound_kind = sound_kind_t::keysound,
            .tags = {"Vocal", "Keep"}}};
        history = make_document_history(std::move(song));
        const auto bound = make_workspace_controller(history);
        REQUIRE(bound.workspace);
        workspace = *bound.workspace;
        workspace.active_tab = workspace_tab_t::audio_slices;
        workspace.selected_chart = chart_id_t{2};
        action.sound_properties_apply = true;
        action.sound_properties_project_id = 1;
        action.sound_properties_chart_id = 2;
        action.sound_properties_resource_id = 4;
        action.sound_properties_document_revision = history.document.revision;
        action.sound_properties_kind = static_cast<std::uint8_t>(sound_kind_t::sample);
        std::strcpy(action.sound_properties_name, " New voice ");
    }
    void dispatch() { REQUIRE(sys::dispatch_sound_properties_action(action, history, workspace)); }
};
}

TEST_CASE("Sounds property Apply preserves tags and asset bindings in one undo step",
          "[sys][sound-properties]") {
    properties_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    fixture.dispatch();
    const auto& resource = fixture.history.document.resources.front();
    check_properties(resource.name == "New voice");
    check_properties(resource.sound_kind == sound_kind_t::sample);
    check_properties(resource.tags == std::vector<std::string>{"Vocal", "Keep"});
    check_properties(resource.asset == asset_id_t{3});
    check_properties(fixture.history.document.revision == revision + 1);
    REQUIRE(undo_document_command(fixture.history));
    check_properties(fixture.history.document.resources.front().name == "Voice");
    check_properties(fixture.history.document.resources.front().sound_kind == sound_kind_t::keysound);
}

TEST_CASE("Sounds property drafts reject stale target and malformed names atomically",
          "[sys][sound-properties]") {
    properties_fixture_t fixture;
    SECTION("project") { fixture.action.sound_properties_project_id = 8; }
    SECTION("chart") { fixture.action.sound_properties_chart_id = 8; }
    SECTION("revision") { ++fixture.action.sound_properties_document_revision; }
    SECTION("tab") { fixture.workspace.active_tab = workspace_tab_t::compose; }
    SECTION("resource") { fixture.action.sound_properties_resource_id = 8; }
    SECTION("unterminated") { std::memset(fixture.action.sound_properties_name, 'x', 257); }
    SECTION("empty") { std::strcpy(fixture.action.sound_properties_name, "  "); }
    SECTION("invalid kind") { fixture.action.sound_properties_kind = 255; }
    const auto revision = fixture.history.document.revision;
    fixture.dispatch();
    check_properties(fixture.history.document.revision == revision);
    check_properties(fixture.history.document.resources.front().name == "Voice");
    check_properties(fixture.history.document.resources.front().sound_kind == sound_kind_t::keysound);
    check_properties(!undo_document_command(fixture.history));
}
