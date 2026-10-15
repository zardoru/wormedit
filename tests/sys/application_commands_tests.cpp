#include "../support/stale_action_assertions.h"
#include "../sys/application_dispatch_fixture.h"
#include "compose_area_dispatch_fixture.h"
#include "sys/application_command_dispatch.h"
#include "sys/application_commands.h"
#include "sys/application_timing_stage.h"
#include "sys/audio_offset_actions.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/compose_semantic_layer_commands.h"
#include "sys/compose_sound_actions.h"
#include "sys/native_chart_edit_action.h"
#include "sys/transport_player_actions.h"

#include <wormedit/core/command_registry.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/notifications.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <string>
#include <fstream>
#include <iterator>
#include <span>
#include <filesystem>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>

using namespace wormedit::sys;

#ifndef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Application binding validation accepts current inputs and preserves missing targets",
          "[unit][sys][application-stamp]") {
    const wormedit::document_history_t history{.document = {.id = wormedit::song_id_t{1}, .revision = 7}};
    wormedit::workspace_controller_t workspace{.active_tab = wormedit::workspace_tab_t::compose,
        .selected_chart = wormedit::chart_id_t{2}};
    const wormedit::timing_lane_pseudonote_outcome_t stage{wormedit::timing_lane_pseudonote_proposal_t{.stamp = wormedit::chart_stamp_t::from(history.document, wormedit::chart_id_t{2})}};
    WormUiWorkspaceAction action{};
    action.timing_chart_id = 2;
    action.timing_document_revision = 7;
    action.audio_offset_drag_chart_id = 2;
    action.audio_offset_drag_document_revision = 7;
    action.audio_offset_drag_sample_rate = 48'000;
    CHECK_FALSE(wormedit::sys::validate_timing_stage_binding(action, history, workspace, stage));
    CHECK_FALSE(wormedit::sys::validate_audio_offset_drag_binding(action, history.document, workspace, 48'000));
    workspace.selected_chart.reset();
    const auto timing = wormedit::sys::validate_timing_stage_binding(action, history, workspace, stage);
    REQUIRE(timing.has_value());
    CHECK(*timing == "Timing event staging expired because its chart or revision changed.");
    const auto audio = wormedit::sys::validate_audio_offset_drag_binding(action, history.document, workspace, 48'000);
    REQUIRE(audio.has_value());
    CHECK(*audio == "Audio offset drag expired after its chart or waveform changed.");
}

TEST_CASE("Application timing stage validation keeps captured identity and request stamps",
          "[unit][sys][application-stamp]") {
    const auto source = GENERATE(0, 1, 2);
    wormedit::document_history_t history{.document = {.id = wormedit::song_id_t{1}, .revision = 7}};
    wormedit::workspace_controller_t workspace{.active_tab = wormedit::workspace_tab_t::compose,
        .selected_chart = wormedit::chart_id_t{2}};
    wormedit::timing_lane_pseudonote_outcome_t stage{wormedit::timing_lane_pseudonote_proposal_t{.stamp = wormedit::chart_stamp_t::from(history.document, wormedit::chart_id_t{2}),
        .payload = {.report = {.existing_event = wormedit::object_id_t{3}}}}};
    WormUiWorkspaceAction action{};
    action.timing_chart_id = 2;
    action.timing_object_id = 3;
    action.timing_document_revision = 7;
    if (source == 0) --action.timing_document_revision;
    else if (source == 1) --stage.value->stamp.document.revision;
    else stage.value->stamp.chart = wormedit::chart_id_t{4};
    const auto failure = wormedit::sys::validate_timing_stage_binding(action, history, workspace, stage);
    REQUIRE(failure.has_value());
    wormedit::test::check_stale_action_message(*failure, "Timing event staging");
}

TEST_CASE("Application audio offset validation separates stamp expiry from missing waveform",
          "[unit][sys][application-stamp]") {
    const auto source = GENERATE(0, 1);
    const wormedit::song_document_t document{.id = wormedit::song_id_t{1}, .revision = 7};
    const wormedit::workspace_controller_t workspace{.active_tab = wormedit::workspace_tab_t::metadata,
        .selected_chart = wormedit::chart_id_t{2}};
    WormUiWorkspaceAction action{};
    action.audio_offset_drag_chart_id = source == 0 ? 2 : 3;
    action.audio_offset_drag_document_revision = source == 0 ? 6 : 7;
    action.audio_offset_drag_sample_rate = 48'000;
    const auto failure = wormedit::sys::validate_audio_offset_drag_binding(action, document, workspace, 48'000);
    REQUIRE(failure.has_value());
    wormedit::test::check_stale_action_message(*failure, "Audio offset drag");
    const auto missing = wormedit::sys::validate_audio_offset_drag_binding(action, document, workspace, std::nullopt);
    REQUIRE(missing.has_value());
    CHECK(*missing == "Audio offset drag expired after its chart or waveform changed.");
}

TEST_CASE("Compose navigation cleanup preserves command results", "[unit][sys][commands]") {
    std::string status = "Semantic layer added.";
    // Focus loss can emit cleanup alongside a widget action or on later frames.
    for (int frame = 0; frame < 3; ++frame) {
        status = compose_input_status_with_precedence(status, "Compose navigation canceled.");
        CHECK(status == "Semantic layer added.");
    }
    status = compose_input_status_with_precedence(status, "");
    CHECK(status == "Semantic layer added.");

    status = "Semantic layer was not added.";
    status = compose_input_status_with_precedence(status, "Compose navigation canceled.");
    CHECK(status == "Semantic layer was not added.");

    status = compose_input_status_with_precedence(status, "Compose cursor moved.");
    CHECK(status == "Compose cursor moved.");
    CHECK(compose_input_status_with_precedence("", "Compose navigation canceled.") ==
          "Compose navigation canceled.");
}

TEST_CASE("application command IDs parse into typed commands", "[unit][sys][commands]") {
    CHECK(parse_application_command("workspace.options") ==
          application_command_t{.kind = application_command_kind_t::workspace_options});
    CHECK(parse_application_command("history.undo") ==
          application_command_t{.kind = application_command_kind_t::history_undo});
    CHECK(parse_application_command("workspace.open_compare_validate") ==
          application_command_t{.kind = application_command_kind_t::workspace_open_compare_validate});
    CHECK(parse_application_command("semantic_layer.remove") ==
          application_command_t{.kind = application_command_kind_t::semantic_layer_remove});
    CHECK((parse_application_command("semantic_layer.select_slot_12") ==
           application_command_t{.kind = application_command_kind_t::semantic_layer_select_slot,
                                 .semantic_layer_slot = 12}));
}

TEST_CASE("Dock command IDs parse into typed commands", "[unit][sys][commands][dock-shortcuts]") {
    CHECK(parse_application_command("document.export_output_group") ==
          application_command_t{.kind = application_command_kind_t::document_export_output_group});
    CHECK(parse_application_command("view.open_layers") ==
          application_command_t{.kind = application_command_kind_t::view_open_layers});
    CHECK(parse_application_command("semantic_layer.create") ==
          application_command_t{.kind = application_command_kind_t::semantic_layer_create});
    CHECK(parse_application_command("view.toggle_sounds") ==
          application_command_t{.kind = application_command_kind_t::view_toggle_sounds});
}

TEST_CASE("application command parser rejects unknown IDs and malformed slots",
          "[unit][sys][commands]") {
    CHECK_FALSE(parse_application_command("not.a.command"));
    CHECK_FALSE(parse_application_command("history.undo.extra"));

    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_0"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_-1"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_2x"));
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_1.0"));

    const auto overflow = std::to_string(std::numeric_limits<std::size_t>::max()) + "0";
    CHECK_FALSE(parse_application_command("semantic_layer.select_slot_" + overflow));
}

TEST_CASE("every built-in command ID has a typed representation", "[unit][sys][commands]") {
    const auto registry = wormedit::command_registry_t::from(
        [](const wormedit::command_invocation_context_t&) {
            return wormedit::command_invocation_result_t{};
        });
    for (const auto& command : registry.commands) {
        INFO(command.definition.id.value);
        CHECK(parse_application_command(command.definition.id.value).has_value());
    }
}

TEST_CASE("Command menu has a stable typed application command", "[unit][sys][commands][command-menu]") {
    CHECK(parse_application_command("workspace.command_menu") ==
          application_command_t{.kind = application_command_kind_t::workspace_command_menu});
    CHECK_FALSE(parse_application_command("workspace.command_menu.extra"));
}

#endif

#include "sys/compose_area_actions.h"
#include <algorithm>
#include <array>
#include <wormedit/formats/native_project.h>

namespace {
struct AreaFixture {
    wormedit::document_history_t history;
    wormedit::editor_interaction_state_t state;
    WormUiWorkspaceAction action{};
    AreaFixture() {
        using namespace wormedit;
        chart_document_t chart{
            .id = chart_id_t{7},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}},
            .lane_layouts = {{.id = object_id_t{100},
                              .position = {measure_id_t{1}, 0},
                              .layout = {.profile_id = "area-test",
                                         .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
            .bpms = {{.id = object_id_t{101},
                      .position = {measure_id_t{1}, 0},
                      .beats_per_minute = {120, 1}},
                     {.id = object_id_t{102},
                      .position = {measure_id_t{1}, 480},
                      .beats_per_minute = {150, 1}}}};
        chart.measures.front().notes = {
            {.id = object_id_t{1}, .position = {measure_id_t{1}, 121}, .lane = lane_id_t{1}},
            {.id = object_id_t{2}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{1}},
            {.id = object_id_t{3}, .position = {measure_id_t{1}, 720}, .lane = lane_id_t{1}}};
        history = document_history_t::from({.id = song_id_t{1}, .revision = 9, .charts = {chart}});
        constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}};
        auto setup = editor_interaction_state_t::from(history, chart_id_t{7}, lane_id_t{1},
                                             {measure_id_t{1}, 0}, snaps, 4);
        REQUIRE(setup);
        state = *setup.state;
        state.retained_selection_range =
            position_span_t{{measure_id_t{1}, 121}, {measure_id_t{1}, 480}};
        state.retained_canvas_selection_range = canvas_row_span_t{121, 480};
        state.selected_notes = {object_id_t{1}};
        state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}}};
        action.compose_input_chart_id = 7;
        action.compose_input_document_revision = history.document.revision;
        action.compose_area_begin_row = 121;
        action.compose_area_end_row = 620;
    }
    auto dispatch() -> std::string {
        const auto result = dispatch_compose_area_action(action, history, state);
        REQUIRE(result);
        return *result;
    }
};
} // namespace

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
namespace {
auto sound_fixture_path(std::string_view filename) -> std::string {
    return (std::filesystem::path{__FILE__}.parent_path().parent_path() / "fixtures" /
            "audio" / filename).string();
}

auto sound_import_song() -> wormedit::song_document_t {
    const auto path = std::filesystem::path{__FILE__}.parent_path().parent_path() / "fixtures" / "bmson" /
                      "published-vox-slicing.bmson";
    std::ifstream stream(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream),
                            std::istreambuf_iterator<char>()};
    const auto parsed = wormedit::parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song);
    return *parsed.song;
}

struct SoundImportDispatchFixture {
    wormedit::notification_center_t center;
    wormedit::notification_sink_t sink{center};
    wormedit::document_history_t history{wormedit::document_history_t::from(sound_import_song())};
    wormedit::workspace_controller_t workspace;
    wormedit::sys::application_command_state_t application_state;
    wormedit::chart_id_t chart{history.document.charts.front().id};

    SoundImportDispatchFixture() {
        const auto created = wormedit::workspace_controller_t::from(history);
        REQUIRE(created);
        workspace = *created.workspace;
    }

    auto dispatch(const WormUiSoundImportItem* items, std::size_t item_count,
                  const WormUiSoundImportGroup* groups, std::size_t group_count)
        -> std::optional<std::string> {
        WormUiWorkspaceAction action{};
        action.compose_action = WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT;
        action.sound_import_project_id = history.document.id.value;
        action.compose_input_chart_id = chart.value;
        action.compose_input_document_revision = history.document.revision;
        action.sound_import_items = items;
        action.sound_import_item_count = item_count;
        action.sound_import_groups = groups;
        action.sound_import_group_count = group_count;
        return dispatch_compose_sound_action(action, history, workspace, chart, application_state, sink);
    }
};

void make_sound_import_chart_bms(wormedit::chart_document_t& chart) {
    chart.source_family = wormedit::format_family_t::bms;
    for (auto& lane : chart.bgm_source_lanes) lane.stem_channel.reset();
}

struct SoundSlotBatchDispatchFixture {
    wormedit::document_history_t history{wormedit::document_history_t::from(sound_import_song())};
    wormedit::workspace_controller_t workspace;
    wormedit::sys::application_command_state_t application_state;
    wormedit::chart_id_t chart{history.document.charts.front().id};

    SoundSlotBatchDispatchFixture() {
        using namespace wormedit;
        make_sound_import_chart_bms(history.document.charts.front());
        history.document.resources.front().target_identities.push_back(
            {format_family_t::bms, "05", "WAV"});
        const auto created = workspace_controller_t::from(history);
        REQUIRE(created);
        workspace = *created.workspace;
    }

    auto dispatch(const WormUiSoundImportItem* items, std::size_t count,
                  std::uint16_t slot, bool confirmed = false,
                  std::uint64_t revision = 0) -> std::optional<std::string> {
        WormUiWorkspaceAction action{};
        action.compose_action = WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH;
        action.sound_import_project_id = history.document.id.value;
        action.compose_input_chart_id = chart.value;
        action.compose_input_document_revision = revision == 0 ? history.document.revision : revision;
        action.compose_sound_slot = slot;
        action.compose_sound_slot_confirmed = confirmed;
        action.sound_import_items = items;
        action.sound_import_item_count = count;
        return dispatch_compose_sound_action(action, history, workspace, chart, application_state);
    }
};

void leave_two_fragmented_bms_ids(wormedit::song_document_t& document) {
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    make_sound_import_chart_bms(document.charts.front());
    for (auto& resource : document.resources)
        std::erase_if(resource.target_identities, [](const auto& identity) {
            return identity.family == wormedit::format_family_t::bms &&
                   identity.namespace_name == "WAV";
        });
    auto& identities = document.resources.front().target_identities;
    for (std::size_t value = 1; value < 36 * 36; ++value) {
        if (value == 1 || value == 3) continue;
        identities.push_back({wormedit::format_family_t::bms,
            std::string{digits[value / 36], digits[value % 36]}, "WAV"});
    }
}
} // namespace

