#include <wormedit/core/document_index.h>
#include <wormedit/core/catalogs.h>
#include <wormedit/core/document.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <type_traits>

using namespace wormedit;

namespace {

auto make_valid_song() -> song_document_t;

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

auto has_message(const diagnostic_list_t& diagnostics, std::string_view text) -> bool {
    return std::ranges::any_of(diagnostics, [text](const auto& diagnostic) {
        return diagnostic.message.find(text) != std::string::npos;
    });
}

auto make_bms_control_song() -> song_document_t {
    auto song = make_valid_song();
    const std::string source = "#RANDOM 2\n#TITLE x\n#ENDRANDOM\n";
    auto bytes = std::make_shared<std::vector<std::byte>>();
    bytes->reserve(source.size());
    for (const auto character : source) {
        bytes->push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }

    auto& chart = song.charts.front();
    chart.metadata.format_groups.push_back({
        .family = format_family_t::bms,
        .encoding =
            {
                .detected = text_encoding_t::ascii,
                .original_bytes = bytes,
                .unchanged_since_import = true,
            },
    });
    chart.bms_control_tree = bms_control_node_t{
        .id = branch_id_t{100},
        .kind = bms_control_kind_t::root,
        .source_span = {0, source.size()},
        .children =
            {
                {
                    .id = branch_id_t{101},
                    .kind = bms_control_kind_t::random,
                    .value = 2,
                    .original_text = "#RANDOM 2",
                    .command_token = "RANDOM",
                    .raw_value = " 2",
                    .source_span = {0, 10},
                    .source_order = 0,
                    .line_ending = bms_line_ending_t::lf,
                    .close_source_span = bms_source_span_t{19, source.size()},
                    .close_text = "#ENDRANDOM",
                    .close_source_order = 2,
                    .close_line_ending = bms_line_ending_t::lf,
                    .children =
                        {
                            {
                                .id = branch_id_t{102},
                                .kind = bms_control_kind_t::raw_statement,
                                .original_text = "#TITLE x",
                                .command_token = "TITLE",
                                .raw_value = " x",
                                .source_span = {10, 19},
                                .source_order = 1,
                                .line_ending = bms_line_ending_t::lf,
                            },
                        },
                },
            },
    };
    chart.active_branch_selection = {{branch_id_t{101}, 2}};
    return song;
}

auto make_valid_song() -> song_document_t {
    lane_layout_t layout{
        .profile_id = "test-4k",
        .display_name = "Test 4K",
        .lanes =
            {
                {.id = lane_id_t{1}, .name = "Left", .display_order = 0},
                {.id = lane_id_t{2}, .name = "Down", .display_order = 1},
                {.id = lane_id_t{3}, .name = "Up", .display_order = 2},
                {.id = lane_id_t{4}, .name = "Right", .display_order = 3},
            },
    };
    input_profile_t input{
        .profile_id = "test-4k",
        .bindings =
            {
                {.key = physical_key_t::digit_1, .lane = lane_id_t{1}},
                {.key = physical_key_t::digit_2, .lane = lane_id_t{2}},
                {.key = physical_key_t::digit_3, .lane = lane_id_t{3}},
                {.key = physical_key_t::digit_4, .lane = lane_id_t{4}},
            },
    };

    chart_document_t chart{
        .id = chart_id_t{1},
        .metadata = {.name = "Normal", .author = "Fixture"},
        .source_family = format_family_t::bmson,
        .measures =
            {
                {.id = measure_id_t{1}},
                {
                    .id = measure_id_t{2},
                    .definition = {{3, 4}, meter_t{6, 8}},
                },
            },
        .lane_layouts =
            {
                {
                    .id = object_id_t{1},
                    .position = {measure_id_t{1}, 0},
                    .layout = std::move(layout),
                },
            },
        .input_profile = std::move(input),
        .semantic_layers =
            {
                {
                    .id = layer_id_t{1},
                    .name = "Vocal",
                    .color = {255, 220, 0, 255},
                },
            },
        .bgm_source_lanes =
            {
                {.id = object_id_t{2}, .name = "#00101 line 1", .source_order = 0},
            },
        .bpms =
            {
                {
                    .id = object_id_t{5},
                    .position = {measure_id_t{1}, 0},
                    .beats_per_minute = {120, 1},
                },
            },
        .pauses =
            {
                {
                    .id = object_id_t{6},
                    .position = {measure_id_t{1}, 480},
                    .kind = pause_kind_t::stop,
                    .duration = {1, 2},
                    .source_unit = pause_unit_t::bms_stop_ticks,
                },
            },
        .warps =
            {
                {
                    .id = object_id_t{7},
                    .span = {{measure_id_t{1}, 720}, {measure_id_t{2}, 0}},
                    .source_encoding = warp_provenance_t::negative_bpm,
                },
            },
        .scrolls =
            {
                {
                    .id = object_id_t{9},
                    .position = {measure_id_t{1}, 240},
                    .multiplier = {-1, 2},
                },
            },
        .speeds =
            {
                {
                    .id = object_id_t{10},
                    .position = {measure_id_t{1}, 480},
                    .multiplier = {2, 1},
                    .transition_beats = {1, 1},
                },
            },
        .kiai =
            {
                {
                    .id = object_id_t{8},
                    .position = {measure_id_t{2}, 240},
                    .enabled = true,
                    .retained_effect_bits = 9,
                },
            },
        .timing_anchors =
            {
                {
                    .id = object_id_t{11},
                    .position = {measure_id_t{1}, 720},
                    .source_sample_frame = 96'000,
                },
            },
        .bookmarks =
            {
                {
                    .id = object_id_t{12},
                    .label = "Verse",
                    .position = local_position_t{measure_id_t{1}, 240},
                },
            },
        .sections =
            {
                {
                    .id = object_id_t{13},
                    .label = "Phrase",
                    .beat_span =
                        position_span_t{
                            {measure_id_t{1}, 240},
                            {measure_id_t{1}, 480},
                        },
                },
            },
        .bga_events =
            {
                {
                    .id = object_id_t{14},
                    .position = {measure_id_t{1}, 240},
                    .resource = resource_id_t{2},
                },
            },
        .sound_channels =
            {
                {
                    .id = object_id_t{15},
                    .name = "long voice",
                    .source_clip =
                        {
                            .id = clip_id_t{1},
                            .source = asset_id_t{1},
                            .begin_frame = 0,
                            .end_frame = 96'000,
                        },
                    .events =
                        {
                            {
                                .id = object_id_t{16},
                                .position = {measure_id_t{1}, 240},
                                .end = local_position_t{measure_id_t{1}, 480},
                                .lane = lane_id_t{2},
                                .continuation = true,
                            },
                        },
                },
            },
    };
    chart.measures[0].notes.push_back({
        .id = object_id_t{3},
        .position = {measure_id_t{1}, 240},
        .lane = lane_id_t{1},
        .kind = note_kind_t::hold,
        .end = local_position_t{measure_id_t{2}, 120},
        .sound = resource_id_t{1},
        .semantic_layers = {layer_id_t{1}},
        .label = "lead",
    });
    chart.measures[0].bgm_events.push_back({
        .id = object_id_t{4},
        .position = {measure_id_t{1}, 0},
        .source_lane = object_id_t{2},
        .sound = resource_id_t{1},
        .structural_slot = 0,
    });

    return {
        .id = song_id_t{1},
        .metadata =
            {
                .title = "Document fixture",
                .artist = "wormedit",
                .ordered_search_tags = {"exact", "roundtrip"},
            },
        .audio_assets =
            {
                {
                    .id = asset_id_t{1},
                    .source_path = "kick.ogg",
                    .stream =
                        {
                            .sample_rate = 48'000,
                            .channels = 2,
                            .frame_count = 96'000,
                            .seekable = true,
                        },
                },
            },
        .visual_assets =
            {
                {
                    .id = asset_id_t{2},
                    .kind = asset_kind_t::image,
                    .source_path = "background.png",
                    .width = 1280,
                    .height = 720,
                },
            },
        .resources =
            {
                {
                    .id = resource_id_t{1},
                    .name = "kick",
                    .asset = asset_id_t{1},
                    .roles = {resource_role_t::sample, resource_role_t::bgm},
                },
                {
                    .id = resource_id_t{2},
                    .name = "background",
                    .asset = asset_id_t{2},
                    .roles = {resource_role_t::bga},
                },
            },
        .charts = {std::move(chart)},
        .output_groups =
            {
                {
                    .name = "BMSON",
                    .family = format_family_t::bmson,
                    .charts = {chart_id_t{1}},
                    .relative_output_path = "fixture.bmson",
                },
            },
    };
}

} // namespace

