#include "../support/workspace_result_assertions.h"
#include "../support/stale_action_assertions.h"
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/core/command.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/workspace.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <tuple>
#include <variant>

using namespace wormedit;

namespace {

auto workspace_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{40},
        .revision = 7,
        .metadata = {.title = "Workspace fixture", .artist = "Test artist"},
        .audio_assets = {{.id = asset_id_t{20},
                          .source_path = "song.ogg",
                          .stream = {.sample_rate = 48'000,
                                     .channels = 2,
                                     .frame_count = 1'000,
                                     .seekable = true}}},
        .visual_assets = {{.id = asset_id_t{21}, .kind = asset_kind_t::image}},
        .charts =
            {
                {
                    .id = chart_id_t{1},
                    .metadata = {.name = "Another",
                                 .author = "Chart author",
                                 .rating = signed_rational_t{12, 1}},
                    .beat_zero_audio_offset_seconds = {-3, 8},
                    .measures = {{.id = measure_id_t{1},
                                  .notes = {{.id = object_id_t{2},
                                             .position = {measure_id_t{1}, 0},
                                             .lane = lane_id_t{1}}}}},
                    .lane_layouts = {{.id = object_id_t{1},
                                      .position = {measure_id_t{1}, 0},
                                      .layout = {.profile_id = "workspace-test",
                                                 .display_name = "One lane",
                                                 .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
                    .bpms = {{.id = object_id_t{5},
                              .position = {measure_id_t{1}, 0},
                              .beats_per_minute = {120, 1}}},
                    .sound_channels = {{.id = object_id_t{3},
                                        .name = "Music",
                                        .source_clip = {.id = clip_id_t{4},
                                                        .source = asset_id_t{20},
                                                        .begin_frame = 10,
                                                        .end_frame = 900}}},
                },
                {.id = chart_id_t{2},
                 .metadata = {.name = "Hard", .rating = signed_rational_t{18, 1}}},
            },
    };
    auto& hard = song.charts.back();
    hard.measures = {
        {.id = measure_id_t{2},
         .notes = {
             {.id = object_id_t{7}, .position = {measure_id_t{2}, 0}, .lane = lane_id_t{2}}}}};
    hard.lane_layouts = {{.id = object_id_t{6},
                          .position = {measure_id_t{2}, 0},
                          .layout = {.profile_id = "workspace-test-hard",
                                     .display_name = "One lane",
                                     .lanes = {{.id = lane_id_t{2}, .name = "Key"}}}}};
    hard.bpms = {
        {.id = object_id_t{8}, .position = {measure_id_t{2}, 0}, .beats_per_minute = {150, 1}}};
    return song;
}

auto bms_branch_workspace_song() -> song_document_t {
    auto song = workspace_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bms;
    chart.bms_control_tree = bms_control_node_t{
        .id = branch_id_t{1000},
        .kind = bms_control_kind_t::root,
        .children = {
            {
                .id = branch_id_t{1001},
                .kind = bms_control_kind_t::random,
                .value = 2,
                .original_text = "#RANDOM 2",
                .source_order = 0,
                .children = {
                    {
                        .id = branch_id_t{1002},
                        .kind = bms_control_kind_t::if_group,
                        .children = {
                            {
                                .id = branch_id_t{1003},
                                .kind = bms_control_kind_t::if_branch,
                                .value = 1,
                                .original_text = "#IF 1",
                                .source_order = 1,
                                .children = {{.id = branch_id_t{1004},
                                              .kind = bms_control_kind_t::raw_statement,
                                              .original_text = "#TITLE inactive",
                                              .source_order = 2}},
                            },
                            {
                                .id = branch_id_t{1005},
                                .kind = bms_control_kind_t::else_branch,
                                .original_text = "#ELSE",
                                .source_order = 3,
                                .children = {
                                    {.id = branch_id_t{1006},
                                     .kind = bms_control_kind_t::raw_statement,
                                     .original_text = "#TITLE selected",
                                     .source_order = 4},
                                    {
                                        .id = branch_id_t{1007},
                                        .kind = bms_control_kind_t::switch_block,
                                        .value = 3,
                                        .original_text = "#SWITCH 3",
                                        .source_order = 5,
                                        .children = {
                                            {.id = branch_id_t{1008},
                                             .kind = bms_control_kind_t::case_branch,
                                             .value = 1,
                                             .original_text = "#CASE 1",
                                             .source_order = 6,
                                             .children = {{.id = branch_id_t{1009},
                                                           .kind = bms_control_kind_t::raw_statement,
                                                           .original_text = "#BPM 120",
                                                           .source_order = 7}}},
                                            {.id = branch_id_t{1010},
                                             .kind = bms_control_kind_t::case_branch,
                                             .value = 2,
                                             .original_text = "#CASE 2",
                                             .source_order = 8,
                                             .children = {
                                                 {.id = branch_id_t{1011},
                                                  .kind = bms_control_kind_t::raw_statement,
                                                  .original_text = "#BPM 150",
                                                  .source_order = 9},
                                                 {.id = branch_id_t{1012},
                                                  .kind = bms_control_kind_t::skip,
                                                  .original_text = "#SKIP",
                                                  .source_order = 10},
                                             }},
                                            {.id = branch_id_t{1013},
                                             .kind = bms_control_kind_t::default_branch,
                                             .original_text = "#DEF",
                                             .source_order = 11,
                                             .children = {{.id = branch_id_t{1014},
                                                           .kind = bms_control_kind_t::raw_statement,
                                                           .original_text = "#BPM 180",
                                                           .source_order = 12}}},
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            },
        },
    };
    chart.active_branch_selection = {{branch_id_t{1001}, 2}, {branch_id_t{1007}, 2}};
    return song;
}

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

void check_workspace_stale_feedback(const dispatch_outcome_t& result) {
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().message == stale_action_message("Workspace action"));
}

auto apply(workspace_controller_t& workspace, const song_document_t& song,
           const workspace_action_t& action)
    -> bool {
    return apply_workspace_action(workspace, song, action)
        .is_accepted(diagnostic_acceptance_policy_t::require_empty);
}

} // namespace

TEST_CASE("Workspace view binding rejects captured project and revision expiry",
          "[unit][core][workspace][view-stamp]") {
    const auto different_project = GENERATE(false, true);
    auto song = workspace_song();
    const auto binding = workspace_controller_t::from(song);
    REQUIRE(binding.value.has_value());
    if (different_project) ++song.id.value;
    else ++song.revision;
    const auto result = workspace_view_t::from(*binding.value, song);
    REQUIRE_FALSE((result.value && result.diagnostics.empty()));
    test::check_stale_action_message(result.diagnostics, "Workspace view");
}

TEST_CASE("workspace tabs retain chart, transport, and isolated logical focus",
          "[unit][core][workspace]") {
    const auto song = workspace_song();
    CHECK(validate_document(song).empty());
    auto shared_bound = workspace_controller_t::from(song);
    auto bound = std::move(shared_bound);
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    CHECK(workspace.selected_chart == chart_id_t{1});
    CHECK((workspace.comparison_charts == std::vector{chart_id_t{1}, chart_id_t{2}}));

    CHECK_FALSE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::timing}));
    CHECK_FALSE(is_workspace_tab_enabled(workspace_tab_t::timing));
    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    REQUIRE(apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::metadata, "metadata.events", object_id_t{5}}));
    REQUIRE(apply(workspace, song, workspace_select_chart_action_t{chart_id_t{2}}));
    REQUIRE(apply(workspace, song,
                  workspace_select_asset_action_t{
                      .asset = asset_id_t{20}, .slot = wormedit::workspace_asset_slot_t::audio}));
    REQUIRE(apply(workspace, song,
                  workspace_select_asset_action_t{
                      .asset = asset_id_t{21}, .slot = wormedit::workspace_asset_slot_t::visual}));
    CHECK(workspace.selected_visual_asset == asset_id_t{21});
    REQUIRE(apply(workspace, song, workspace_seek_action_t{640}));
    REQUIRE(apply(workspace, song, workspace_set_playing_action_t{true}));
    REQUIRE(apply(workspace, song,
                  workspace_set_loop_action_t{std::pair{std::uint64_t{100}, std::uint64_t{900}}}));
    REQUIRE(apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::compose, "compose.field", chart_id_t{1}}));

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));
    auto shared_compose = workspace_view_t::from(workspace, song);
    auto compose = std::move(shared_compose);
    wormedit::test::require_workspace_result(compose);
    CHECK(compose.value->selected_difficulty->id == chart_id_t{2});
    CHECK(compose.value->selected_difficulty->difficulty_name == "Hard");
    CHECK(compose.value->transport.position_frame == 640);
    CHECK(compose.value->transport.playing);
    CHECK(compose.value->focus.region == "compose.field");
    REQUIRE(std::holds_alternative<workspace_compose_view_t>(compose.value->tab_view));
    CHECK(std::get<workspace_compose_view_t>(compose.value->tab_view).note_count == 1);

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    auto metadata = workspace_view_t::from(workspace, song);
    wormedit::test::require_workspace_result(metadata);
    CHECK(metadata.value->focus.region == "metadata.events");
    CHECK(metadata.value->selected_difficulty->id == chart_id_t{2});
    REQUIRE(metadata.value->transport.loop_frames.has_value());
    CHECK((*metadata.value->transport.loop_frames ==
           std::pair{std::uint64_t{100}, std::uint64_t{900}}));

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::audio_slices}));
    REQUIRE(apply(workspace, song, workspace_select_clip_action_t{clip_id_t{4}}));
    auto audio = workspace_view_t::from(workspace, song);
    wormedit::test::require_workspace_result(audio);
    CHECK(audio.value->transport.selected_clip == clip_id_t{4});
    CHECK(audio.value->transport.position_frame == 10);
    CHECK_FALSE(audio.value->transport.playing);
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::compose)].region ==
          "compose.field");
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::metadata)].region ==
          "metadata.events");
    CHECK((workspace.comparison_charts == std::vector{chart_id_t{1}, chart_id_t{2}}));
}