namespace {
auto reviewed_sound_import_dispatch_commits_once() -> bool {
    using namespace wormedit;
    SoundImportDispatchFixture fixture;
    const auto existing_group = sound_group_id_t{55};
    fixture.history.document.sound_groups.push_back({.id = existing_group, .name = "Loops"});
    fixture.history.document.resources.front().sound_group = existing_group;
    const auto before_resources = fixture.history.document.resources.size();
    const auto kick_main = sound_fixture_path("keysound-c4.ogg");
    const auto loop_main = sound_fixture_path("keysound-e4.ogg");
    const auto kick_layer = sound_fixture_path("keysound-g4.ogg");
    const std::array items{
        WormUiSoundImportItem{kick_main.c_str(), "Kick Main", 2},
        WormUiSoundImportItem{loop_main.c_str(), "Loop Main", 2},
        WormUiSoundImportItem{kick_layer.c_str(), "Kick Layer", 2}};
    const std::array kick_members{std::size_t{0}, std::size_t{2}};
    const std::array loop_members{std::size_t{1}};
    const std::array groups{
        WormUiSoundImportGroup{"Renamed Kicks", 0, kick_members.data(), kick_members.size()},
        WormUiSoundImportGroup{"Renamed Loops", existing_group.value, loop_members.data(),
                               loop_members.size()}};

    const auto result = fixture.dispatch(items.data(), items.size(), groups.data(), groups.size());
    if (!result || result->find("Added 3") == std::string::npos ||
        fixture.history.document.resources.size() != before_resources + 3 ||
        fixture.history.document.sound_groups.size() != 2) return false;
    if (fixture.history.document.resources[before_resources].name != "Kick Main" ||
        fixture.history.document.resources[before_resources + 1].name != "Loop Main" ||
        fixture.history.document.resources[before_resources + 2].name != "Kick Layer") return false;
    const auto kicks = fixture.history.document.sound_groups.back();
    if (kicks.name != "Renamed Kicks" ||
        fixture.history.document.resources[before_resources].sound_group != kicks.id ||
        fixture.history.document.resources[before_resources + 2].sound_group != kicks.id ||
        fixture.history.document.sound_groups.front().name != "Renamed Loops" ||
        fixture.history.document.resources[before_resources + 1].sound_group != existing_group ||
        fixture.history.document.resources.front().sound_group != existing_group ||
        fixture.history.undo.size() != 1 || !undo_document_command(fixture.history)) return false;
    return fixture.history.document.resources.size() == before_resources &&
           fixture.history.document.sound_groups.size() == 1 &&
           fixture.history.document.sound_groups.front().name == "Loops" &&
           fixture.history.document.resources.front().sound_group == existing_group;
}

auto rejected_sound_imports_leave_history_unchanged() -> bool {
    using namespace wormedit;
    SoundImportDispatchFixture fixture;
    const auto before_resources = fixture.history.document.resources;
    const auto before_groups = fixture.history.document.sound_groups;
    const auto before_assets = fixture.history.document.audio_assets.size();
    const auto before_revision = fixture.history.document.revision;
    const auto one_path = sound_fixture_path("keysound-c4.ogg");
    const auto two_path = sound_fixture_path("keysound-e4.ogg");
    const std::array items{
        WormUiSoundImportItem{one_path.c_str(), "One", 2},
        WormUiSoundImportItem{two_path.c_str(), "Two", 2}};
    const std::array first{std::size_t{0}};
    const std::array second{std::size_t{1}};
    const std::array duplicate_names{
        WormUiSoundImportGroup{"Same", 0, first.data(), first.size()},
        WormUiSoundImportGroup{"same", 0, second.data(), second.size()}};

    const auto rejected = fixture.dispatch(items.data(), items.size(), duplicate_names.data(),
                                           duplicate_names.size());
    if (!rejected || rejected->find("already in use") == std::string::npos ||
        fixture.history.document.revision != before_revision ||
        fixture.history.document.resources.size() != before_resources.size() ||
        fixture.history.document.resources.front().name != before_resources.front().name ||
        fixture.history.document.resources.front().asset != before_resources.front().asset ||
        fixture.history.document.sound_groups != before_groups ||
        fixture.history.document.audio_assets.size() != before_assets ||
        !fixture.history.undo.empty()) return false;

    auto missing_items = items;
    missing_items[1].path = "/tmp/wormedit-missing-reviewed-sound.wav";
    const auto missing = fixture.dispatch(missing_items.data(), missing_items.size(), nullptr, 0);
    return missing && missing->find("no longer readable") != std::string::npos &&
           fixture.history.document.revision == before_revision &&
           fixture.history.document.resources.size() == before_resources.size() &&
           fixture.history.document.sound_groups == before_groups &&
           fixture.history.document.audio_assets.size() == before_assets &&
           fixture.history.undo.empty();
}

auto fragmented_ungrouped_sound_import_reports_ranges() -> bool {
    using namespace wormedit;
    SoundImportDispatchFixture fixture;
    leave_two_fragmented_bms_ids(fixture.history.document);
    const auto first_path = sound_fixture_path("keysound-c4.ogg");
    const auto second_path = sound_fixture_path("keysound-e4.ogg");
    const std::array items{
        WormUiSoundImportItem{first_path.c_str(), "Loose 1", 2},
        WormUiSoundImportItem{second_path.c_str(), "Loose 2", 2}};
    const auto message = fixture.dispatch(items.data(), items.size(), nullptr, 0);
    if (!message || message->find("Group split across 2 ID ranges") == std::string::npos ||
        fixture.history.document.resources.size() < 2) return false;
    const auto& first = fixture.history.document.resources[fixture.history.document.resources.size() - 2];
    const auto& second = fixture.history.document.resources.back();
    return std::ranges::any_of(first.target_identities, [](const auto& identity) {
               return identity.family == format_family_t::bms && identity.namespace_name == "WAV" &&
                      identity.identifier == "01";
           }) &&
           std::ranges::any_of(second.target_identities, [](const auto& identity) {
               return identity.family == format_family_t::bms && identity.namespace_name == "WAV" &&
                      identity.identifier == "03";
           }) && fixture.history.undo.size() == 1;
}

auto sound_slot_batch_dispatch_commits_natural_order_once() -> bool {
    using namespace wormedit;
    SoundSlotBatchDispatchFixture fixture;
    const auto first = sound_fixture_path("keysound-e4.ogg");
    const auto second = sound_fixture_path("keysound-c4.ogg");
    const std::array items{WormUiSoundImportItem{first.c_str(), "Ten", 2},
                           WormUiSoundImportItem{second.c_str(), "Two", 2}};
    const auto result = fixture.dispatch(items.data(), items.size(), 10, true);
    if (!result || result->find("Assigned 2 sounds") == std::string::npos ||
        fixture.history.undo.size() != 1) return false;
    const auto& resources = fixture.history.document.resources;
    const auto ten = std::ranges::find(resources, std::string{"Ten"}, &resource_entry_t::name);
    const auto two = std::ranges::find(resources, std::string{"Two"}, &resource_entry_t::name);
    return ten != resources.end() && two != resources.end() &&
        std::ranges::any_of(ten->target_identities, [](const auto& identity) {
            return identity.family == format_family_t::bms && identity.namespace_name == "WAV" &&
                   identity.identifier == "0B";
        }) &&
        std::ranges::any_of(two->target_identities, [](const auto& identity) {
            return identity.family == format_family_t::bms && identity.namespace_name == "WAV" &&
                   identity.identifier == "0A";
        });
}

auto rejected_sound_slot_batches_leave_history_unchanged() -> bool {
    using namespace wormedit;
    SoundSlotBatchDispatchFixture fixture;
    const auto before_revision = fixture.history.document.revision;
    const auto before_assets = fixture.history.document.audio_assets.size();
    const auto before_resources = fixture.history.document.resources.size();
    const auto one = sound_fixture_path("keysound-c4.ogg");
    const std::array occupied{WormUiSoundImportItem{one.c_str(), "Replace", 2}};
    const auto needs_confirmation = fixture.dispatch(occupied.data(), occupied.size(), 5, false);
    if (!needs_confirmation || needs_confirmation->find("confirmation") == std::string::npos ||
        fixture.history.document.revision != before_revision || !fixture.history.undo.empty())
        return false;
    const auto missing_path = std::string{"/tmp/wormedit-absent-slot-import.wav"};
    const std::array missing{WormUiSoundImportItem{missing_path.c_str(), "Missing", 2}};
    const auto rejected = fixture.dispatch(missing.data(), missing.size(), 6, true);
    return rejected && rejected->find("no longer readable") != std::string::npos &&
        fixture.history.document.revision == before_revision &&
        fixture.history.document.audio_assets.size() == before_assets &&
        fixture.history.document.resources.size() == before_resources && fixture.history.undo.empty();
}

auto prepared_sound_slot_batch_maps_natural_destinations() -> bool {
    using namespace wormedit;
    SoundSlotBatchDispatchFixture fixture;
    const std::array paths{sound_fixture_path("keysound-e4.ogg"),
                           sound_fixture_path("keysound-c4.ogg")};
    const auto prepared = prepare_sound_slot_batch(fixture.history.document, fixture.chart,
                                                    4, paths);
    return prepared.reason.empty() && prepared.items.size() == 2 &&
        std::filesystem::path{prepared.items[0].path}.filename() == "keysound-c4.ogg" &&
        std::filesystem::path{prepared.items[1].path}.filename() == "keysound-e4.ogg" &&
        prepared.items[0].occupied_resource_id == 0 &&
        prepared.items[1].occupied_resource_id == fixture.history.document.resources.front().id.value &&
        prepared.items[1].occupied_name == fixture.history.document.resources.front().name;
}

auto prepared_sound_slot_batch_rejects_all_invalid_inputs() -> bool {
    using namespace wormedit;
    SoundSlotBatchDispatchFixture fixture;
    const auto missing = std::string{"/tmp/wormedit-absent-prepared-slot.wav"};
    const std::array duplicate{missing, missing};
    const auto bad_files = prepare_sound_slot_batch(fixture.history.document, fixture.chart,
                                                     1, duplicate);
    const std::array valid{sound_fixture_path("keysound-c4.ogg"),
                           sound_fixture_path("keysound-e4.ogg")};
    const auto overflow = prepare_sound_slot_batch(fixture.history.document, fixture.chart,
                                                    1295, valid);
    return !bad_files.reason.empty() && bad_files.items.size() == duplicate.size() &&
        bad_files.items[0].path == bad_files.items[1].path &&
        !overflow.reason.empty() && overflow.items.size() == valid.size();
}
} // namespace

namespace {
void section_dispatch_check(bool value) { CHECK(value); }
void section_dispatch_require(bool value) { REQUIRE(value); }

void section_dispatch_preserves_model(std::uint8_t kind, bool reviewed) {
    using namespace wormedit;
    SoundImportDispatchFixture fixture;
    const auto before = fixture.history.document.charts.front();
    const auto resources = fixture.history.document.resources.size();
    const auto path = sound_fixture_path("keysound-c4.ogg");
    const std::array paths{path};
    const auto prepared = prepare_keysound_import(fixture.history.document, fixture.chart, paths, kind);
    section_dispatch_require(prepared.entries.size() == 1);
    section_dispatch_check(static_cast<std::uint8_t>(prepared.entries.front().kind) == kind);
    std::optional<std::string> message;
    if (reviewed) {
        const WormUiSoundImportItem item{path.c_str(), "Section sound", kind, true};
        message = fixture.dispatch(&item, 1, nullptr, 0);
    } else {
        const auto* raw_path = path.c_str();
        WormUiWorkspaceAction action{};
        action.compose_action = WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES;
        action.sound_import_kind = kind;
        action.compose_import_paths = &raw_path;
        action.compose_import_path_count = 1;
        message = dispatch_compose_sound_action(action, fixture.history, fixture.workspace,
            fixture.chart, fixture.application_state, fixture.sink);
    }
    section_dispatch_require(message.has_value());
    section_dispatch_require(fixture.history.document.resources.size() == resources + 1);
    section_dispatch_check(static_cast<std::uint8_t>(fixture.history.document.resources.back().sound_kind) == kind);
    const auto& after = fixture.history.document.charts.front();
    const auto extra = kind == static_cast<std::uint8_t>(sound_kind_t::stem) ? 1U : 0U;
    section_dispatch_require(after.sound_channels.size() == before.sound_channels.size() + extra);
    section_dispatch_require(after.bgm_source_lanes.size() == before.bgm_source_lanes.size() + extra);
    if (extra) {
        section_dispatch_check(after.sound_channels.back().events.empty());
        section_dispatch_check(after.bgm_source_lanes.back().stem_channel == after.sound_channels.back().id);
    }
    for (std::size_t i = 0; i < before.measures.size(); ++i)
        section_dispatch_check(after.measures[i].bgm_events.size() == before.measures[i].bgm_events.size());
    section_dispatch_check(fixture.history.undo.size() == 1);
    section_dispatch_require(static_cast<bool>(undo_document_command(fixture.history)));
    section_dispatch_check(fixture.history.document.resources.size() == resources);
    section_dispatch_check(fixture.history.document.charts.front().sound_channels.size() == before.sound_channels.size());
}
}

TEST_CASE("sound_import section producer and dispatch preserve stem and keysound intent",
          "[unit][sys][sounds][import][sound_import]") {
    section_dispatch_preserves_model(1, false);
    section_dispatch_preserves_model(2, false);
    section_dispatch_preserves_model(1, true);
    section_dispatch_preserves_model(2, true);
}

TEST_CASE("sound_import fixed section confirmation rejects stale revision atomically",
          "[unit][sys][sounds][import][sound_import]") {
    SoundImportDispatchFixture fixture;
    const auto path = sound_fixture_path("keysound-c4.ogg");
    const WormUiSoundImportItem item{path.c_str(), "Section stem", 1, true};
    WormUiWorkspaceAction action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT;
    action.sound_import_project_id = fixture.history.document.id.value;
    action.compose_input_chart_id = fixture.chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.sound_import_items = &item;
    action.sound_import_item_count = 1;
    action.sound_import_kind = 1;
    ++fixture.history.document.revision;
    const auto resources = fixture.history.document.resources.size();
    const auto channels = fixture.history.document.charts.front().sound_channels.size();
    const auto result = dispatch_compose_sound_action(action, fixture.history, fixture.workspace,
        fixture.chart, fixture.application_state, fixture.sink);
    section_dispatch_require(result.has_value());
    section_dispatch_check(result->find("changed") != std::string::npos);
    section_dispatch_check(fixture.history.document.resources.size() == resources);
    section_dispatch_check(fixture.history.document.charts.front().sound_channels.size() == channels);
    section_dispatch_check(fixture.history.undo.empty());
}

TEST_CASE("confirmed sound import dispatches reviewed names and memberships in one undo step",
          "[unit][sys][sounds][import]") {
    CHECK(reviewed_sound_import_dispatch_commits_once());
}

TEST_CASE("confirmed sound import dispatch rejects invalid proposals without mutation",
          "[unit][sys][sounds][import]") {
    CHECK(rejected_sound_imports_leave_history_unchanged());
}

namespace {
auto sound_import_notifications_preserve_outcomes() -> bool {
    using namespace wormedit;
    SoundImportDispatchFixture fixture;
    const auto path = sound_fixture_path("keysound-c4.ogg");
    const WormUiSoundImportItem item{path.c_str(), "Imported", 2};
    const auto accepted = fixture.dispatch(&item, 1, nullptr, 0);
    if (!accepted || fixture.history.undo.size() != 1) return false;
    fixture.center.drain_inbox(notification_time_t{0});
    const auto success = fixture.center.snapshot(notification_capacity);
    if (success.size() != 1 || success[0].severity != notification_severity_t::success ||
        success[0].source != notification_source_t::import_ || success[0].message != *accepted) return false;
    const auto revision = fixture.history.document.revision;
    const auto rejected = fixture.dispatch(nullptr, 1, nullptr, 0);
    if (!rejected || fixture.history.document.revision != revision || fixture.history.undo.size() != 1) return false;
    fixture.center.drain_inbox(notification_time_t{0});
    const auto failure = fixture.center.snapshot(notification_capacity);
    return failure.size() == 2 && failure[0].severity == notification_severity_t::warning &&
        failure[0].source == notification_source_t::import_ && failure[0].message == *rejected;
}
}

TEST_CASE("sound import producer publishes exact accepted and rejected outcomes once", "[unit][sys][sounds][import][notification]") {
    CHECK(sound_import_notifications_preserve_outcomes());
}

TEST_CASE("confirmed ungrouped BMS import reports fragmented WAV ranges",
          "[unit][sys][sounds][import][bms-identifiers]") {
    CHECK(fragmented_ungrouped_sound_import_reports_ranges());
}

TEST_CASE("confirmed direct WAV slot batch dispatches natural order atomically",
          "[unit][sys][sounds][bms-identifiers]") {
    CHECK(sound_slot_batch_dispatch_commits_natural_order_once());
}

TEST_CASE("Direct sound slot actions reject stale chart stamps before payload preparation",
          "[unit][sys][sounds][slot-stamp]") {
    const auto kind = GENERATE(WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT,
                              WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH);
    SoundSlotBatchDispatchFixture fixture;
    WormUiWorkspaceAction action{};
    action.compose_action = kind;
    action.compose_input_chart_id = fixture.chart.value;
    action.compose_input_document_revision = fixture.history.document.revision + 1;
    const auto response = dispatch_compose_sound_action(action, fixture.history, fixture.workspace,
        fixture.chart, fixture.application_state);
    REQUIRE(response.has_value());
    wormedit::test::check_stale_action_message(*response, "Sound slot assignment");
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("direct WAV slot batch rejection preserves history",
          "[unit][sys][sounds][bms-identifiers]") {
    CHECK(rejected_sound_slot_batches_leave_history_unchanged());
}

TEST_CASE("slot batch preparation keeps natural destination occupant mapping",
          "[unit][sys][sounds][bms-identifiers]") {
    CHECK(prepared_sound_slot_batch_maps_natural_destinations());
}

TEST_CASE("slot batch preparation rejects the complete invalid or overflowing batch",
          "[unit][sys][sounds][bms-identifiers]") {
    CHECK(prepared_sound_slot_batch_rejects_all_invalid_inputs());
}

namespace {
struct TimingInsertFixture {
    AreaFixture fixture;
    wormedit::workspace_controller_t workspace;
    std::optional<wormedit::editor_interaction_state_t> compose_state;
    std::optional<wormedit::timing_lane_pseudonote_outcome_t> preview;
    application_command_state_t application_state;
    wormedit::notification_center_t center;
    wormedit::notification_sink_t sink{center};
    wormedit::local_position_t cursor{wormedit::measure_id_t{1}, 720};

    TimingInsertFixture() {
        using namespace wormedit;
        auto& chart = fixture.history.document.charts.front();
        chart.scrolls.push_back({.id = object_id_t{103},
                                 .position = {measure_id_t{1}, 600},
                                 .multiplier = {3, 2}});
        const auto moved = dispatch_editor_action(
            fixture.history, fixture.state,
            jump_edit_cursor_action_t{cursor, fixture.history.document.revision});
        REQUIRE(moved.accepted);
        const auto workspace_result = workspace_controller_t::from(fixture.history);
        REQUIRE(workspace_result);
        workspace = *workspace_result.workspace;
        workspace.active_tab = workspace_tab_t::compose;
        workspace.selected_chart = chart.id;
        compose_state = fixture.state;
    }

    auto invoke(application_command_kind_t kind) {
        return invoke_application_command({.kind = kind}, fixture.history, workspace,
                                          compose_state, preview, application_state, sink);
    }

    auto latest_message() -> std::string {
        center.drain_inbox(wormedit::notification_time_t{0});
        const auto entries = center.snapshot(1);
        return entries.empty() ? std::string{} : entries.front().message;
    }
};

void check_paint_cell(bool downscroll, bool snap) {
    using namespace wormedit;
    TimingInsertFixture fixture;
    auto& state = *fixture.compose_state;
    state.snap_enabled = snap;
    state.viewport_height_pixels = 600;
    state.viewport_scroll_pixels = 0;
    state.pixels_per_standard_measure = 960;
    const auto& chart = fixture.fixture.history.document.charts.front();
    const auto inspected = inspect_editor_interaction(fixture.fixture.history, state);
    const auto& view = std::get<editor_interaction_view_model_t>(inspected);
    const auto built = build_editor_hit_test_index(chart, fixture.fixture.history.document.revision);
    const auto& index = std::get<editor_hit_test_index_t>(built);
    const auto target = resolve_compose_paint_cell(chart, index, view, 50,
        downscroll ? 263 : 337, 42, 48, downscroll, edit_snap_denominator(chart, state));
    REQUIRE(target);
    CHECK(target->position.row == (snap ? 240 : 337));
    state.active_lane = target->lane;
    const auto placed = dispatch_editor_action(fixture.fixture.history, state,
        place_tap_action_t{target->position});
    REQUIRE(placed.document_changed);
    const auto& notes = fixture.fixture.history.document.charts.front().measures.front().notes;
    CHECK(std::ranges::any_of(notes, [&](const auto& note) { return note.position == target->position; }));
}

TEST_CASE("Compose paint ghost cell equals placement with snap and either scroll direction",
          "[sys][application_commands][paint-ghost]") {
    check_paint_cell(false, false);
    check_paint_cell(false, true);
    check_paint_cell(true, false);
    check_paint_cell(true, true);
}

TEST_CASE("Note kind shortcut activates paint and keeps it active when repeated", "[sys][application_commands][palette-repro]") {
    TimingInsertFixture fixture;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::edit_note_kind, .note_kind = 1},
        fixture.fixture.history, fixture.workspace, fixture.compose_state, fixture.preview,
        fixture.application_state, fixture.sink);
    REQUIRE(result.accepted);
    CHECK(fixture.compose_state->armed_note_kind == wormedit::note_kind_t::hold);
    CHECK(fixture.application_state.project.compose_gesture.note_paint_mode_active);
    CHECK(invoke_application_command(
        {.kind = application_command_kind_t::edit_note_kind, .note_kind = 1},
        fixture.fixture.history, fixture.workspace, fixture.compose_state, fixture.preview,
        fixture.application_state, fixture.sink).accepted);
    CHECK(fixture.application_state.project.compose_gesture.note_paint_mode_active);
}

TEST_CASE("Timing Settings command toggles without changing chart cursor selection or tool", "[sys][timing-settings]") {
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;
    const auto cursor = fixture.compose_state->cursor;
    const auto tool = fixture.application_state.project.compose_gesture.note_paint_mode_active;
    const auto selection = fixture.compose_state->selected_objects;
    CHECK(fixture.invoke(application_command_kind_t::timing_open_settings).accepted);
    CHECK(fixture.application_state.preferences.timing_settings_visible);
    CHECK(fixture.invoke(application_command_kind_t::timing_open_settings).accepted);
    CHECK_FALSE(fixture.application_state.preferences.timing_settings_visible);
    CHECK(fixture.fixture.history.document.revision == revision);
    CHECK(fixture.fixture.history.undo.empty());
    CHECK(fixture.compose_state->cursor == cursor);
    CHECK(fixture.application_state.project.compose_gesture.note_paint_mode_active == tool);
    CHECK(fixture.compose_state->selected_objects == selection);
}

TEST_CASE("Select shortcut ends note paint without changing the armed kind", "[sys][application_commands]") {
    TimingInsertFixture fixture;
    fixture.application_state.project.compose_gesture.note_paint_mode_active = true;
    fixture.compose_state->armed_note_kind = wormedit::note_kind_t::hold;
    REQUIRE(fixture.invoke(application_command_kind_t::edit_tool_select).accepted);
    CHECK_FALSE(fixture.application_state.project.compose_gesture.note_paint_mode_active);
    CHECK(fixture.compose_state->armed_note_kind == wormedit::note_kind_t::hold);
    fixture.workspace.active_tab = wormedit::workspace_tab_t::metadata;
    CHECK_FALSE(fixture.invoke(application_command_kind_t::edit_tool_select).accepted);
}

TEST_CASE("Disallowed note kind leaves Select and the armed kind unchanged", "[sys][application_commands]") {
    TimingInsertFixture fixture;
    fixture.fixture.history.document.charts.front().source_family = wormedit::format_family_t::bms;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::edit_note_kind, .note_kind = 2},
        fixture.fixture.history, fixture.workspace, fixture.compose_state, fixture.preview,
        fixture.application_state, fixture.sink);
    CHECK_FALSE(result.accepted);
    CHECK_FALSE(fixture.application_state.project.compose_gesture.note_paint_mode_active);
    CHECK(fixture.compose_state->armed_note_kind == wormedit::note_kind_t::tap);
}