TEST_CASE("stable IDs never allocate the reserved zero value", "[unit][core][document]") {
    id_allocator_t allocator;
    CHECK(allocate_id<chart_id_t>(allocator) == chart_id_t{1});
    CHECK(allocate_id<measure_id_t>(allocator) == measure_id_t{2});

    allocator.next_value = std::numeric_limits<std::uint64_t>::max();
    CHECK(allocate_id<object_id_t>(allocator) ==
          object_id_t{std::numeric_limits<std::uint64_t>::max()});
    CHECK_FALSE(allocate_id<object_id_t>(allocator).has_value());
}

TEST_CASE("measure index preserves exact variable-length prefixes", "[unit][core][document]") {
    const auto song = make_valid_song();
    const auto result = build_measure_index(song.charts.front());
    REQUIRE(std::holds_alternative<measure_index_t>(result));

    const auto& index = std::get<measure_index_t>(result);
    REQUIRE(index.entries.size() == 2);
    CHECK(index.entries[0].absolute_begin_row == 0);
    CHECK(index.entries[0].row_capacity == 960);
    CHECK(index.entries[1].absolute_begin_row == 960);
    CHECK(index.entries[1].row_capacity == 720);
    CHECK(index.total_rows == 1680);
    CHECK(absolute_row(index, {measure_id_t{2}, 120}) == 1080);
    CHECK_FALSE(absolute_row(index, {measure_id_t{2}, 720}));
    CHECK_FALSE(absolute_row(index, {measure_id_t{99}, 0}));
}

namespace {

auto chart_with_measure_ids(std::initializer_list<std::uint64_t> ids) -> chart_document_t {
    auto chart = make_valid_song().charts.front();
    const auto prototype = chart.measures.front();
    chart.measures.clear();
    for (const auto id : ids) {
        auto measure = prototype;
        measure.id = measure_id_t{id};
        chart.measures.push_back(measure);
    }
    return chart;
}

} // namespace

