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

auto apply(workspace_controller_t& workspace, const song_document_t& song,
           const workspace_action_t& action, std::uint64_t revision = 7)
    -> workspace_action_result_t {
    return apply_workspace_action(workspace, song, revision, action);
}

} // namespace

TEST_CASE("workspace tabs retain chart, transport, and isolated logical focus",
          "[unit][core][workspace]") {
    const auto song = workspace_song();
    CHECK(validate_document(song).empty());
    auto bound = make_workspace_controller(song);
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    CHECK(workspace.selected_chart == chart_id_t{1});
    CHECK((workspace.comparison_charts == std::vector{chart_id_t{1}, chart_id_t{2}}));

    CHECK_FALSE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::timing}));
    CHECK_FALSE(workspace_tab_enabled(workspace_tab_t::timing));
    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    REQUIRE(apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::metadata, "metadata.events", object_id_t{5}}));
    REQUIRE(apply(workspace, song, workspace_select_chart_action_t{chart_id_t{2}}));
    REQUIRE(apply(workspace, song, workspace_select_audio_asset_action_t{asset_id_t{20}}));
    REQUIRE(apply(workspace, song, workspace_select_visual_asset_action_t{asset_id_t{21}}));
    CHECK(workspace.selected_visual_asset == asset_id_t{21});
    REQUIRE(apply(workspace, song, workspace_seek_action_t{640}));
    REQUIRE(apply(workspace, song, workspace_set_playing_action_t{true}));
    REQUIRE(apply(workspace, song,
                  workspace_set_loop_action_t{std::pair{std::uint64_t{100}, std::uint64_t{900}}}));
    REQUIRE(apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::compose, "compose.field", chart_id_t{1}}));

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));
    auto compose = make_workspace_view(workspace, song);
    REQUIRE(compose);
    CHECK(compose.view->selected_difficulty->id == chart_id_t{2});
    CHECK(compose.view->selected_difficulty->difficulty_name == "Hard");
    CHECK(compose.view->transport.position_frame == 640);
    CHECK(compose.view->transport.playing);
    CHECK(compose.view->focus.region == "compose.field");
    REQUIRE(std::holds_alternative<workspace_compose_view_t>(compose.view->tab_view));
    CHECK(std::get<workspace_compose_view_t>(compose.view->tab_view).note_count == 1);

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    auto metadata = make_workspace_view(workspace, song);
    REQUIRE(metadata);
    CHECK(metadata.view->focus.region == "metadata.events");
    CHECK(metadata.view->selected_difficulty->id == chart_id_t{2});
    REQUIRE(metadata.view->transport.loop_frames.has_value());
    CHECK((*metadata.view->transport.loop_frames ==
           std::pair{std::uint64_t{100}, std::uint64_t{900}}));

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::audio_slices}));
    REQUIRE(apply(workspace, song, workspace_select_clip_action_t{clip_id_t{4}}));
    auto audio = make_workspace_view(workspace, song);
    REQUIRE(audio);
    CHECK(audio.view->transport.selected_clip == clip_id_t{4});
    CHECK(audio.view->transport.position_frame == 10);
    CHECK_FALSE(audio.view->transport.playing);
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::compose)].region ==
          "compose.field");
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::metadata)].region ==
          "metadata.events");
    CHECK((workspace.comparison_charts == std::vector{chart_id_t{1}, chart_id_t{2}}));
}

TEST_CASE("workspace rejects stale revisions and rebinds stable context atomically",
          "[unit][core][workspace][revision]") {
    auto song = workspace_song();
    auto bound = make_workspace_controller(song);
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    REQUIRE(apply(workspace, song, workspace_select_chart_action_t{chart_id_t{2}}));
    REQUIRE(apply(workspace, song, workspace_select_audio_asset_action_t{asset_id_t{20}}));
    REQUIRE(apply(workspace, song, workspace_select_visual_asset_action_t{asset_id_t{21}}));
    CHECK_FALSE(apply(workspace, song,
                      workspace_select_visual_asset_action_t{asset_id_t{20}}));
    CHECK(workspace.selected_visual_asset == asset_id_t{21});
    REQUIRE(apply(workspace, song, workspace_seek_action_t{700}));
    REQUIRE(apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::metadata, "metadata.title", chart_id_t{2}}));

    auto changed = song;
    changed.revision = 8;
    const auto stale_action = apply_workspace_action(
        workspace, changed, 7, workspace_switch_tab_action_t{workspace_tab_t::compose});
    CHECK_FALSE(stale_action);
    CHECK(has_code(stale_action.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(workspace.active_tab == workspace_tab_t::setup);
    CHECK_FALSE(make_workspace_view(workspace, changed));

    REQUIRE(rebind_workspace(workspace, changed));
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
    REQUIRE(rebind_workspace(workspace, removed));
    CHECK(workspace.selected_chart == chart_id_t{1});
    CHECK_FALSE(workspace.transport.audio_asset.has_value());
    CHECK_FALSE(workspace.selected_visual_asset.has_value());
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::metadata)].region.empty());
    CHECK(workspace.comparison_charts.size() < 2);
}