auto bpm_preview_matches(const wormedit::timing_lane_pseudonote_outcome_t& preview,
                         wormedit::local_position_t cursor) -> bool {
    using namespace wormedit;
    return preview.report()->kind == timing_lane_kind_t::bpm && preview.report()->position == cursor &&
           preview.report()->initial_value_source == timing_lane_initial_value_source_t::effective_at_position &&
           std::get<timing_bpm_value_t>(preview.report()->value).beats_per_minute == signed_rational_t{150, 1};
}

auto sv_preview_matches(const wormedit::timing_lane_pseudonote_outcome_t& preview,
                        wormedit::local_position_t cursor) -> bool {
    using namespace wormedit;
    return preview.report()->kind == timing_lane_kind_t::scroll && preview.report()->position == cursor &&
           preview.report()->initial_value_source == timing_lane_initial_value_source_t::effective_at_position &&
           std::get<timing_scroll_value_t>(preview.report()->value).multiplier == signed_rational_t{3, 2};
}

auto history_unchanged(const wormedit::document_history_t& history, std::uint64_t revision) -> bool {
    return history.document.revision == revision && history.undo.empty();
}

auto bpm_stage_ready(const TimingInsertFixture& fixture,
                     const wormedit::command_invocation_result_t& result,
                     std::uint64_t revision) -> bool {
    return result.accepted && fixture.preview && bpm_preview_matches(*fixture.preview, fixture.cursor) &&
           history_unchanged(fixture.fixture.history, revision);
}

auto confirm_bpm_once(TimingInsertFixture& fixture, std::uint64_t revision) -> bool {
    if (!fixture.preview) return false;
    const auto committed = wormedit::apply_timing_lane_pseudonote(
        fixture.fixture.history, *fixture.preview, false);
    return committed.applied && fixture.fixture.history.document.revision == revision + 1 &&
           fixture.fixture.history.undo.size() == 1;
}

auto sv_stage_ready(const TimingInsertFixture& fixture,
                    const wormedit::command_invocation_result_t& result,
                    std::uint64_t revision) -> bool {
    return result.accepted && fixture.preview && sv_preview_matches(*fixture.preview, fixture.cursor) &&
           history_unchanged(fixture.fixture.history, revision);
}
} // namespace

TEST_CASE("BPM insertion stages effective cursor BPM and commits only on confirmation",
          "[unit][sys][commands][timing]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;

    const auto result = fixture.invoke(application_command_kind_t::timing_insert_bpm);
    CHECK(bpm_stage_ready(fixture, result, revision));
    CHECK(confirm_bpm_once(fixture, revision));
}

TEST_CASE("SV insertion stages the effective cursor scroll multiplier",
          "[unit][sys][commands][timing]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;

    const auto result = fixture.invoke(application_command_kind_t::timing_insert_sv);
    CHECK(sv_stage_ready(fixture, result, revision));
}

TEST_CASE("timing insertion rejects locked edits and missing charts",
          "[unit][sys][commands][timing]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;
    fixture.application_state.project.timing_recipe.timing_locked = true;
    CHECK_FALSE(fixture.invoke(application_command_kind_t::timing_insert_bpm).accepted);
    CHECK(fixture.latest_message() == "Timing edits are locked.");
    CHECK_FALSE(fixture.preview);
    fixture.application_state.project.timing_recipe.timing_locked = false;
    fixture.workspace.selected_chart.reset();
    CHECK_FALSE(fixture.invoke(application_command_kind_t::timing_insert_sv).accepted);
    CHECK_FALSE(fixture.preview);
    CHECK(fixture.fixture.history.document.revision == revision);
}

namespace {
void check_undo_notification(const std::vector<wormedit::notification_entry_t>& entries) {
    CHECK(entries[1].message == "Nothing to undo.");
    CHECK(entries[1].severity == wormedit::notification_severity_t::info);
    CHECK(entries[1].source == wormedit::notification_source_t::edit);
}

void check_clear_notification(const std::vector<wormedit::notification_entry_t>& entries) {
    CHECK(entries[0].message == "Semantic-layer target cleared.");
    CHECK(entries[0].severity == wormedit::notification_severity_t::info);
}

void check_long_command_detail(const TimingInsertFixture& fixture, const std::string& message) {
    const auto entries = fixture.center.snapshot(1);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].message.size() == wormedit::notification_message_max_bytes);
    CHECK(entries[0].detail == message);
}

void check_long_command_classification(const TimingInsertFixture& fixture) {
    const auto entries = fixture.center.snapshot(1);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].severity == wormedit::notification_severity_t::error);
    CHECK(entries[0].source == wormedit::notification_source_t::document);
}

void check_success_current_line(const wormedit::notification_entry_t& entry) {
    CHECK(entry.message == "Semantic-layer membership updated.");
    CHECK(entry.severity == wormedit::notification_severity_t::success);
    CHECK(entry.source == wormedit::notification_source_t::edit);
}
} // namespace

TEST_CASE("application command posts ordered typed no-op and state-clear results",
          "[unit][sys][application_commands][notifications]") {
    TimingInsertFixture fixture;
    const auto original_revision = fixture.fixture.history.document.revision;
    const auto undo = fixture.invoke(application_command_kind_t::history_undo);
    REQUIRE(undo.accepted);
    const auto cleared = fixture.invoke(application_command_kind_t::semantic_layer_clear_target);
    REQUIRE(cleared.accepted);

    fixture.center.drain_inbox(wormedit::notification_time_t{0});
    const auto entries = fixture.center.snapshot(4);
    REQUIRE(entries.size() == 2);
    check_undo_notification(entries);
    check_clear_notification(entries);
    CHECK(fixture.fixture.history.document.revision == original_revision);
    CHECK(fixture.fixture.history.undo.empty());
}

TEST_CASE("semantic command success remains current after a navigation hint",
          "[unit][sys][application_commands][notifications]") {
    TimingInsertFixture fixture;
    auto& chart = fixture.fixture.history.document.charts.front();
    chart.semantic_layers = {
        {.id = wormedit::layer_id_t{7}, .name = "First", .color = {32, 64, 128, 255}},
        {.id = wormedit::layer_id_t{8}, .name = "Second", .color = {64, 128, 32, 255}}};
    fixture.compose_state->selected_notes = {wormedit::object_id_t{1}, wormedit::object_id_t{2}};
    fixture.application_state.project.semantic_layer.semantic_layer_target = wormedit::layer_id_t{8};
    const auto applied = fixture.invoke(application_command_kind_t::semantic_layer_apply);
    REQUIRE(applied.accepted);
    fixture.sink.post(wormedit::notification_severity_t::hint,
                       wormedit::notification_source_t::input, "Navigation canceled.");
    fixture.center.drain_inbox(wormedit::notification_time_t{0});

    REQUIRE(fixture.center.current_line() != nullptr);
    check_success_current_line(*fixture.center.current_line());
}

TEST_CASE("application command does not post an empty producer callback result",
          "[unit][sys][application_commands][notifications]") {
    TimingInsertFixture fixture;
    fixture.application_state.services.external_player = [](external_player_action_t) { return std::string{}; };
    const auto result = fixture.invoke(application_command_kind_t::transport_external_play_start);
    REQUIRE(result.accepted);
    fixture.center.drain_inbox(wormedit::notification_time_t{0});
    CHECK(fixture.center.size() == 0);
}

TEST_CASE("application command selection clear invokes producer-owned cursor restore once",
          "[unit][sys][application_commands][notifications]") {
    TimingInsertFixture fixture;
    int restored{};
    const auto original = wormedit::write_native_project(fixture.fixture.history.document).bytes;
    fixture.application_state.services.transport_inline_active = [] { return true; };
    fixture.application_state.services.transport_cancel_restore = [&] {
        ++restored;
        fixture.sink.post(wormedit::notification_severity_t::success, wormedit::notification_source_t::audio,
                          "Chart preview stopped; cursor restored.");
        return std::string{"Chart preview stopped; cursor restored."};
    };
    REQUIRE(fixture.invoke(application_command_kind_t::selection_clear).accepted);
    CHECK(restored == 1);
    fixture.center.drain_inbox(wormedit::notification_time_t{0});
    const auto entries = fixture.center.snapshot(4);
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().message == "Chart preview stopped; cursor restored.");
    CHECK(wormedit::write_native_project(fixture.fixture.history.document).bytes == original);
}

TEST_CASE("application command notification severity preserves original rejection diagnostic",
          "[unit][sys][application_commands][notifications]") {
    TimingInsertFixture fixture;
    const auto result = reject_application_command(fixture.sink, "Precondition was rejected.",
        wormedit::notification_severity_t::warning, wormedit::notification_source_t::edit);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().severity == wormedit::diagnostic_severity_t::error);
    CHECK_FALSE(result.accepted);
    fixture.center.drain_inbox(wormedit::notification_time_t{0});
    REQUIRE(fixture.center.current_line());
    CHECK(fixture.center.current_line()->severity == wormedit::notification_severity_t::warning);
}

TEST_CASE("application command rejection preserves long diagnostic detail", "[notifications]") {
    TimingInsertFixture fixture;
    const auto message = std::string(600, 'x');
    const auto original_revision = fixture.fixture.history.document.revision;
    const auto result = reject_application_command(
        fixture.sink, message, wormedit::notification_severity_t::error,
        wormedit::notification_source_t::document,
        wormedit::diagnostic_code_t::invalid_id);
    CHECK_FALSE(result.accepted);
    fixture.center.drain_inbox(wormedit::notification_time_t{0});
    check_long_command_detail(fixture, message);
    check_long_command_classification(fixture);
    CHECK(fixture.fixture.history.document.revision == original_revision);
}

namespace {
void check_layer_command(bool condition) { CHECK(condition); }
void require_layer_command(bool condition) { REQUIRE(condition); }
void seed_layer_commands(TimingInsertFixture& fixture, bool locked = false) {
    using namespace wormedit;
    auto& chart = fixture.fixture.history.document.charts.front();
    chart.semantic_layers = {
        {.id = layer_id_t{7}, .name = "First", .color = {32, 64, 128, 255}, .locked = locked},
        {.id = layer_id_t{8}, .name = "Second", .color = {64, 128, 32, 255}}};
    chart.measures.front().notes.front().semantic_layers = {layer_id_t{7}};
    fixture.compose_state->selected_notes = {object_id_t{1}, object_id_t{2}};
}
}

TEST_CASE("Application route groups retain their own missing-context rejection",
          "[sys][application_commands][route-parity]") {
    using kind_t = application_command_kind_t;
    const auto route = GENERATE(
        std::pair{kind_t::edit_tool_select, "Select requires the active Compose chart."},
        std::pair{kind_t::edit_note_kind, "Note kind requires the active Compose chart."},
        std::pair{kind_t::measures_insert_before, "Measure editing requires the active Compose chart."},
        std::pair{kind_t::transport_loop_selection, "Select a committed area to loop playback."},
        std::pair{kind_t::transport_play_pause, "Chart preview is unavailable."},
        std::pair{kind_t::transport_external_play_start, "External player is unavailable."},
        std::pair{kind_t::edit_move_to_bgm, "Move to BGM requires selected notes on an editable BMS chart."},
        std::pair{kind_t::edit_copy, "Clipboard requires the active Compose chart."},
        std::pair{kind_t::edit_delete, "Delete requires the active Compose chart."},
        std::pair{kind_t::selection_select_all, "Selection command requires the active Compose chart."},
        std::pair{kind_t::navigation_next_snap, "Snap navigation requires the active Compose chart."},
        std::pair{kind_t::navigation_go_to_bookmark, "Bookmark chooser requires the active Compose chart."},
        std::pair{kind_t::view_open_layers, "Dock command requires the active Compose chart."},
        std::pair{kind_t::navigation_next_bookmark, "Bookmark commands require the active Compose chart."},
        std::pair{kind_t::timing_insert_bpm, "Timing insertion requires the active Compose chart."},
        std::pair{kind_t::timing_toggle_lock, "Timing Lock requires Compose with Timing focus."},
        std::pair{kind_t::semantic_layer_select_slot, "Layer selection requires the active Compose chart."},
        std::pair{kind_t::semantic_layer_apply, "Choose a semantic layer and select playable notes first."});
    INFO(static_cast<int>(route.first));
    TimingInsertFixture fixture;
    fixture.compose_state.reset();
    const auto revision = fixture.fixture.history.document.revision;
    const auto result = fixture.invoke(route.first);
    check_layer_command(!(result.accepted));
    require_layer_command(result.diagnostics.size() == 1);
    check_layer_command(result.diagnostics.front().message == route.second);
    check_layer_command(fixture.fixture.history.document.revision == revision);
    check_layer_command(fixture.fixture.history.undo.empty());
    fixture.center.drain_inbox(wormedit::notification_time_t{});
    require_layer_command(fixture.center.current_line());
    check_layer_command(fixture.center.current_line()->message == route.second);
    check_layer_command(fixture.center.current_line()->count == 1);
}

TEST_CASE("Application unknown and outer-frame commands retain unsupported fallback",
          "[sys][application_commands][route-parity]") {
    const auto kind = GENERATE(static_cast<application_command_kind_t>(9999),
                              application_command_kind_t::document_new_project,
                              application_command_kind_t::workspace_options,
                              application_command_kind_t::workspace_command_menu);
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;
    const auto result = fixture.invoke(kind);
    check_layer_command(!(result.accepted));
    require_layer_command(result.diagnostics.size() == 1);
    check_layer_command(result.diagnostics.front().message == "Command is not implemented by the application.");
    fixture.center.drain_inbox(wormedit::notification_time_t{});
    const auto entries = fixture.center.snapshot(4);
    require_layer_command(entries.size() == 1);
    check_layer_command(entries.front().message == result.diagnostics.front().message);
    check_layer_command(entries.front().source == wormedit::notification_source_t::system);
    check_layer_command(entries.front().count == 1);
    check_layer_command(fixture.fixture.history.document.revision == revision);
    check_layer_command(fixture.fixture.history.undo.empty());
}

TEST_CASE("Application history and measure feedback precedes a failed workspace bind",
          "[sys][application_commands][route-parity][notifications]") {
    using namespace wormedit;
    const auto kind = GENERATE(application_command_kind_t::history_undo,
                              application_command_kind_t::history_redo,
                              application_command_kind_t::measures_insert_before,
                              application_command_kind_t::measures_insert_after,
                              application_command_kind_t::measures_split_at_cursor);
    TimingInsertFixture fixture;
    auto& history = fixture.fixture.history;
    require_layer_command(apply_semantic_layer_edit(history,
        insert_edit_t<semantic_layer_edit_value_t>{.chart = chart_id_t{7},
            .value = {.name = "Seed"}}).applied);
    if (kind == application_command_kind_t::history_redo)
        require_layer_command(undo_document_command(history));
    fixture.workspace.song = song_id_t{99};
    const auto result = fixture.invoke(kind);
    check_layer_command(!(result.accepted));
    require_layer_command(result.diagnostics.size() == 1);
    check_layer_command(result.diagnostics.front().code == diagnostic_code_t::invalid_id);
    fixture.center.drain_inbox(notification_time_t{});
    const auto entries = fixture.center.snapshot(4);
    require_layer_command(entries.size() == 2);
    check_layer_command(entries.front().source == notification_source_t::document);
    check_layer_command(entries.front().severity == notification_severity_t::error);
    check_layer_command(entries.front().message == "workspace cannot be rebound to a different song identity");
    check_layer_command(entries.back().source == notification_source_t::edit);
    check_layer_command(entries.back().severity == notification_severity_t::success);
    check_layer_command(entries.front().count == 1);
    check_layer_command(entries.back().count == 1);
    check_layer_command(entries.front().post_sequence > entries.back().post_sequence);
}

TEST_CASE("Application semantic feedback follows successful binding and accepts unchanged membership",
          "[sys][application_commands][route-parity][notifications]") {
    using namespace wormedit;
    const auto mode = GENERATE(0, 1, 2); // applied, failed bind, semantic no-op
    TimingInsertFixture fixture;
    seed_layer_commands(fixture);
    fixture.compose_state->selected_notes = {object_id_t{1}};
    fixture.application_state.project.semantic_layer.semantic_layer_target = layer_id_t{7};
    if (mode != 2)
        fixture.application_state.project.semantic_layer.semantic_layer_target = layer_id_t{8};
    if (mode != 0) fixture.workspace.song = song_id_t{99};
    const auto revision = fixture.fixture.history.document.revision;
    const auto result = fixture.invoke(application_command_kind_t::semantic_layer_apply);
    check_layer_command(result.accepted == (mode != 1));
    check_layer_command(fixture.fixture.history.undo.size() == (mode == 2 ? 0 : 1));
    check_layer_command((fixture.fixture.history.document.revision == revision) == (mode == 2));
    if (mode == 0) {
        check_layer_command(fixture.workspace.document_revision == fixture.fixture.history.document.revision);
        check_layer_command(fixture.workspace.history.undo_depth == 1);
    }
    fixture.center.drain_inbox(notification_time_t{});
    const auto entries = fixture.center.snapshot(4);
    require_layer_command(entries.size() == 1);
    check_layer_command(entries.front().count == 1);
    check_layer_command(entries.front().message == (mode == 0 ? "Semantic-layer membership updated."
        : mode == 1 ? "workspace cannot be rebound to a different song identity"
                    : "Semantic-layer membership was unchanged."));
    check_layer_command(entries.front().source == (mode == 1 ? notification_source_t::document
                                              : notification_source_t::edit));
    check_layer_command(entries.front().severity == (mode == 0 ? notification_severity_t::success
        : mode == 1 ? notification_severity_t::error : notification_severity_t::info));
}

TEST_CASE("Application tab commands require applied switches and preserve repeated-tab behavior",
          "[sys][application_commands][route-parity]") {
    using namespace wormedit;
    const auto mode = GENERATE(0, 1, 2); // same tab, disabled tab, stale binding
    TimingInsertFixture fixture;
    if (mode == 2) --fixture.workspace.document_revision;
    const auto revision = fixture.fixture.history.document.revision;
    const auto result = fixture.invoke(mode == 1 ? application_command_kind_t::workspace_open_storyboard
                                                 : application_command_kind_t::workspace_open_compose);
    check_layer_command(result.accepted == (mode == 0));
    check_layer_command(result.diagnostics.empty() == (mode == 0));
    check_layer_command(fixture.workspace.active_tab == workspace_tab_t::compose);
    check_layer_command(fixture.fixture.history.document.revision == revision);
    check_layer_command(fixture.fixture.history.undo.empty());
    fixture.center.drain_inbox(notification_time_t{});
    const auto entries = fixture.center.snapshot(4);
    require_layer_command(entries.size() == 1);
    check_layer_command(entries.front().count == 1);
    check_layer_command(entries.front().source == notification_source_t::input);
    check_layer_command(entries.front().message == (mode == 0 ? "Workspace tab changed."
        : mode == 1 ? "storyboard editing is not available yet" : stale_action_message("Workspace action")));
}

