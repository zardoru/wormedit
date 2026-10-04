#include "sys/sound_usage_actions.h"

#include <wormedit/core/lane_profiles.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

namespace {
using namespace wormedit;
using namespace wormedit::sys;

auto usage_history() -> document_history_t {
    auto profile = make_bms_5k_profile();
    chart_document_t chart{
        .id = chart_id_t{7},
        .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}}},
        .lane_layouts = {{.id = object_id_t{5},
                          .position = {measure_id_t{1}, 0},
                          .layout = profile.layout}},
        .sound_channels = {
            {.id = object_id_t{30},
             .name = "Voice",
             .source_clip = {.id = clip_id_t{31}, .source = asset_id_t{10}, .begin_frame = 0}}}};
    chart.measures.front().notes = {{.id = object_id_t{11},
                                     .position = {measure_id_t{1}, 240},
                                     .lane = profile.layout.lanes.front().id,
                                     .sound = resource_id_t{20}}};
    song_document_t song{.id = song_id_t{1}, .revision = 4, .charts = {chart}};
    song.audio_assets = {{.id = asset_id_t{10}}};
    song.resources = {{.id = resource_id_t{20},
                       .name = "Voice",
                       .asset = asset_id_t{10},
                       .roles = {resource_role_t::sample}}};
    return make_document_history(std::move(song));
}

auto usage_action(const document_history_t& history, bool source_row = false)
    -> WormUiWorkspaceAction {
    const auto usage = project_sound_usage(history.document, resource_id_t{20});
    const auto kind =
        source_row ? sound_usage_kind_t::sound_channel_source : sound_usage_kind_t::note;
    const auto found = std::ranges::find(usage.rows, kind, &sound_usage_row_t::kind);
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE;
    if (found == usage.rows.end())
        return action;
    action.sound_usage_song_id = usage.song.value;
    action.sound_usage_document_revision = usage.revision;
    action.sound_usage_resource_id = usage.resource.value;
    action.sound_usage_asset_id = usage.asset.value;
    action.sound_usage_chart_id = found->chart.value;
    action.sound_usage_kind = static_cast<int>(found->kind);
    action.sound_usage_scope = static_cast<int>(found->scope);
    action.sound_usage_object_id = found->object.value;
    action.sound_usage_channel_id = found->channel.value;
    action.sound_usage_clip_id = found->clip.value;
    action.sound_usage_referenced_resource_id = found->referenced_resource.value;
    action.sound_usage_referenced_asset_id = found->referenced_asset.value;
    action.sound_usage_position_valid = found->position.has_value();
    action.sound_usage_measure_id = found->position ? found->position->measure.value : 0;
    action.sound_usage_row = found->position ? found->position->row : 0;
    return action;
}

auto valid_request() -> WormUiSoundUsageRequest {
    return {.selected_project_id = 1, .selected_resource_id = 20, .find_uses_open = true};
}

auto audio_workspace(const song_document_t& document) -> std::optional<workspace_controller_t> {
    auto bound = make_workspace_controller(document);
    if (!bound)
        return std::nullopt;
    bound.workspace->active_tab = workspace_tab_t::audio_slices;
    return std::move(bound.workspace);
}

void check_document_unchanged(const document_history_t& history, std::uint64_t revision,
                              std::uint64_t undo_depth, std::size_t note_count) {
    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().measures.front().notes.size() == note_count);
    CHECK(history.undo.size() == undo_depth);
}

void check_navigation_unchanged(const workspace_controller_t& workspace,
                                const editor_interaction_state_t& interaction,
                                const workspace_controller_t& original_workspace,
                                const editor_interaction_state_t& original_interaction) {
    CHECK(workspace.selected_chart == original_workspace.selected_chart);
    CHECK(workspace.active_tab == original_workspace.active_tab);
    CHECK(interaction.cursor == original_interaction.cursor);
}