TEST_CASE("workspace exposes one shared document-history context across tabs",
          "[unit][core][workspace][history]") {
    auto history = make_document_history(workspace_song(), 8, true);
    auto bound = make_workspace_controller(history);
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    CHECK(workspace.history.available);
    CHECK_FALSE(workspace.history.dirty);
    CHECK(workspace.history.undo_depth == 0);

    auto proposed = history.document;
    proposed.metadata.title = "Edited title";
    REQUIRE(commit_document_command(history, std::move(proposed), "Edit title"));
    REQUIRE(rebind_workspace(workspace, history));
    CHECK(workspace.history.dirty);
    CHECK(workspace.history.undo_depth == 1);
    CHECK(workspace.history.redo_depth == 0);

    REQUIRE(apply_workspace_action(workspace, history.document, history.document.revision,
                                   workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    const auto metadata = make_workspace_view(workspace, history.document);
    REQUIRE(metadata);
    CHECK(metadata.view->history == workspace.history);
    CHECK(metadata.view->history.dirty);
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
    auto bound = make_workspace_controller(song);
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    const auto result = make_workspace_view(workspace, song);
    REQUIRE(result);
    const auto& metadata = std::get<workspace_metadata_view_t>(result.view->tab_view);
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

    auto history = make_document_history(song);
    REQUIRE(apply_metadata_edit(
        history, history.document.revision,
        replace_format_metadata_field_action_t{
            {std::nullopt, format_family_t::bms}, 1, "SUBARTIST", "edited primary group"}));
    CHECK(history.document.metadata.format_groups[1].ordered_fields[1].second ==
          "edited primary group");
    CHECK(history.document.metadata.format_groups[2].ordered_fields.front().second ==
          "later group");

    auto count_capped = make_workspace_controller(song, {.maximum_summary_items = 3});
    REQUIRE(count_capped);
    REQUIRE(apply(*count_capped.workspace, song,
                  workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    CHECK_FALSE(make_workspace_view(*count_capped.workspace, song));

    auto byte_capped = make_workspace_controller(song, {.maximum_metadata_text_bytes = 8});
    REQUIRE(byte_capped);
    REQUIRE(apply(*byte_capped.workspace, song,
                  workspace_switch_tab_action_t{workspace_tab_t::metadata}));
    CHECK_FALSE(make_workspace_view(*byte_capped.workspace, song));
}

TEST_CASE("workspace validates stable identities, focus targets, and resource caps",
          "[unit][core][workspace][bounds]") {
    auto song = workspace_song();
    auto invalid_song = song;
    invalid_song.id = {};
    const auto invalid_root = make_workspace_controller(invalid_song);
    CHECK_FALSE(invalid_root);
    CHECK(has_code(invalid_root.diagnostics, diagnostic_code_t::invalid_id));

    auto duplicate = song;
    duplicate.charts.push_back(duplicate.charts.front());
    const auto duplicate_bind = make_workspace_controller(duplicate);
    CHECK_FALSE(duplicate_bind);
    CHECK(has_code(duplicate_bind.diagnostics, diagnostic_code_t::duplicate_id));

    const auto over_cap = make_workspace_controller(song, {.maximum_charts = 1});
    CHECK_FALSE(over_cap);
    CHECK(has_code(over_cap.diagnostics, diagnostic_code_t::invalid_resolution));
    const auto invalid_compare_cap =
        make_workspace_controller(song, {.maximum_comparison_charts = 1});
    CHECK_FALSE(invalid_compare_cap);

    auto bound = make_workspace_controller(song, {.maximum_focus_region_bytes = 8});
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    const auto bad_focus = apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::setup, "focus-name-too-long", chart_id_t{1}});
    CHECK_FALSE(bad_focus);
    CHECK(has_code(bad_focus.diagnostics, diagnostic_code_t::invalid_resolution));
    const auto unknown_focus =
        apply(workspace, song,
              workspace_set_focus_action_t{workspace_tab_t::setup, "chart", chart_id_t{99}});
    CHECK_FALSE(unknown_focus);
    CHECK(has_code(unknown_focus.diagnostics, diagnostic_code_t::invalid_id));
    const auto zero_chart = apply(workspace, song, workspace_select_chart_action_t{chart_id_t{}});
    CHECK_FALSE(zero_chart);
    CHECK(workspace.selected_chart == chart_id_t{1});

    const auto zero_asset =
        apply(workspace, song, workspace_select_audio_asset_action_t{asset_id_t{}});
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
    CHECK_FALSE(make_workspace_view(workspace, song));
    REQUIRE(rebind_workspace(workspace, song));
    CHECK((workspace.comparison_charts == std::vector{chart_id_t{1}, chart_id_t{2}}));
}

TEST_CASE("storyboard is visible as a disabled placeholder and cannot be activated",
          "[unit][core][workspace][placeholder]") {
    const auto song = workspace_song();
    auto bound = make_workspace_controller(song);
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    CHECK(workspace_tab_enabled(workspace_tab_t::setup));
    CHECK_FALSE(workspace_tab_enabled(workspace_tab_t::storyboard));

    const auto placeholder = make_workspace_view(workspace, song, workspace_tab_t::storyboard);
    REQUIRE(placeholder);
    CHECK_FALSE(placeholder.view->tab_enabled);
    REQUIRE(std::holds_alternative<workspace_storyboard_view_t>(placeholder.view->tab_view));
    const auto& placeholder_body =
        std::get<workspace_storyboard_view_t>(placeholder.view->tab_view);
    CHECK_FALSE(placeholder_body.enabled);
    CHECK_FALSE(placeholder_body.placeholder.empty());

    const auto rejected =
        apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::storyboard});
    CHECK_FALSE(rejected);
    CHECK(has_code(rejected.diagnostics, diagnostic_code_t::unsupported_feature));
    CHECK(workspace.active_tab == workspace_tab_t::setup);

    REQUIRE(
        apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compare_validate}));
    const auto validation = make_workspace_view(workspace, song);
    REQUIRE(validation);
    REQUIRE(std::holds_alternative<workspace_compare_validate_view_t>(validation.view->tab_view));
    const auto& body = std::get<workspace_compare_validate_view_t>(validation.view->tab_view);
    CHECK((body.comparison_charts == std::vector{chart_id_t{1}, chart_id_t{2}}));
    CHECK(body.error_count == 0);
    CHECK(body.diagnostics.empty());
}