TEST_CASE("Application No layer command clears selection and disables other membership operations",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    seed_layer_commands(fixture);
    fixture.application_state.project.semantic_layer.semantic_layer_target_none = true;
    require_layer_command(fixture.invoke(application_command_kind_t::semantic_layer_apply).accepted);
    check_layer_command(fixture.fixture.history.undo.size() == 1);
    check_layer_command(fixture.fixture.history.document.charts.front().measures.front().notes.front().semantic_layers.empty());
    check_layer_command(!fixture.invoke(application_command_kind_t::semantic_layer_remove).accepted);
    check_layer_command(fixture.latest_message() == "No layer only assigns");
    check_layer_command(!fixture.invoke(application_command_kind_t::semantic_layer_toggle).accepted);
    check_layer_command(fixture.fixture.history.undo.size() == 1);
    require_layer_command(fixture.invoke(application_command_kind_t::semantic_layer_clear_target).accepted);
    check_layer_command(!fixture.application_state.project.semantic_layer.semantic_layer_target_none);
}

TEST_CASE("Application layer commands replace membership and reject locked sources without edits",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    for (const auto locked : {false, true}) {
        TimingInsertFixture fixture;
        seed_layer_commands(fixture, locked);
        fixture.application_state.project.semantic_layer.semantic_layer_target = layer_id_t{8};
        const auto original = write_native_project(fixture.fixture.history.document).bytes;
        const auto result = fixture.invoke(application_command_kind_t::semantic_layer_apply);
        check_layer_command(result.accepted == !locked);
        if (locked) {
            check_layer_command(fixture.latest_message().find("locked") != std::string::npos);
            check_layer_command(write_native_project(fixture.fixture.history.document).bytes == original);
        } else {
            const auto& notes = fixture.fixture.history.document.charts.front().measures.front().notes;
            check_layer_command(notes[0].semantic_layers == std::vector{layer_id_t{8}});
            check_layer_command(notes[1].semantic_layers == std::vector{layer_id_t{8}});
        }
        check_layer_command(fixture.application_state.project.semantic_layer.semantic_layer_target == layer_id_t{8});
    }
}

TEST_CASE("Application numbered layer selection resets No layer target",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    seed_layer_commands(fixture);
    fixture.application_state.project.semantic_layer.semantic_layer_target_none = true;
    const auto result = invoke_application_command(
        {.kind = application_command_kind_t::semantic_layer_select_slot, .semantic_layer_slot = 1},
        fixture.fixture.history, fixture.workspace, fixture.compose_state, fixture.preview,
        fixture.application_state, fixture.sink);
    require_layer_command(result.accepted);
    check_layer_command(!fixture.application_state.project.semantic_layer.semantic_layer_target_none);
    check_layer_command(fixture.application_state.project.semantic_layer.semantic_layer_target == layer_id_t{7});
}


TEST_CASE("bookmark dispatcher preserves one typed seek producer warning", "[unit][sys][commands][notification]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    auto& chart = fixture.fixture.history.document.charts.front();
    chart.bookmarks.push_back({.id = object_id_t{1001}, .label = "Next",
        .anchor = marker_anchor_t::beat, .position = local_position_t{measure_id_t{1}, 900}});
    fixture.application_state.services.transport_preview_playing = [] { return true; };
    fixture.application_state.project.transport.preview_playhead_absolute_row = 0.0;
    fixture.application_state.services.transport_seek = [&](std::uint64_t) {
        return compose_action_outcome_t::from(
            std::string{"Opaque seek failure."}, wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::audio, compose_feedback_delivery_t::notification);
    };
    check_layer_command(fixture.invoke(application_command_kind_t::navigation_previous_bookmark).accepted);
    fixture.center.drain_inbox(notification_time_t{0});
    const auto entries = fixture.center.snapshot(notification_capacity);
    require_layer_command(entries.size() == 1);
    check_layer_command(entries[0].severity == notification_severity_t::warning);
    check_layer_command(entries[0].source == notification_source_t::audio);
    check_layer_command(entries[0].count == 1);
}

TEST_CASE("next marker reaches an audio-time bookmark through the timing index",
          "[unit][sys][commands][markers]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    auto& chart = fixture.fixture.history.document.charts.front();
    chart.bookmarks.push_back({.id = object_id_t{1002}, .label = "Audio",
        .anchor = marker_anchor_t::audio_time, .audio_milliseconds = 2000});
    check_layer_command(fixture.invoke(application_command_kind_t::navigation_next_marker).accepted);
    // Cursor row 720 sits at 1400 ms (BPM 150 from row 480); 2000 ms is 360 rows later.
    check_layer_command(fixture.compose_state->cursor == local_position_t{measure_id_t{2}, 120});
}

#else
namespace {
void check_area_unchanged(const AreaFixture& fixture, const std::optional<std::string>& original) {
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.history.undo.empty());
    CHECK(fixture.state.selected_notes ==
          std::vector<wormedit::object_id_t>{wormedit::object_id_t{1}});
}
void check_original_area(const AreaFixture& fixture) {
    using namespace wormedit;
    CHECK(fixture.state.retained_selection_range ==
          position_span_t{{measure_id_t{1}, 121}, {measure_id_t{1}, 480}});
}
} // namespace

TEST_CASE("Compose area resizing snaps only the moving edge and leaves edits and selection intact",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    const auto selected = fixture.state.selected_objects;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    CHECK(fixture.dispatch() == "Selected area resized.");
    CHECK(fixture.state.retained_canvas_selection_range == canvas_row_span_t{121, 720});
    check_area_unchanged(fixture, original);
    CHECK(fixture.state.selected_objects == selected);
}

TEST_CASE("Compose area edge crossing normalizes rows and cancellation restores unsnapped bounds",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    const auto selected = fixture.state.selected_objects;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    fixture.action.compose_area_begin_edge = true;
    fixture.action.compose_area_begin_row = 900;
    fixture.action.compose_area_end_row = 480;
    CHECK(fixture.dispatch() == "Selected area resized.");
    CHECK(fixture.state.retained_canvas_selection_range == canvas_row_span_t{480, 960});
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE;
    fixture.action.compose_area_begin_row = 121;
    fixture.action.compose_area_end_row = 480;
    CHECK(fixture.dispatch() == "Area resize canceled.");
    check_original_area(fixture);
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.state.selected_objects == selected);
}

TEST_CASE("Compose area actions reject stale sources and extreme rows atomically",
          "[unit][sys][commands]") {
    AreaFixture fixture;
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    const auto range = fixture.state.retained_selection_range;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    SECTION("stale revision") {
        ++fixture.action.compose_input_document_revision;
    }
    SECTION("other chart") {
        ++fixture.action.compose_input_chart_id;
    }
    SECTION("overflow row") {
        fixture.action.compose_area_end_row = std::numeric_limits<std::uint64_t>::max();
    }
    CHECK(fixture.dispatch() != "Selected area resized.");
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.state.retained_selection_range == range);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Compose area quantization changes selected notes only", "[unit][sys][commands]") {
    AreaFixture fixture;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE;
    fixture.action.compose_quantize_divisions = fixture.state.active_snap_denominator;
    fixture.action.compose_quantize_holds = static_cast<std::uint8_t>(wormedit::quantization_hold_t::both_endpoints);
    CHECK(fixture.dispatch() == "Selected objects quantized.");
    const auto& chart = fixture.history.document.charts.front();
    CHECK(chart.measures.front().notes[0].position.row == 240);
    CHECK(chart.measures.front().notes[1].position.row == 480);
    CHECK(chart.measures.front().notes[2].position.row == 720);
    CHECK(chart.bpms[1].position.row == 480);
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Compose area deletion preserves notes and timing at the ending boundary",
          "[unit][sys][commands]") {
    AreaFixture fixture;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    CHECK(fixture.dispatch() == "Selected area contents deleted.");
    const auto& chart = fixture.history.document.charts.front();
    REQUIRE(chart.measures.front().notes.size() == 2);
    CHECK(chart.measures.front().notes.front().id == wormedit::object_id_t{2});
    CHECK(chart.measures.front().notes.back().id == wormedit::object_id_t{3});
    REQUIRE(chart.bpms.size() == 2);
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Compose area resizing retains virtual canvas rows without materializing measures",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    fixture.history.document.charts.front().source_family = format_family_t::bms;
    fixture.state.retained_selection_range.reset();
    fixture.state.retained_canvas_selection_range = canvas_row_span_t{2400, 4800};
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    fixture.action.compose_area_virtual = true;
    fixture.action.compose_area_begin_row = 2400;
    fixture.action.compose_area_end_row = 5000;
    CHECK(fixture.dispatch() == "Selected area resized.");
    CHECK(fixture.state.retained_canvas_selection_range == canvas_row_span_t{2400, 5040});
    CHECK_FALSE(fixture.state.retained_selection_range);
    check_area_unchanged(fixture, original);
    fixture.action.compose_area_end_row = 960000;
    CHECK(fixture.dispatch() != "Selected area resized.");
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
}

TEST_CASE("Compose area deletion rejects unsupported timing atomically", "[unit][sys][commands]") {
    AreaFixture fixture;
    fixture.history.document.charts.front().bpms[1].position.row = 240;
    const auto original = wormedit::write_native_project(fixture.history.document).bytes;
    REQUIRE(original);
    CHECK_FALSE(compose_area_delete_supported(fixture.history, fixture.state));
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    CHECK(fixture.dispatch().find("notes and BGM only") != std::string::npos);
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == original);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Compose area deletion removes interior BGM and preserves the ending boundary",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.bpms.resize(1);
    chart.bgm_source_lanes.push_back({.id = object_id_t{300}, .name = "BGM"});
    chart.measures.front().bgm_events = {{.id = object_id_t{200},
                                          .position = {measure_id_t{1}, 240},
                                          .source_lane = object_id_t{300}},
                                         {.id = object_id_t{201},
                                          .position = {measure_id_t{1}, 480},
                                          .source_lane = object_id_t{300}},
                                         {.id = object_id_t{202},
                                          .position = {measure_id_t{1}, 720},
                                          .source_lane = object_id_t{300}}};
    REQUIRE(compose_area_delete_supported(fixture.history, fixture.state));
    const auto index = std::get<measure_index_t>(build_measure_index(chart));
    CHECK(compose_area_delete_supported(chart, index, fixture.state));
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_DELETE;
    CHECK(fixture.dispatch() == "Selected area contents deleted.");
    const auto& events = fixture.history.document.charts.front().measures.front().bgm_events;
    REQUIRE(events.size() == 2);
    CHECK(events.front().id == object_id_t{201});
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Compose area edges follow measure-local active grid across odd measure lengths",
          "[unit][sys][commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.measures.front().definition = {{5, 8}, meter_t{5, 8}};
    chart.measures.front().notes.back().position.row = 520;
    fixture.state.retained_selection_range =
        position_span_t{{measure_id_t{2}, 17}, {measure_id_t{2}, 480}};
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    fixture.action.compose_area_begin_row = 617;
    fixture.action.compose_area_end_row = 1100;
    CHECK(fixture.dispatch() == "Selected area resized.");
    CHECK(fixture.state.retained_canvas_selection_range == canvas_row_span_t{617, 1080});
    REQUIRE(fixture.state.retained_selection_range);
    CHECK(fixture.state.retained_selection_range->begin == local_position_t{measure_id_t{2}, 17});
    CHECK(fixture.state.retained_selection_range->end == local_position_t{measure_id_t{2}, 480});
}

TEST_CASE("Compose area quantization rejects ambiguous identities without edits", "[unit][sys][commands]") {
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.bpms.back().id = wormedit::object_id_t{1};
    const auto revision = fixture.history.document.revision;
    const auto position = chart.measures.front().notes.front().position;
    fixture.action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE;
    fixture.action.compose_quantize_divisions = fixture.state.active_snap_denominator;
    fixture.action.compose_quantize_holds = static_cast<std::uint8_t>(wormedit::quantization_hold_t::both_endpoints);
    CHECK(fixture.dispatch().find("reuses an object ID") != std::string::npos);
    CHECK(fixture.history.document.revision == revision);
    CHECK(chart.measures.front().notes.front().position == position);
    CHECK(chart.bpms.back().id == wormedit::object_id_t{1});
    CHECK(fixture.history.undo.empty());
}


TEST_CASE("Transport rates accept exact preferences and reject boundaries", "[unit][sys][commands]") {
    application_command_state_t state;
    wormedit::rational_t requested{1, 1};
    int calls = 0;
    state.services.transport_set_playback_rate = [&](wormedit::rational_t rate) {
        requested = rate;
        ++calls;
        return true;
    };
    CHECK_FALSE(step_transport_rate(state, true));
    CHECK(calls == 0);
    for (std::size_t index = 8; index > 0; --index) {
        REQUIRE(step_transport_rate(state, false));
        CHECK(state.preferences.transport_rate_index == index - 1);
        CHECK(requested == wormedit::audio::playback_rates[index - 1]);
    }
    CHECK_FALSE(step_transport_rate(state, false));
    CHECK(calls == 8);
    CHECK_FALSE(set_transport_rate(state, 9));
    state.services.transport_set_playback_rate = [](wormedit::rational_t) { return false; };
    CHECK_FALSE(step_transport_rate(state, true));
    CHECK(state.preferences.transport_rate_index == 0);
    CHECK_FALSE(state.preferences.transport_keep_pitch_available);
    CHECK_FALSE(state.preferences.transport_keep_pitch);
    REQUIRE(parse_application_command("transport.rate_slower"));
    REQUIRE(parse_application_command("transport.rate_faster"));
    CHECK(parse_application_command("transport.rate_slower")->kind == application_command_kind_t::transport_rate_slower);
    CHECK(parse_application_command("transport.rate_faster")->kind == application_command_kind_t::transport_rate_faster);
}

namespace {
void check_note_kind_command_parser(std::string_view name, unsigned kind) {
    const auto id = "edit.note_kind_" + std::string(name);
    const auto command = parse_application_command(id);
    REQUIRE(command);
    CHECK(command->kind == application_command_kind_t::edit_note_kind);
    CHECK(command->note_kind == kind);
}
void check_note_kind_command_shortcut(const wormedit::command_registry_t& registry,
                                      std::string_view name, std::size_t index) {
    using namespace wormedit;
    const auto id = "edit.note_kind_" + std::string(name);
    const auto registration = std::ranges::find_if(
        registry.commands, [&](const auto& entry) { return entry.definition.id.value == id; });
    REQUIRE(registration != registry.commands.end());
    if (index < 2) {
        CHECK(registration->definition.default_shortcuts.empty());
        return;
    }
    REQUIRE(registration->definition.default_shortcuts.size() == 1);
    CHECK(registration->definition.default_shortcuts.front().key ==
          static_cast<shortcut_key_t>(static_cast<unsigned>(shortcut_key_t::digit_2) + index));
    CHECK(registration->definition.default_shortcuts.front().modifiers ==
          static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt));
}
} // namespace

TEST_CASE("Note kind commands preserve lane and layer shortcut chords", "[unit][sys][commands]") {
    using namespace wormedit;
    const auto registry = command_registry_t::from(
        [](const command_invocation_context_t&) { return command_invocation_result_t{}; });
    const std::array names{"tap", "hold", "mine", "invisible", "lift", "fake", "roll"};
    const std::array kinds{0U, 1U, 4U, 3U, 5U, 6U, 2U};
    for (std::size_t index = 0; index < names.size(); ++index) {
        check_note_kind_command_parser(names[index], kinds[index]);
        check_note_kind_command_shortcut(registry, names[index], index);
    }
    CHECK_FALSE(parse_application_command("edit.note_kind_invalid"));
    CHECK_FALSE(parse_application_command("edit.note_kind_tap.extra"));
}

TEST_CASE("Minimap transport gestures preserve playing paused and stopped policies", "[unit][sys][commands]") {
    application_command_state_t state;
    bool playing = true, paused = false;
    std::uint64_t resume = 0;
    int seeks = 0, stops = 0;
    state.services.transport_preview_playing = [&] { return playing; };
    state.services.transport_preview_paused = [&] { return paused; };
    state.services.transport_seek = [&](std::uint64_t frame) {
        ++seeks;
        resume = frame;
        return compose_action_outcome_t::from("seek", wormedit::notification_severity_t::hint,
                                              wormedit::notification_source_t::input);
    };
    state.services.transport_stop = [&] { ++stops; paused = false; resume = 0; };
    CHECK(navigate_compose_minimap_transport(state, 100, false, false) == "seek");
    CHECK(seeks == 1);
    CHECK(resume == 100);
    CHECK(navigate_compose_minimap_transport(state, 200, true, false).empty());
    CHECK(seeks == 1);
    playing = false; paused = true;
    CHECK(navigate_compose_minimap_transport(state, 200, false, false).empty());
    CHECK(stops == 1);
    CHECK_FALSE(paused);
    CHECK(resume == 0);
    paused = true;
    CHECK(navigate_compose_minimap_transport(state, 300, false, true) == "seek");
    CHECK(paused);
    CHECK(resume == 300);
    state.preferences.minimap_click_moves_paused_playhead = true;
    CHECK(navigate_compose_minimap_transport(state, 400, false, false) == "seek");
    CHECK(resume == 400);
    CHECK(navigate_compose_minimap_transport(state, 500, false, true).empty());
    CHECK(stops == 2);
    const int before = seeks;
    CHECK(navigate_compose_minimap_transport(state, 600, false, false).empty());
    CHECK(seeks == before);
    playing = true;
    state.services.transport_seek = [](std::uint64_t) {
        return compose_action_outcome_t::from("seek failed", wormedit::notification_severity_t::hint,
                                              wormedit::notification_source_t::input);
    };
    CHECK(navigate_compose_minimap_transport(state, 600, false, false) == "seek failed");
    CHECK(playing);
}

TEST_CASE("Minimap mapping converts rates offsets snaps and chart endpoints without document edits", "[unit][sys][commands]") {
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.beat_zero_audio_offset_seconds = {1, 1};
    const auto timing = wormedit::build_timing_index(chart);
    const auto measures = wormedit::build_measure_index(chart);
    REQUIRE(timing);
    REQUIRE(std::holds_alternative<wormedit::measure_index_t>(measures));
    const auto before = wormedit::write_native_project(fixture.history.document).bytes;
    const auto early = compose_minimap_target(0, 48001, 48000, 44100, *timing.index,
        std::get<wormedit::measure_index_t>(measures), 4);
    REQUIRE(early);
    CHECK(early->frame == 0);
    CHECK(early->snapped_row == 0);
    const auto at_zero = compose_minimap_target(1, 48001, 48000, 44100, *timing.index,
        std::get<wormedit::measure_index_t>(measures), 4);
    REQUIRE(at_zero);
    CHECK(at_zero->frame == 44100);
    CHECK(at_zero->snapped_row == 0);
    const auto target = compose_minimap_target(1, 72001, 48000, 44100, *timing.index,
        std::get<wormedit::measure_index_t>(measures), 4);
    REQUIRE(target);
    CHECK(target->frame == 66150);
    CHECK(target->snapped_row == 240);
    REQUIRE(move_compose_minimap_cursor(fixture.history, fixture.state, std::get<wormedit::measure_index_t>(measures), *timing.index, *target));
    CHECK((fixture.state.cursor == wormedit::local_position_t{wormedit::measure_id_t{1}, 240}));
    CHECK(wormedit::write_native_project(fixture.history.document).bytes == before);
    const auto clamped = compose_minimap_target(1, 72001, 48000, 44100, *timing.index,
        std::get<wormedit::measure_index_t>(measures), 4, 44100);
    REQUIRE(clamped);
    CHECK(clamped->frame == 44100);
    CHECK(clamped->snapped_row == 0);
    CHECK_FALSE(compose_minimap_target(1.1, 72001, 48000, 44100, *timing.index,
        std::get<wormedit::measure_index_t>(measures), 4));
}