TEST_CASE("measure index keeps document order for unsorted unique IDs", "[unit][core][document]") {
    const auto result = build_measure_index(chart_with_measure_ids({7, 3, 9}));
    REQUIRE(std::holds_alternative<measure_index_t>(result));
    const auto& index = std::get<measure_index_t>(result);
    REQUIRE(index.entries.size() == 3);
    CHECK(index.entries[0].id == measure_id_t{7});
    CHECK(index.entries[1].id == measure_id_t{3});
    CHECK(index.entries[2].id == measure_id_t{9});
    CHECK(index.entries[2].absolute_begin_row == index.entries[1].absolute_begin_row +
                                                     index.entries[1].row_capacity);
}

TEST_CASE("measure index diagnoses each later duplicate by its measure number",
          "[unit][core][document]") {
    for (const auto& ids : {std::initializer_list<std::uint64_t>{1, 1},
                            std::initializer_list<std::uint64_t>{2, 5, 2},
                            std::initializer_list<std::uint64_t>{4, 4, 9, 4}}) {
        const auto chart = chart_with_measure_ids(ids);
        const auto result = build_measure_index(chart);
        REQUIRE(std::holds_alternative<diagnostic_list_t>(result));
        const auto& diagnostics = std::get<diagnostic_list_t>(result);
        std::size_t expected = 0;
        std::vector<std::uint64_t> seen;
        for (std::size_t position = 0; position < ids.size(); ++position) {
            const auto id = *(ids.begin() + position);
            if (std::ranges::find(seen, id) != seen.end()) {
                ++expected;
                CHECK(has_message(diagnostics, "measure " + std::to_string(position + 1) +
                                                   " ID is duplicated"));
            }
            seen.push_back(id);
        }
        CHECK(std::ranges::count_if(diagnostics, [](const auto& diagnostic) {
                  return diagnostic.code == diagnostic_code_t::duplicate_id;
              }) == static_cast<std::ptrdiff_t>(expected));
    }
}

TEST_CASE("measure index still rejects zero IDs without calling them duplicates",
          "[unit][core][document]") {
    const auto result = build_measure_index(chart_with_measure_ids({0, 0, 3}));
    REQUIRE(std::holds_alternative<diagnostic_list_t>(result));
    const auto& diagnostics = std::get<diagnostic_list_t>(result);
    CHECK(has_code(diagnostics, diagnostic_code_t::invalid_id));
    CHECK_FALSE(has_code(diagnostics, diagnostic_code_t::duplicate_id));
}

TEST_CASE("loss-aware document aggregates validate as one graph", "[unit][core][document]") {
    const auto song = make_valid_song();
    const auto diagnostics = validate_document(song);
    CHECK(diagnostics.empty());

    const auto& chart = song.charts.front();
    CHECK(chart.divisions_per_standard_measure == 960);
    CHECK(chart.measures[0].notes[0].label == "lead");
    CHECK(chart.warps[0].source_encoding == warp_provenance_t::negative_bpm);
    CHECK(chart.kiai[0].retained_effect_bits == 9);
    CHECK(song.metadata.ordered_search_tags == std::vector<std::string>{"exact", "roundtrip"});
}

TEST_CASE("document validation diagnoses dangling and impossible objects",
          "[unit][core][document][negative]") {
    auto song = make_valid_song();
    auto& chart = song.charts.front();
    chart.measures[1].id = measure_id_t{1};

    auto diagnostics = validate_document(song);
    CHECK(has_code(diagnostics, diagnostic_code_t::duplicate_id));

    song = make_valid_song();
    song.charts.front().measures[0].notes[0].sound = resource_id_t{99};
    diagnostics = validate_document(song);
    CHECK(has_code(diagnostics, diagnostic_code_t::dangling_resource));

    song = make_valid_song();
    auto& note = song.charts.front().measures.front().notes.front();
    note.end = local_position_t{measure_id_t{1}, 120};
    diagnostics = validate_document(song);
    CHECK(has_code(diagnostics, diagnostic_code_t::invalid_hold));

    song = make_valid_song();
    song.charts.front().measures.front().definition.length = {1, 7};
    diagnostics = validate_document(song);
    CHECK(has_code(diagnostics, diagnostic_code_t::invalid_measure));

    song = make_valid_song();
    auto misplaced = song.charts.front().measures.front().notes.front();
    misplaced.id = object_id_t{20};
    misplaced.position.measure = measure_id_t{1};
    song.charts.front().measures[1].notes.push_back(misplaced);
    diagnostics = validate_document(song);
    CHECK(has_code(diagnostics, diagnostic_code_t::invalid_measure));

    song = make_valid_song();
    auto& changed_layout_chart = song.charts.front();
    auto changed_layout = changed_layout_chart.lane_layouts.front().layout;
    changed_layout.lanes.erase(changed_layout.lanes.begin());
    changed_layout_chart.lane_layouts.push_back({
        .id = object_id_t{20},
        .position = {measure_id_t{2}, 0},
        .layout = std::move(changed_layout),
    });
    changed_layout_chart.measures[1].notes.push_back({
        .id = object_id_t{21},
        .position = {measure_id_t{2}, 120},
        .lane = lane_id_t{1},
    });
    diagnostics = validate_document(song);
    CHECK(has_code(diagnostics, diagnostic_code_t::unknown_lane));
}