TEST_CASE("transport range and compose summary caps reject partial updates",
          "[unit][core][workspace][bounds]") {
    auto song = workspace_song();
    auto bound = make_workspace_controller(song, {.maximum_summary_items = 1});
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    REQUIRE(apply(workspace, song, workspace_select_audio_asset_action_t{asset_id_t{20}}));
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
    const auto capped = make_workspace_view(workspace, song);
    CHECK_FALSE(capped);
    CHECK_FALSE(capped.view.has_value());
    CHECK(has_code(capped.diagnostics, diagnostic_code_t::invalid_resolution));
}

TEST_CASE("workspace exposes the complete BMS branch outline with current active projection",
          "[unit][core][workspace][bms-branches]") {
    const auto song = bms_branch_workspace_song();
    auto bound = make_workspace_controller(song);
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));

    const auto result = make_workspace_view(workspace, song);
    REQUIRE(result);
    const auto& compose = std::get<workspace_compose_view_t>(result.view->tab_view);
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
    auto row_capped = make_workspace_controller(song, {.maximum_bms_branch_rows = 4});
    REQUIRE(row_capped);
    REQUIRE(apply(*row_capped.workspace, song,
                  workspace_switch_tab_action_t{workspace_tab_t::compose}));
    auto row_result = make_workspace_view(*row_capped.workspace, song);
    REQUIRE(row_result);
    const auto& row_outline =
        std::get<workspace_compose_view_t>(row_result.view->tab_view).bms_branch_outline;
    CHECK(row_outline.rows.size() == 4);
    CHECK(row_outline.rows_truncated);
    CHECK(row_outline.choices_truncated);
    CHECK(has_code(row_outline.diagnostics, diagnostic_code_t::invalid_resolution));

    auto text_capped = make_workspace_controller(
        song, {.maximum_bms_branch_text_bytes = 5, .maximum_bms_branch_display_bytes = 12});
    REQUIRE(text_capped);
    REQUIRE(apply(*text_capped.workspace, song,
                  workspace_switch_tab_action_t{workspace_tab_t::compose}));
    auto text_result = make_workspace_view(*text_capped.workspace, song);
    REQUIRE(text_result);
    const auto& text_outline =
        std::get<workspace_compose_view_t>(text_result.view->tab_view).bms_branch_outline;
    CHECK(text_outline.display_text_truncated);
    CHECK(has_code(text_outline.diagnostics, diagnostic_code_t::invalid_resolution));
    CHECK(std::ranges::all_of(text_outline.rows,
                              [](const auto& item) { return item.display_text.size() <= 5; }));

    auto diagnostics_capped = make_workspace_controller(
        song, {.maximum_validation_diagnostics = 0, .maximum_bms_branch_rows = 4});
    REQUIRE(diagnostics_capped);
    REQUIRE(apply(*diagnostics_capped.workspace, song,
                  workspace_switch_tab_action_t{workspace_tab_t::compose}));
    auto diagnostics_result = make_workspace_view(*diagnostics_capped.workspace, song);
    REQUIRE(diagnostics_result);
    const auto& diagnostics_outline =
        std::get<workspace_compose_view_t>(diagnostics_result.view->tab_view).bms_branch_outline;
    CHECK(diagnostics_outline.rows_truncated);
    CHECK(diagnostics_outline.diagnostics.empty());
    CHECK(diagnostics_outline.diagnostics_truncated);
}