TEST_CASE("Minimap exact starts expire when the fractional cursor changes", "[unit][sys][commands]") {
    AreaFixture fixture;
    application_command_state_t application;
    application.project.transport.minimap_start_frame = application_command_state_t::minimap_start_frame_t{
        fixture.state.chart, fixture.history.document.revision, fixture.state.cursor, 44'100,
        fixture.state.cursor_subrow};
    REQUIRE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
    wormedit::set_cursor_subrow(fixture.state, fixture.state.cursor_subrow + 0.125);
    CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
}

TEST_CASE("Minimap audio-tail starts are bound to chart revision and cursor", "[unit][sys][commands]") {
    AreaFixture fixture;
    application_command_state_t application;
    const auto timing = wormedit::build_timing_index(fixture.history.document.charts.front());
    const auto measures = wormedit::build_measure_index(fixture.history.document.charts.front());
    REQUIRE(timing);
    const auto target = compose_minimap_target(1, 441001, 44100, 44100, *timing.index,
        std::get<wormedit::measure_index_t>(measures), 4);
    REQUIRE(target);
    REQUIRE(target->absolute_row == timing.index->total_rows);
    SECTION("stopped and canceled pause retain the exact frame") {
        bool paused = true;
        application.services.transport_preview_paused = [&] { return paused; };
        application.services.transport_stop = [&] { paused = false; };
        (void)navigate_compose_minimap_transport(application, target->frame, false, false);
        REQUIRE_FALSE(paused);
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        CHECK(compose_minimap_start_frame(application, fixture.history, &fixture.state) == target->frame);
    }
    SECTION("normal cursor movement discards the binding") {
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        fixture.state.cursor.row = 240;
        CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
        fixture.state.cursor.row = 0;
        CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
    }
    SECTION("revision changes discard the binding") {
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        ++fixture.history.document.revision;
        CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
    }
    SECTION("chart changes discard the binding") {
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        fixture.state.chart = wormedit::chart_id_t{8};
        CHECK_FALSE(compose_minimap_start_frame(application, fixture.history, &fixture.state));
    }
    SECTION("playing and paused exact seeks require no next-start binding") {
        application.services.transport_preview_playing = [] { return true; };
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        CHECK_FALSE(application.project.transport.minimap_start_frame);
        application.services.transport_preview_playing = [] { return false; };
        application.services.transport_preview_paused = [] { return true; };
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        CHECK_FALSE(application.project.transport.minimap_start_frame);
    }
    SECTION("in-range clicks use their active snapped cursor") {
        const auto in_range = compose_minimap_target(0.1, 441001, 44100, 44100, *timing.index,
        std::get<wormedit::measure_index_t>(measures), 4);
        REQUIRE(in_range);
        retain_compose_minimap_start(application, fixture.history, fixture.state, *in_range, *timing.index);
        CHECK_FALSE(application.project.transport.minimap_start_frame);
    }
}

TEST_CASE("Minimap view scrolling preserves floor snap and authored BMS tail cursor", "[unit][sys][commands]") {
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    SECTION("off-grid row floors instead of nearest rounding") {
        const auto timing = wormedit::build_timing_index(chart);
        const auto measures = wormedit::build_measure_index(chart);
        REQUIRE(timing);
        REQUIRE(std::holds_alternative<wormedit::measure_index_t>(measures));
        compose_minimap_target_t target{36750, 400, 240};
        REQUIRE(move_compose_minimap_cursor(fixture.history, fixture.state,
            std::get<wormedit::measure_index_t>(measures), *timing.index, target));
        CHECK(fixture.state.cursor.row == 240);
        CHECK_FALSE(fixture.state.virtual_bms_cursor_row);
    }
    SECTION("audio tail retains an authored cursor on BMS") {
        chart.source_family = wormedit::format_family_t::bms;
        const auto timing = wormedit::build_timing_index(chart);
        const auto measures = wormedit::build_measure_index(chart);
        REQUIRE(timing);
        REQUIRE(std::holds_alternative<wormedit::measure_index_t>(measures));
        const auto target = compose_minimap_target(1, 441001, 44100, 44100, *timing.index,
        std::get<wormedit::measure_index_t>(measures), 4);
        REQUIRE(target);
        REQUIRE(move_compose_minimap_cursor(fixture.history, fixture.state,
            std::get<wormedit::measure_index_t>(measures), *timing.index, *target));
        CHECK_FALSE(fixture.state.virtual_bms_cursor_row);
        CHECK((fixture.state.cursor == wormedit::local_position_t{wormedit::measure_id_t{2}, 720}));
        application_command_state_t application;
        retain_compose_minimap_start(application, fixture.history, fixture.state, *target, *timing.index);
        CHECK(compose_minimap_start_frame(application, fixture.history, &fixture.state) == 441000);
    }
}

namespace { void expect_keep_pitch(bool value) { CHECK(value); } }

TEST_CASE("Keep pitch dispatch accepts backend preferences without changing documents", "[unit][sys][commands][keep-pitch]") {
    application_command_state_t state;
    wormedit::stepmania_export_policy_t policy;
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH;
    action.transport_keep_pitch = true;
    expect_keep_pitch(dispatch_transport_player_action(action, state, policy) == "Keep pitch is unavailable.");
    state.preferences.transport_keep_pitch_available = true;
    bool requested{};
    state.services.transport_set_keep_pitch = [&](bool value) { requested = value; return true; };
    expect_keep_pitch(dispatch_transport_player_action(action, state, policy) == "Keep pitch enabled.");
    expect_keep_pitch(requested);
    expect_keep_pitch(state.preferences.transport_keep_pitch);
    state.services.transport_set_keep_pitch = [](bool) { return false; };
    action.transport_keep_pitch = false;
    expect_keep_pitch(dispatch_transport_player_action(action, state, policy) == "Keep pitch is unavailable.");
    expect_keep_pitch(state.preferences.transport_keep_pitch);
}

TEST_CASE("Tap timing commands parse independently of document editing", "[unit][sys][commands]") {
    CHECK(parse_application_command("timing.tap_assistant") ==
          application_command_t{.kind = application_command_kind_t::timing_tap_assistant});
    CHECK(parse_application_command("timing.tap") ==
          application_command_t{.kind = application_command_kind_t::timing_tap});
}

TEST_CASE("notification history has a stable typed application command", "[unit][sys][notifications]") {
    CHECK(parse_application_command("view.notifications") ==
          application_command_t{.kind = application_command_kind_t::view_notifications});
    CHECK_FALSE(parse_application_command("view.notifications.extra"));
}

namespace { void expect_play_stop(bool value) { CHECK(value); } }

TEST_CASE("Plain Space transport starts or hard stops without pausing", "[unit][sys][commands]") {
    application_command_state_t state;
    bool playing{};
    bool paused{};
    int starts{};
    int stops{};
    state.services.transport_preview_playing = [&] { return playing; };
    state.services.transport_play_pause = [&] {
        if (playing) paused = true;
        else { ++starts; playing = true; paused = false; }
        return std::string{"started"};
    };
    state.services.transport_stop = [&] { ++stops; playing = false; paused = false; };
    expect_play_stop(play_stop_transport(state) == "started");
    expect_play_stop(starts == 1);
    expect_play_stop(play_stop_transport(state) == "Audio preview stopped.");
    expect_play_stop(stops == 1);
    expect_play_stop(!playing);
    expect_play_stop(!paused);
    paused = true;
    expect_play_stop(play_stop_transport(state) == "started");
    expect_play_stop(starts == 2);
    expect_play_stop(!paused);
    state.services.transport_stop = {};
    expect_play_stop(!play_stop_transport(state));
    expect_play_stop(playing);
}

namespace {
void check_transport_notification(bool value) { CHECK(value); }
void require_transport_notification(bool value) { REQUIRE(value); }
}

TEST_CASE("transport UI producer posts typed rejection and accepted settings once", "[unit][sys][commands][notification]") {
    using namespace wormedit;
    notification_center_t center;
    notification_sink_t sink{center};
    application_command_state_t state;
    stepmania_export_policy_t policy;
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH;
    const auto rejected = dispatch_transport_player_action(action, state, policy, sink);
    check_transport_notification(rejected == "Keep pitch is unavailable.");
    check_transport_notification(!state.preferences.transport_keep_pitch);
    center.drain_inbox(notification_time_t{0});
    auto entries = center.snapshot(notification_capacity);
    require_transport_notification(entries.size() == 1);
    check_transport_notification(entries[0].severity == notification_severity_t::warning);
    check_transport_notification(entries[0].source == notification_source_t::audio);
    state.preferences.transport_keep_pitch_available = true;
    state.services.transport_set_keep_pitch = [](bool) { return true; };
    action.transport_keep_pitch = true;
    check_transport_notification(dispatch_transport_player_action(action, state, policy, sink) == "Keep pitch enabled.");
    check_transport_notification(state.preferences.transport_keep_pitch);
    center.drain_inbox(notification_time_t{0});
    entries = center.snapshot(notification_capacity);
    require_transport_notification(entries.size() == 2);
    check_transport_notification(entries[0].severity == notification_severity_t::success);
    check_transport_notification(!dispatch_transport_player_action(WormUiWorkspaceAction{}, state, policy, sink));
    center.drain_inbox(notification_time_t{0});
    check_transport_notification(center.snapshot(notification_capacity).size() == 2);
}

TEST_CASE("transport UI playback callback owns notification publication", "[unit][sys][commands][notification]") {
    using namespace wormedit;
    notification_center_t center;
    notification_sink_t sink{center};
    application_command_state_t state;
    stepmania_export_policy_t policy;
    state.services.transport_play_pause = [&] {
        sink.post(notification_severity_t::error, notification_source_t::audio, "device failed");
        return std::string{"device failed"};
    };
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE;
    check_transport_notification(dispatch_transport_player_action(action, state, policy, sink) == "device failed");
    center.drain_inbox(notification_time_t{0});
    const auto entries = center.snapshot(notification_capacity);
    require_transport_notification(entries.size() == 1);
    check_transport_notification(entries[0].severity == notification_severity_t::error);
    check_transport_notification(entries[0].count == 1);
}


TEST_CASE("bookmark commands parse to their application kinds", "[unit][sys][commands]") {
    using kind = application_command_kind_t;
    const auto previous = parse_application_command("navigation.previous_bookmark");
    const auto next = parse_application_command("navigation.next_bookmark");
    const auto add = parse_application_command("markers.add_bookmark");
    REQUIRE(previous);
    REQUIRE(next);
    REQUIRE(add);
    CHECK(previous->kind == kind::navigation_previous_bookmark);
    CHECK(next->kind == kind::navigation_next_bookmark);
    CHECK(add->kind == kind::markers_add_bookmark);
}

namespace {
void check_layer_lock(bool condition) { CHECK(condition); }
void require_layer_lock(bool condition) { REQUIRE(condition); }
} // namespace

TEST_CASE("Layer row lock commands round trip through shared history and undo",
          "[unit][sys][commands][application_commands]") {
    using namespace wormedit;
    AreaFixture fixture;
    fixture.history.document.charts.front().semantic_layers = {
        {.id = layer_id_t{7}, .name = "Layer", .color = {32, 64, 128, 255}},
        {.id = layer_id_t{9}, .name = "Other", .color = {64, 128, 32, 255}}};
    const auto update = [&](bool locked) {
        return apply_semantic_layer_edit(
            fixture.history, wormedit::update_edit_t<wormedit::semantic_layer_edit_value_t>{
                                 .chart = chart_id_t{7},
                                 .id = layer_id_t{9},
                                 .value = {.name = "Other",
                                           .color = {64.0 / 255, 128.0 / 255, 32.0 / 255, 1},
                                           .visible = true,
                                           .locked = locked}});
    };
    require_layer_lock(update(true).applied);
    check_layer_lock(!fixture.history.document.charts.front().semantic_layers.front().locked);
    check_layer_lock(fixture.history.document.charts.front().semantic_layers.back().locked);
    require_layer_lock(update(false).applied);
    check_layer_lock(!fixture.history.document.charts.front().semantic_layers.back().locked);
    require_layer_lock(undo_document_command(fixture.history));
    check_layer_lock(fixture.history.document.charts.front().semantic_layers.back().locked);
    require_layer_lock(undo_document_command(fixture.history));
    check_layer_lock(!fixture.history.document.charts.front().semantic_layers.back().locked);
}


namespace {
void seed_membership_fixture(AreaFixture& fixture, bool locked = false) {
    using namespace wormedit;
    auto& chart = fixture.history.document.charts.front();
    chart.semantic_layers = {
        {.id = layer_id_t{7}, .name = "First", .color = {32, 64, 128, 255}, .locked = locked},
        {.id = layer_id_t{8}, .name = "Second", .color = {64, 128, 32, 255}}};
    chart.measures.front().notes[0].semantic_layers = {layer_id_t{7}};
    chart.measures.front().notes[1].semantic_layers = {layer_id_t{8}};
    fixture.state.selected_notes = {object_id_t{1}, object_id_t{2}};
}
}

namespace {
void check_membership(bool condition) { CHECK(condition); }
void require_membership(bool condition) { REQUIRE(condition); }
}

TEST_CASE("Compose No layer assigns mixed selection in one step and binds target after success",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    const auto original = fixture.history.document;
    bool none = false;
    std::optional<layer_id_t> target = layer_id_t{7};
    const auto result = apply_compose_layer_assignment(fixture.history, fixture.state, 9, true, {}, none, target);
    require_membership(static_cast<bool>(result));
    check_membership(static_cast<bool>(none));
    check_membership(static_cast<bool>(!(target)));
    check_membership(static_cast<bool>(fixture.history.undo.size() == 1));
    check_membership(static_cast<bool>(fixture.state.selected_notes.size() == 2));
    for (const auto& note : fixture.history.document.charts.front().measures.front().notes)
        check_membership(static_cast<bool>(note.semantic_layers.empty()));
    require_membership(static_cast<bool>(undo_document_command(fixture.history)));
    check_membership(static_cast<bool>(write_native_project(fixture.history.document).bytes == write_native_project(original).bytes));
}

TEST_CASE("Compose semantic-layer assignment includes selected BGM events with selected notes",
          "[unit][sys][application_commands][semantic-layer][bgm]") {
    using namespace wormedit;
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.semantic_layers = {
        {.id = layer_id_t{7}, .name = "Old", .color = {32, 64, 128, 255}},
        {.id = layer_id_t{8}, .name = "Target", .color = {64, 128, 32, 255}}};
    chart.bgm_source_lanes = {{.id = object_id_t{30}, .name = "BGM"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{20},
                                          .position = {measure_id_t{1}, 240},
                                          .source_lane = object_id_t{30},
                                          .semantic_layers = {layer_id_t{7}}}};
    fixture.state.selected_notes = {object_id_t{1}};
    fixture.state.selected_objects = {
        {editor_selection_object_kind_t::note, object_id_t{1}},
        {editor_selection_object_kind_t::bgm_event, object_id_t{20}}};
    bool target_none = false;
    std::optional<layer_id_t> target = layer_id_t{7};
    const auto assigned = apply_compose_layer_assignment(
        fixture.history, fixture.state, fixture.history.document.revision,
        false, layer_id_t{8}, target_none, target);
    REQUIRE(static_cast<bool>(assigned));
    const auto& assigned_chart = fixture.history.document.charts.front();
    CHECK(assigned_chart.measures.front().notes.front().semantic_layers ==
          std::vector{layer_id_t{8}});
    CHECK(assigned_chart.measures.front().bgm_events.front().semantic_layers ==
          std::vector{layer_id_t{8}});
    CHECK(fixture.history.undo.size() == 1);
    CHECK(fixture.state.selected_notes == std::vector{object_id_t{1}});
    CHECK(fixture.state.selected_objects.size() == 2);
}

TEST_CASE("Compose rejected clear and replacement preserve target and selection",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture, true);
    const auto original = fixture.history.document;
    const auto selection = fixture.state.selected_notes;
    bool none = false;
    std::optional<layer_id_t> target = layer_id_t{7};
    for (const auto clear : {true, false}) {
        const auto result = apply_compose_layer_assignment(fixture.history, fixture.state, 9,
            clear, layer_id_t{8}, none, target);
        check_membership(static_cast<bool>(!(result)));
        check_membership(static_cast<bool>(!(result.diagnostics.empty())));
        check_membership(static_cast<bool>(!(none)));
        check_membership(static_cast<bool>(target == layer_id_t{7}));
        check_membership(static_cast<bool>(fixture.state.selected_notes == selection));
        check_membership(static_cast<bool>(write_native_project(fixture.history.document).bytes == write_native_project(original).bytes));
        check_membership(static_cast<bool>(fixture.history.undo.empty()));
    }
}

TEST_CASE("Compose No layer paint clears mixed memberships once and undo restores each source",
          "[unit][sys][application_commands][semantic-layer][paint]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    const auto original = fixture.history.document;
    require_membership(static_cast<bool>(begin_compose_layer_paint(fixture.history, fixture.state, true, {}, object_id_t{1}, true)));
    require_membership(static_cast<bool>(dispatch_editor_action(fixture.history, fixture.state,
        update_semantic_layer_paint_gesture_action_t{object_id_t{2}, true})));
    require_membership(static_cast<bool>(finish_compose_layer_paint(fixture.history, fixture.state, {})));
    check_membership(static_cast<bool>(fixture.history.undo.size() == 1));
    check_membership(static_cast<bool>(!(fixture.state.gesture<wormedit::pending_semantic_layer_paint_gesture_t>())));
    check_membership(static_cast<bool>(fixture.state.selected_notes.size() == 2));
    for (const auto& note : fixture.history.document.charts.front().measures.front().notes)
        check_membership(static_cast<bool>(note.semantic_layers.empty()));
    require_membership(static_cast<bool>(undo_document_command(fixture.history)));
    check_membership(static_cast<bool>(write_native_project(fixture.history.document).bytes == write_native_project(original).bytes));
}

TEST_CASE("Compose No layer paint rejects locked membership and exposes status diagnostic",
          "[unit][sys][application_commands][semantic-layer][paint]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture, true);
    const auto original = fixture.history.document;
    require_membership(static_cast<bool>(begin_compose_layer_paint(fixture.history, fixture.state, true, {}, object_id_t{2}, true)));
    require_membership(static_cast<bool>(dispatch_editor_action(fixture.history, fixture.state,
        update_semantic_layer_paint_gesture_action_t{object_id_t{1}, true})));
    const auto result = finish_compose_layer_paint(fixture.history, fixture.state, {});
    check_membership(static_cast<bool>(!(result)));
    require_membership(static_cast<bool>(!(result.diagnostics.empty())));
    check_membership(static_cast<bool>(result.diagnostics.front().message.find("locked") != std::string::npos));
    check_membership(static_cast<bool>(!(fixture.state.gesture<wormedit::pending_semantic_layer_paint_gesture_t>())));
    check_membership(static_cast<bool>(write_native_project(fixture.history.document).bytes == write_native_project(original).bytes));
    check_membership(static_cast<bool>(fixture.history.undo.empty()));
    check_membership(static_cast<bool>(fixture.state.selected_notes.size() == 2));
}