TEST_CASE("BMS branch graphs validate IDs, selections and source spans",
          "[unit][core][document][bms]") {
    auto song = make_bms_control_song();
    CHECK(validate_document(song).empty());

    song = make_bms_control_song();
    song.charts.front().bms_control_tree->children.front().children.front().id = branch_id_t{101};
    CHECK(has_code(validate_document(song), diagnostic_code_t::duplicate_id));

    song = make_bms_control_song();
    song.charts.front().bms_control_tree->children.front().id = branch_id_t{};
    CHECK(has_code(validate_document(song), diagnostic_code_t::invalid_id));

    song = make_bms_control_song();
    song.charts.front().active_branch_selection.front().selected_value = 3;
    CHECK(has_message(validate_document(song), "outside its declared range"));

    song = make_bms_control_song();
    song.charts.front().active_branch_selection.front().branch = branch_id_t{102};
    CHECK(has_message(validate_document(song), "must refer to a RANDOM or SWITCH block"));

    song = make_bms_control_song();
    song.charts.front().bms_control_tree->children.front().children.front().source_span = {8, 19};
    CHECK(has_message(validate_document(song), "escapes its containing control block"));

    song = make_bms_control_song();
    song.charts.front().bms_control_tree->children.front().close_source_span =
        bms_source_span_t{8, 31};
    CHECK(has_code(validate_document(song), diagnostic_code_t::invalid_id));

    song = make_bms_control_song();
    auto& unclosed_random = song.charts.front().bms_control_tree->children.front();
    unclosed_random.close_source_span.reset();
    unclosed_random.close_text.reset();
    unclosed_random.close_source_order.reset();
    unclosed_random.close_line_ending.reset();
    song.charts.front().active_branch_selection.clear();
    CHECK(has_message(validate_document(song), "missing its closing directive"));

    song = make_bms_control_song();
    auto& unclosed_if_group = song.charts.front().bms_control_tree->children.front();
    unclosed_if_group.kind = bms_control_kind_t::if_group;
    unclosed_if_group.value.reset();
    unclosed_if_group.source_span = {0, 0};
    unclosed_if_group.original_text.clear();
    unclosed_if_group.command_token.clear();
    unclosed_if_group.raw_value.clear();
    unclosed_if_group.source_order = 0;
    unclosed_if_group.line_ending = bms_line_ending_t::none;
    unclosed_if_group.close_source_span.reset();
    unclosed_if_group.close_text.reset();
    unclosed_if_group.close_source_order.reset();
    unclosed_if_group.close_line_ending.reset();
    unclosed_if_group.children.front().kind = bms_control_kind_t::if_branch;
    unclosed_if_group.children.front().value = 1;
    song.charts.front().active_branch_selection.clear();
    CHECK(has_message(validate_document(song), "missing its closing directive"));

    song = make_bms_control_song();
    auto& unclosed_switch = song.charts.front().bms_control_tree->children.front();
    unclosed_switch.kind = bms_control_kind_t::switch_block;
    unclosed_switch.value = 1;
    unclosed_switch.close_source_span.reset();
    unclosed_switch.close_text.reset();
    unclosed_switch.close_source_order.reset();
    unclosed_switch.close_line_ending.reset();
    song.charts.front().active_branch_selection.clear();
    CHECK(has_message(validate_document(song), "missing its closing directive"));
}

TEST_CASE("resource identities are valid and unique per target family",
          "[unit][core][document][resources]") {
    auto song = make_valid_song();
    song.resources[0].target_identities = {
        {format_family_t::bms, "a1", "WAV"},
        {format_family_t::bmson, "sample-a"},
    };
    song.resources[0].roles = {resource_role_t::sample, resource_role_t::bgm};
    CHECK(validate_document(song).empty());

    song.resources[1].target_identities = {{format_family_t::bms, "A1", "BMP"}};
    CHECK(validate_document(song).empty());

    song.resources[1].target_identities = {{format_family_t::bms, "A1", "wav"}};
    CHECK(has_message(validate_document(song), "assigned more than once"));

    song = make_valid_song();
    song.resources[0].target_identities = {
        {format_family_t::bms, "01", "WAV"},
        {format_family_t::bms, "01", "wav"},
    };
    CHECK(has_code(validate_document(song), diagnostic_code_t::duplicate_id));

    song = make_valid_song();
    song.resources[0].target_identities = {{format_family_t::bms, " \t", "WAV"}};
    CHECK(has_message(validate_document(song), "empty or invalid namespace/identifier"));

    song = make_valid_song();
    song.resources[0].target_identities = {{format_family_t::bms, "01"}};
    CHECK(has_message(validate_document(song), "empty or invalid namespace/identifier"));

    song = make_valid_song();
    song.resources[0].target_identities = {
        {static_cast<format_family_t>(255), "opaque-id"},
    };
    CHECK(has_message(validate_document(song), "unknown format family"));

    song = make_valid_song();
    song.resources[0].asset = asset_id_t{99};
    CHECK(has_code(validate_document(song), diagnostic_code_t::dangling_asset));

    song = make_valid_song();
    song.charts.front().sound_channels.front().source_clip.source = asset_id_t{2};
    CHECK(has_message(validate_document(song), "unknown audio asset"));
}