TEST_CASE("BMS branch choices expose one reachable unresolved control at a time",
          "[unit][core][workspace][bms-branches][choices]") {
    auto song = bms_branch_workspace_song();
    song.charts.front().active_branch_selection.clear();
    auto bound = make_workspace_controller(song);
    REQUIRE(bound);
    REQUIRE(apply(*bound.workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));

    const auto result = make_workspace_view(*bound.workspace, song);
    REQUIRE(result);
    const auto& outline =
        std::get<workspace_compose_view_t>(result.view->tab_view).bms_branch_outline;
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
    auto bound = make_workspace_controller(song);
    REQUIRE(bound);
    REQUIRE(apply(*bound.workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));

    const auto result = make_workspace_view(*bound.workspace, song);
    REQUIRE(result);
    const auto& outline =
        std::get<workspace_compose_view_t>(result.view->tab_view).bms_branch_outline;
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
    auto bound = make_workspace_controller(song);
    REQUIRE(bound);
    auto& workspace = *bound.workspace;
    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::compose}));

    auto changed = song;
    changed.revision = song.revision + 1;
    const auto stale_view = make_workspace_view(workspace, changed);
    CHECK_FALSE(stale_view);
    CHECK(has_code(stale_view.diagnostics, diagnostic_code_t::invalid_id));

    auto stale_choice_song = song;
    stale_choice_song.charts.front().active_branch_selection.push_back({branch_id_t{9999}, 1});
    REQUIRE(rebind_workspace(workspace, stale_choice_song));
    const auto updated = make_workspace_view(workspace, stale_choice_song);
    REQUIRE(updated);
    const auto& outline =
        std::get<workspace_compose_view_t>(updated.view->tab_view).bms_branch_outline;
    CHECK(has_code(outline.diagnostics, diagnostic_code_t::invalid_id));
    CHECK_FALSE(outline.rows_truncated);
}

