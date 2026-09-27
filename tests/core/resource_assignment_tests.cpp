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

TEST_CASE("resource placement picker advances in eligible catalog order without wrapping",
          "[unit][core][resources]") {
    auto song = assignment_song();
    CHECK(next_eligible_placement_resource(song, resource_id_t{1}, resource_role_t::sample) ==
          resource_id_t{2});
    CHECK_FALSE(next_eligible_placement_resource(song, resource_id_t{2},
                                                 resource_role_t::sample));
    CHECK(next_eligible_placement_resource(song, resource_id_t{3}, resource_role_t::bga) ==
          resource_id_t{4});
    CHECK_FALSE(next_eligible_placement_resource(song, resource_id_t{4}, resource_role_t::bga));
    CHECK_FALSE(next_eligible_placement_resource(song, resource_id_t{99},
                                                 resource_role_t::sample));
    song.resources.insert(song.resources.begin() + 1,
                          {.id = resource_id_t{6}, .asset = asset_id_t{99},
                           .roles = {resource_role_t::sample}});
    CHECK(next_eligible_placement_resource(song, resource_id_t{1}, resource_role_t::sample) ==
          resource_id_t{2});
}

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
    CHECK(history.document.resources.back().name == "one.png");
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

TEST_CASE("resource import registers decoded asset and catalog entry in one history command",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto imported =
        apply_resource_asset_import(history, 4,
                                    {.source_path = "/tmp/new-audio.wav",
                                     .role = resource_role_t::sample,
                                     .name = "New audio",
                                     .allocate_bms_identifier = true,
                                     .audio_stream = audio_stream_info_t{48'000, 2, 48'000, true}});
    REQUIRE(imported);
    REQUIRE(history.document.audio_assets.size() == 4);
    REQUIRE(history.document.resources.size() == 6);
    CHECK(history.document.resources.back().asset == history.document.audio_assets.back().id);
    CHECK(history.document.resources.back().target_identities.front().identifier == "01");
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.audio_assets.size() == 3);
    CHECK(history.document.resources.size() == 5);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.resources.size() == 6);

    const auto reused =
        apply_resource_asset_import(history, history.document.revision,
                                    {.source_path = "/tmp/new-audio.wav",
                                     .role = resource_role_t::bgm,
                                     .audio_stream = audio_stream_info_t{48'000, 2, 48'000, true}});
    REQUIRE(reused);
    CHECK(history.document.audio_assets.size() == 4);
    CHECK(history.document.resources.back().asset == history.document.audio_assets.back().id);
}

TEST_CASE("resource import rejects path and decoded-type errors without partial assets",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    CHECK(has_code(apply_resource_asset_import(history, 3,
                                               {.source_path = "/tmp/new.png",
                                                .role = resource_role_t::bga,
                                                .image_geometry = std::pair{640U, 480U}}),
                   diagnostic_code_t::invalid_id));
    CHECK(has_code(apply_resource_asset_import(history, 4,
                                               {.source_path = "relative.png",
                                                .role = resource_role_t::bga,
                                                .image_geometry = std::pair{640U, 480U}}),
                   diagnostic_code_t::invalid_id));
    CHECK(has_code(
        apply_resource_asset_import(history, 4,
                                    {.source_path = "/tmp/new.png",
                                     .role = resource_role_t::bga,
                                     .audio_stream = audio_stream_info_t{48'000, 2, 100, true}}),
        diagnostic_code_t::unsupported_feature));
    CHECK(history.document.visual_assets.size() == 2);
    CHECK(history.document.resources.size() == 5);
    CHECK(history.undo.empty());
}

TEST_CASE("copied resource import binds a reserved project path in one undo command",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto reserved = next_resource_asset_id(history.document);
    REQUIRE(reserved);
    const auto relative =
        std::filesystem::path{"assets"} / (std::to_string(reserved->value) + "-new-audio.wav");
    const import_resource_asset_action_t action{.source_path = "/tmp/new-audio.wav",
                                                .copied_project_relative_path = relative,
                                                .expected_new_asset_id = *reserved,
                                                .role = resource_role_t::bgm,
                                                .name = "Copied audio",
                                                .audio_stream =
                                                    audio_stream_info_t{48'000, 2, 48'000, true}};
    const auto imported = apply_resource_asset_import(history, history.document.revision, action);
    REQUIRE(imported);
    CHECK(history.document.audio_assets.back().id == *reserved);
    CHECK(history.document.audio_assets.back().source_path == relative);
    CHECK(history.document.resources.back().asset == *reserved);
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.audio_assets.size() == 3);
    CHECK(history.document.resources.size() == 5);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.audio_assets.back().source_path == relative);

    auto invalid = action;
    invalid.copied_project_relative_path = "assets/../outside.wav";
    CHECK(has_code(apply_resource_asset_import(history, history.document.revision, invalid),
                   diagnostic_code_t::invalid_id));
    invalid = action;
    invalid.expected_new_asset_id = asset_id_t{reserved->value + 8};
    CHECK(has_code(apply_resource_asset_import(history, history.document.revision, invalid),
                   diagnostic_code_t::invalid_id));
}

TEST_CASE("export-staged import reserves a new asset while retaining its external source",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto reserved = next_resource_asset_id(history.document);
    REQUIRE(reserved);
    const import_resource_asset_action_t action{.source_path = "/tmp/export-audio.ogg",
                                                .expected_new_asset_id = *reserved,
                                                .role = resource_role_t::bgm,
                                                .audio_stream =
                                                    audio_stream_info_t{44'100, 2, 22'050, true}};
    REQUIRE(apply_resource_asset_import(history, history.document.revision, action));
    CHECK(history.document.audio_assets.back().id == *reserved);
    CHECK(history.document.audio_assets.back().source_path == action.source_path);
    CHECK(history.undo.size() == 1);
    CHECK(has_code(apply_resource_asset_import(history, history.document.revision, action),
                   diagnostic_code_t::invalid_id));
}

TEST_CASE("visual resource import records decoded geometry and BMP identity",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto imported = apply_resource_asset_import(
        history, 4,
        {.source_path = "/tmp/new-background.png",
         .role = resource_role_t::bga,
         .allocate_bms_identifier = true,
         .image_geometry = std::pair{640U, 480U}});
    REQUIRE(imported);
    REQUIRE(history.document.visual_assets.size() == 3);
    REQUIRE(history.document.resources.size() == 6);
    const auto& asset = history.document.visual_assets.back();
    CHECK(asset.kind == asset_kind_t::image);
    CHECK(asset.width == 640);
    CHECK(asset.height == 480);
    CHECK(history.document.resources.back().asset == asset.id);
    CHECK(history.document.resources.back().target_identities.front().namespace_name == "BMP");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.visual_assets.size() == 2);
}