TEST_CASE("input collisions are scoped by focused stage", "[unit][core][input]") {
    const lane_layout_t layout{
        .lanes =
            {
                {.id = lane_id_t{1}, .name = "L", .stage = stage_group_t::left},
                {.id = lane_id_t{2}, .name = "R", .stage = stage_group_t::right},
            },
    };
    input_profile_t banked{
        .profile_id = "banked",
        .bindings =
            {
                {
                    .key = physical_key_t::key_z,
                    .lane = lane_id_t{1},
                    .focused_stage = 0,
                },
                {
                    .key = physical_key_t::key_z,
                    .lane = lane_id_t{2},
                    .focused_stage = 1,
                },
            },
        .stage_count = 2,
        .requires_focused_stage = true,
    };
    CHECK(validate_input_profile(layout, banked).empty());

    banked.bindings[1].focused_stage = 0;
    const auto diagnostics = validate_input_profile(layout, banked);
    CHECK(has_code(diagnostics, diagnostic_code_t::duplicate_binding));
}

TEST_CASE("exact redivision previews every moved position", "[unit][core][document]") {
    const auto song = make_valid_song();
    const auto result = redivision_plan_t::from(song.charts.front(), 192);
    REQUIRE(result.value.chart.has_value());
    REQUIRE(result.value.chart.has_value());
    CHECK(result.value.chart->divisions_per_standard_measure == 192);
    CHECK(result.value.chart->measures[0].notes[0].position.row == 48);
    CHECK(result.value.chart->measures[0].notes[0].end->row == 24);
    CHECK(result.value.chart->pauses[0].position.row == 96);
    CHECK(result.value.chart->warps[0].span.begin.row == 144);
    CHECK(result.value.chart->kiai[0].position.row == 48);
    CHECK(result.value.chart->scrolls[0].position.row == 48);
    CHECK(result.value.chart->speeds[0].position.row == 96);
    CHECK(result.value.chart->timing_anchors[0].position.row == 144);
    CHECK(result.value.chart->bookmarks[0].position->row == 48);
    CHECK(result.value.chart->sections[0].beat_span->begin.row == 48);
    CHECK(result.value.chart->sections[0].beat_span->end.row == 96);
    CHECK(result.value.chart->bga_events[0].position.row == 48);
    CHECK(result.value.chart->sound_channels[0].events[0].position.row == 48);
    CHECK(result.value.chart->sound_channels[0].events[0].end->row == 96);
    CHECK(result.value.moves.size() == 14);

    const auto index = build_measure_index(*result.value.chart);
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    CHECK(std::get<measure_index_t>(index).total_rows == 336);
}

TEST_CASE("inexact redivision rejects atomically", "[unit][core][document][negative]") {
    auto song = make_valid_song();
    auto& original = song.charts.front();
    original.bookmarks.front().position->row = 241;

    const auto result = redivision_plan_t::from(original, 192);
    CHECK_FALSE(result.value.chart.has_value());
    CHECK_FALSE(result.value.chart.has_value());
    CHECK(result.value.moves.empty());
    CHECK(has_code(result.diagnostics, diagnostic_code_t::row_out_of_range));
    CHECK(original.divisions_per_standard_measure == 960);
    CHECK(original.bookmarks.front().position->row == 241);

    original.bookmarks.front().position = local_position_t{measure_id_t{99}, 0};
    const auto unknown_measure = redivision_plan_t::from(original, 1920);
    CHECK_FALSE(unknown_measure.value.chart.has_value());
    CHECK(has_code(unknown_measure.diagnostics, diagnostic_code_t::row_out_of_range));
}

