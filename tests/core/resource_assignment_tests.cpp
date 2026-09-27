#include <wormedit/core/resource_assignment.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

using namespace wormedit;

namespace {

auto assignment_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .audio_assets = {{.id = asset_id_t{1}, .source_path = "one.wav"},
                         {.id = asset_id_t{2}, .source_path = "two.wav"},
                         {.id = asset_id_t{3}, .source_path = "missing.wav"}},
        .visual_assets = {{.id = asset_id_t{4}, .source_path = "one.png"},
                          {.id = asset_id_t{5}, .source_path = "two.png"}},
        .resources =
            {{.id = resource_id_t{1},
              .asset = asset_id_t{1},
              .roles = {resource_role_t::sample, resource_role_t::bgm}},
             {.id = resource_id_t{2},
              .asset = asset_id_t{2},
              .roles = {resource_role_t::sample, resource_role_t::bgm}},
             {.id = resource_id_t{3}, .asset = asset_id_t{4}, .roles = {resource_role_t::bga}},
             {.id = resource_id_t{4}, .asset = asset_id_t{5}, .roles = {resource_role_t::bga}},
             {.id = resource_id_t{5},
              .asset = asset_id_t{3},
              .roles = {resource_role_t::sample},
              .missing = true}},
        .charts = {{
            .id = chart_id_t{2},
            .measures = {{
                .id = measure_id_t{3},
                .notes = {{.id = object_id_t{10},
                           .position = {measure_id_t{3}, 120},
                           .lane = lane_id_t{1},
                           .sound = resource_id_t{1}}},
                .bgm_events = {{.id = object_id_t{11},
                                .position = {measure_id_t{3}, 240},
                                .source_lane = object_id_t{20},
                                .sound = resource_id_t{1}}},
            }},
            .lane_layouts =
                {{.id = object_id_t{21},
                  .position = {measure_id_t{3}, 0},
                  .layout = {.profile_id = "one-key",
                             .lanes = {{.id = lane_id_t{1}, .name = "Key", .display_order = 0}}}}},
            .input_profile = {.profile_id = "one-key",
                              .bindings = {{.key = physical_key_t::digit_1, .lane = lane_id_t{1}}}},
            .bgm_source_lanes = {{.id = object_id_t{20}, .name = "BGM"}},
            .bpms = {{.id = object_id_t{22},
                      .position = {measure_id_t{3}, 0},
                      .beats_per_minute = {120, 1}}},
            .bga_events = {{.id = object_id_t{12},
                            .position = {measure_id_t{3}, 360},
                            .resource = resource_id_t{3}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& item) { return item.code == code; });
}

} // namespace

TEST_CASE("resource creation assigns stable IDs and independent BMS namespaces",
          "[unit][core][resources]") {
    auto song = assignment_song();
    song.resources.front().target_identities = {
        {.family = format_family_t::bms, .identifier = "01", .namespace_name = "WAV"}};
    song.resources[1].target_identities = {
        {.family = format_family_t::bms, .identifier = "02", .namespace_name = "WAV"}};
    auto history = make_document_history(std::move(song));
    const auto created = apply_resource_creation(
        history, 4, {.asset = asset_id_t{2}, .role = resource_role_t::sample,
                     .name = "New sample", .allocate_bms_identifier = true});
    REQUIRE(created);
    REQUIRE(history.document.resources.size() == 6);
    const auto& sample = history.document.resources.back();
    CHECK(sample.id == resource_id_t{6});
    CHECK(sample.asset == asset_id_t{2});
    CHECK(sample.target_identities.front().identifier == "03");
    CHECK(sample.target_identities.front().namespace_name == "WAV");
    const auto visual = apply_resource_creation(
        history, 5, {.asset = asset_id_t{4}, .role = resource_role_t::bga,
                     .allocate_bms_identifier = true});
    REQUIRE(visual);
    CHECK(history.document.resources.back().id == resource_id_t{7});
    CHECK(history.document.resources.back().target_identities.front().identifier == "01");
    CHECK(history.document.resources.back().target_identities.front().namespace_name == "BMP");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.size() == 6);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.size() == 5);
}