TEST_CASE("workspace rejects stale revisions and rebinds stable context atomically",
          "[unit][core][workspace][revision]") {
    auto song = workspace_song();
    auto bound = workspace_controller_t::from(song);
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    REQUIRE(apply(workspace, song, workspace_select_chart_action_t{chart_id_t{2}}));
    REQUIRE(apply(workspace, song,
                  workspace_select_asset_action_t{
                      .asset = asset_id_t{20}, .slot = wormedit::workspace_asset_slot_t::audio}));
    REQUIRE(apply(workspace, song,
                  workspace_select_asset_action_t{
                      .asset = asset_id_t{21}, .slot = wormedit::workspace_asset_slot_t::visual}));
    CHECK_FALSE(
        apply(workspace, song,
              workspace_select_asset_action_t{.asset = asset_id_t{20},
                                              .slot = wormedit::workspace_asset_slot_t::visual}));
    CHECK(workspace.selected_visual_asset == asset_id_t{21});
    REQUIRE(apply(workspace, song, workspace_seek_action_t{700}));
    REQUIRE(apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::metadata, "metadata.title", chart_id_t{2}}));

    auto changed = song;
    changed.revision = 8;
    const auto stale_action = apply_workspace_action(
        workspace, changed, workspace_switch_tab_action_t{workspace_tab_t::compose});
    wormedit::test::check_workspace_rejection(stale_action);
    check_workspace_stale_feedback(stale_action);
    CHECK(has_code(stale_action.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(workspace.active_tab == workspace_tab_t::setup);
    wormedit::test::check_workspace_rejection(workspace_view_t::from(workspace, changed));

    wormedit::test::require_workspace_result(rebind_workspace(workspace, changed));
    CHECK(workspace.document_revision == 8);
    CHECK(workspace.selected_chart == chart_id_t{2});
    CHECK(workspace.transport.audio_asset == asset_id_t{20});
    CHECK(workspace.selected_visual_asset == asset_id_t{21});
    CHECK(workspace.transport.position_frame == 700);
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::metadata)].target ==
          workspace_focus_target_t{chart_id_t{2}});

    auto removed = changed;
    removed.revision = 9;
    removed.charts.erase(removed.charts.begin() + 1);
    removed.audio_assets.clear();
    removed.visual_assets.clear();
    wormedit::test::require_workspace_result(rebind_workspace(workspace, removed));
    CHECK(workspace.selected_chart == chart_id_t{1});
    CHECK_FALSE(workspace.transport.audio_asset.has_value());
    CHECK_FALSE(workspace.selected_visual_asset.has_value());
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::metadata)].region.empty());
    CHECK(workspace.comparison_charts.size() < 2);
}

