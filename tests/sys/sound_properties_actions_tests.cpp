#include "sys/compose/actions/compose_sound_actions.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <algorithm>

namespace {
using namespace wormedit;
void check_properties(bool condition) { CHECK(condition); }
struct properties_fixture_t {
    document_history_t history;
    workspace_controller_t workspace;
    wormedit::ui::workspace_action_t action{};
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
        history = document_history_t::from(std::move(song));
        const auto bound = workspace_controller_t::from(history);
        REQUIRE(bound.value);
        workspace = *bound.value;
        workspace.active_tab = workspace_tab_t::audio_slices;
        workspace.selected_chart = chart_id_t{2};
        action.sound_properties_apply = true;
        action.sound_properties_project_id = song_id_t{1};
        action.sound_properties_chart_id = chart_id_t{2};
        action.sound_properties_resource_id = resource_id_t{4};
        action.sound_properties_document_revision = history.document.revision;
        action.sound_properties_kind = sound_kind_t::sample;
        action.sound_properties_name = " New voice ";
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
    SECTION("project") { fixture.action.sound_properties_project_id = song_id_t{8}; }
    SECTION("chart") { fixture.action.sound_properties_chart_id = chart_id_t{8}; }
    SECTION("revision") { ++fixture.action.sound_properties_document_revision; }
    SECTION("tab") { fixture.workspace.active_tab = workspace_tab_t::compose; }
    SECTION("resource") { fixture.action.sound_properties_resource_id = resource_id_t{8}; }
    SECTION("overlong") { fixture.action.sound_properties_name.assign(257, 'x'); }
    SECTION("empty") { fixture.action.sound_properties_name = "  "; }
    SECTION("invalid kind") { fixture.action.sound_properties_kind = static_cast<sound_kind_t>(255); }
    SECTION("overlong tag") { fixture.action.sound_properties_tags = {std::string(65, 'x')}; }
    SECTION("too many tags") {
        fixture.action.sound_properties_tags = std::vector<std::string>{};
        for (int index = 0; index < 33; ++index)
            fixture.action.sound_properties_tags->push_back(std::to_string(index));
    }
    const auto revision = fixture.history.document.revision;
    fixture.dispatch();
    check_properties(fixture.history.document.revision == revision);
    check_properties(fixture.history.document.resources.front().name == "Voice");
    check_properties(fixture.history.document.resources.front().sound_kind == sound_kind_t::keysound);
    check_properties(!undo_document_command(fixture.history));
}

TEST_CASE("Sounds tag edits add remove and deduplicate through the metadata command",
          "[sys][sound-properties][sound-tags]") {
    properties_fixture_t fixture;
    std::vector<std::string> expected;
    SECTION("add") {
        fixture.action.sound_properties_tags = {"Vocal", "Keep", " Drum "};
        expected = {"Vocal", "Keep", "Drum"};
    }
    SECTION("remove") {
        fixture.action.sound_properties_tags = {"Keep"};
        expected = {"Keep"};
    }
    SECTION("duplicate") {
        fixture.action.sound_properties_tags = {"Vocal", "vocal", "Keep"};
        expected = {"Vocal", "Keep"};
    }
    SECTION("clear") { fixture.action.sound_properties_tags = std::vector<std::string>{}; }
    const auto original = fixture.history.document.resources.front();
    fixture.dispatch();
    const auto& tags = fixture.history.document.resources.front().tags;
    check_properties(tags == expected);
    check_properties(fixture.history.document.resources.front().name == original.name);
    check_properties(fixture.history.document.resources.front().sound_kind == original.sound_kind);
    check_properties(undo_document_command(fixture.history) == (expected != original.tags));
    check_properties(fixture.history.document.resources.front().tags == original.tags);
    check_properties(fixture.history.document.resources.front().name == original.name);
    check_properties(fixture.history.document.resources.front().sound_kind == original.sound_kind);
    check_properties(!undo_document_command(fixture.history));
}

TEST_CASE("Sounds duplicate-only tag additions leave history unchanged",
          "[sys][sound-properties][sound-tags]") {
    properties_fixture_t fixture;
    fixture.action.sound_properties_name = "Voice";
    fixture.action.sound_properties_kind = sound_kind_t::keysound;
    fixture.action.sound_properties_tags = {"Vocal", "Keep", " vocal "};
    const auto revision = fixture.history.document.revision;
    fixture.dispatch();
    CHECK(fixture.history.document.revision == revision);
    CHECK_FALSE(undo_document_command(fixture.history));
}