TEST_CASE("resource creation rejects stale revisions and wrong asset types atomically",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    CHECK(has_code(apply_resource_creation(
                       history, 3, {.asset = asset_id_t{1}, .role = resource_role_t::sample}),
                   diagnostic_code_t::invalid_id));
    CHECK(has_code(apply_resource_creation(
                       history, 4, {.asset = asset_id_t{4}, .role = resource_role_t::sample}),
                   diagnostic_code_t::dangling_asset));
    CHECK(has_code(apply_resource_creation(
                       history, 4, {.asset = asset_id_t{1}, .role = resource_role_t::bga}),
                   diagnostic_code_t::dangling_asset));
    CHECK(has_code(apply_resource_creation(
                       history, 4, {.asset = asset_id_t{1}, .role = resource_role_t::music}),
                   diagnostic_code_t::unsupported_feature));
    CHECK(history.document.resources.size() == 5);
    CHECK(history.undo.empty());
}

TEST_CASE("resource placement adds typed events with collision and lane allocation",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto sample = apply_resource_placement(
        history, 4, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 480}, .lane = lane_id_t{1},
                     .kind = resource_placement_kind_t::sample_note});
    REQUIRE(sample);
    CHECK(history.document.charts.front().measures.front().notes.back().sound ==
          resource_id_t{2});

    const auto bgm = apply_resource_placement(
        history, 5, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 240},
                     .kind = resource_placement_kind_t::bgm_event});
    REQUIRE(bgm);
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.bgm_source_lanes.size() == 2);
    CHECK(chart.measures.front().bgm_events.back().source_lane ==
          chart.bgm_source_lanes.back().id);
    const auto reused = apply_resource_placement(
        history, 6, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 480},
                     .kind = resource_placement_kind_t::bgm_event});
    REQUIRE(reused);
    CHECK(history.document.charts.front().bgm_source_lanes.size() == 2);
    CHECK(history.document.charts.front().measures.front().bgm_events.back().source_lane ==
          object_id_t{20});

    const auto bga = apply_resource_placement(
        history, 7, {.chart = chart_id_t{2}, .resource = resource_id_t{4},
                     .position = {measure_id_t{3}, 240},
                     .kind = resource_placement_kind_t::bga_background});
    REQUIRE(bga);
    CHECK(history.document.charts.front().bga_events.back().resource == resource_id_t{4});
    CHECK(history.undo.size() == 4);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bga_events.size() == 1);
}

TEST_CASE("resource placement rejects stale incompatible and occupied targets atomically",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto sample = place_resource_action_t{
        .chart = chart_id_t{2}, .resource = resource_id_t{2},
        .position = {measure_id_t{3}, 120}, .lane = lane_id_t{1},
        .kind = resource_placement_kind_t::sample_note};
    CHECK(has_code(apply_resource_placement(history, 3, sample),
                   diagnostic_code_t::invalid_id));
    CHECK(has_code(apply_resource_placement(history, 4, sample),
                   diagnostic_code_t::collision));
    auto incompatible = sample;
    incompatible.resource = resource_id_t{4};
    CHECK(has_code(apply_resource_placement(history, 4, incompatible),
                   diagnostic_code_t::unsupported_feature));
    auto missing = sample;
    missing.resource = resource_id_t{5};
    CHECK(has_code(apply_resource_placement(history, 4, missing),
                   diagnostic_code_t::dangling_resource));
    auto visual = place_resource_action_t{
        .chart = chart_id_t{2}, .resource = resource_id_t{4},
        .position = {measure_id_t{3}, 360},
        .kind = resource_placement_kind_t::bga_background};
    CHECK(has_code(apply_resource_placement(history, 4, visual),
                   diagnostic_code_t::collision));
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}

TEST_CASE("BGM placement creates the first source lane when the chart has none",
          "[unit][core][resources]") {
    auto song = assignment_song();
    song.charts.front().measures.front().bgm_events.clear();
    song.charts.front().bgm_source_lanes.clear();
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    const auto applied = apply_resource_placement(
        history, 4, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 240},
                     .kind = resource_placement_kind_t::bgm_event});
    REQUIRE(applied);
    REQUIRE(history.document.charts.front().bgm_source_lanes.size() == 1);
    REQUIRE(history.document.charts.front().measures.front().bgm_events.size() == 1);
    CHECK(history.document.charts.front().measures.front().bgm_events.front().source_lane ==
          history.document.charts.front().bgm_source_lanes.front().id);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bgm_source_lanes.empty());
}