TEST_CASE("workspace exposes one shared document-history context across tabs",
          "[unit][core][workspace][history]") {
    auto history = document_history_t::from(workspace_song(), 8, true);
    auto bound = workspace_controller_t::from(history);
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    CHECK(workspace.history.available);
    CHECK_FALSE(workspace.history.dirty);
    CHECK(workspace.history.undo_depth == 0);

    auto proposed = history.document;
    proposed.metadata.title = "Edited title";
    REQUIRE(commit_document_command(history, std::move(proposed), "Edit title"));
    wormedit::test::require_workspace_result(rebind_workspace(workspace, history));
    CHECK(workspace.history.dirty);
    CHECK(workspace.history.undo_depth == 1);
    CHECK(workspace.history.redo_depth == 0);

    wormedit::test::require_workspace_result(apply_workspace_action(workspace, history.document,
                                   workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    const auto metadata = workspace_view_t::from(workspace, history.document);
    wormedit::test::require_workspace_result(metadata);
    CHECK(metadata.value->history == workspace.history);
    CHECK(metadata.value->history.dirty);
}

TEST_CASE("metadata workspace exposes bounded ordered song and difficulty format fields",
          "[unit][core][workspace][metadata]") {
    auto song = workspace_song();
    song.metadata.format_groups = {
        {.family = format_family_t::native_project},
        {.family = format_family_t::bms,
         .ordered_fields = {{"SUBARTIST", "one"}, {"SUBARTIST", "two"}, {"STAGEFILE", "stage.png"}},
         .ordered_field_source_orders = {8, 9}},
        {.family = format_family_t::bms, .ordered_fields = {{"SUBARTIST", "later group"}}}};
    song.charts.front().metadata.format_groups = {
        {.family = format_family_t::stepmania,
         .ordered_fields = {{"CREDIT", "Difficulty author"}},
         .ordered_field_source_orders = {20}}};
    auto bound = workspace_controller_t::from(song);
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    const auto result = workspace_view_t::from(workspace, song);
    wormedit::test::require_workspace_result(result);
    const auto& metadata = std::get<workspace_metadata_view_t>(result.value->tab_view);
    CHECK(metadata.beat_zero_audio_offset_seconds == signed_rational_t{-3, 8});
    REQUIRE(metadata.format_fields.size() == 5);
    CHECK(metadata.format_fields[0].name == "SUBARTIST");
    CHECK(metadata.format_fields[1].name == "SUBARTIST");
    CHECK(metadata.format_fields[0].source_order == 8);
    CHECK(metadata.format_fields[0].group_index == 1);
    CHECK(metadata.format_fields[0].group_field_count == 3);
    CHECK(metadata.format_fields[0].first_group_for_family);
    CHECK_FALSE(metadata.format_fields[2].source_order.has_value());
    CHECK(metadata.format_fields[3].family == format_family_t::bms);
    CHECK(metadata.format_fields[3].group_index == 2);
    CHECK_FALSE(metadata.format_fields[3].first_group_for_family);
    CHECK(metadata.format_fields[4].chart_scoped);
    CHECK(metadata.format_fields[4].family == format_family_t::stepmania);

    auto history = document_history_t::from(song);
    REQUIRE(
        apply_metadata_edit(history, edit_format_metadata_field_action_t{
                                         .target = {std::nullopt, format_family_t::bms},
                                         .index = 1,
                                         .name = "SUBARTIST",
                                         .value = "edited primary group",
                                         .mode = wormedit::format_metadata_field_mode_t::replace}));
    CHECK(history.document.metadata.format_groups[1].ordered_fields[1].second ==
          "edited primary group");
    CHECK(history.document.metadata.format_groups[2].ordered_fields.front().second ==
          "later group");

    auto count_capped = workspace_controller_t::from(song, {.maximum_summary_items = 3});
    wormedit::test::require_workspace_result(count_capped);
    REQUIRE(apply(*count_capped.value, song,
                  workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    wormedit::test::check_workspace_rejection(workspace_view_t::from(*count_capped.value, song));

    auto byte_capped = workspace_controller_t::from(song, {.maximum_metadata_text_bytes = 8});
    wormedit::test::require_workspace_result(byte_capped);
    REQUIRE(apply(*byte_capped.value, song,
                  workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    wormedit::test::check_workspace_rejection(workspace_view_t::from(*byte_capped.value, song));
}

TEST_CASE("workspace validates stable identities, focus targets, and resource caps",
          "[unit][core][workspace][bounds]") {
    auto song = workspace_song();
    auto invalid_song = song;
    invalid_song.id = {};
    const auto invalid_root = workspace_controller_t::from(invalid_song);
    wormedit::test::check_workspace_rejection(invalid_root);
    CHECK(has_code(invalid_root.diagnostics, diagnostic_code_t::invalid_id));

    auto duplicate = song;
    duplicate.charts.push_back(duplicate.charts.front());
    const auto duplicate_bind = workspace_controller_t::from(duplicate);
    wormedit::test::check_workspace_rejection(duplicate_bind);
    CHECK(has_code(duplicate_bind.diagnostics, diagnostic_code_t::duplicate_id));

    const auto over_cap = workspace_controller_t::from(song, {.maximum_charts = 1});
    wormedit::test::check_workspace_rejection(over_cap);
    CHECK(has_code(over_cap.diagnostics, diagnostic_code_t::invalid_resolution));
    const auto invalid_compare_cap =
        workspace_controller_t::from(song, {.maximum_comparison_charts = 1});
    wormedit::test::check_workspace_rejection(invalid_compare_cap);

    auto bound = workspace_controller_t::from(song, {.maximum_focus_region_bytes = 8});
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    const auto bad_focus = apply_workspace_action(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::setup, "focus-name-too-long", chart_id_t{1}});
    wormedit::test::check_workspace_rejection(bad_focus);
    CHECK(has_code(bad_focus.diagnostics, diagnostic_code_t::invalid_resolution));
    const auto unknown_focus =
        apply_workspace_action(workspace, song,
              workspace_set_focus_action_t{workspace_tab_t::setup, "chart", chart_id_t{99}});
    wormedit::test::check_workspace_rejection(unknown_focus);
    CHECK(has_code(unknown_focus.diagnostics, diagnostic_code_t::invalid_id));
    const auto zero_chart = apply(workspace, song, workspace_select_chart_action_t{chart_id_t{}});
    CHECK_FALSE(zero_chart);
    CHECK(workspace.selected_chart == chart_id_t{1});

    const auto zero_asset =
        apply(workspace, song,
              workspace_select_asset_action_t{.asset = asset_id_t{},
                                              .slot = wormedit::workspace_asset_slot_t::audio});
    CHECK_FALSE(zero_asset);
    CHECK_FALSE(workspace.transport.audio_asset.has_value());

    const auto comparison_before = workspace.comparison_charts;
    const auto duplicate_comparison = apply(
        workspace, song, workspace_set_comparison_charts_action_t{{chart_id_t{1}, chart_id_t{1}}});
    CHECK_FALSE(duplicate_comparison);
    CHECK(workspace.comparison_charts == comparison_before);
    REQUIRE(apply(workspace, song,
                  workspace_set_comparison_charts_action_t{{chart_id_t{2}, chart_id_t{1}}}));
    CHECK((workspace.comparison_charts == std::vector{chart_id_t{2}, chart_id_t{1}}));
    workspace.comparison_charts = {chart_id_t{1}, chart_id_t{1}};
    wormedit::test::check_workspace_rejection(workspace_view_t::from(workspace, song));
    wormedit::test::require_workspace_result(rebind_workspace(workspace, song));
    CHECK((workspace.comparison_charts == std::vector{chart_id_t{1}, chart_id_t{2}}));
}

TEST_CASE("storyboard is visible as a disabled placeholder and cannot be activated",
          "[unit][core][workspace][placeholder]") {
    const auto song = workspace_song();
    auto bound = workspace_controller_t::from(song);
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    CHECK(is_workspace_tab_enabled(workspace_tab_t::setup));
    CHECK_FALSE(is_workspace_tab_enabled(workspace_tab_t::storyboard));

    const auto placeholder = workspace_view_t::from(workspace, song, workspace_tab_t::storyboard);
    wormedit::test::require_workspace_result(placeholder);
    CHECK_FALSE(placeholder.value->tab_enabled);
    REQUIRE(std::holds_alternative<workspace_storyboard_view_t>(placeholder.value->tab_view));
    const auto& placeholder_body =
        std::get<workspace_storyboard_view_t>(placeholder.value->tab_view);
    CHECK_FALSE(placeholder_body.enabled);
    CHECK_FALSE(placeholder_body.placeholder.empty());

    const auto rejected =
        apply_workspace_action(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::storyboard});
    wormedit::test::check_workspace_rejection(rejected);
    CHECK(has_code(rejected.diagnostics, diagnostic_code_t::unsupported_feature));
    CHECK(workspace.active_tab == workspace_tab_t::setup);

    REQUIRE(
        apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compare_validate}));
    const auto validation = workspace_view_t::from(workspace, song);
    wormedit::test::require_workspace_result(validation);
    REQUIRE(std::holds_alternative<workspace_compare_validate_view_t>(validation.value->tab_view));
    const auto& body = std::get<workspace_compare_validate_view_t>(validation.value->tab_view);
    CHECK((body.comparison_charts == std::vector{chart_id_t{1}, chart_id_t{2}}));
    CHECK(body.error_count == 0);
    CHECK(body.diagnostics.empty());
}

TEST_CASE("transport range and compose summary caps reject partial updates",
          "[unit][core][workspace][bounds]") {
    auto song = workspace_song();
    auto bound = workspace_controller_t::from(song, {.maximum_summary_items = 1});
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    REQUIRE(apply(workspace, song,
                  workspace_select_asset_action_t{
                      .asset = asset_id_t{20}, .slot = wormedit::workspace_asset_slot_t::audio}));
    REQUIRE(apply(workspace, song, workspace_seek_action_t{1'000}));
    const auto out_of_range = apply(workspace, song, workspace_seek_action_t{1'001});
    CHECK_FALSE(out_of_range);
    CHECK(workspace.transport.position_frame == 1'000);
    const auto invalid_loop =
        apply(workspace, song,
              workspace_set_loop_action_t{std::pair{std::uint64_t{80}, std::uint64_t{1'001}}});
    CHECK_FALSE(invalid_loop);
    CHECK_FALSE(workspace.transport.loop_frames.has_value());

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));
    const auto capped = workspace_view_t::from(workspace, song);
    wormedit::test::check_workspace_rejection(capped);
    CHECK_FALSE(capped.value.has_value());
    CHECK(has_code(capped.diagnostics, diagnostic_code_t::invalid_resolution));
}

TEST_CASE("workspace exposes the complete BMS branch outline with current active projection",
          "[unit][core][workspace][bms-branches]") {
    const auto song = bms_branch_workspace_song();
    auto bound = workspace_controller_t::from(song);
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));

    const auto result = workspace_view_t::from(workspace, song);
    wormedit::test::require_workspace_result(result);
    const auto& compose = std::get<workspace_compose_view_t>(result.value->tab_view);
    const auto& outline = compose.bms_branch_outline;
    REQUIRE(outline.available);
    CHECK_FALSE(outline.rows_truncated);
    CHECK_FALSE(outline.display_text_truncated);
    CHECK(outline.diagnostics.empty());
    CHECK(outline.rows.size() == 15);
    REQUIRE(outline.choices.size() == 2);
    CHECK_FALSE(outline.choices_truncated);
    CHECK(outline.choices[0].id == branch_id_t{1001});
    CHECK(outline.choices[0].kind == bms_control_kind_t::random);
    CHECK(outline.choices[0].active);
    CHECK(outline.choices[0].editable);
    CHECK(outline.choices[0].selected_value == std::optional<std::int64_t>{2});
    CHECK(outline.choices[0].previous_value == std::optional<std::int64_t>{1});
    CHECK_FALSE(outline.choices[0].next_value.has_value());
    CHECK(outline.choices[0].can_clear);
    CHECK(outline.choices[1].id == branch_id_t{1007});
    CHECK(outline.choices[1].kind == bms_control_kind_t::switch_block);
    CHECK(outline.choices[1].active);
    CHECK(outline.choices[1].selected_value == std::optional<std::int64_t>{2});
    CHECK(outline.choices[1].previous_value == std::optional<std::int64_t>{1});

    const auto row = [&outline](branch_id_t id) -> const workspace_bms_branch_row_t* {
        const auto found = std::ranges::find(outline.rows, id, &workspace_bms_branch_row_t::id);
        return found == outline.rows.end() ? nullptr : &*found;
    };
    REQUIRE(row(branch_id_t{1001}) != nullptr);
    CHECK(row(branch_id_t{1001})->kind == bms_control_kind_t::random);
    CHECK(row(branch_id_t{1001})->selected);
    CHECK(row(branch_id_t{1001})->active);
    REQUIRE(row(branch_id_t{1005}) != nullptr);
    CHECK(row(branch_id_t{1005})->active);
    CHECK(row(branch_id_t{1005})->selected);
    REQUIRE(row(branch_id_t{1004}) != nullptr);
    CHECK_FALSE(row(branch_id_t{1004})->active);
    REQUIRE(row(branch_id_t{1010}) != nullptr);
    CHECK(row(branch_id_t{1010})->active);
    CHECK(row(branch_id_t{1010})->selected);
    REQUIRE(row(branch_id_t{1012}) != nullptr);
    CHECK(row(branch_id_t{1012})->active);
    REQUIRE(row(branch_id_t{1011}) != nullptr);
    REQUIRE(row(branch_id_t{1013}) != nullptr);
    CHECK_FALSE(row(branch_id_t{1013})->active);
    CHECK(row(branch_id_t{1013})->source_order == std::optional<std::size_t>{11});
    CHECK(row(branch_id_t{1011})->depth > row(branch_id_t{1001})->depth);
}

TEST_CASE("BMS branch outline reports bounded rows and display text",
          "[unit][core][workspace][bms-branches][bounds]") {
    const auto song = bms_branch_workspace_song();
    auto row_capped = workspace_controller_t::from(song, {.maximum_bms_branch_rows = 4});
    wormedit::test::require_workspace_result(row_capped);
    REQUIRE(apply(*row_capped.value, song,
                  workspace_switch_tab_action_t{workspace_tab_t::compose}));
    auto row_result = workspace_view_t::from(*row_capped.value, song);
    wormedit::test::require_workspace_result(row_result);
    const auto& row_outline =
        std::get<workspace_compose_view_t>(row_result.value->tab_view).bms_branch_outline;
    CHECK(row_outline.rows.size() == 4);
    CHECK(row_outline.rows_truncated);
    CHECK(row_outline.choices_truncated);
    CHECK(has_code(row_outline.diagnostics, diagnostic_code_t::invalid_resolution));

    auto text_capped = workspace_controller_t::from(
        song, {.maximum_bms_branch_text_bytes = 5, .maximum_bms_branch_display_bytes = 12});
    wormedit::test::require_workspace_result(text_capped);
    REQUIRE(apply(*text_capped.value, song,
                  workspace_switch_tab_action_t{workspace_tab_t::compose}));
    auto text_result = workspace_view_t::from(*text_capped.value, song);
    wormedit::test::require_workspace_result(text_result);
    const auto& text_outline =
        std::get<workspace_compose_view_t>(text_result.value->tab_view).bms_branch_outline;
    CHECK(text_outline.display_text_truncated);
    CHECK(has_code(text_outline.diagnostics, diagnostic_code_t::invalid_resolution));
    CHECK(std::ranges::all_of(text_outline.rows,
                              [](const auto& item) { return item.display_text.size() <= 5; }));

    auto diagnostics_capped = workspace_controller_t::from(
        song, {.maximum_validation_diagnostics = 0, .maximum_bms_branch_rows = 4});
    wormedit::test::require_workspace_result(diagnostics_capped);
    REQUIRE(apply(*diagnostics_capped.value, song,
                  workspace_switch_tab_action_t{workspace_tab_t::compose}));
    auto diagnostics_result = workspace_view_t::from(*diagnostics_capped.value, song);
    wormedit::test::require_workspace_result(diagnostics_result);
    const auto& diagnostics_outline =
        std::get<workspace_compose_view_t>(diagnostics_result.value->tab_view).bms_branch_outline;
    CHECK(diagnostics_outline.rows_truncated);
    CHECK(diagnostics_outline.diagnostics.empty());
    CHECK(diagnostics_outline.diagnostics_truncated);
}

TEST_CASE("BMS branch choices expose one reachable unresolved control at a time",
          "[unit][core][workspace][bms-branches][choices]") {
    auto song = bms_branch_workspace_song();
    song.charts.front().active_branch_selection.clear();
    auto bound = workspace_controller_t::from(song);
    wormedit::test::require_workspace_result(bound);
    REQUIRE(apply(*bound.value, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));

    const auto result = workspace_view_t::from(*bound.value, song);
    wormedit::test::require_workspace_result(result);
    const auto& outline =
        std::get<workspace_compose_view_t>(result.value->tab_view).bms_branch_outline;
    REQUIRE(outline.choices.size() == 2);
    const auto& random = outline.choices.front();
    CHECK(random.id == branch_id_t{1001});
    CHECK(random.active);
    CHECK(random.editable);
    CHECK_FALSE(random.selected_value.has_value());
    CHECK_FALSE(random.previous_value.has_value());
    CHECK(random.next_value == std::optional<std::int64_t>{1});
    CHECK_FALSE(random.can_clear);
    CHECK(outline.choices[1].id == branch_id_t{1007});
    CHECK_FALSE(outline.choices[1].active);
    CHECK_FALSE(outline.choices[1].editable);
}

TEST_CASE("BMS branch choices keep invalid imported selections clearable",
          "[unit][core][workspace][bms-branches][choices][recovery]") {
    auto song = bms_branch_workspace_song();
    song.charts.front().active_branch_selection.front().selected_value = 99;
    auto bound = workspace_controller_t::from(song);
    wormedit::test::require_workspace_result(bound);
    REQUIRE(apply(*bound.value, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));

    const auto result = workspace_view_t::from(*bound.value, song);
    wormedit::test::require_workspace_result(result);
    const auto& outline =
        std::get<workspace_compose_view_t>(result.value->tab_view).bms_branch_outline;
    REQUIRE_FALSE(outline.choices.empty());
    const auto& random = outline.choices.front();
    CHECK(random.active);
    CHECK_FALSE(random.editable);
    CHECK(random.selected_value == std::optional<std::int64_t>{99});
    CHECK_FALSE(random.previous_value.has_value());
    CHECK_FALSE(random.next_value.has_value());
    CHECK(random.can_clear);
    CHECK(has_code(outline.diagnostics, diagnostic_code_t::invalid_resolution));
}

TEST_CASE("BMS branch outline rejects a stale workspace revision and diagnoses stale choices",
          "[unit][core][workspace][bms-branches][revision]") {
    auto song = bms_branch_workspace_song();
    auto bound = workspace_controller_t::from(song);
    wormedit::test::require_workspace_result(bound);
    auto& workspace = *bound.value;
    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));

    auto changed = song;
    changed.revision = song.revision + 1;
    const auto stale_view = workspace_view_t::from(workspace, changed);
    wormedit::test::check_workspace_rejection(stale_view);
    CHECK(has_code(stale_view.diagnostics, diagnostic_code_t::invalid_id));

    auto stale_choice_song = song;
    stale_choice_song.charts.front().active_branch_selection.push_back({branch_id_t{9999}, 1});
    wormedit::test::require_workspace_result(rebind_workspace(workspace, stale_choice_song));
    const auto updated = workspace_view_t::from(workspace, stale_choice_song);
    wormedit::test::require_workspace_result(updated);
    const auto& outline =
        std::get<workspace_compose_view_t>(updated.value->tab_view).bms_branch_outline;
    CHECK(has_code(outline.diagnostics, diagnostic_code_t::invalid_id));
    CHECK_FALSE(outline.rows_truncated);
}

namespace {
auto branch_cache_rows_equal(const workspace_bms_branch_outline_view_t& left,
                             const workspace_bms_branch_outline_view_t& right) -> bool {
    return std::ranges::equal(left.rows, right.rows, [](const auto& a, const auto& b) {
        return std::tie(a.kind, a.id, a.depth, a.source_order, a.active, a.selected,
                        a.display_text, a.display_text_truncated) ==
               std::tie(b.kind, b.id, b.depth, b.source_order, b.active, b.selected,
                        b.display_text, b.display_text_truncated);
    });
}
auto branch_cache_choices_equal(const workspace_bms_branch_outline_view_t& left,
                                const workspace_bms_branch_outline_view_t& right) -> bool {
    return std::ranges::equal(left.choices, right.choices, [](const auto& a, const auto& b) {
        return std::tie(a.id, a.kind, a.source_order, a.active, a.editable, a.selected_value,
                        a.previous_value, a.next_value, a.can_clear) ==
               std::tie(b.id, b.kind, b.source_order, b.active, b.editable, b.selected_value,
                        b.previous_value, b.next_value, b.can_clear);
    });
}
auto branch_cache_outlines_equal(const workspace_bms_branch_outline_view_t& left,
                                 const workspace_bms_branch_outline_view_t& right) -> bool {
    const bool diagnostics = std::ranges::equal(left.diagnostics, right.diagnostics,
        [](const auto& a, const auto& b) {
            return std::tie(a.severity, a.code, a.message, a.object) ==
                   std::tie(b.severity, b.code, b.message, b.object);
        });
    return diagnostics && branch_cache_rows_equal(left, right) &&
        branch_cache_choices_equal(left, right) &&
        std::tie(left.available, left.rows_truncated, left.choices_truncated,
                 left.display_text_truncated, left.diagnostics_truncated) ==
        std::tie(right.available, right.rows_truncated, right.choices_truncated,
                 right.display_text_truncated, right.diagnostics_truncated);
}
auto branch_cache_matches_reference(const song_document_t& song,
    const workspace_controller_t& workspace, workspace_bms_branch_outline_cache_t& cache) -> bool {
    const auto cached = workspace_view_t::from(workspace, song, workspace_tab_t::compose, nullptr, &cache);
    const auto reference = workspace_view_t::from(workspace, song, workspace_tab_t::compose);
    if (!(cached.value && cached.diagnostics.empty()) || !(reference.value && reference.diagnostics.empty())) return false;
    return branch_cache_outlines_equal(
        std::get<workspace_compose_view_t>(cached.value->tab_view).bms_branch_outline,
        std::get<workspace_compose_view_t>(reference.value->tab_view).bms_branch_outline);
}
auto branch_cache_large_song() -> song_document_t {
    auto song = bms_branch_workspace_song();
    for (std::uint64_t id = 1; id <= 600; ++id)
        song.resources.push_back({.id = resource_id_t{id}, .name = "sound" + std::to_string(id),
            .asset = asset_id_t{20}, .roles = {resource_role_t::sample}});
    auto& chart = song.charts.front();
    chart.bpms.clear();
    for (std::uint64_t index = 0; index < 70; ++index)
        chart.bpms.push_back({.id = object_id_t{20000 + index},
            .position = {measure_id_t{1}, index},
            .beats_per_minute = {120 + static_cast<std::int64_t>(index), 1}});
    return song;
}
auto branch_cache_tracks_choices_without_revision() -> bool {
    auto song = branch_cache_large_song();
    auto binding = workspace_controller_t::from(song);
    if (!binding.value || !binding.diagnostics.empty()) return false;
    auto& workspace = *binding.value;
    workspace_bms_branch_outline_cache_t cache;
    if (!branch_cache_matches_reference(song, workspace, cache) ||
        !branch_cache_matches_reference(song, workspace, cache) || cache.build_count() != 1)
        return false;
    const auto before = cache.value();
    song.charts.front().active_branch_selection.front().selected_value = 1;
    return branch_cache_matches_reference(song, workspace, cache) && cache.build_count() == 2 &&
        !branch_cache_outlines_equal(before, cache.value());
}
auto branch_cache_tracks_limits_revision_and_identity() -> bool {
    auto song = branch_cache_large_song();
    auto binding = workspace_controller_t::from(song);
    if (!binding.value || !binding.diagnostics.empty()) return false;
    auto& workspace = *binding.value;
    workspace_bms_branch_outline_cache_t cache;
    if (!branch_cache_matches_reference(song, workspace, cache)) return false;
    workspace.limits.maximum_bms_branch_rows = 5;
    if (!branch_cache_matches_reference(song, workspace, cache) || cache.build_count() != 2 ||
        !cache.value().rows_truncated) return false;
    workspace.limits.maximum_bms_branch_display_bytes = 1;
    if (!branch_cache_matches_reference(song, workspace, cache) || cache.build_count() != 3 ||
        !cache.value().display_text_truncated) return false;
    ++song.revision;
    workspace.document_revision = song.revision;
    if (!branch_cache_matches_reference(song, workspace, cache) || cache.build_count() != 4)
        return false;
    auto replacement = song;
    replacement.charts.front().bms_control_tree->original_text = "different retained root";
    if (!branch_cache_matches_reference(replacement, workspace, cache) || cache.build_count() != 5)
        return false;
    replacement.charts.back().bms_control_tree = replacement.charts.front().bms_control_tree;
    workspace.selected_chart = replacement.charts.back().id;
    return branch_cache_matches_reference(replacement, workspace, cache) && cache.build_count() == 6;
}
auto branch_cache_preserves_plain_sidebar_and_skips_absent_tree() -> bool {
    auto song = workspace_song();
    auto& chart = song.charts.front();
    chart.bms_control_tree = bms_control_node_t{.id = branch_id_t{1000},
        .kind = bms_control_kind_t::root, .children = {
            {.id = branch_id_t{1001}, .kind = bms_control_kind_t::raw_statement,
             .original_text = "#TITLE ordinary sidebar statement"}}};
    auto binding = workspace_controller_t::from(song);
    if (!binding.value || !binding.diagnostics.empty()) return false;
    workspace_bms_branch_outline_cache_t cache;
    if (!branch_cache_matches_reference(song, *binding.value, cache) ||
        cache.value().rows.size() != 2 || cache.build_count() != 1) return false;
    chart.bms_control_tree.reset();
    return branch_cache_matches_reference(song, *binding.value, cache) && cache.build_count() == 1;
}
} // namespace

TEST_CASE("Workspace branch cache matches large reference views and current branch choices",
          "[unit][core][workspace][workspace-cache]") {
    REQUIRE(branch_cache_tracks_choices_without_revision());
}
TEST_CASE("Workspace branch cache keys include limits revision and document identity",
          "[unit][core][workspace][workspace-cache]") {
    REQUIRE(branch_cache_tracks_limits_revision_and_identity());
}
TEST_CASE("Workspace branch cache preserves ordinary statements and skips only absent trees",
          "[unit][core][workspace][workspace-cache]") {
    REQUIRE(branch_cache_preserves_plain_sidebar_and_skips_absent_tree());
}

namespace {
auto cached_binding_view(const song_document_t& song, const workspace_controller_t& workspace,
                         workspace_binding_validation_cache_t& cache) -> workspace_view_outcome_t {
    return workspace_view_t::from(workspace, song, workspace.active_tab, nullptr, nullptr, &cache);
}
auto binding_cache_tracks_document_and_limits() -> bool {
    auto song = workspace_song();
    auto binding = workspace_controller_t::from(song);
    if (!binding.value || !binding.diagnostics.empty()) return false;
    auto& workspace = *binding.value;
    workspace_binding_validation_cache_t cache;
    for (unsigned frame = 0; frame < 240; ++frame) {
        workspace.transport.position_frame = frame * 800;
        if (const auto view = cached_binding_view(song, workspace, cache);
            !view.value || !view.diagnostics.empty()) return false;
    }
    if (cache.build_count() != 1) return false;
    ++song.revision;
    workspace.document_revision = song.revision;
    if (const auto view = cached_binding_view(song, workspace, cache);
        !view.value || !view.diagnostics.empty() || cache.build_count() != 2) return false;
    auto replacement = song;
    if (const auto view = cached_binding_view(replacement, workspace, cache);
        !view.value || !view.diagnostics.empty() || cache.build_count() != 3) return false;
    workspace.limits.maximum_audio_assets = 0;
    const auto rejected = cached_binding_view(replacement, workspace, cache);
    const auto reference = workspace_view_t::from(workspace, replacement);
    return !(rejected.value && rejected.diagnostics.empty()) &&
        !(reference.value && reference.diagnostics.empty()) && cache.build_count() == 4 &&
        rejected.diagnostics.front().code == reference.diagnostics.front().code &&
        rejected.diagnostics.front().message == reference.diagnostics.front().message;
}
auto binding_cache_keeps_dynamic_guards() -> bool {
    auto song = workspace_song();
    auto binding = workspace_controller_t::from(song);
    if (!binding.value || !binding.diagnostics.empty()) return false;
    auto& workspace = *binding.value;
    workspace_binding_validation_cache_t cache;
    if (const auto view = cached_binding_view(song, workspace, cache);
        !view.value || !view.diagnostics.empty()) return false;
    workspace.selected_chart = chart_id_t{999};
    if (const auto view = cached_binding_view(song, workspace, cache);
        (view.value && view.diagnostics.empty())) return false;
    workspace.selected_chart = song.charts.front().id;
    workspace.comparison_charts = {chart_id_t{999}};
    if (const auto view = cached_binding_view(song, workspace, cache);
        (view.value && view.diagnostics.empty())) return false;
    workspace.comparison_charts.clear();
    workspace.tab_focus[static_cast<std::size_t>(workspace.active_tab)].target = object_id_t{999};
    if (const auto view = cached_binding_view(song, workspace, cache);
        (view.value && view.diagnostics.empty())) return false;
    workspace.tab_focus[static_cast<std::size_t>(workspace.active_tab)] = {};
    workspace.active_tab = static_cast<workspace_tab_t>(999);
    if (const auto view = cached_binding_view(song, workspace, cache);
        (view.value && view.diagnostics.empty())) return false;
    workspace.active_tab = workspace_tab_t::setup;
    --workspace.document_revision;
    const auto stale = cached_binding_view(song, workspace, cache);
    return !(stale.value && stale.diagnostics.empty()) && cache.build_count() == 1;
}
auto binding_cache_revalidates_invalid_id_edits() -> bool {
    auto song = workspace_song();
    auto binding = workspace_controller_t::from(song);
    if (!binding.value || !binding.diagnostics.empty()) return false;
    auto& workspace = *binding.value;
    workspace_binding_validation_cache_t cache;
    if (const auto view = cached_binding_view(song, workspace, cache);
        !view.value || !view.diagnostics.empty()) return false;
    song.audio_assets.push_back(song.audio_assets.front());
    workspace.document_revision = ++song.revision;
    const auto rejected = cached_binding_view(song, workspace, cache);
    if ((rejected.value && rejected.diagnostics.empty()) || rejected.diagnostics.empty() || cache.build_count() != 2) return false;
    if (const auto view = cached_binding_view(song, workspace, cache);
        (view.value && view.diagnostics.empty()) || cache.build_count() != 2) return false;
    song.audio_assets.pop_back();
    workspace.document_revision = ++song.revision;
    const auto restored = cached_binding_view(song, workspace, cache);
    return restored.value && restored.diagnostics.empty() && cache.build_count() == 3;
}
} // namespace

TEST_CASE("Workspace binding validation reuses immutable revisions and invalidates limits and identity",
          "[workspace][cache][T558]") {
    CHECK(binding_cache_tracks_document_and_limits());
}
TEST_CASE("Workspace binding validation cache keeps per-frame workspace guards live",
          "[workspace][cache][T558]") {
    CHECK(binding_cache_keeps_dynamic_guards());
}
TEST_CASE("Workspace binding validation cache revalidates invalid edits and repairs",
          "[workspace][cache][T558]") {
    CHECK(binding_cache_revalidates_invalid_id_edits());
}

TEST_CASE("workspace adapters preserve strict warning rejection for successful effects",
          "[unit][core][workspace]") {
    const diagnostic_t annotation{.severity = diagnostic_severity_t::warning,
                                 .code = diagnostic_code_t::unsupported_feature, .message = "annotation"};
    auto binding = workspace_bind_outcome_t{.value = workspace_controller_t{}, .diagnostics = {annotation}};
    REQUIRE(binding.ok());
    wormedit::test::check_workspace_rejection(binding);
    auto action = dispatch_outcome_t{.value = true, .diagnostics = {annotation}};
    REQUIRE(action.ok());
    wormedit::test::check_workspace_rejection(action);
    auto view = workspace_view_outcome_t{.value = workspace_view_t{}, .diagnostics = {annotation}};
    REQUIRE(view.ok());
    wormedit::test::check_workspace_rejection(view);
}