void check_rejection_unchanged(sound_usage_action_result_t result,
                               const document_history_t& history, std::uint64_t revision,
                               std::uint64_t undo_depth, std::size_t note_count,
                               const workspace_controller_t& workspace,
                               const editor_interaction_state_t& interaction,
                               const workspace_controller_t& original_workspace,
                               const editor_interaction_state_t& original_interaction) {
    CHECK_FALSE(result.accepted);
    check_document_unchanged(history, revision, undo_depth, note_count);
    check_navigation_unchanged(workspace, interaction, original_workspace, original_interaction);
}

void verify_positioned_workspace(const sound_usage_action_result_t& result,
                                 const workspace_controller_t& workspace) {
    CHECK_FALSE(result.source_selection.has_value());
    CHECK(workspace.selected_chart == chart_id_t{7});
    CHECK(workspace.active_tab == workspace_tab_t::compose);
}

void verify_positioned_cursor(const std::optional<editor_interaction_state_t>& interaction) {
    REQUIRE(interaction.has_value());
    CHECK(interaction->cursor == (local_position_t{measure_id_t{1}, 240}));
}

void verify_document_addresses(const document_history_t& history,
                               const song_document_t* document_address,
                               const chart_document_t* chart_storage, const note_t* note_storage) {
    CHECK(history.document.revision == 4);
    CHECK(&history.document == document_address);
    CHECK(history.document.charts.data() == chart_storage);
    CHECK(history.document.charts.front().measures.front().notes.data() == note_storage);
}

void verify_positioned_navigation(const sound_usage_action_result_t& result,
                                  const document_history_t& history,
                                  const workspace_controller_t& workspace,
                                  const std::optional<editor_interaction_state_t>& interaction,
                                  const song_document_t* document_address,
                                  const chart_document_t* chart_storage,
                                  const note_t* note_storage) {
    verify_positioned_workspace(result, workspace);
    verify_positioned_cursor(interaction);
    verify_document_addresses(history, document_address, chart_storage, note_storage);
}

void verify_source_identity(const sound_usage_action_result_t& result) {
    REQUIRE(result.source_selection.has_value());
    CHECK(result.source_selection->project_id == 1);
    CHECK(result.source_selection->resource_id == 20);
    CHECK(result.source_selection->channel_id == 30);
    CHECK(result.source_selection->clip_id == 31);
}

void verify_source_workspace(const workspace_controller_t& workspace) {
    CHECK(workspace.selected_chart == chart_id_t{7});
    CHECK(workspace.active_tab == workspace_tab_t::audio_slices);
    CHECK(workspace.transport.audio_asset == asset_id_t{10});
}

void verify_source_cursor(const std::optional<editor_interaction_state_t>& interaction,
                          local_position_t cursor) {
    REQUIRE(interaction.has_value());
    CHECK(interaction->cursor == cursor);
}

void verify_source_document(const document_history_t& history,
                            const song_document_t* document_address,
                            const chart_document_t* chart_storage) {
    CHECK(history.document.revision == 4);
    CHECK(&history.document == document_address);
    CHECK(history.document.charts.data() == chart_storage);
}

void verify_source_navigation(const sound_usage_action_result_t& result,
                              const document_history_t& history,
                              const workspace_controller_t& workspace,
                              const std::optional<editor_interaction_state_t>& interaction,
                              local_position_t cursor, const song_document_t* document_address,
                              const chart_document_t* chart_storage) {
    verify_source_identity(result);
    verify_source_workspace(workspace);
    verify_source_cursor(interaction, cursor);
    verify_source_document(history, document_address, chart_storage);
}

} // namespace