TEST_CASE("Compose ordinary layer paint replaces other sources while toggling its own target",
          "[unit][sys][application_commands][semantic-layer][paint]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    require_membership(static_cast<bool>(begin_compose_layer_paint(fixture.history, fixture.state, false, layer_id_t{8}, object_id_t{1}, true)));
    require_membership(static_cast<bool>(finish_compose_layer_paint(fixture.history, fixture.state, object_id_t{2})));
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    check_membership(static_cast<bool>(notes[0].semantic_layers == std::vector{layer_id_t{8}}));
    check_membership(static_cast<bool>(notes[1].semantic_layers.empty()));
    check_membership(static_cast<bool>(fixture.history.undo.size() == 1));
}


TEST_CASE("Compose No layer no-op still binds target and releases an unassigned paint stroke",
          "[unit][sys][application_commands][semantic-layer]") {
    using namespace wormedit;
    AreaFixture fixture;
    bool none = false;
    std::optional<layer_id_t> target = layer_id_t{7};
    const auto result = apply_compose_layer_assignment(fixture.history, fixture.state, 9, true, {}, none, target);
    check_membership(!result.applied && result.diagnostics.empty());
    check_membership(none && !target);
    require_membership(static_cast<bool>(begin_compose_layer_paint(fixture.history, fixture.state, true, {}, object_id_t{1}, true)));
    require_membership(static_cast<bool>(finish_compose_layer_paint(fixture.history, fixture.state, {})));
    check_membership(!fixture.state.gesture<wormedit::pending_semantic_layer_paint_gesture_t>());
    check_membership(fixture.history.undo.empty());
}

#endif // WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY

#ifndef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Note context selects only an unselected hit before assigning one layer",
          "[unit][sys][application_commands][note-menu]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    fixture.state.selected_notes = {object_id_t{1}};
    require_membership(static_cast<bool>(select_compose_note_context(fixture.history, fixture.state, 9, object_id_t{2})));
    check_membership(fixture.state.selected_notes == std::vector{object_id_t{2}});
    bool none = false;
    std::optional<layer_id_t> target;
    require_membership(static_cast<bool>(apply_compose_layer_assignment(fixture.history,
        fixture.state, 9, false, layer_id_t{7}, none, target)));
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    check_membership(notes[0].semantic_layers == std::vector{layer_id_t{7}});
    check_membership(notes[1].semantic_layers == std::vector{layer_id_t{7}});
    check_membership(fixture.history.undo.size() == 1);
    require_membership(undo_document_command(fixture.history));
    check_membership(fixture.history.document.charts.front().measures.front().notes[1].semantic_layers ==
                     std::vector{layer_id_t{8}});
}

TEST_CASE("Note context preserves selected groups and clearing removes mixed layers once",
          "[unit][sys][application_commands][note-menu]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    require_membership(static_cast<bool>(select_compose_note_context(fixture.history, fixture.state, 9, object_id_t{2})));
    check_membership(fixture.state.selected_notes == std::vector{object_id_t{1}, object_id_t{2}});
    bool none = false;
    std::optional<layer_id_t> target{layer_id_t{8}};
    require_membership(static_cast<bool>(apply_compose_layer_assignment(fixture.history,
        fixture.state, 9, true, {}, none, target)));
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    check_membership(notes[0].semantic_layers.empty());
    check_membership(notes[1].semantic_layers.empty());
    check_membership(fixture.history.undo.size() == 1);
}

TEST_CASE("Note context rejects stale and unknown hits without replacing selection",
          "[unit][sys][application_commands][note-menu]") {
    using namespace wormedit;
    AreaFixture fixture;
    seed_membership_fixture(fixture);
    const auto selection = fixture.state.selected_notes;
    const auto stale = select_compose_note_context(fixture.history, fixture.state, 8, object_id_t{3});
    check_membership(!stale);
    test::check_stale_action_message(stale.diagnostics, "Note context");
    check_membership(!select_compose_note_context(fixture.history, fixture.state, 9, object_id_t{999}));
    check_membership(fixture.state.selected_notes == selection);
    check_membership(fixture.history.undo.empty());
}

#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
namespace {
void check_bookmark_callback(bool value) { CHECK(value); }
void require_bookmark_callback(bool value) { REQUIRE(value); }
struct bookmark_chooser_recorder_t {
    unsigned calls{};
    std::uint64_t project{}, chart{}, revision{};
    bool accepted{true};
    auto operator==(const bookmark_chooser_recorder_t&) const -> bool = default;
    static auto open(void* context, std::uint64_t project, std::uint64_t chart,
                      std::uint64_t revision) -> bool {
        auto& self = *static_cast<bookmark_chooser_recorder_t*>(context);
        ++self.calls;
        self.project = project;
        self.chart = chart;
        self.revision = revision;
        return self.accepted;
    }
};
}
TEST_CASE("Go To Bookmark command opens the retained chooser with the active context",
          "[sys][application_commands][bookmark]") {
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;
    const auto cursor = fixture.compose_state->cursor;
    bookmark_chooser_recorder_t chooser;
    fixture.application_state.bookmarks.chooser_backend = {
        .open = bookmark_chooser_recorder_t::open, .context = &chooser};
    require_bookmark_callback(fixture.invoke(application_command_kind_t::navigation_go_to_bookmark).accepted);
    check_bookmark_callback(chooser.calls == 1);
    check_bookmark_callback(chooser.project == fixture.fixture.history.document.id.value);
    check_bookmark_callback(chooser.chart == fixture.compose_state->chart.value);
    check_bookmark_callback(chooser.revision == revision);
    check_bookmark_callback(fixture.fixture.history.document.revision == revision);
    check_bookmark_callback(fixture.compose_state->cursor == cursor);
    fixture.workspace.active_tab = wormedit::workspace_tab_t::metadata;
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::navigation_go_to_bookmark).accepted);
    check_bookmark_callback(chooser.calls == 1);
}

TEST_CASE("Go To Bookmark command rejects a missing or blocked chooser callback",
          "[sys][application_commands][bookmark]") {
    TimingInsertFixture fixture;
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::navigation_go_to_bookmark).accepted);
    bookmark_chooser_recorder_t chooser{.accepted = false};
    fixture.application_state.bookmarks.chooser_backend = {
        .open = bookmark_chooser_recorder_t::open, .context = &chooser};
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::navigation_go_to_bookmark).accepted);
}
TEST_CASE("Bookmark navigation reset retains the chooser backend and clock and clears project seeks",
          "[sys][application_commands][bookmark][ownership]") {
    application_command_state_t state;
    bookmark_chooser_recorder_t chooser;
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds{10};
    state.bookmarks.chooser_backend = {.open = bookmark_chooser_recorder_t::open, .context = &chooser};
    state.bookmarks.fixed_clock = &now;
    state.bookmarks.record_seek(wormedit::chart_id_t{7}, 192);
    reset_application_project_edit_state(state);
    check_bookmark_callback(!state.bookmarks.recent_seek_row(wormedit::chart_id_t{7}));
    check_bookmark_callback(state.bookmarks.now() == now);
    require_bookmark_callback(state.bookmarks.open_chooser(1, 7, 2));
    check_bookmark_callback(chooser.calls == 1);
    now += std::chrono::milliseconds{301};
    check_bookmark_callback(state.bookmarks.now() == now);
}
TEST_CASE("Dock commands use the active Compose context without editing history",
          "[sys][application_commands][dock-shortcuts]") {
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;
    const auto undo_size = fixture.fixture.history.undo.size();
    unsigned calls{};
    fixture.application_state.services.compose_dock_command = [&](WormUiComposeDockCommand dock,
        std::uint64_t project, std::uint64_t chart, std::uint64_t expected_revision) {
        ++calls;
        check_bookmark_callback(dock <= WORM_UI_COMPOSE_DOCK_TOGGLE_SOUNDS);
        check_bookmark_callback(project == fixture.fixture.history.document.id.value);
        check_bookmark_callback(chart == fixture.compose_state->chart.value);
        check_bookmark_callback(expected_revision == revision);
        return true;
    };
    for (const auto kind : {application_command_kind_t::view_open_layers,
                            application_command_kind_t::semantic_layer_create,
                            application_command_kind_t::view_toggle_sounds}) {
        require_bookmark_callback(fixture.invoke(kind).accepted);
        fixture.workspace.active_tab = wormedit::workspace_tab_t::metadata;
        check_bookmark_callback(!fixture.invoke(kind).accepted);
        fixture.workspace.active_tab = wormedit::workspace_tab_t::compose;
        fixture.compose_state->active_gesture.emplace();
        check_bookmark_callback(!fixture.invoke(kind).accepted);
        fixture.compose_state->active_gesture.reset();
        fixture.application_state.project.compose_view.compose_timing_focus = true;
        check_bookmark_callback(!fixture.invoke(kind).accepted);
        fixture.application_state.project.compose_view.compose_timing_focus = false;
    }
    check_bookmark_callback(calls == 3);
    check_bookmark_callback(fixture.fixture.history.document.revision == revision);
    check_bookmark_callback(fixture.fixture.history.undo.size() == undo_size);
}

TEST_CASE("Dock commands reject absent and unavailable callbacks",
          "[sys][application_commands][dock-shortcuts]") {
    TimingInsertFixture fixture;
    for (const auto kind : {application_command_kind_t::view_open_layers,
                            application_command_kind_t::semantic_layer_create,
                            application_command_kind_t::view_toggle_sounds}) {
        fixture.application_state.services.compose_dock_command = {};
        check_bookmark_callback(!fixture.invoke(kind).accepted);
        fixture.application_state.services.compose_dock_command = [](auto, auto, auto, auto) { return false; };
        check_bookmark_callback(!fixture.invoke(kind).accepted);
    }
}

TEST_CASE("Go to sound command opens the retained chooser with the active context",
          "[sys][application_commands][sound-picker]") {
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;
    const auto cursor = fixture.compose_state->cursor;
    unsigned calls{};
    fixture.application_state.services.open_sound_picker = [&](std::uint64_t project,
        std::uint64_t chart, std::uint64_t expected_revision) {
        ++calls;
        check_bookmark_callback(project == fixture.fixture.history.document.id.value);
        check_bookmark_callback(chart == fixture.compose_state->chart.value);
        check_bookmark_callback(expected_revision == revision);
        return true;
    };
    require_bookmark_callback(fixture.invoke(application_command_kind_t::navigation_go_to_sound).accepted);
    check_bookmark_callback(calls == 1);
    check_bookmark_callback(fixture.fixture.history.document.revision == revision);
    check_bookmark_callback(fixture.compose_state->cursor == cursor);
    fixture.workspace.active_tab = wormedit::workspace_tab_t::metadata;
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::navigation_go_to_sound).accepted);
    check_bookmark_callback(calls == 1);
}

TEST_CASE("Go to sound command rejects a missing or blocked chooser callback",
          "[sys][application_commands][sound-picker]") {
    TimingInsertFixture fixture;
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::navigation_go_to_sound).accepted);
    fixture.application_state.services.open_sound_picker = [](auto, auto, auto) { return false; };
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::navigation_go_to_sound).accepted);
    fixture.application_state.services.open_sound_picker = [](auto, auto, auto) { return true; };
    fixture.compose_state->active_gesture.emplace();
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::navigation_go_to_sound).accepted);
}
#endif

namespace {
void check_transient_transport(bool value) { CHECK(value); }
void require_transient_transport(bool value) { REQUIRE(value); }
}
#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Space and explicit stop commands keep routine feedback out of history", "[commands][notification][transient]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    auto& state = fixture.application_state;
    int stops{};
    state.services.transport_preview_playing = [] { return true; };
    state.services.transport_active = [] { return true; };
    state.services.transport_stop = [&] { ++stops; };
    SECTION("Space hard stop") {
        require_transient_transport(fixture.invoke(application_command_kind_t::transport_play_stop).accepted);
    }
    SECTION("explicit stop") {
        require_transient_transport(fixture.invoke(application_command_kind_t::transport_stop).accepted);
    }
    fixture.center.drain_inbox(notification_time_t{0});
    check_transient_transport(stops == 1);
    check_transient_transport(fixture.center.snapshot(256).empty());
    require_transient_transport(fixture.center.current_line() != nullptr);
    check_transient_transport(fixture.center.current_line()->message == "Audio preview stopped.");
    check_transient_transport(fixture.center.statistics().posted == 0);
}

#endif
#ifndef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Toolbar stop is transient while genuine audio failure remains durable", "[commands][notification][transient]") {
    using namespace wormedit;
    notification_center_t center{[] { return notification_time_t{0}; }};
    notification_sink_t sink{center};
    application_command_state_t state;
    stepmania_export_policy_t policy;
    int stops{};
    state.services.transport_stop = [&] { ++stops; };
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP;
    check_transient_transport(dispatch_transport_player_action(action, state, policy, sink) == "Audio preview stopped.");
    center.drain_inbox(notification_time_t{0});
    check_transient_transport(stops == 1 && center.size() == 0);
    require_transient_transport(center.current_line() != nullptr);
    check_transient_transport(center.current_line()->message == "Audio preview stopped.");
    state.services.transport_stop = {};
    check_transient_transport(dispatch_transport_player_action(action, state, policy, sink) == "Audio preview is unavailable.");
    center.drain_inbox(notification_time_t{0});
    const auto entries = center.snapshot(256);
    require_transient_transport(entries.size() == 1);
    check_transient_transport(entries[0].severity == notification_severity_t::warning);
    check_transient_transport(center.unread_counts().warnings == 1);
}

#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Draw and Layer shortcuts select tools without changing the armed note kind or history",
          "[sys][application_commands][draw]") {
    TimingInsertFixture fixture;
    const auto revision = fixture.fixture.history.document.revision;
    fixture.compose_state->armed_note_kind = wormedit::note_kind_t::hold;
    require_transient_transport(fixture.invoke(application_command_kind_t::edit_tool_draw).accepted);
    check_transient_transport(fixture.application_state.project.compose_gesture.note_paint_mode_active);
    check_transient_transport(!fixture.application_state.project.compose_gesture.semantic_layer_paint_mode_active);
    check_transient_transport(!fixture.invoke(application_command_kind_t::edit_tool_layer).accepted);
    check_transient_transport(fixture.application_state.project.compose_gesture.note_paint_mode_active);
    fixture.fixture.history.document.charts.front().semantic_layers = {
        {.id = wormedit::layer_id_t{7}, .name = "Brush"}};
    fixture.application_state.project.semantic_layer.semantic_layer_target = wormedit::layer_id_t{7};
    require_transient_transport(fixture.invoke(application_command_kind_t::edit_tool_layer).accepted);
    check_transient_transport(!fixture.application_state.project.compose_gesture.note_paint_mode_active);
    check_transient_transport(fixture.application_state.project.compose_gesture.semantic_layer_paint_mode_active);
    require_transient_transport(fixture.invoke(application_command_kind_t::edit_tool_select).accepted);
    check_transient_transport(!fixture.application_state.project.compose_gesture.semantic_layer_paint_mode_active);
    check_transient_transport(fixture.compose_state->armed_note_kind == wormedit::note_kind_t::hold);
    check_transient_transport(fixture.fixture.history.document.revision == revision);
    check_transient_transport(fixture.fixture.history.undo.empty());
}

TEST_CASE("Draw shortcut rejects a retained interaction without editable chart geometry",
          "[sys][application_commands][draw]") {
    TimingInsertFixture fixture;
    auto& history = fixture.fixture.history;
    history.document.charts.front().lane_layouts.clear();
    const auto revision = history.document.revision;
    check_transient_transport(!fixture.invoke(application_command_kind_t::edit_tool_draw).accepted);
    check_transient_transport(!fixture.application_state.project.compose_gesture.note_paint_mode_active);
    check_transient_transport(history.document.revision == revision);
    check_transient_transport(history.undo.empty());
    check_transient_transport(fixture.latest_message() ==
                             "Draw and Layer require editable chart geometry.");
}
#endif

// Placement audition needs the compose input path, which only the frame-timing executable links.
#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
#include "sys/compose_input.h"
#include "sys/compose_view.h"
#include "sys/options_runtime.h"
namespace {
auto click_audition_song() -> wormedit::song_document_t {
    using namespace wormedit;
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 1;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.lane_layouts = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "click-audition", .display_name = "Click audition",
                   .lanes = {{.id = lane_id_t{1}, .name = "Key", .role = lane_role_t::playable}}}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    measure_chunk_t measure;
    measure.id = measure_id_t{1};
    chart.measures.push_back(std::move(measure));
    song.charts.push_back(std::move(chart));
    return song;
}

// Clicking empty playable space in note paint mode places one note; it auditions only when enabled.
void check_placement_audition(bool enabled) {
    using namespace wormedit;
    document_history_t history{document_history_t::from(click_audition_song())};
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.workspace.has_value());
    binding.workspace->active_tab = workspace_tab_t::compose;
    binding.workspace->selected_chart = chart_id_t{1};
    compose_scene_cache_t cache;
    application_command_state_t application;
    application.preferences.editor_preferences.audition_on_click = enabled;
    application.project.compose_gesture.note_paint_mode_active = true;
    std::optional<editor_interaction_state_t> interaction;
    flattened_workspace_t flattened;
    flattened.view.compose.visible = true;
    populate_compose_view(flattened, history, *binding.workspace, cache, 1280, 980, interaction,
                          application, nullptr);
    REQUIRE(flattened.view.compose.scene_available);
    REQUIRE(interaction.has_value());
    const auto& compose = flattened.view.compose;
    const auto& chart = history.document.charts.front();
    const local_position_t position{chart.measures.front().id, 240};
    const auto view = inspect_editor_interaction(history, *interaction);
    const auto y = editor_position_y_pixels(chart, std::get<editor_interaction_view_model_t>(view), position);
    REQUIRE(y.has_value());
    WormUiWorkspaceAction action{};
    action.shortcut_window_focused = true;
    action.compose_input_chart_id = interaction->chart.value;
    action.compose_input_document_revision = history.document.revision;
    action.compose_input_viewport_width_pixels = compose.viewport_width_pixels;
    action.compose_input_count = 1;
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .x_pixels = compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, interaction->viewport_height_pixels,
            compose_field_downscroll(chart, application.preferences.editor_preferences))};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, history, *binding.workspace, *interaction,
        viewport, navigation, application, cache, 0.016);
    REQUIRE(history.document.charts.front().measures.front().notes.size() == 1);
    CHECK(cache.frame.audition_request.has_value() == enabled);
}
} // namespace

TEST_CASE("Placement auditions the placed note only while click audition is on",
          "[sys][application_commands][audition]") {
    check_placement_audition(false);
    check_placement_audition(true);
}

TEST_CASE("Click audition command flips the preference through the program save path",
          "[sys][application_commands][audition]") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "wormedit-t268-click-audition";
    fs::create_directories(dir);
    program_preferences_runtime_t program;
    program.path = dir / "preferences.json";
    bool enabled = program.preferences.editor.audition_on_click;
    REQUIRE(enabled);
    CHECK(toggle_audition_on_click(program, enabled).empty());
    CHECK(!enabled);
    CHECK(!program.preferences.editor.audition_on_click);
    CHECK(fs::exists(program.path));
    CHECK(toggle_audition_on_click(program, enabled).empty());
    CHECK(enabled);
    CHECK(program.preferences.editor.audition_on_click);
    fs::remove_all(dir);
}
#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Move to BGM application commands gate formats and retain rejection selection",
          "[unit][sys][application_commands][t424]") {
    using namespace wormedit;
    TimingInsertFixture fixture;
    auto& chart = fixture.fixture.history.document.charts.front();
    fixture.compose_state->selected_notes = {object_id_t{3}, object_id_t{1}, object_id_t{2}};
    const auto original_selection = fixture.compose_state->selected_notes;
    const auto revision = fixture.fixture.history.document.revision;
    check_layer_command(!fixture.invoke(application_command_kind_t::edit_move_to_bgm).accepted);
    check_layer_command(fixture.fixture.history.document.revision == revision);
    check_layer_command(fixture.compose_state->selected_notes == original_selection);
    chart.source_family = format_family_t::bms;
    const auto moved = fixture.invoke(application_command_kind_t::edit_move_next_to_bgm);
    require_layer_command(moved.accepted);
    check_layer_command(fixture.compose_state->selected_notes == std::vector{object_id_t{3}, object_id_t{2}});
    check_layer_command(fixture.compose_state->selected_objects.empty());
    check_layer_command(fixture.fixture.history.undo.size() == 1);
    check_layer_command(fixture.fixture.history.document.charts.front().measures.front().bgm_events.front().id == object_id_t{1});
    check_layer_command(undo_document_command(fixture.fixture.history));
}