TEST_CASE("measure buckets validate and index fifty thousand notes",
          "[unit][core][document][stress]") {
    song_document_t song{.id = song_id_t{1}};
    chart_document_t chart{.id = chart_id_t{1}};
    chart.measures.reserve(1'000);
    std::uint64_t next_note_id = 10'000;
    for (std::uint64_t measure_index = 0; measure_index < 1'000; ++measure_index) {
        measure_chunk_t measure{.id = measure_id_t{measure_index + 1}};
        measure.notes.reserve(50);
        for (std::uint64_t row = 0; row < 50; ++row) {
            measure.notes.push_back({
                .id = object_id_t{next_note_id++},
                .position = {measure.id, row * 19},
                .lane = lane_id_t{1},
            });
        }
        chart.measures.push_back(std::move(measure));
    }
    chart.lane_layouts.push_back({
        .id = object_id_t{1},
        .position = {chart.measures.front().id, 0},
        .layout =
            {
                .profile_id = "stress-1k",
                .lanes = {{.id = lane_id_t{1}, .name = "Key"}},
            },
    });
    song.charts.push_back(std::move(chart));

    CHECK(validate_document(song).empty());
    const auto index = build_measure_index(song.charts.front());
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    CHECK(std::get<measure_index_t>(index).entries.size() == 1'000);
    CHECK(std::get<measure_index_t>(index).total_rows == 960'000);
    std::size_t note_count{};
    for (const auto& measure : song.charts.front().measures) {
        note_count += measure.notes.size();
    }
    CHECK(note_count == 50'000);
}

TEST_CASE("BMS field and measure projection provenance validates as parallel bounded data",
          "[unit][core][document][bms][provenance]") {
    auto song = make_valid_song();
    auto& chart = song.charts.front();
    song.metadata.format_groups.push_back({
        .family = format_family_t::bms,
        .ordered_fields = {{"TITLE", "one"}, {"ARTIST", "two"}},
        .ordered_field_source_orders = {10, 11},
    });
    chart.bms_measure_length_projections.push_back({
        .measure_index = 0,
        .source_order = 12,
        .imported_length = {1, 1},
    });
    chart.bms_bpm_projections.push_back({
        .event = object_id_t{5},
        .source_order = 0,
        .imported_position = {measure_id_t{1}, 0},
        .imported_beats_per_minute = {120, 1},
    });
    chart.bms_pause_projections.push_back({
        .event = object_id_t{6},
        .source_order = 0,
        .imported_position = {measure_id_t{1}, 480},
        .imported_kind = pause_kind_t::stop,
        .imported_duration = {1, 2},
        .imported_source_unit = pause_unit_t::bms_stop_ticks,
    });
    CHECK(validate_document(song).empty());

    chart.bms_bpm_projections.front().source_order = 9;
    CHECK(has_message(validate_document(song), "source identity does not match"));
    chart.bms_bpm_projections.front().source_order = 0;
    chart.bms_pause_projections.push_back(chart.bms_pause_projections.front());
    CHECK(has_message(validate_document(song), "pause projection is duplicated"));
    chart.bms_pause_projections.pop_back();

    song.metadata.format_groups.back().ordered_field_source_orders.push_back(12);
    CHECK(has_message(validate_document(song), "more source-order identities than fields"));
    song.metadata.format_groups.back().ordered_field_source_orders = {10, 10};
    CHECK(has_message(validate_document(song), "source-order identity is duplicated"));

    song.metadata.format_groups.back().ordered_field_source_orders = {10, 11};
    chart.bms_measure_length_projections.push_back({
        .measure_index = 0,
        .source_order = 13,
        .imported_length = {1, 1},
    });
    CHECK(has_message(validate_document(song), "projection is duplicated"));
    chart.bms_measure_length_projections.back().measure_index = 99;
    chart.bms_measure_length_projections.back().imported_length = {0, 1};
    const auto invalid_projection = validate_document(song);
    CHECK(has_message(invalid_projection, "unknown measure index"));
    CHECK(has_message(invalid_projection, "baseline must be positive"));
}

namespace {
void check_owner_query(bool value) { CHECK(value); }
void require_owner_query(bool value) { REQUIRE(value); }
}

TEST_CASE("Document owner queries preserve first matches and mutable access", "[unit][document]") {
    song_document_t song;
    song.charts = {{.id = chart_id_t{7}}, {.id = chart_id_t{7}}};
    song.audio_assets = {{.id = asset_id_t{9}}, {.id = asset_id_t{9}}};
    auto& chart = song.charts.front();
    chart.measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}};
    chart.measures.front().notes = {{.id = object_id_t{10}}};
    chart.measures.back().notes = {{.id = object_id_t{10}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{11}}};
    chart.measures.back().bgm_events = {{.id = object_id_t{11}}};
    check_owner_query(song.find_chart(chart_id_t{7}) == &song.charts.front());
    check_owner_query(find_chart(song, chart_id_t{7}) == song.find_chart(chart_id_t{7}));
    check_owner_query(song.find_audio_asset(asset_id_t{9}) == &song.audio_assets.front());
    check_owner_query(chart.find_measure(measure_id_t{4}) == &chart.measures.back());
    check_owner_query(chart.find_note(object_id_t{10}) == &chart.measures.front().notes.front());
    check_owner_query(chart.find_bgm_event(object_id_t{11}) == &chart.measures.front().bgm_events.front());
    chart.find_note(object_id_t{10})->label = "changed";
    const auto& read_only = song;
    check_owner_query(read_only.find_chart(chart_id_t{7})->find_note(object_id_t{10})->label == "changed");
    check_owner_query(read_only.find_audio_asset(asset_id_t{9}) == &song.audio_assets.front());
    check_owner_query(!song.find_chart(chart_id_t{8}) && !song.find_audio_asset(asset_id_t{8}));
    check_owner_query(!chart.find_measure(measure_id_t{8}) && !chart.find_note(object_id_t{8}) &&
                       !chart.find_bgm_event(object_id_t{8}));
}

TEST_CASE("Lane query equality keeps full identity separate from source shape", "[unit][document]") {
    lane_layout_t layout{.profile_id = "test", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}};
    check_owner_query(layout.has_lane(lane_id_t{1}) && !layout.has_lane(lane_id_t{2}));
    check_owner_query(layout.find(lane_id_t{1}) == &layout.lanes.front());
    check_owner_query(layout.lanes.front().is_editable());
    auto changed = layout;
    changed.lanes.front().id = lane_id_t{2};
    changed.lanes.front().stage = stage_group_t::left;
    changed.display_name = "Presentation";
    check_owner_query(changed != layout && changed.has_same_shape(layout));
    changed.lanes.front().bms_channels.mine = "D1";
    check_owner_query(!changed.has_same_shape(layout));
    for (const auto role : {lane_role_t::autoplay, lane_role_t::invisible, lane_role_t::mine}) {
        changed.lanes.front().role = role;
        check_owner_query(!changed.lanes.front().is_editable());
    }
    const target_resource_identity_t id{format_family_t::bms, "01", "wav"};
    auto other = id;
    check_owner_query(id == other);
    other.namespace_name = "bmp";
    check_owner_query(id != other);
}

TEST_CASE("Layout query row overload preserves unordered segments and later ties", "[unit][document]") {
    chart_document_t chart{.divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}}};
    chart.lane_layouts = {
        {.position = {measure_id_t{1}, 240}, .layout = {.profile_id = "first"}},
        {.position = {measure_id_t{1}, 0}, .layout = {.profile_id = "zero"}},
        {.position = {measure_id_t{1}, 240}, .layout = {.profile_id = "last"}},
        {.position = {measure_id_t{2}, 0}, .layout = {.profile_id = "invalid"}},
    };
    const auto built = build_measure_index(chart);
    require_owner_query(std::holds_alternative<measure_index_t>(built));
    const auto& index = std::get<measure_index_t>(built);
    check_owner_query(layout_at(chart, index, std::uint64_t{239}) == &chart.lane_layouts[1].layout);
    check_owner_query(layout_at(chart, index, std::uint64_t{240}) == &chart.lane_layouts[2].layout);
    check_owner_query(layout_at(chart, index, local_position_t{measure_id_t{1}, 240}) ==
                       layout_at(chart, index, std::uint64_t{240}));
    check_owner_query(!layout_at(chart, index, local_position_t{measure_id_t{2}, 0}));
    // Row callers deliberately support virtual chart tails.
    check_owner_query(layout_at(chart, index, std::uint64_t{1920}) == &chart.lane_layouts[2].layout);
}