TEST_CASE("resource assignment changes typed note and BGM references atomically",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto applied = apply_resource_assignment(
        history, 4,
        {.chart = chart_id_t{2},
         .resource = resource_id_t{2},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{10}},
                     {editor_selection_object_kind_t::bgm_event, object_id_t{11}}}});
    REQUIRE(applied);
    const auto& measure = history.document.charts.front().measures.front();
    CHECK(measure.notes.front().sound == resource_id_t{2});
    CHECK(measure.bgm_events.front().sound == resource_id_t{2});
    CHECK(history.document.charts.front().bga_events.front().resource == resource_id_t{3});
    CHECK(history.undo.size() == 1);

    const auto no_op = apply_resource_assignment(
        history, 5,
        {.chart = chart_id_t{2},
         .resource = resource_id_t{2},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{10}}}});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.front().notes.front().sound == resource_id_t{1});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().measures.front().bgm_events.front().sound ==
          resource_id_t{2});
}

TEST_CASE("resource assignment changes BGA references through the same command",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto applied = apply_resource_assignment(
        history, 4,
        {.chart = chart_id_t{2},
         .resource = resource_id_t{4},
         .objects = {{editor_selection_object_kind_t::bga_event, object_id_t{12}}}});
    REQUIRE(applied);
    CHECK(history.document.charts.front().bga_events.front().resource == resource_id_t{4});
    CHECK(history.document.charts.front().bga_events.front().kind ==
          visual_event_kind_t::background);
}

TEST_CASE("resource assignment rejects stale incompatible and incomplete selections",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto note =
        editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{10}};
    const auto bgm =
        editor_selection_object_t{editor_selection_object_kind_t::bgm_event, object_id_t{11}};
    CHECK(has_code(
        apply_resource_assignment(
            history, 3, {.chart = chart_id_t{2}, .resource = resource_id_t{2}, .objects = {note}}),
        diagnostic_code_t::invalid_id));
    CHECK(has_code(
        apply_resource_assignment(
            history, 4, {.chart = chart_id_t{2}, .resource = resource_id_t{4}, .objects = {note}}),
        diagnostic_code_t::unsupported_feature));
    CHECK(has_code(
        apply_resource_assignment(
            history, 4, {.chart = chart_id_t{2}, .resource = resource_id_t{5}, .objects = {note}}),
        diagnostic_code_t::dangling_resource));
    CHECK(has_code(
        apply_resource_assignment(
            history, 4,
            {.chart = chart_id_t{2}, .resource = resource_id_t{2}, .objects = {note, note}}),
        diagnostic_code_t::duplicate_id));
    CHECK(
        has_code(apply_resource_assignment(
                     history, 4,
                     {.chart = chart_id_t{2},
                      .resource = resource_id_t{2},
                      .objects = {note, {editor_selection_object_kind_t::note, object_id_t{99}}}}),
                 diagnostic_code_t::invalid_id));
    CHECK(has_code(
        apply_resource_assignment(
            history, 4,
            {.chart = chart_id_t{2},
             .resource = resource_id_t{2},
             .objects = {note, {editor_selection_object_kind_t::bga_event, object_id_t{12}}}}),
        diagnostic_code_t::unsupported_feature));
    CHECK(has_code(apply_resource_assignment(
                       history, 4,
                       {.chart = chart_id_t{2}, .resource = resource_id_t{2}, .objects = {bgm}},
                       {.maximum_chart_objects = 1}),
                   diagnostic_code_t::invalid_resolution));
    CHECK(history.document.revision == 4);
    CHECK(history.document.charts.front().measures.front().notes.front().sound == resource_id_t{1});
    CHECK(history.document.charts.front().measures.front().bgm_events.front().sound ==
          resource_id_t{1});
    CHECK(history.undo.empty());
}

TEST_CASE("resource assignment protects retained BMSON sound links",
          "[unit][core][resources][bmson]") {
    auto song = assignment_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes.front().sound_channel_event = object_id_t{51};
    chart.sound_channels = {
        {.id = object_id_t{50},
         .source_clip = {.id = clip_id_t{1}, .source = asset_id_t{1}, .end_frame = 48'000},
         .events = {
             {.id = object_id_t{51}, .position = {measure_id_t{3}, 120}, .lane = lane_id_t{1}}}}};
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    const auto rejected = apply_resource_assignment(
        history, 4,
        {.chart = chart_id_t{2},
         .resource = resource_id_t{2},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{10}}}});
    CHECK_FALSE(rejected);
    CHECK(has_code(rejected, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}