TEST_CASE("sound usage action rejects stale project context and malformed rows atomically") {
    auto baseline_history = usage_history();
    auto baseline_workspace_result = audio_workspace(baseline_history.document);
    REQUIRE(baseline_workspace_result.has_value());
    auto baseline_workspace = *baseline_workspace_result;
    const auto baseline_action = usage_action(baseline_history);
    std::optional<editor_interaction_state_t> baseline_interaction;
    const auto baseline =
        dispatch_sound_usage_action(baseline_action, valid_request(), baseline_history,
                                    baseline_workspace, baseline_interaction);
    INFO(baseline.message);
    REQUIRE(baseline.accepted);

    const auto reject = [](auto mutate_action, auto mutate_request, auto mutate_document) {
        auto history = usage_history();
        auto workspace_result = audio_workspace(history.document);
        REQUIRE(workspace_result.has_value());
        auto workspace = *workspace_result;
        std::optional<editor_interaction_state_t> interaction;
        auto seed = usage_action(history);
        const auto setup = make_editor_interaction(
            history, chart_id_t{7},
            history.document.charts.front().lane_layouts.front().layout.lanes.front().id,
            {measure_id_t{1}, 0}, std::array<std::uint64_t, 2>{4, 8}, 4);
        interaction = setup.state;
        const auto original_workspace = workspace;
        const auto original_interaction = *interaction;
        const auto revision = history.document.revision;
        const auto undo_depth = history.undo.size();
        auto request = valid_request();
        mutate_action(seed);
        mutate_request(request);
        mutate_document(history);
        const auto note_count = history.document.charts.front().measures.front().notes.size();
        const auto result =
            dispatch_sound_usage_action(seed, request, history, workspace, interaction);
        check_rejection_unchanged(result, history, revision, undo_depth, note_count, workspace,
                                  *interaction, original_workspace, original_interaction);
    };
    SECTION("stale song") {
        reject([](auto& a) { a.sound_usage_song_id = 9; }, [](auto&) {}, [](auto&) {});
    }
    SECTION("stale revision") {
        reject([](auto& a) { ++a.sound_usage_document_revision; }, [](auto&) {}, [](auto&) {});
    }
    SECTION("stale selected resource") {
        reject([](auto&) {}, [](auto& r) { r.selected_resource_id = 99; }, [](auto&) {});
    }
    SECTION("stale asset") {
        reject([](auto& a) { ++a.sound_usage_asset_id; }, [](auto&) {}, [](auto&) {});
    }
    SECTION("deleted row") {
        reject([](auto&) {}, [](auto&) {},
               [](auto& h) { h.document.charts.front().measures.front().notes.clear(); });
    }
    SECTION("mismatched full row") {
        reject([](auto& a) { ++a.sound_usage_object_id; }, [](auto&) {}, [](auto&) {});
    }
}

TEST_CASE("positioned sound usage selects chart and exact cursor") {
    auto history = usage_history();
    auto workspace_result = audio_workspace(history.document);
    REQUIRE(workspace_result.has_value());
    auto workspace = *workspace_result;
    std::optional<editor_interaction_state_t> interaction;
    auto action = usage_action(history);
    const auto* document_address = &history.document;
    const auto* chart_storage = history.document.charts.data();
    const auto* note_storage = history.document.charts.front().measures.front().notes.data();
    const auto result =
        dispatch_sound_usage_action(action, valid_request(), history, workspace, interaction);
    INFO(result.message);
    REQUIRE(result.accepted);
    verify_positioned_navigation(result, history, workspace, interaction, document_address,
                                 chart_storage, note_storage);
}

TEST_CASE("source sound usage selects channel identity without moving cursor") {
    auto history = usage_history();
    auto workspace_result = audio_workspace(history.document);
    REQUIRE(workspace_result.has_value());
    auto workspace = *workspace_result;
    auto profile = history.document.charts.front().lane_layouts.front().layout;
    const auto setup =
        make_editor_interaction(history, chart_id_t{7}, profile.lanes.front().id,
                                {measure_id_t{1}, 0}, std::array<std::uint64_t, 1>{4}, 4);
    std::optional<editor_interaction_state_t> interaction{setup.state};
    const auto before = interaction->cursor;
    auto action = usage_action(history, true);
    const auto* document_address = &history.document;
    const auto* chart_storage = history.document.charts.data();
    const auto result =
        dispatch_sound_usage_action(action, valid_request(), history, workspace, interaction);
    INFO(result.message);
    REQUIRE(result.accepted);
    verify_source_navigation(result, history, workspace, interaction, before, document_address,
                             chart_storage);
}
