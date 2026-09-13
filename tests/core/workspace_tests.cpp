#include <wormedit/core/workspace.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
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

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::timing}));
    REQUIRE(apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::timing, "timing.events", object_id_t{5}}));
    REQUIRE(apply(workspace, song, workspace_select_chart_action_t{chart_id_t{2}}));
    REQUIRE(apply(workspace, song, workspace_select_audio_asset_action_t{asset_id_t{20}}));
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

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::timing}));
    auto timing = make_workspace_view(workspace, song);
    REQUIRE(timing);
    CHECK(timing.view->focus.region == "timing.events");
    CHECK(timing.view->selected_difficulty->id == chart_id_t{2});
    REQUIRE(timing.view->transport.loop_frames.has_value());
    CHECK(
        (*timing.view->transport.loop_frames == std::pair{std::uint64_t{100}, std::uint64_t{900}}));
    REQUIRE(std::holds_alternative<workspace_timing_view_t>(timing.view->tab_view));
    CHECK(std::get<workspace_timing_view_t>(timing.view->tab_view).bpm_count == 1);

    REQUIRE(apply(workspace, song, workspace_switch_tab_action_t{workspace_tab_t::audio_slices}));
    REQUIRE(apply(workspace, song, workspace_select_clip_action_t{clip_id_t{4}}));
    auto audio = make_workspace_view(workspace, song);
    REQUIRE(audio);
    CHECK(audio.view->transport.selected_clip == clip_id_t{4});
    CHECK(audio.view->transport.position_frame == 10);
    CHECK_FALSE(audio.view->transport.playing);
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::compose)].region ==
          "compose.field");
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::timing)].region ==
          "timing.events");
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
    REQUIRE(apply(workspace, song, workspace_seek_action_t{700}));
    REQUIRE(apply(
        workspace, song,
        workspace_set_focus_action_t{workspace_tab_t::metadata, "metadata.title", chart_id_t{2}}));

    auto changed = song;
    changed.revision = 8;
    const auto stale_action = apply_workspace_action(
        workspace, changed, 7, workspace_switch_tab_action_t{workspace_tab_t::timing});
    CHECK_FALSE(stale_action);
    CHECK(has_code(stale_action.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(workspace.active_tab == workspace_tab_t::setup);
    CHECK_FALSE(make_workspace_view(workspace, changed));

    REQUIRE(rebind_workspace(workspace, changed));
    CHECK(workspace.document_revision == 8);
    CHECK(workspace.selected_chart == chart_id_t{2});
    CHECK(workspace.transport.audio_asset == asset_id_t{20});
    CHECK(workspace.transport.position_frame == 700);
    CHECK(workspace.tab_focus[static_cast<std::size_t>(workspace_tab_t::metadata)].target ==
          workspace_focus_target_t{chart_id_t{2}});

    auto removed = changed;
    removed.revision = 9;
    removed.charts.erase(removed.charts.begin() + 1);
    removed.audio_assets.clear();
    REQUIRE(rebind_workspace(workspace, removed));
    CHECK(workspace.selected_chart == chart_id_t{1});
    CHECK_FALSE(workspace.transport.audio_asset.has_value());
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
        {.family = format_family_t::bms,
         .ordered_fields = {{"SUBARTIST", "one"}, {"SUBARTIST", "two"}, {"STAGEFILE", "stage.png"}},
         .ordered_field_source_orders = {8, 9}}};
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
    REQUIRE(metadata.format_fields.size() == 4);
    CHECK(metadata.format_fields[0].name == "SUBARTIST");
    CHECK(metadata.format_fields[1].name == "SUBARTIST");
    CHECK(metadata.format_fields[0].source_order == 8);
    CHECK_FALSE(metadata.format_fields[2].source_order.has_value());
    CHECK(metadata.format_fields[3].chart_scoped);
    CHECK(metadata.format_fields[3].family == format_family_t::stepmania);

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