TEST_CASE("BMS control predicates keep random scope distinctions", "[unit][document]") {
    check_owner_query(is_choice_control(bms_control_kind_t::random));
    check_owner_query(is_branch_boundary(bms_control_kind_t::random));
    check_owner_query(!is_branch_scope(bms_control_kind_t::random));
    check_owner_query(is_branch_scope(bms_control_kind_t::if_group));
    check_owner_query(is_branch_body(bms_control_kind_t::case_branch));
    check_owner_query(is_switch_branch(bms_control_kind_t::default_branch));
    check_owner_query(!is_conditional_branch(bms_control_kind_t::default_branch));
    check_owner_query(has_control_value(bms_control_kind_t::set_random));
    check_owner_query(!has_control_value(bms_control_kind_t::else_branch));
    check_owner_query(can_have_control_close(bms_control_kind_t::switch_block));
    check_owner_query(!is_valid_control_kind(static_cast<bms_control_kind_t>(100)));
}

static_assert(std::is_aggregate_v<song_document_t>);
static_assert(std::is_aggregate_v<chart_document_t>);
static_assert(std::is_aggregate_v<lane_layout_t>);
static_assert(std::is_aggregate_v<lane_definition_t>);
static_assert(std::is_aggregate_v<bms_channel_map_t>);
static_assert(std::is_aggregate_v<measure_index_entry_t>);
static_assert(std::is_aggregate_v<measure_index_t>);
static_assert(std::is_aggregate_v<measure_lookup_t>);
static_assert(std::is_aggregate_v<chart_metadata_t>);
static_assert(std::is_aggregate_v<song_metadata_t>);

TEST_CASE("Measure row owners agree at shortened measure boundaries",
          "[unit][core][document][measure-lookup]") {
    const measure_index_t index{
        .entries = {{measure_id_t{1}, 0, 960}, {measure_id_t{2}, 960, 480}},
        .total_rows = 1440};
    const auto lookup = measure_lookup_t::from(index);
    for (const auto row : std::array<std::uint64_t, 4>{0, 959, 960, 1439}) {
        const auto position = index.position_at(row);
        REQUIRE(position);
        check_owner_query(index.row_of(*position) == row);
        check_owner_query(lookup.row_of(*position) == row);
        check_owner_query(absolute_row(index, *position) == row);
    }
    check_owner_query(index.entry_for(measure_id_t{2}) == &index.entries.back());
    check_owner_query(lookup.entry_for(measure_id_t{2}) == &index.entries.back());
    check_owner_query(!index.row_of({measure_id_t{2}, 480}));
    check_owner_query(!lookup.row_of({measure_id_t{2}, 480}));
    check_owner_query(!index.position_at(index.total_rows));
}

TEST_CASE("Measure chart end requires an explicit policy",
          "[unit][core][document][measure-lookup]") {
    const measure_index_t index{
        .entries = {{measure_id_t{1}, 0, 960}, {measure_id_t{2}, 960, 480}},
        .total_rows = 1440};
    const auto lookup = measure_lookup_t::from(index);
    const local_position_t end{measure_id_t{2}, 480};
    const auto policy = measure_chart_end_policy_t::allow;
    check_owner_query(index.position_at(index.total_rows, policy) == end);
    check_owner_query(index.row_of(end, policy) == index.total_rows);
    check_owner_query(lookup.row_of(end, policy) == index.total_rows);
    check_owner_query(!index.row_of({measure_id_t{1}, 960}, policy));
    check_owner_query(!lookup.row_of({measure_id_t{9}, 480}, policy));
    check_owner_query(!index.position_at(index.total_rows + 1, policy));
}

TEST_CASE("Measure lookup retains the first duplicate identity and its bucket",
          "[unit][core][document][measure-lookup]") {
    const measure_index_t index{
        .entries = {{measure_id_t{7}, 0, 960}, {measure_id_t{7}, 960, 480}},
        .total_rows = 1440};
    const auto lookup = measure_lookup_t::from(index);
    check_owner_query(lookup.entry_for(measure_id_t{7}) == &index.entries.front());
    check_owner_query(index.entry_for(measure_id_t{7}) == &index.entries.front());
    check_owner_query(lookup.bucket_for(measure_id_t{7}) == 0);
    check_owner_query(lookup.row_of({measure_id_t{7}, 479}) == 479);
    check_owner_query(!lookup.row_of({measure_id_t{7}, 960}));
    check_owner_query(!lookup.bucket_for(measure_id_t{9}));
}