namespace {
auto branch_cache_rows_equal(const workspace_bms_branch_outline_t& left,
                              const workspace_bms_branch_outline_t& right) -> bool {
    return std::ranges::equal(left.rows, right.rows, [](const auto& a, const auto& b) {
        return std::tie(a.kind, a.id, a.depth, a.source_order, a.active, a.selected,
                        a.display_text, a.display_text_truncated) ==
               std::tie(b.kind, b.id, b.depth, b.source_order, b.active, b.selected,
                        b.display_text, b.display_text_truncated);
    });
}
auto branch_cache_choices_equal(const workspace_bms_branch_outline_t& left,
                                 const workspace_bms_branch_outline_t& right) -> bool {
    return std::ranges::equal(left.choices, right.choices, [](const auto& a, const auto& b) {
        return std::tie(a.id, a.kind, a.source_order, a.active, a.editable, a.selected_value,
                        a.previous_value, a.next_value, a.can_clear) ==
               std::tie(b.id, b.kind, b.source_order, b.active, b.editable, b.selected_value,
                        b.previous_value, b.next_value, b.can_clear);
    });
}
auto branch_cache_outlines_equal(const workspace_bms_branch_outline_t& left,
                                 const workspace_bms_branch_outline_t& right) -> bool {
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
    const auto cached = make_workspace_view(workspace, song, workspace_tab_t::compose, nullptr, &cache);
    const auto reference = make_workspace_view(workspace, song, workspace_tab_t::compose);
    if (!cached || !reference) return false;
    return branch_cache_outlines_equal(
        std::get<workspace_compose_view_t>(cached.view->tab_view).bms_branch_outline,
        std::get<workspace_compose_view_t>(reference.view->tab_view).bms_branch_outline);
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
    auto binding = make_workspace_controller(song);
    if (!binding) return false;
    auto& workspace = *binding.workspace;
    workspace_bms_branch_outline_cache_t cache;
    if (!branch_cache_matches_reference(song, workspace, cache) ||
        !branch_cache_matches_reference(song, workspace, cache) || cache.build_count != 1)
        return false;
    const auto before = cache.outline;
    song.charts.front().active_branch_selection.front().selected_value = 1;
    return branch_cache_matches_reference(song, workspace, cache) && cache.build_count == 2 &&
        !branch_cache_outlines_equal(before, cache.outline);
}
auto branch_cache_tracks_limits_revision_and_identity() -> bool {
    auto song = branch_cache_large_song();
    auto binding = make_workspace_controller(song);
    if (!binding) return false;
    auto& workspace = *binding.workspace;
    workspace_bms_branch_outline_cache_t cache;
    if (!branch_cache_matches_reference(song, workspace, cache)) return false;
    workspace.limits.maximum_bms_branch_rows = 5;
    if (!branch_cache_matches_reference(song, workspace, cache) || cache.build_count != 2 ||
        !cache.outline.rows_truncated) return false;
    workspace.limits.maximum_bms_branch_display_bytes = 1;
    if (!branch_cache_matches_reference(song, workspace, cache) || cache.build_count != 3 ||
        !cache.outline.display_text_truncated) return false;
    ++song.revision;
    workspace.document_revision = song.revision;
    if (!branch_cache_matches_reference(song, workspace, cache) || cache.build_count != 4)
        return false;
    auto replacement = song;
    replacement.charts.front().bms_control_tree->original_text = "different retained root";
    if (!branch_cache_matches_reference(replacement, workspace, cache) || cache.build_count != 5)
        return false;
    replacement.charts.back().bms_control_tree = replacement.charts.front().bms_control_tree;
    workspace.selected_chart = replacement.charts.back().id;
    return branch_cache_matches_reference(replacement, workspace, cache) && cache.build_count == 6;
}
auto branch_cache_preserves_plain_sidebar_and_skips_absent_tree() -> bool {
    auto song = workspace_song();
    auto& chart = song.charts.front();
    chart.bms_control_tree = bms_control_node_t{.id = branch_id_t{1000},
        .kind = bms_control_kind_t::root, .children = {
            {.id = branch_id_t{1001}, .kind = bms_control_kind_t::raw_statement,
             .original_text = "#TITLE ordinary sidebar statement"}}};
    auto binding = make_workspace_controller(song);
    if (!binding) return false;
    workspace_bms_branch_outline_cache_t cache;
    if (!branch_cache_matches_reference(song, *binding.workspace, cache) ||
        cache.outline.rows.size() != 2 || cache.build_count != 1) return false;
    chart.bms_control_tree.reset();
    return branch_cache_matches_reference(song, *binding.workspace, cache) && cache.build_count == 1;
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