#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Loop selection command passes committed source rows without editing the chart", "[sys][application_commands][loop-selection]") {
    TimingInsertFixture fixture;
    unsigned calls{};
    fixture.application_state.services.transport_set_loop_selection = [&](wormedit::chart_id_t chart,
        std::uint64_t revision, std::uint64_t begin, std::uint64_t end, bool enabled) {
        ++calls;
        check_bookmark_callback(chart == wormedit::chart_id_t{7});
        check_bookmark_callback(revision == 9);
        check_bookmark_callback(begin == 121 && end == 480 && enabled);
        return true;
    };
    require_bookmark_callback(fixture.invoke(application_command_kind_t::transport_loop_selection).accepted);
    check_bookmark_callback(calls == 1);
    check_bookmark_callback(fixture.fixture.history.undo.empty());
    check_bookmark_callback(fixture.fixture.history.document.revision == 9);
}

TEST_CASE("Loop selection command rejects missing invalid and unavailable areas", "[sys][application_commands][loop-selection]") {
    TimingInsertFixture fixture;
    unsigned calls{};
    fixture.application_state.services.transport_set_loop_selection = [&](auto, auto, auto, auto, bool) {
        ++calls;
        return false;
    };
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::transport_loop_selection).accepted);
    check_bookmark_callback(calls == 1);
    fixture.compose_state->retained_selection_range.reset();
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::transport_loop_selection).accepted);
    check_bookmark_callback(calls == 1);
    fixture.compose_state->retained_selection_range = wormedit::position_span_t{
        {wormedit::measure_id_t{1}, 480}, {wormedit::measure_id_t{1}, 121}};
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::transport_loop_selection).accepted);
    check_bookmark_callback(calls == 1);
    fixture.workspace.active_tab = wormedit::workspace_tab_t::metadata;
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::transport_loop_selection).accepted);
    fixture.application_state.services.transport_set_loop_selection = {};
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::transport_loop_selection).accepted);
}
#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Measure split command keeps later heads fixed and cursor on the new head", "[sys][application_commands][measure-split]") {
    TimingInsertFixture fixture;
    const auto result = fixture.invoke(application_command_kind_t::measures_split_at_cursor);
    require_bookmark_callback(result.accepted);
    const auto& chart = fixture.fixture.history.document.charts.front();
    check_bookmark_callback(chart.measures.size() == 3);
    check_bookmark_callback(chart.measures.back().id == wormedit::measure_id_t{2});
    check_bookmark_callback(fixture.compose_state->cursor.row == 0);
    check_bookmark_callback(fixture.compose_state->cursor.measure == chart.measures[1].id);
    check_bookmark_callback(fixture.fixture.history.undo.size() == 1);
    require_bookmark_callback(fixture.invoke(application_command_kind_t::measures_split_at_cursor).accepted);
    check_bookmark_callback(fixture.fixture.history.undo.size() == 1);
}
#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Edit menu Delete stages stem and ordinary removal atomically",
          "[unit][sys][application_commands][mixed-delete][bmson]") {
    using namespace wormedit;
    const auto blocked = GENERATE(false, true);
    TimingInsertFixture f;
    auto& history = f.fixture.history;
    history.document.audio_assets = {{.id = asset_id_t{5}, .stream = {48000, 2, 480000, true}}};
    history.document.resources = {{.id = resource_id_t{6}, .asset = asset_id_t{5}, .roles = {resource_role_t::sample}}};
    auto& chart = history.document.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.measures.front().notes.resize(2);
    auto& stem = chart.measures.front().notes.front();
    stem.sound = resource_id_t{6};
    stem.sound_channel_event = object_id_t{31};
    chart.bgm_source_lanes = {{.id = object_id_t{22}, .stem_channel = object_id_t{30}}};
    chart.sound_channels = {{.id = object_id_t{30}, .source_clip = {.id = clip_id_t{32}, .source = asset_id_t{5}},
        .events = {{.id = object_id_t{31}, .position = stem.position, .lane = stem.lane}}}};
    f.compose_state->selected_notes = {object_id_t{1}, object_id_t{2}};
    f.compose_state->selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}},
                                       {editor_selection_object_kind_t::note, object_id_t{2}}};
    if (blocked) {
        chart.measures.front().bgm_events.push_back({.id = object_id_t{33},
            .position = {measure_id_t{1}, 720}, .source_lane = object_id_t{22},
            .sound = resource_id_t{6}, .sound_channel_event = object_id_t{34}});
        chart.sound_channels.front().events.push_back({.id = object_id_t{34},
            .position = {measure_id_t{1}, 720}, .continuation = true, .playable = false});
        f.compose_state->selected_objects.push_back({editor_selection_object_kind_t::bgm_event, object_id_t{33}});
    }
    require_layer_command(validate_document(history.document).empty());
    const auto before = write_native_project(history.document).bytes;
    const auto selection = f.compose_state->selected_objects;
    const auto result = f.invoke(application_command_kind_t::edit_delete);
    check_layer_command(result.accepted != blocked);
    check_layer_command(history.undo.size() == (blocked ? 0 : 1));
    if (blocked) {
        check_layer_command(write_native_project(history.document).bytes == before);
        check_layer_command(f.compose_state->selected_objects == selection);
    } else {
        check_layer_command(history.document.charts.front().measures.front().notes.empty());
        check_layer_command(history.document.charts.front().measures.front().bgm_events.size() == 1);
        require_layer_command(undo_document_command(history));
        check_layer_command(write_native_project(history.document).bytes == before);
    }
}
#endif

namespace {
void check_measure_minimap_target(const std::optional<compose_minimap_target_t>& target,
                                 unsigned row) {
    REQUIRE(target);
    CHECK(target->snapped_row == (row < 560 ? 320 : 560));
}
}

TEST_CASE("Minimap floor snap restarts at shortened measure heads", "[unit][sys][commands]") {
    AreaFixture fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.measures.front().definition.length = {1, 3};
    chart.measures.front().definition.meter.reset();
    chart.measures.front().notes.clear();
    chart.bpms.resize(1);
    chart.bpms.front().beats_per_minute = {120, 1};
    const auto timing = wormedit::build_timing_index(chart);
    const auto built = wormedit::build_measure_index(chart);
    REQUIRE(timing);
    REQUIRE(std::holds_alternative<wormedit::measure_index_t>(built));
    const auto& measures = std::get<wormedit::measure_index_t>(built);
    for (const auto row : {320U, 350U, 559U, 560U, 600U}) {
        const auto target = compose_minimap_target(1, row * 100 + 1, 48000, 48000,
                                                  *timing.index, measures, 4);
        check_measure_minimap_target(target, row);
    }
    auto stale = measures;
    ++stale.total_rows;
    CHECK_FALSE(compose_minimap_target(1, 35001, 48000, 48000, *timing.index, stale, 4));
}

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Quantize registry commands toggle session preview and preserve Apply Undo", "[sys][application_commands][quantize-intent]") {
    TimingInsertFixture fixture;
    auto& history = fixture.fixture.history;
    auto& note = history.document.charts.front().measures.front().notes.front();
    note.position.row = 130;
    const auto before = note.position;
    fixture.compose_state->selected_notes = {note.id};
    const auto revision = history.document.revision;
    require_bookmark_callback(fixture.invoke(application_command_kind_t::edit_toggle_quantize_preview).accepted);
    check_bookmark_callback(fixture.application_state.project.quantize.quantize_editor->preview_enabled);
    check_bookmark_callback(history.document.revision == revision && history.undo.empty());
    require_bookmark_callback(fixture.invoke(application_command_kind_t::edit_toggle_quantize_preview).accepted);
    check_bookmark_callback(!fixture.application_state.project.quantize.quantize_editor->preview_enabled);
    fixture.application_state.project.quantize.quantize_editor->request.divisions = 4;
    require_bookmark_callback(fixture.invoke(application_command_kind_t::edit_apply_quantize).accepted);
    check_bookmark_callback(history.undo.size() == 1);
    check_bookmark_callback(fixture.application_state.project.quantize.quantize_applied_report.has_value());
    require_bookmark_callback(fixture.invoke(application_command_kind_t::history_undo).accepted);
    check_bookmark_callback(history.document.charts.front().measures.front().notes.front().position == before);
    fixture.workspace.active_tab = wormedit::workspace_tab_t::metadata;
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::edit_toggle_quantize_preview).accepted);
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::edit_apply_quantize).accepted);
}

#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Quantize command rejects collisions with preview off and rejects timing focus", "[sys][application_commands][quantize-intent]") {
    TimingInsertFixture fixture;
    auto& history = fixture.fixture.history;
    auto& notes = history.document.charts.front().measures.front().notes;
    notes[0].position.row = 130;
    notes[1].position.row = 240;
    const auto revision = history.document.revision;
    fixture.compose_state->selected_notes = {notes[0].id};
    fixture.compose_state->selected_objects = {{wormedit::editor_selection_object_kind_t::note, notes[0].id}};
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::edit_apply_quantize).accepted);
    check_bookmark_callback(history.document.revision == revision && history.undo.empty());
    check_bookmark_callback(!fixture.application_state.project.quantize.quantize_editor->preview_enabled);
    fixture.application_state.project.compose_view.compose_timing_focus = true;
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::edit_toggle_quantize_preview).accepted);
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::edit_apply_quantize).accepted);
}
#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Project export command opens from every project tab without Compose dock routing",
          "[sys][application_commands][project-access]") {
    TimingInsertFixture fixture;
    unsigned calls{};
    fixture.application_state.services.compose_dock_command = {};
    fixture.application_state.services.open_project_export = [&](auto project, auto chart, auto revision) {
        ++calls;
        check_bookmark_callback(project == fixture.fixture.history.document.id.value);
        check_bookmark_callback(chart == fixture.workspace.selected_chart->value);
        check_bookmark_callback(revision == fixture.fixture.history.document.revision);
        return true;
    };
    for (const auto tab : {wormedit::workspace_tab_t::setup, wormedit::workspace_tab_t::metadata,
                           wormedit::workspace_tab_t::compose, wormedit::workspace_tab_t::audio_slices,
                           wormedit::workspace_tab_t::compare_validate}) {
        fixture.workspace.active_tab = tab;
        require_bookmark_callback(fixture.invoke(application_command_kind_t::document_export_output_group).accepted);
    }
    check_bookmark_callback(calls == 5);
    fixture.compose_state->active_gesture.emplace();
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::document_export_output_group).accepted);
    fixture.compose_state->active_gesture.reset();
    fixture.application_state.services.open_project_export = {};
    check_bookmark_callback(!fixture.invoke(application_command_kind_t::document_export_output_group).accepted);
}

#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
namespace {
void selection_require(bool value) { REQUIRE(value); }
void selection_check(bool value) { CHECK(value); }
auto invoke_selection_command(TimingInsertFixture& f, std::string id, bool modal = false) -> wormedit::command_dispatch_result_t {
    using namespace wormedit;
    const auto registry = command_registry_t::from([&](const command_invocation_context_t& context) {
        const auto parsed = parse_application_command(context.command.value);
        if (!parsed) return command_invocation_result_t{};
        return f.invoke(parsed->kind);
    });
    return invoke_command(registry, {std::move(id)}, {.modal_active = modal, .workspace_active = true,
        .active_tab = workspace_tab_t::compose, .visible_tabs = {workspace_tab_t::compose}});
}
void add_selection_spans(TimingInsertFixture& f) {
    using namespace wormedit;
    auto& chart = f.fixture.history.document.charts.front();
    chart.kiai = {{.id = object_id_t{200}, .position = {measure_id_t{1}, 240}, .enabled = true},
                  {.id = object_id_t{201}, .position = {measure_id_t{1}, 480}, .enabled = false}};
    chart.sv_recipes = {{.id = recipe_id_t{300}, .name = "Selection recipe",
        .range = {{measure_id_t{1}, 0}, {measure_id_t{1}, 480}}, .output = sv_recipe_output_t::sv,
        .curve = sv_recipe_curve_t::linear, .parameters = {.value_from = {1, 1}, .value_to = {2, 1}}}};
    f.application_state.project.timing_recipe.kiai_selected_head = 200;
}
}

TEST_CASE("Registered Delete removes mixed notes timing Kiai and recipe with one Undo",
          "[sys][application_commands][gesture-delete]") {
    using namespace wormedit;
    TimingInsertFixture f;
    add_selection_spans(f);
    f.application_state.project.timing_recipe.recipe_selected = 300;
    f.compose_state->selected_objects.push_back({editor_selection_object_kind_t::bpm_event, object_id_t{102}});
    const auto original = write_native_project(f.fixture.history.document).bytes;
    selection_require(original.has_value());
    selection_require(static_cast<bool>(invoke_selection_command(f, "edit.delete")));
    const auto& chart = f.fixture.history.document.charts.front();
    selection_check(chart.measures.front().notes.size() == 2);
    selection_check(chart.bpms.size() == 1);
    selection_check(chart.kiai.empty());
    selection_check(chart.sv_recipes.empty());
    selection_check(f.application_state.project.timing_recipe.kiai_selected_head == 0);
    selection_check(f.application_state.project.timing_recipe.recipe_selected == 0);
    selection_check(f.fixture.history.undo.size() == 1);
    selection_require(undo_document_command(f.fixture.history));
    selection_check(write_native_project(f.fixture.history.document).bytes == original);
}

TEST_CASE("Registered Cut rejects a late stem deletion without publishing timing or clipboard",
          "[sys][application_commands][gesture-delete]") {
    using namespace wormedit;
    TimingInsertFixture f;
    add_selection_spans(f);
    auto& history = f.fixture.history;
    auto& chart = history.document.charts.front();
    chart.source_family = format_family_t::bmson;
    history.document.audio_assets = {{.id = asset_id_t{5}, .stream = {48000, 2, 480000, true}}};
    history.document.resources = {{.id = resource_id_t{6}, .asset = asset_id_t{5}, .roles = {resource_role_t::bgm}}};
    chart.bgm_source_lanes = {{.id = object_id_t{22}, .stem_channel = object_id_t{30}}};
    chart.sound_channels = {{.id = object_id_t{30}, .name = "Stem",
        .source_clip = {.id = clip_id_t{32}, .source = asset_id_t{5}, .end_frame = 480000},
        .events = {{.id = object_id_t{31}, .position = {measure_id_t{1}, 0}, .playable = false},
                   {.id = object_id_t{34}, .position = {measure_id_t{1}, 720}, .continuation = true, .playable = false}}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{33}, .position = {measure_id_t{1}, 0},
        .source_lane = object_id_t{22}, .sound = resource_id_t{6}, .sound_channel_event = object_id_t{31}},
        {.id = object_id_t{35}, .position = {measure_id_t{1}, 720}, .source_lane = object_id_t{22},
         .sound = resource_id_t{6}, .sound_channel_event = object_id_t{34}}};
    f.compose_state->selected_notes.clear();
    f.compose_state->selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{33}},
        {editor_selection_object_kind_t::bgm_event, object_id_t{35}},
        {editor_selection_object_kind_t::bpm_event, object_id_t{102}}};
    f.application_state.project.clipboard.chart_clipboard = chart_clipboard_t{song_id_t{999}, chart_id_t{999}, {}};
    selection_require(validate_document(history.document).empty());
    auto metadata_edit = history.document;
    metadata_edit.metadata.title = "Redo must survive rejection";
    selection_require(static_cast<bool>(commit_document_command(history, std::move(metadata_edit), "Metadata edit")));
    selection_require(undo_document_command(history));
    selection_require(history.redo.size() == 1);
    const auto redo_bytes = write_native_project(history.redo.front().after).bytes;
    const auto original = write_native_project(history.document).bytes;
    const auto selected = f.compose_state->selected_objects;
    const auto next_revision = history.next_revision;
    selection_check(!invoke_selection_command(f, "edit.cut"));
    selection_check(write_native_project(history.document).bytes == original);
    selection_check(history.undo.empty());
    selection_check(history.next_revision == next_revision);
    selection_require(history.redo.size() == 1);
    selection_check(write_native_project(history.redo.front().after).bytes == redo_bytes);
    selection_check(f.compose_state->selected_objects == selected);
    selection_check(f.application_state.project.timing_recipe.kiai_selected_head == 200);
    selection_require(f.application_state.project.clipboard.chart_clipboard.has_value());
    selection_check(f.application_state.project.clipboard.chart_clipboard->source_song == song_id_t{999});
}
#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Registered Delete handles a span-only Kiai or recipe selection",
          "[sys][application_commands][gesture-delete]") {
    using namespace wormedit;
    const bool recipe = GENERATE(false, true);
    TimingInsertFixture f;
    add_selection_spans(f);
    f.compose_state->selected_notes.clear(); f.compose_state->selected_objects.clear();
    f.application_state.project.timing_recipe.kiai_selected_head = recipe ? 0 : 200;
    f.application_state.project.timing_recipe.recipe_selected = recipe ? 300 : 0;
    const auto original = write_native_project(f.fixture.history.document).bytes;
    selection_require(static_cast<bool>(invoke_selection_command(f, "edit.delete")));
    const auto& chart = f.fixture.history.document.charts.front();
    selection_check(chart.kiai.empty() == !recipe);
    selection_check(chart.sv_recipes.empty() == recipe);
    selection_check(chart.measures.front().notes.size() == 3);
    selection_check(f.fixture.history.undo.size() == 1);
    selection_require(undo_document_command(f.fixture.history));
    selection_check(write_native_project(f.fixture.history.document).bytes == original);
}

TEST_CASE("Registered Cut publishes mixed copied objects with one Undo and Redo",
          "[sys][application_commands][gesture-delete]") {
    using namespace wormedit;
    TimingInsertFixture f;
    add_selection_spans(f);
    f.compose_state->selected_objects.push_back({editor_selection_object_kind_t::bpm_event, object_id_t{102}});
    const auto original = write_native_project(f.fixture.history.document).bytes;
    selection_require(static_cast<bool>(invoke_selection_command(f, "edit.cut")));
    selection_require(f.application_state.project.clipboard.chart_clipboard.has_value());
    selection_check(f.application_state.project.clipboard.chart_clipboard->objects.size() == 4);
    selection_check(f.application_state.project.timing_recipe.kiai_selected_head == 0);
    const auto& chart = f.fixture.history.document.charts.front();
    selection_check(chart.measures.front().notes.size() == 2);
    selection_check(chart.bpms.size() == 1);
    selection_check(chart.kiai.empty());
    selection_check(chart.sv_recipes.size() == 1);
    selection_check(f.fixture.history.undo.size() == 1);
    const auto cut = write_native_project(f.fixture.history.document).bytes;
    selection_require(undo_document_command(f.fixture.history));
    selection_check(write_native_project(f.fixture.history.document).bytes == original);
    selection_require(redo_document_command(f.fixture.history));
    selection_check(write_native_project(f.fixture.history.document).bytes == cut);
}