TEST_CASE("Measure row owners retain checked addition without an extra total-row bound",
          "[unit][core][document][measure-lookup]") {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const measure_index_t index{
        .entries = {{measure_id_t{1}, maximum - 1, 4}}, .total_rows = maximum};
    const auto lookup = measure_lookup_t::from(index);
    check_owner_query(!index.entries.front().row_of(2));
    check_owner_query(!index.row_of({measure_id_t{1}, 2}));
    check_owner_query(!lookup.row_of({measure_id_t{1}, 2}));
    check_owner_query(!absolute_row(index, {measure_id_t{1}, 2}));
    check_owner_query(index.row_of({measure_id_t{1}, 1}) == maximum);
    check_owner_query(lookup.row_of({measure_id_t{1}, 1}) == maximum);
}

TEST_CASE("Empty and default measure lookups reject every position",
          "[unit][core][document][measure-lookup]") {
    const measure_index_t index;
    const auto lookup = measure_lookup_t::from(index);
    const measure_lookup_t unbound;
    for (const auto policy : {measure_chart_end_policy_t::reject,
                             measure_chart_end_policy_t::allow}) {
        check_owner_query(!index.position_at(0, policy));
        check_owner_query(!index.row_of({measure_id_t{1}, 0}, policy));
        check_owner_query(!lookup.row_of({measure_id_t{1}, 0}, policy));
        check_owner_query(!unbound.row_of({measure_id_t{1}, 0}, policy));
    }
    check_owner_query(lookup.entry_for(measure_id_t{1}) == nullptr);
    check_owner_query(!lookup.bucket_for(measure_id_t{1}));
}

namespace {
void check_fact_catalog(bool condition) { REQUIRE(condition); }
}
TEST_CASE("Core fact catalogs retain offered policies labels and BMS extensions", "[core][catalog][D9][D16][D17]") {
    check_fact_catalog(!format_families[static_cast<std::size_t>(format_family_t::midi)].offered_as_output);
    check_fact_catalog(!format_families[static_cast<std::size_t>(format_family_t::quaver)].offered_as_output);
    for (const auto& family : format_families) check_fact_catalog(family.offered_as_metadata);
    check_fact_catalog(sound_kind_label(sound_kind_t::keysound) == "Keysound");
    check_fact_catalog(is_bms_extension(".bml"));
    check_fact_catalog(!is_bms_extension(".bmson"));
    check_fact_catalog(text_encoding_label(text_encoding_t::windows_1252) == "Windows-1252");
    check_fact_catalog(text_encoding_iconv_name(text_encoding_t::windows_1252) == "WINDOWS-1252");
    check_fact_catalog(!text_encoding_iconv_name(text_encoding_t::utf8));
    check_fact_catalog(!bms_text_encodings[0].offered);
    check_fact_catalog(bms_line_ending_bytes(bms_line_ending_t::crlf) == "\r\n");
}

TEST_CASE("Note catalog preserves enum identity labels and palette shortcuts", "[core][catalog]") {
    check_fact_catalog(note_kind_label(note_kind_t::invisible) == "Invisible note");
    check_fact_catalog(note_kinds[static_cast<std::size_t>(note_kind_t::roll)].shortcut_digit == 8);
    check_fact_catalog(note_kinds[static_cast<std::size_t>(note_kind_t::mine)].shortcut_digit == 4);
    check_fact_catalog(note_palette_kinds.front() == note_kind_t::tap);
    check_fact_catalog(note_palette_kinds.back() == note_kind_t::roll);
    check_fact_catalog(bms_extension_for_profile("pms-standard-9button") == ".pms");
    check_fact_catalog(bms_extension_for_profile("bme-7k-outside-scratch") == ".bme");
    check_fact_catalog(bms_extension_for_profile("bml-custom") == ".bml");
    check_fact_catalog(bms_extension_for_profile("custom") == ".bms");
    check_fact_catalog(format_family_label(static_cast<format_family_t>(-1)) == "Unknown");
    check_fact_catalog(text_encoding_label(static_cast<text_encoding_t>(100)) == "unknown");
}

TEST_CASE("Document pointer indexes retain first matches and borrow the source values", "[core][document-index]") {
    resource_entry_t resources[]{
        {.id = resource_id_t{1}, .name = "first"},
        {.id = resource_id_t{1}, .name = "duplicate"},
        {.id = resource_id_t{0}, .name = "zero"}};
    const auto index = document_id_index_t<resource_entry_t>::from(resources);
    CHECK(index.find(resource_id_t{1}) == &resources[0]);
    CHECK(index.at(resource_id_t{0}) == &resources[2]);
    resources[0].name = "changed";
    CHECK(index.find(resource_id_t{1})->name == "changed");
}
TEST_CASE("Document pointer indexes preserve missing-key lookup contracts", "[core][document-index]") {
    const auto index = document_id_index_t<audio_asset_t>::from({});
    CHECK_FALSE(index.has_entry(asset_id_t{1}));
    CHECK(index.find(asset_id_t{1}) == nullptr);
    CHECK_THROWS_AS(index.at(asset_id_t{1}), std::out_of_range);
}