TEST_CASE("resource asset relink preserves IDs and rejects escaping relative paths",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto relinked = apply_resource_asset_relink(
        history, 4, {.asset = asset_id_t{1},
                     .source_path = "assets/drums/one.wav"});
    REQUIRE(relinked);
    CHECK(history.document.audio_assets.front().source_path ==
          std::filesystem::path{"assets/drums/one.wav"});
    CHECK(history.document.resources.front().asset == asset_id_t{1});
    CHECK(history.document.charts.front().measures.front().notes.front().sound ==
          resource_id_t{1});
    CHECK(has_code(apply_resource_asset_relink(
                       history, 5, {.asset = asset_id_t{1},
                                    .source_path = "../outside.wav"}),
                   diagnostic_code_t::invalid_id));
    const auto no_op = apply_resource_asset_relink(
        history, 5, {.asset = asset_id_t{1},
                     .source_path = "assets/drums/one.wav"});
    CHECK_FALSE(no_op);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.audio_assets.front().source_path ==
          std::filesystem::path{"one.wav"});

    const auto visual = apply_resource_asset_relink(
        history, history.document.revision,
        {.asset = asset_id_t{4}, .source_path = "assets/background.png"});
    REQUIRE(visual);
    CHECK(history.document.visual_assets.front().source_path ==
          std::filesystem::path{"assets/background.png"});
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

TEST_CASE("pointer resource placement snaps exactly and respects a requested BGM lane",
          "[unit][core][resources]") {
    auto history = make_document_history(assignment_song());
    const auto& chart = history.document.charts.front();
    const auto snapped = resolve_resource_placement_position(
        chart, {measure_id_t{3}, 190}, 8);
    REQUIRE(snapped);
    CHECK(snapped->row == 240);
    const auto tie = resolve_resource_placement_position(
        chart, {measure_id_t{3}, 180}, 8);
    REQUIRE(tie);
    CHECK(tie->row == 120);
    CHECK_FALSE(resolve_resource_placement_position(
        chart, {measure_id_t{3}, 190}, 7));

    const auto occupied = apply_resource_placement(
        history, 4, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 190},
                     .source_lane = object_id_t{20},
                     .kind = resource_placement_kind_t::bgm_event,
                     .snap_denominator = 8});
    CHECK_FALSE(occupied);
    CHECK(has_code(occupied, diagnostic_code_t::collision));
    const auto placed = apply_resource_placement(
        history, 4, {.chart = chart_id_t{2}, .resource = resource_id_t{2},
                     .position = {measure_id_t{3}, 510},
                     .source_lane = object_id_t{20},
                     .kind = resource_placement_kind_t::bgm_event,
                     .snap_denominator = 8});
    REQUIRE(placed);
    CHECK(history.document.charts.front().measures.front().bgm_events.back().position.row == 480);
    CHECK(history.document.charts.front().measures.front().bgm_events.back().source_lane ==
          object_id_t{20});
    CHECK(history.document.charts.front().bgm_source_lanes.size() == 1);
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