TEST_CASE("Registered Delete and Cut reject locked mixed selection atomically",
          "[sys][application_commands][gesture-delete]") {
    using namespace wormedit;
    const auto id = GENERATE("edit.delete", "edit.cut");
    TimingInsertFixture f;
    add_selection_spans(f);
    f.application_state.project.timing_recipe.timing_locked = true;
    f.application_state.project.clipboard.chart_clipboard = chart_clipboard_t{song_id_t{999}, chart_id_t{999}, {}};
    f.compose_state->selected_objects.push_back({editor_selection_object_kind_t::bpm_event, object_id_t{102}});
    const auto original = write_native_project(f.fixture.history.document).bytes;
    const auto selection = f.compose_state->selected_objects;
    selection_check(!invoke_selection_command(f, id));
    selection_check(write_native_project(f.fixture.history.document).bytes == original);
    selection_check(f.fixture.history.undo.empty());
    selection_check(f.compose_state->selected_objects == selection);
    selection_check(f.application_state.project.timing_recipe.kiai_selected_head == 200);
    selection_check(f.application_state.project.clipboard.chart_clipboard->source_song == song_id_t{999});
}

TEST_CASE("Registered Cut rejects unsupported recipe clipboard without dropping selected notes",
          "[sys][application_commands][gesture-delete]") {
    using namespace wormedit;
    TimingInsertFixture f;
    add_selection_spans(f);
    f.application_state.project.timing_recipe.recipe_selected = 300;
    const auto original = write_native_project(f.fixture.history.document).bytes;
    selection_check(!invoke_selection_command(f, "edit.cut"));
    selection_check(f.latest_message().find("SV recipes cannot be copied") != std::string::npos);
    selection_check(write_native_project(f.fixture.history.document).bytes == original);
    selection_check(f.fixture.history.undo.empty());
    selection_check(f.application_state.project.timing_recipe.recipe_selected == 300);
}
#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Registered menu verbs reject paste and pending gestures atomically", "[sys][gesture-verbs]") {
    using namespace wormedit;
    const auto id = GENERATE("edit.apply_quantize", "markers.add_bookmark", "selection.select_same_sound_chart",
        "edit.move_to_bgm", "transport.loop_selection", "transport.toggle_loop_selection",
        "measures.insert_before", "measures.insert_after", "measures.remove_empty",
        "measures.split_at_cursor");
    const int owner = GENERATE(0, 1, 2);
    TimingInsertFixture f;
    f.fixture.history.document.charts.front().source_family = format_family_t::bms;
    unsigned calls{};
    f.application_state.services.transport_set_loop_selection = [&](auto, auto, auto, auto, bool) { ++calls; return true; };
    if (owner == 1) f.application_state.project.clipboard.paste_session = application_command_state_t::paste_session_t{};
    else if (owner == 0) f.compose_state->active_gesture.emplace();
    const auto original = write_native_project(f.fixture.history.document).bytes;
    const auto selected = f.compose_state->selected_notes;
    selection_check(!static_cast<bool>(invoke_selection_command(f, id, owner == 2)));
    selection_check(write_native_project(f.fixture.history.document).bytes == original);
    selection_check(f.compose_state->selected_notes == selected);
    selection_check(f.fixture.history.undo.empty());
    selection_check(calls == 0);
}

TEST_CASE("Registered Quantize uses panel settings rather than the active snap fallback", "[sys][gesture-verbs]") {
    TimingInsertFixture f;
    auto& history = f.fixture.history;
    f.compose_state->active_snap_denominator = 4;
    f.application_state.project.quantize.quantize_editor = application_command_state_t::quantize_editor_t{
        .stamp = wormedit::chart_stamp_t::from(history.document, f.compose_state->chart), .request = {.divisions = 8}};
    selection_require(static_cast<bool>(invoke_selection_command(f, "edit.apply_quantize")));
    selection_check(history.document.charts.front().measures.front().notes.front().position.row == 120);
    selection_check(history.undo.size() == 1);
    selection_require(undo_document_command(history));
    selection_check(history.document.charts.front().measures.front().notes.front().position.row == 121);
}

TEST_CASE("Registered bookmark Add snaps the playing cursor and deduplicates", "[sys][gesture-verbs]") {
    TimingInsertFixture f;
    f.application_state.services.transport_preview_playing = [] { return true; };
    f.application_state.project.transport.preview_playhead_absolute_row = 241.0;
    selection_require(static_cast<bool>(invoke_selection_command(f, "markers.add_bookmark")));
    const auto& bookmarks = f.fixture.history.document.charts.front().bookmarks;
    selection_require(bookmarks.size() == 1);
    selection_check(bookmarks.front().position->row == 240);
    selection_check(bookmarks.front().label.empty());
    const auto revision = f.fixture.history.document.revision;
    (void)invoke_selection_command(f, "markers.add_bookmark");
    selection_check(f.fixture.history.document.revision == revision);
    selection_check(f.fixture.history.undo.size() == 1);
}

TEST_CASE("Registered loop toggle disables without a retained area and shares rejection guards", "[sys][gesture-verbs]") {
    TimingInsertFixture f;
    f.compose_state->retained_selection_range.reset();
    f.application_state.services.transport_loop_selection_active = [] { return true; };
    unsigned calls{};
    f.application_state.services.transport_set_loop_selection = [&](auto chart, auto revision, auto begin, auto end, bool enabled) {
        ++calls;
        selection_check(chart == f.compose_state->chart && revision == f.fixture.history.document.revision);
        selection_check(!enabled && begin == 0 && end == 0);
        return true;
    };
    selection_require(static_cast<bool>(invoke_selection_command(f, "transport.toggle_loop_selection")));
    selection_check(calls == 1);
    selection_check(!static_cast<bool>(invoke_selection_command(f, "transport.loop_selection")));
    selection_check(calls == 1);
}

TEST_CASE("Registered measure insert shares Split guards and retains one Undo", "[sys][gesture-verbs]") {
    TimingInsertFixture f;
    const auto before = wormedit::write_native_project(f.fixture.history.document).bytes;
    selection_require(static_cast<bool>(invoke_selection_command(f, "measures.insert_before")));
    selection_check(f.fixture.history.document.charts.front().measures.size() == 3);
    selection_check(f.fixture.history.undo.size() == 1);
    selection_require(wormedit::undo_document_command(f.fixture.history));
    selection_check(wormedit::write_native_project(f.fixture.history.document).bytes == before);
}

TEST_CASE("Registered BGM move consumes chart order and rejects Timing focus", "[sys][gesture-verbs]") {
    using namespace wormedit;
    TimingInsertFixture f;
    f.fixture.history.document.charts.front().source_family = format_family_t::bms;
    f.compose_state->selected_notes = {object_id_t{3}, object_id_t{1}, object_id_t{2}};
    f.application_state.project.compose_view.compose_timing_focus = true;
    selection_check(!static_cast<bool>(invoke_selection_command(f, "edit.move_to_bgm")));
    f.application_state.project.compose_view.compose_timing_focus = false;
    for (const auto id : {1ULL, 2ULL, 3ULL}) {
        selection_require(static_cast<bool>(invoke_selection_command(f, "edit.move_next_to_bgm")));
        selection_check(f.fixture.history.document.charts.front().measures.front().bgm_events.back().id == object_id_t{id});
    }
    selection_check(f.compose_state->selected_notes.empty());
    selection_check(f.fixture.history.undo.size() == 3);
}
#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Project reset preserves display-derived scratch presentation", "[sys][ownership]") {
    application_command_state_t state;
    wormedit::display_preferences_t display;
    state.scratch_on_right = resolve_scratch_positions(display, {true, false});
    reset_application_project_edit_state(state);
    check_transient_transport(state.scratch_on_right[0] == true);
    check_transient_transport(state.scratch_on_right[1] == false);
    // Both project replacement and recovery use this same reset boundary.
    reset_application_project_edit_state(state);
    check_transient_transport(state.scratch_on_right[0] == true);
    check_transient_transport(state.scratch_on_right[1] == false);
}

TEST_CASE("Project reset retains application services in place", "[sys][ownership]") {
    application_command_state_t state;
    auto* services = &state.services;
    unsigned calls{};
    state.services.transport_stop = [&calls] { ++calls; };
    reset_application_project_edit_state(state);
    require_transient_transport(static_cast<bool>(state.services.transport_stop));
    state.services.transport_stop();
    check_transient_transport(calls == 1);
    check_transient_transport(&state.services == services);
}
#endif

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY
TEST_CASE("Project reset retains exactly the session preference allowlist", "[sys][ownership]") {
    application_command_state_t state;
    state.preferences.editor_preferences.audition_on_click = true;
    state.preferences.minimap_click_moves_paused_playhead = true;
    state.preferences.transport_rate_index = 3;
    state.preferences.transport_keep_pitch = true;
    state.preferences.transport_keep_pitch_available = true;
    wormedit::sys::metronome_t metronome;
    state.metronome = &metronome;
    metronome.settings.enabled = true;
    metronome.settings.available = true;
    state.preferences.timing_settings_visible = true;
    metronome.settings.volume = 42;
    metronome.settings.count_in = 4;
    reset_application_project_edit_state(state);
    check_transient_transport(state.metronome == &metronome);
    check_transient_transport(state.preferences.editor_preferences.audition_on_click);
    check_transient_transport(state.preferences.minimap_click_moves_paused_playhead);
    check_transient_transport(state.preferences.transport_rate_index == 3);
    check_transient_transport(state.preferences.transport_keep_pitch);
    check_transient_transport(state.preferences.transport_keep_pitch_available);
    check_transient_transport(state.metronome_settings().enabled);
    check_transient_transport(state.metronome_settings().available);
    check_transient_transport(state.preferences.timing_settings_visible);
    check_transient_transport(state.metronome_settings().volume == 42);
    check_transient_transport(state.metronome_settings().count_in == 4);
}

TEST_CASE("Project reset keeps formerly reset view settings project scoped", "[sys][ownership]") {
    application_command_state_t state;
    auto& view = state.project.compose_view;
    view.edit_snap_enabled = false;
    view.y_lock = true;
    view.compose_timing_focus = true;
    view.compose_timing_lane_visibility_mask = 1;
    view.player_preview_visible = true;
    view.player_preview_skin = wormedit::player_preview_skin_t::mania;
    view.compose_editor_right_pixels = 720;
    view.requested_snap_denominators.emplace(7, 8);
    view.bgm_horizontal_scroll_pixels.emplace(7, 120);
    state.project.transport.preview_playhead_absolute_row = 240;
    reset_application_project_edit_state(state);
    check_transient_transport(view.edit_snap_enabled);
    check_transient_transport(!view.y_lock);
    check_transient_transport(!view.compose_timing_focus);
    check_transient_transport(view.compose_timing_lane_visibility_mask == 0xff);
    check_transient_transport(!view.player_preview_visible);
    check_transient_transport(view.player_preview_skin == wormedit::player_preview_skin_t::neutral);
    check_transient_transport(view.compose_editor_right_pixels == 0);
    check_transient_transport(view.requested_snap_denominators.empty());
    check_transient_transport(view.bgm_horizontal_scroll_pixels.empty());
    check_transient_transport(!state.project.transport.preview_playhead_absolute_row);
}
#endif

namespace {
wormedit::rational_t catalog_requested_rate;
auto accept_catalog_rate(wormedit::rational_t rate) -> bool {
    catalog_requested_rate = rate;
    return true;
}
void check_catalog_rate_dispatch(const wormedit::audio::playback_rate_descriptor_t& entry) {
    application_command_state_t state;
    state.services.transport_set_playback_rate = accept_catalog_rate;
    catalog_requested_rate = {};
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE;
    action.transport_rate_index = 255; // A contradictory legacy position must not control dispatch.
    action.transport_rate_id = static_cast<std::uint32_t>(entry.id);
    wormedit::stepmania_export_policy_t policy;
    const auto status = dispatch_transport_player_action(action, state, policy,
                                                         wormedit::notification_sink_t::muted());
    CHECK((status && *status == "Playback rate changed."));
    CHECK(catalog_requested_rate == entry.rate);
    CHECK(state.preferences.transport_rate_index == static_cast<std::size_t>(entry.id));
}
} // namespace

TEST_CASE("Transport UI dispatch uses stable playback catalog ids", "[sys][catalog-wiring]") {
    for (const auto& entry : wormedit::audio::playback_rate_catalog)
        check_catalog_rate_dispatch(entry);
}

TEST_CASE("Transport UI dispatch rejects unknown playback catalog ids", "[sys][catalog-wiring]") {
    application_command_state_t state;
    state.services.transport_set_playback_rate = accept_catalog_rate;
    catalog_requested_rate = {};
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE;
    action.transport_rate_id = 255;
    wormedit::stepmania_export_policy_t policy;
    const auto status = dispatch_transport_player_action(action, state, policy,
                                                         wormedit::notification_sink_t::muted());
    CHECK((status && *status == "Playback rate unchanged."));
    CHECK(catalog_requested_rate == wormedit::rational_t{});
}

#ifdef WORMEDIT_TEST_APPLICATION_COMMAND_DISPATCH_ONLY

namespace {
auto native_edit_binding_history() -> wormedit::document_history_t {
    using namespace wormedit;
    return document_history_t::from({.id = song_id_t{1},
                                     .revision = 4,
                                     .charts = {{.id = chart_id_t{2},
                                                 .divisions_per_standard_measure = 960,
                                                 .measures = {{.id = measure_id_t{3}}},
                                                 .bpms = {{.id = object_id_t{10},
                                                           .position = {measure_id_t{3}, 0},
                                                           .beats_per_minute = {120, 1}}}}}});
}
constexpr compose_action_description_t native_edit_binding_description{
    .success = "Changed.", .unchanged = "Unchanged.", .rebind_failure = "Rebind failed."};

void check_rejected_native_edit(const compose_action_outcome_t& outcome) {
    CHECK_FALSE(outcome.accepted);
    CHECK_FALSE(outcome.mutated);
    CHECK_FALSE(outcome.diagnostics.empty());
}

void check_unpublished_native_edit(const wormedit::document_history_t& history,
                                    const std::optional<std::string>& before,
                                    std::uint64_t revision) {
    CHECK(wormedit::write_native_project(history.document).bytes == before);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
}

auto insert_native_binding_bookmark(wormedit::document_history_t& history,
                                     const wormedit::marker_edit_action_t& edit)
    -> wormedit::object_id_t {
    using namespace wormedit;
    REQUIRE(run_native_chart_edit<marker_edit_action_t>(history, edit, native_edit_binding_description)
                .mutated);
    REQUIRE(history.document.charts.front().bookmarks.size() == 1);
    REQUIRE(history.undo.size() == 1);
    CHECK(history.undo.back().label == "Insert bookmark");
    return history.document.charts.front().bookmarks.front().id;
}

void update_native_binding_bookmark(wormedit::document_history_t& history,
                                     wormedit::object_id_t id, wormedit::local_position_t position) {
    using namespace wormedit;
    REQUIRE(run_native_chart_edit<marker_edit_action_t>(
                history, update_edit_t<bookmark_edit_value_t>{.chart = chart_id_t{2}, .id = id,
                    .value = {.label = "Renamed", .position = position}}, native_edit_binding_description)
                .mutated);
    REQUIRE(history.undo.size() == 2);
    CHECK(history.undo.back().label == "Update bookmark");
    CHECK(history.document.charts.front().bookmarks.front().label == "Renamed");
}

void delete_native_binding_bookmark(wormedit::document_history_t& history, wormedit::object_id_t id) {
    using namespace wormedit;
    REQUIRE(run_native_chart_edit<marker_edit_action_t>(
                history, delete_edit_t<bookmark_edit_value_t>{.chart = chart_id_t{2}, .id = id},
                native_edit_binding_description)
                .mutated);
    CHECK(history.document.charts.front().bookmarks.empty());
    REQUIRE(history.undo.size() == 3);
    CHECK(history.undo.back().label == "Delete bookmark");
}

void check_native_binding_marker_undo(wormedit::document_history_t& history) {
    using namespace wormedit;
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bookmarks.front().label == "Renamed");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bookmarks.front().label == std::string(160, 'x'));
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bookmarks.empty());
}
} // namespace

TEMPLATE_TEST_CASE("Native chart action bindings reject invalid targets without publishing",
                   "[sys][native-chart-edit]", wormedit::bpm_edit_action_t,
                   wormedit::marker_edit_action_t, wormedit::pause_warp_edit_action_t,
                   wormedit::scroll_speed_edit_action_t, wormedit::timing_anchor_edit_action_t,
                   wormedit::sv_recipe_edit_action_t, wormedit::semantic_layer_edit_action_t,
                   wormedit::lane_layout_action_t) {
    auto history = native_edit_binding_history();
    const auto before = wormedit::write_native_project(history.document).bytes;
    REQUIRE(before.has_value());
    REQUIRE_FALSE(before->empty());
    const auto revision = history.document.revision;
    const TestType edit{};
    const auto outcome =
        run_native_chart_edit<TestType>(history, edit, native_edit_binding_description);
    check_rejected_native_edit(outcome);
    check_unpublished_native_edit(history, before, revision);
}

TEMPLATE_TEST_CASE("Captured native chart action bindings reject stale transactions before copying",
                   "[sys][native-chart-edit]", wormedit::bpm_edit_action_t,
                   wormedit::marker_edit_action_t, wormedit::pause_warp_edit_action_t,
                   wormedit::scroll_speed_edit_action_t, wormedit::timing_anchor_edit_action_t,
                   wormedit::sv_recipe_edit_action_t) {
    using namespace wormedit;
    auto history = native_edit_binding_history();
    const auto stamp = document_stamp_t::from(history.document);
    ++history.document.revision;
    const auto before = wormedit::write_native_project(history.document).bytes;
    REQUIRE(before.has_value());
    REQUIRE_FALSE(before->empty());
    const auto revision = history.document.revision;
    auto transaction = document_command_transaction_t::from(history, stamp, "Native edit");
    const TestType edit{};
    const auto outcome =
        run_native_chart_edit<TestType>(transaction, edit, native_edit_binding_description);
    check_rejected_native_edit(outcome);
    CHECK_FALSE(transaction.has_copy());
    wormedit::test::check_stale_action_message(outcome.diagnostics, "Native edit");
    check_unpublished_native_edit(history, before, revision);
}

TEST_CASE("Native chart action bindings borrow labels and keep one-step marker history",
          "[sys][native-chart-edit]") {
    using namespace wormedit;
    auto history = native_edit_binding_history();
    const auto initial_revision = history.document.revision;
    const local_position_t position{measure_id_t{3}, 240};
    const marker_edit_action_t insert{insert_edit_t<bookmark_edit_value_t>{
        .chart = chart_id_t{2}, .value = {.label = std::string(160, 'x'), .position = position}}};
    const auto& label = std::get<insert_edit_t<bookmark_edit_value_t>>(insert).value.label;
    const auto* original_buffer = label.data();
    const auto id = insert_native_binding_bookmark(history, insert);
    CHECK(label.data() == original_buffer);
    CHECK(label == std::string(160, 'x'));
    update_native_binding_bookmark(history, id, position);
    delete_native_binding_bookmark(history, id);
    CHECK(history.document.revision == initial_revision + 3);
    check_native_binding_marker_undo(history);
}

#endif
