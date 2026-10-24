#include <wormedit/core/chart_editor.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/preferences.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <limits>
#include <source_location>

using namespace wormedit;

namespace {
void expect_chart(bool value, std::source_location where = std::source_location::current()) {
    INFO("chart check caller line " << where.line());
    CHECK(value);
}

void require_chart(bool value, std::source_location where = std::source_location::current()) {
    INFO("chart requirement caller line " << where.line());
    REQUIRE(value);
}

auto chart_song() -> song_document_t {
    song_document_t song{.id = song_id_t{1}, .revision = 4};
    song.audio_assets = {{.id = asset_id_t{50}, .source_path = "shared.wav"}};
    song.resources = {{.id = resource_id_t{51}, .name = "shared", .asset = asset_id_t{50}}};
    chart_document_t chart{.id = chart_id_t{2}, .metadata = {.name = "Normal"}};
    chart.measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}};
    chart.lane_layouts = {{.id = object_id_t{5}, .position = {measure_id_t{3}, 0},
        .layout = {.profile_id = "test", .lanes = {{.id = lane_id_t{6}, .name = "Key"}}}}};
    chart.input_profile = {.profile_id = "test", .bindings = {{physical_key_t::key_t, lane_id_t{6}}}};
    chart.semantic_layers = {{.id = layer_id_t{7}, .name = "Layer"}};
    chart.measures[0].notes = {{.id = object_id_t{8}, .position = {measure_id_t{3}, 120},
        .lane = lane_id_t{6}, .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{4}, 120},
        .sound = resource_id_t{51}, .sound_channel_event = object_id_t{10},
        .semantic_layers = {layer_id_t{7}}}};
    chart.sound_channels = {{.id = object_id_t{9}, .name = "Channel",
        .source_clip = {.id = clip_id_t{11}, .source = asset_id_t{50}, .end_frame = 44100},
        .events = {{.id = object_id_t{10}, .position = {measure_id_t{3}, 120},
                    .end = local_position_t{measure_id_t{4}, 120}, .lane = lane_id_t{6}}}}};
    chart.bgm_source_lanes = {{.id = object_id_t{12}, .name = "BGM"}};
    chart.measures[0].bgm_events = {{.id = object_id_t{13}, .position = {measure_id_t{3}, 240},
        .source_lane = object_id_t{12}, .sound = resource_id_t{51}}};
    chart.bpms = {{.id = object_id_t{14}, .position = {measure_id_t{3}, 0}, .beats_per_minute = {120, 1}}};
    chart.pauses = {{.id = object_id_t{15}, .position = {measure_id_t{3}, 480}, .duration = {1, 4}}};
    chart.warps = {{.id = object_id_t{16}, .span = {{measure_id_t{3}, 600}, {measure_id_t{3}, 720}}}};
    chart.scrolls = {{.id = object_id_t{17}, .position = {measure_id_t{3}, 0}}};
    chart.speeds = {{.id = object_id_t{18}, .position = {measure_id_t{3}, 0}}};
    chart.kiai = {{.id = object_id_t{19}, .position = {measure_id_t{3}, 0}, .enabled = true}};
    chart.timing_anchors = {{.id = object_id_t{20}, .position = {measure_id_t{3}, 0}}};
    chart.bookmarks = {{.id = object_id_t{21}, .label = "Start", .position = local_position_t{measure_id_t{3}, 0}}};
    chart.sections = {{.id = object_id_t{22}, .label = "Verse",
        .beat_span = position_span_t{{measure_id_t{3}, 0}, {measure_id_t{4}, 0}}}};
    chart.sv_recipes = {{.id = recipe_id_t{23}, .name = "Curve",
        .range = {{measure_id_t{3}, 0}, {measure_id_t{4}, 0}}}};
    song.charts = {std::move(chart)};
    song.output_groups = {{.name = "First", .charts = {chart_id_t{2}}}, {.name = "Second"}};
    return song;
}

void make_chart_stepmania_compatible(song_document_t& song) {
    auto profile = *make_osu_mania_profile(4);
    auto& chart = song.charts.front();
    const auto lane = profile.layout.lanes.front().id;
    chart.lane_layouts.front().layout = std::move(profile.layout);
    chart.input_profile = std::move(profile.input);
    chart.measures.front().notes.front().lane = lane;
    chart.sound_channels.front().events.front().lane = lane;
}

auto duplicate_links_match(const chart_document_t& source, const chart_document_t& copy) -> bool {
    const auto measure = copy.measures[0].id;
    const auto lane = copy.lane_layouts[0].layout.lanes[0].id;
    const auto& note = copy.measures[0].notes[0];
    const auto& channel = copy.sound_channels[0];
    return copy.id != source.id && measure != source.measures[0].id &&
        note.id != source.measures[0].notes[0].id && note.position.measure == measure &&
        note.end->measure == copy.measures[1].id && note.lane == lane &&
        copy.input_profile.bindings[0].lane == lane &&
        note.semantic_layers[0] == copy.semantic_layers[0].id &&
        note.sound_channel_event == channel.events[0].id &&
        channel.source_clip.id != source.sound_channels[0].source_clip.id &&
        channel.source_clip.source == source.sound_channels[0].source_clip.source &&
        copy.measures[0].bgm_events[0].source_lane == copy.bgm_source_lanes[0].id &&
        note.sound == source.measures[0].notes[0].sound &&
        copy.bpms[0].position.measure == measure && copy.pauses[0].position.measure == measure &&
        copy.warps[0].span.begin.measure == measure && copy.scrolls[0].position.measure == measure &&
        copy.speeds[0].position.measure == measure && copy.kiai[0].position.measure == measure &&
        copy.timing_anchors[0].position.measure == measure && copy.bookmarks[0].position->measure == measure &&
        copy.sections[0].beat_span->end.measure == copy.measures[1].id &&
        copy.sv_recipes[0].range.end.measure == copy.measures[1].id &&
        copy.sv_recipes[0].id != source.sv_recipes[0].id;
}
}

TEST_CASE("New chart defaults keep the template profile unchanged and undo in one step", "[chart-editor][default-lane-keys]") {
    auto document = chart_song();
    make_chart_stepmania_compatible(document);
    auto history = document_history_t::from(document);
    const auto original = history.document.charts.front().input_profile;
    default_lane_keys_t defaults{.bindings = {{4, lane_key_bindings_t::dfjk()}}};
    const auto created = apply_add_chart_command(history, document.charts.front().id, &defaults);
    require_chart(created.command.applied && created.payload.chart.has_value());
    const auto& chart = *history.document.find_chart(*created.payload.chart);
    expect_chart(lane_key_bindings_t::from(chart.lane_layouts.front().layout, chart.input_profile) == lane_key_bindings_t::dfjk());
    expect_chart(history.document.charts.front().input_profile == original);
    expect_chart(history.undo.size() == 1);
    expect_chart(undo_document_command(history));
    expect_chart(history.document.charts.size() == 1 && history.document.charts.front().input_profile == original);
}

TEST_CASE("Chart duplicate remaps internal graphs and shares resources", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(validate_document(history.document).empty()));
    const auto result = apply_duplicate_chart_command(history, chart_id_t{2});
    require_chart(static_cast<bool>(result));
    expect_chart(static_cast<bool>(result.payload.chart == history.document.charts.back().id));
    expect_chart(static_cast<bool>(validate_document(history.document).empty()));
    expect_chart(static_cast<bool>(duplicate_links_match(history.document.charts.front(), history.document.charts.back())));
    expect_chart(static_cast<bool>(history.document.charts.back().metadata.name == "Normal (copy)"));
    expect_chart(static_cast<bool>(history.document.resources.size() == 1));
    expect_chart(static_cast<bool>(history.document.audio_assets.size() == 1));
    expect_chart(static_cast<bool>(history.document.output_groups[0].charts == std::vector{chart_id_t{2}}));
}

TEST_CASE("Chart duplicate remaps retained BMS branches and timing projections", "[unit][core][chart-editor]") {
    constexpr std::string_view text = "#BPM 120\n#RANDOM 2\n#IF 1\n#BPM01 150\n#00008:01\n#00011:01\n#ELSE\n#00011:01\n#ENDIF\n#ENDRANDOM\n#WAV01 shared.wav\n";
    auto parsed = parse_bms_bytes(std::as_bytes(std::span{text.data(), text.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch",
         .branch_choices = {{.control_source_order = 1, .selected_value = 1}}});
    require_chart(static_cast<bool>(parsed.value.song));
    auto history = document_history_t::from(*parsed.value.song);
    const auto source = history.document.read_charts().front()->id;
    const auto result = apply_duplicate_chart_command(history, source);
    require_chart(static_cast<bool>(result));
    require_chart(result.payload.chart.has_value());
    const auto& original = *history.document.find_bms_program(source);
    const auto& copy = *history.document.find_bms_program(*result.payload.chart);
    expect_chart(history.document.charts.empty());
    expect_chart(validate_document(history.document).empty());
    expect_chart(copy.prepared().program.root() != original.prepared().program.root());
    expect_chart(copy.prepared().program.nodes().size() == original.prepared().program.nodes().size());
    require_chart(!copy.evaluation().selections.empty());
    expect_chart(copy.evaluation().selections.front().branch != original.evaluation().selections.front().branch);
    require_chart(!copy.read_chart()->measures.front().notes.empty());
    expect_chart(copy.read_chart()->measures.front().notes.front().id != original.read_chart()->measures.front().notes.front().id);
    expect_chart(history.document.resources.size() == parsed.value.song->resources.size());
}

TEST_CASE("Chart management operations undo and redo atomically", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    const auto duplicate = history.document.charts.back().id;
    require_chart(static_cast<bool>(apply_move_chart_group_command(history, chart_id_t{2}, 1)));
    expect_chart(static_cast<bool>(history.document.output_groups[0].charts.empty()));
    require_chart(static_cast<bool>(undo_document_command(history)));
    expect_chart(static_cast<bool>(history.document.output_groups[0].charts == std::vector{chart_id_t{2}}));
    require_chart(static_cast<bool>(apply_reorder_chart_command(history, duplicate, 0)));
    expect_chart(static_cast<bool>(history.document.charts.front().id == duplicate));
    require_chart(static_cast<bool>(undo_document_command(history)));
    expect_chart(static_cast<bool>(history.document.charts.front().id == chart_id_t{2}));
}

TEST_CASE("Chart delete removes group memberships and restores them on undo", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    require_chart(static_cast<bool>(apply_delete_chart_command(history, chart_id_t{2})));
    expect_chart(static_cast<bool>(history.document.output_groups[0].charts.empty()));
    expect_chart(static_cast<bool>(history.document.charts.size() == 1));
    require_chart(static_cast<bool>(undo_document_command(history)));
    expect_chart(static_cast<bool>(history.document.output_groups[0].charts == std::vector{chart_id_t{2}}));
    require_chart(static_cast<bool>(redo_document_command(history)));
    expect_chart(static_cast<bool>(history.document.charts.size() == 1));
    require_chart(static_cast<bool>(undo_document_command(history)));
    require_chart(static_cast<bool>(undo_document_command(history)));
    expect_chart(static_cast<bool>(history.document.charts.size() == 1));
}

TEST_CASE("Chart management rejects invalid destinations", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    const auto before = write_native_project(history.document);
    require_chart(before.value.has_value());
    expect_chart(!static_cast<bool>(apply_delete_chart_command(history, chart_id_t{2})));
    expect_chart(!static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{999})));
    expect_chart(!static_cast<bool>(apply_reorder_chart_command(history, chart_id_t{2}, 1)));
    expect_chart(!static_cast<bool>(apply_move_chart_group_command(history, chart_id_t{2}, 2)));
    expect_chart(static_cast<bool>(history.document.revision == 4));
    expect_chart(static_cast<bool>(history.undo.empty()));
    expect_chart(static_cast<bool>(history.document.charts.front().metadata.name == "Normal"));
    expect_chart(write_native_project(history.document).value == before.value);
}

TEST_CASE("Chart duplication rejects exhausted IDs and invalid proposed documents", "[unit][core][chart-editor]") {
    auto song = chart_song();
    song.charts.front().id = chart_id_t{std::numeric_limits<std::uint64_t>::max()};
    song.output_groups.clear();
    auto history = document_history_t::from(song);
    expect_chart(
        !static_cast<bool>(apply_duplicate_chart_command(history, song.charts.front().id)));
    expect_chart(static_cast<bool>(history.undo.empty()));
    history = document_history_t::from(chart_song());
    history.document.resources.clear();
    expect_chart(!static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    expect_chart(static_cast<bool>(history.undo.empty()));
    expect_chart(static_cast<bool>(history.document.charts.size() == 1));
}

TEST_CASE("Chart duplication does not reuse IDs retained by undo and redo history", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    const auto first_copy = history.document.charts.back();
    require_chart(undo_document_command(history));
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    const auto& second_copy = history.document.charts.back();
    expect_chart(second_copy.id != first_copy.id);
    expect_chart(second_copy.measures[0].id != first_copy.measures[0].id);
    expect_chart(second_copy.measures[0].notes[0].id != first_copy.measures[0].notes[0].id);
    expect_chart(second_copy.sound_channels[0].source_clip.id != first_copy.sound_channels[0].source_clip.id);
    expect_chart(second_copy.sv_recipes[0].id != first_copy.sv_recipes[0].id);
    expect_chart(history.redo.empty());
    expect_chart(validate_document(history.document).empty());
}

TEST_CASE("New chart creates an empty valid profile with fresh history-safe identities", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    const auto original = history.document;
    const auto result = apply_new_chart_command(history);
    require_chart(static_cast<bool>(result));
    const auto& chart = history.document.charts.back();
    expect_chart(result.payload.chart == chart.id && chart.id.value > 51);
    expect_chart(validate_document(history.document).empty());
    expect_chart(chart.measures.size() == 1 && chart.measures[0].notes.empty() && chart.measures[0].bgm_events.empty());
    expect_chart(chart.lane_layouts[0].layout.profile_id == make_bms_7k_profile().layout.profile_id);
    expect_chart(chart.input_profile.bindings.size() == make_bms_7k_profile().input.bindings.size());
    expect_chart(chart.bpms.size() == 1 && chart.bpms[0].beats_per_minute == signed_rational_t{120, 1});
    expect_chart(chart.sound_channels.empty() && chart.semantic_layers.empty() && !chart.bms_control_tree);
    expect_chart(chart.metadata.author.empty() && !chart.metadata.rating && chart.metadata.format_groups.empty());
    expect_chart(history.document.output_groups[0].charts == original.output_groups[0].charts);
    expect_chart(history.document.resources.size() == original.resources.size());
    const auto first_id = chart.id;
    require_chart(static_cast<bool>(undo_document_command(history)));
    const auto second = apply_new_chart_command(history);
    require_chart(static_cast<bool>(second));
    expect_chart(second.payload.chart->value > first_id.value);
}

TEST_CASE("New chart rejects invalid and exhausted requests without changing history", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    const auto original = history.document;
    auto invalid = make_bms_7k_profile();
    invalid.layout.lanes[0].id = lane_id_t{};
    expect_chart(!apply_new_chart_command(history, std::move(invalid)));
    expect_chart(history.document.revision == original.revision && history.document.charts.size() == 1);
    expect_chart(history.undo.empty() && history.redo.empty());
    history.document.audio_assets[0].id = asset_id_t{std::numeric_limits<std::uint64_t>::max()};
    expect_chart(!apply_new_chart_command(history));
    expect_chart(history.undo.empty() && history.document.charts.size() == 1);
}

TEST_CASE("New chart accepts explicit profiles beyond the bounded project table", "[unit][core][chart-editor]") {
    song_document_t song{.id = song_id_t{1}, .revision = 4};
    for (std::uint64_t index = 0; index < 257; ++index) {
        const auto base = 10 + index * 4;
        song.charts.push_back({.id = chart_id_t{base}, .metadata = {.name = "Difficulty"},
            .measures = {{.id = measure_id_t{base + 1}}},
            .lane_layouts = {{.id = object_id_t{base + 2}, .position = {measure_id_t{base + 1}, 0},
                .layout = {.profile_id = "test", .lanes = {{.id = lane_id_t{base + 3}, .name = "Key"}}}}}});
    }
    require_chart(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const auto added =
        apply_new_chart_command(history, make_bms_14k_profile(), format_family_t::bms);
    require_chart(static_cast<bool>(added));
    expect_chart(history.document.read_charts().size() == 258);
    expect_chart(added.payload.chart == history.document.read_charts().back()->id);
    expect_chart(history.document.read_charts().back()->source_family == format_family_t::bms);
    expect_chart(history.document.read_charts().back()->lane_layouts[0].layout.split_stage);
    expect_chart(validate_document(history.document).empty());
}

namespace {
auto four_key_template_song() -> song_document_t {
    constexpr std::string_view bytes = "#VERSION:0.83;\n#TITLE:Template;\n#BPMS:0=120;\n#NOTEDATA:;\n#CHARTNAME:Active4K;\n#STEPSTYPE:dance-single;\n#DIFFICULTY:Hard;\n#METER:8;\n#NOTES:\n1000\n0000\n0100\n0000\n;\n";
    auto parsed = parse_stepmania_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}),
        {.profile = stepmania_profile_t::ssc});
    require_chart(parsed.value.has_value() && parsed.ok());
    auto song = std::move(*parsed.value);
    song.revision = 4;
    song.charts[0].divisions_per_standard_measure = 3840;
    song.charts[0].beat_zero_audio_offset_seconds = {-7, 20};
    song.output_groups = {{.name = "SSC", .charts = {song.charts[0].id}}};
    require_chart(validate_document(song).empty());
    return song;
}
}

TEST_CASE("New chart templates active four-key profile resolution and exact offset without source content", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(four_key_template_song());
    const auto source = history.document.charts[0];
    const auto result = apply_add_chart_command(history, source.id);
    require_chart(static_cast<bool>(result));
    const auto created = history.document.charts.back();
    expect_chart(validate_document(history.document).empty());
    expect_chart(created.source_family == format_family_t::stepmania);
    expect_chart(created.divisions_per_standard_measure == 3840 && created.beat_zero_audio_offset_seconds == signed_rational_t{-7, 20});
    expect_chart(created.lane_layouts.size() == 1 && created.lane_layouts[0].layout.profile_id == source.lane_layouts[0].layout.profile_id);
    expect_chart(created.lane_layouts[0].layout.lanes.size() == 4 && created.input_profile.bindings.size() == 4);
    expect_chart(created.lane_layouts[0].layout.lanes[0].glyph == lane_glyph_t::arrow_left);
    expect_chart(created.lane_layouts[0].layout.lanes[0].id != source.lane_layouts[0].layout.lanes[0].id);
    expect_chart(created.input_profile.bindings[0].lane == created.lane_layouts[0].layout.lanes[0].id);
    expect_chart(created.measures.size() == 1 && created.measures[0].notes.empty());
    expect_chart(created.metadata.format_groups.empty() && !created.metadata.rating && created.metadata.author.empty());
    expect_chart(history.document.charts[0].measures[0].notes.size() == source.measures[0].notes.size());
    expect_chart(history.document.charts[0].metadata.name == source.metadata.name);
    expect_chart(history.document.output_groups[0].charts == std::vector{source.id});
    expect_chart(history.undo.size() == 1);
    require_chart(undo_document_command(history));
    expect_chart(history.document.charts.size() == 1);
    require_chart(redo_document_command(history));
    expect_chart(history.document.charts.back().id == created.id);
    require_chart(undo_document_command(history));
    const auto replacement = apply_add_chart_command(history, source.id);
    require_chart(static_cast<bool>(replacement));
    expect_chart(replacement.payload.chart->value > created.id.value);
}

TEST_CASE("New chart template rejects missing and exhausted requests atomically", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(four_key_template_song());
    const auto source = history.document.charts[0].id;
    expect_chart(!apply_add_chart_command(history, chart_id_t{9999}));
    expect_chart(history.document.charts.size() == 1 && history.document.revision == 4 && history.undo.empty());
    history.document.id = song_id_t{std::numeric_limits<std::uint64_t>::max()};
    expect_chart(!apply_add_chart_command(history, source));
    expect_chart(history.document.charts.size() == 1 && history.undo.empty() && history.redo.empty());
}

TEST_CASE("New chart templates the initial layout without copying later measure extent", "[unit][core][chart-editor]") {
    auto song = four_key_template_song();
    auto& source = song.charts[0];
    source.measures.push_back({.id = measure_id_t{600}});
    source.lane_layouts.push_back({.id = object_id_t{601}, .position = {measure_id_t{600}, 0},
        .layout = {.profile_id = "future", .lanes = {{.id = lane_id_t{602}, .name = "Future key"}}}});
    source.input_profile.bindings.push_back({physical_key_t::key_q, lane_id_t{602}});
    require_chart(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const auto created = apply_add_chart_command(history, history.document.charts[0].id);
    require_chart(static_cast<bool>(created));
    const auto& chart = history.document.charts.back();
    expect_chart(chart.measures.size() == 1 && chart.lane_layouts.size() == 1);
    expect_chart(chart.lane_layouts[0].layout.profile_id == "stepmania:dance-single");
    expect_chart(chart.input_profile.bindings.size() == 4);
    expect_chart(validate_document(history.document).empty());
}

TEST_CASE("New chart covers the longest linked music or BGM using its own timing",
          "[unit][core][chart-editor][audio-extent]") {
    const auto offset = GENERATE(-1, 1);
    auto song = chart_song();
    song.charts.front().beat_zero_audio_offset_seconds = {offset, 1};
    song.audio_assets.front().stream = {48'000, 2, 48'000 * 100, true};
    song.audio_assets.push_back({.id = asset_id_t{60}, .stream = {192'000, 2, 192'000 * 5, true}});
    song.audio_assets.push_back({.id = asset_id_t{62}, .stream = {48'000, 2, 48'000 * 14, true}});
    song.audio_assets.push_back({.id = asset_id_t{64}, .stream = {48'000, 2, 48'000 * 200, true}});
    song.resources.push_back({.id = resource_id_t{61}, .asset = asset_id_t{60},
                              .roles = {resource_role_t::music}});
    song.resources.push_back({.id = resource_id_t{63}, .asset = asset_id_t{62},
                              .roles = {resource_role_t::bgm}});
    auto history = document_history_t::from(std::move(song));
    const auto added = apply_add_chart_command(history, history.document.charts.front().id);
    require_chart(static_cast<bool>(added));
    const auto& chart = history.document.charts.back();
    expect_chart(chart.measures.size() == (offset < 0 ? 8 : 7));
    expect_chart(chart.bpms.size() == 1 && chart.pauses.empty() && chart.warps.empty());
    expect_chart(chart.beat_zero_audio_offset_seconds == signed_rational_t{offset, 1});
    expect_chart(validate_document(history.document).empty());
}

TEST_CASE("New chart refuses excessive linked audio without publishing a partial chart",
          "[unit][core][chart-editor][audio-extent]") {
    auto song = chart_song();
    song.audio_assets.front().stream = {1, 2, 2'000'001, true};
    song.resources.front().roles = {resource_role_t::music};
    auto history = document_history_t::from(std::move(song));
    const auto before = write_native_project(history.document).value;
    const auto added = apply_add_chart_command(history, history.document.charts.front().id);
    expect_chart(!added.command.applied && !added.payload.chart);
    require_chart(!added.command.diagnostics.empty());
    expect_chart(added.command.diagnostics.front().code == diagnostic_code_t::invalid_measure);
    expect_chart(write_native_project(history.document).value == before);
    expect_chart(history.undo.empty() && history.redo.empty());
}

TEST_CASE("Output group CRUD is atomic and undoable without altering chart content", "[unit][output-groups]") {
    auto song = chart_song();
    make_chart_stepmania_compatible(song);
    auto history = document_history_t::from(std::move(song));
    history.document.output_groups.clear();
    const auto before = write_native_project(history.document).value;
    const auto initial_revision = history.document.revision;
    output_group_t group{.name = "Release", .family = format_family_t::bmson,
        .charts = {chart_id_t{2}}, .relative_output_path = "exports/chart.bmson"};
    require_chart(apply_set_output_group_command(history, std::nullopt, group).applied);
    expect_chart(history.document.output_groups.size() == 1);
    group.name = "Renamed";
    group.family = format_family_t::stepmania;
    group.destination_kind = output_destination_kind_t::file;
    group.relative_output_path = "release/song.ssc";
    group.existing_files = existing_files_policy_t::replace;
    group.stepmania = stepmania_export_policy_t{};
    require_chart(apply_set_output_group_command(history, 0, group).applied);
    expect_chart(history.document.output_groups[0].name == "Renamed");
    require_chart(undo_document_command(history));
    expect_chart(history.document.output_groups[0].name == "Release");
    expect_chart(history.document.output_groups[0].existing_files == existing_files_policy_t::ask);
    require_chart(redo_document_command(history));
    expect_chart(history.document.output_groups[0].relative_output_path == "release/song.ssc");
    expect_chart(history.document.output_groups[0].existing_files ==
                 existing_files_policy_t::replace);
    require_chart(apply_delete_output_group_command(history, 0).applied);
    expect_chart(history.document.output_groups.empty());
    require_chart(undo_document_command(history));
    expect_chart(history.document.output_groups[0].charts == group.charts);
    auto unchanged = history.document;
    unchanged.output_groups.clear();
    unchanged.revision = initial_revision;
    expect_chart(write_native_project(unchanged).value == before);
}

TEST_CASE("Output group edits reject unsafe paths stale contexts and invalid memberships atomically", "[unit][output-groups]") {
    auto history = document_history_t::from(chart_song());
    history.document.output_groups.clear();
    const auto revision = history.document.revision;
    output_group_t group{.name = "Release", .family = format_family_t::bmson,
        .charts = {chart_id_t{2}}, .relative_output_path = "exports/chart.bmson"};
    for (const auto* path : {"", "/tmp/chart.bmson", "../chart.bmson", "exports/../chart.bmson", "C:\\chart.bmson", "exports\\chart.bmson"}) {
        group.relative_output_path = path;
        expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    }
    group.relative_output_path = "exports/chart.bmson";
    group.charts = {chart_id_t{999}};
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    group.charts = {chart_id_t{2}, chart_id_t{2}};
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    group.charts = {chart_id_t{2}};
    group.destination_kind = output_destination_kind_t::file;
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    group.destination_kind = output_destination_kind_t::directory;
    group.family = format_family_t::quaver;
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    group.family = format_family_t::stepmania;
    group.destination_kind = output_destination_kind_t::file;
    group.stepmania.reset();
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    group.stepmania = stepmania_export_policy_t{
        .file_profile = stepmania_profile_t::sm,
        .implementation_profile = stepmania_implementation_profile_t::stepmania_5_1};
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    group.family = format_family_t::bmson;
    group.stepmania.reset();
    group.existing_files = static_cast<existing_files_policy_t>(99);
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    group.existing_files = existing_files_policy_t::ask;
    expect_chart(!apply_set_output_group_command(history, 99, group));
    expect_chart(!apply_delete_output_group_command(history, 0));
    expect_chart(history.document.revision == revision);
    expect_chart(history.undo.empty());
    expect_chart(history.document.output_groups.empty());
}

TEST_CASE("Output groups permit drafts and multiple memberships with format-aware file cardinality", "[unit][output-groups]") {
    auto song = chart_song();
    make_chart_stepmania_compatible(song);
    auto history = document_history_t::from(std::move(song));
    history.document.output_groups.clear();
    const auto duplicate = apply_duplicate_chart_command(history, chart_id_t{2});
    require_chart(static_cast<bool>(duplicate));
    output_group_t group{.name = "Draft", .family = format_family_t::bmson,
        .relative_output_path = "exports/charts"};
    require_chart(apply_set_output_group_command(history, std::nullopt, group).applied);
    group.name = "Both charts";
    group.charts = {chart_id_t{2}, *duplicate.payload.chart};
    group.relative_output_path = "exports/song.bmson";
    group.destination_kind = output_destination_kind_t::file;
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
    group.relative_output_path = "exports/charts";
    group.destination_kind = output_destination_kind_t::directory;
    require_chart(apply_set_output_group_command(history, std::nullopt, group).applied);
    expect_chart(history.document.output_groups[1].charts.size() == 2);
    group.name = "Shared SSC";
    group.family = format_family_t::stepmania;
    group.destination_kind = output_destination_kind_t::file;
    group.relative_output_path = "exports/song.ssc";
    group.stepmania = stepmania_export_policy_t{};
    require_chart(apply_set_output_group_command(history, std::nullopt, group).applied);
    expect_chart(history.document.output_groups[1].charts == history.document.output_groups[2].charts);
    expect_chart(!apply_set_output_group_command(history, std::nullopt, group));
}

TEST_CASE("Output membership updates reject one incompatible chart without a partial edit",
          "[unit][output-groups][output-compatibility]") {
    auto song = chart_song();
    make_chart_stepmania_compatible(song);
    auto history = document_history_t::from(std::move(song));
    history.document.output_groups.clear();
    const auto duplicate = apply_duplicate_chart_command(history, chart_id_t{2});
    require_chart(static_cast<bool>(duplicate));
    output_group_t group{.name = "Mania", .family = format_family_t::osu_mania,
        .charts = {chart_id_t{2}}, .relative_output_path = "exports/osu"};
    require_chart(apply_set_output_group_command(history, std::nullopt, group).applied);
    group.charts.push_back(*duplicate.payload.chart);
    require_chart(apply_set_output_group_command(history, 0, group).applied);
    require_chart(undo_document_command(history));
    expect_chart(history.document.output_groups.front().charts == std::vector{chart_id_t{2}});

    auto& incompatible = history.document.charts.back().lane_layouts.front().layout;
    incompatible.lanes.front().role = lane_role_t::scratch;
    const auto revision = history.document.revision;
    const auto undo_size = history.undo.size();
    const auto rejected = apply_set_output_group_command(history, 0, group);
    require_chart(!rejected.applied);
    require_chart(!rejected.diagnostics.empty());
    expect_chart(rejected.diagnostics.front().message.find("osu!mania requires") != std::string::npos);
    expect_chart(history.document.output_groups.front().charts == std::vector{chart_id_t{2}});
    expect_chart(history.document.revision == revision);
    expect_chart(history.undo.size() == undo_size);
}

TEST_CASE("Selected charts duplicate and delete together with one undo boundary", "[core][chart-editor][project-selection]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    history = document_history_t::from(history.document);
    const std::array selection{history.document.charts[0].id, history.document.charts[1].id};
    history.document.output_groups.front().charts.assign(selection.begin(), selection.end());
    const auto before = write_native_project(history.document).value;
    require_chart(static_cast<bool>(apply_duplicate_charts_command(history, selection)));
    expect_chart(history.document.charts.size() == 5 && history.undo.size() == 1);
    expect_chart(duplicate_links_match(history.document.charts[0], history.document.charts[3]));
    expect_chart(duplicate_links_match(history.document.charts[1], history.document.charts[4]));
    expect_chart(history.document.charts[3].id != history.document.charts[4].id);
    require_chart(undo_document_command(history));
    expect_chart(write_native_project(history.document).value == before);
    require_chart(static_cast<bool>(apply_delete_charts_command(history, selection)));
    expect_chart(history.document.charts.size() == 1 && history.undo.size() == 1);
    expect_chart(history.document.output_groups.front().charts.empty());
    require_chart(undo_document_command(history));
    expect_chart(write_native_project(history.document).value == before);
}

TEST_CASE("Chart selections reject unknown repeated empty and all-chart deletion atomically", "[core][chart-editor][project-selection]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    history = document_history_t::from(history.document);
    const auto before = write_native_project(history.document).value;
    const auto selection = GENERATE(std::vector<chart_id_t>{},
        (std::vector{chart_id_t{2}, chart_id_t{2}}), (std::vector{chart_id_t{2}, chart_id_t{999}}));
    expect_chart(!apply_duplicate_charts_command(history, selection));
    expect_chart(!apply_delete_charts_command(history, selection));
    const std::array all{history.document.charts[0].id, history.document.charts[1].id};
    expect_chart(!apply_delete_charts_command(history, all));
    expect_chart(history.undo.empty());
    expect_chart(write_native_project(history.document).value == before);
}

TEST_CASE("Batch duplication rolls back earlier copies when later IDs are exhausted", "[core][chart-editor][project-selection]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, chart_id_t{2})));
    history.document.audio_assets.push_back({.id = asset_id_t{UINT64_MAX - 30}, .source_path = "unused.wav"});
    history = document_history_t::from(history.document);
    const auto before = write_native_project(history.document).value;
    const std::array selection{history.document.charts[0].id, history.document.charts[1].id};
    expect_chart(!apply_duplicate_charts_command(history, selection));
    expect_chart(history.undo.empty());
    expect_chart(write_native_project(history.document).value == before);
}
TEST_CASE("New difficulties inherit only selected designated timing sources in one Undo step", "[core][chart-editor][timing-links]") {
    auto song = chart_song();
    song.charts.front().bpms.front().beats_per_minute = {180, 1};
    timing_link_selection_t timing;
    timing.sources[static_cast<std::size_t>(timing_category_t::bpm)] = chart_id_t{2};
    timing.sources[static_cast<std::size_t>(timing_category_t::measures)] = chart_id_t{2};
    auto history = document_history_t::from(std::move(song));
    const auto created = apply_add_chart_command(history, chart_id_t{2}, nullptr, timing);
    require_chart(created.command.applied);
    require_chart(created.payload.chart.has_value());
    const auto& target = history.document.charts.back();
    expect_chart(target.bpms.front().beats_per_minute == signed_rational_t{180, 1});
    expect_chart(target.timing_links.value(timing_category_t::bpm)->source == chart_id_t{2});
    expect_chart(target.timing_links.value(timing_category_t::measures)->source == chart_id_t{2});
    expect_chart(!target.timing_links.value(timing_category_t::scroll));
    expect_chart(target.measures.size() == 2);
    expect_chart(target.measures.front().notes.empty());
    expect_chart(history.undo.size() == 1);
    require_chart(undo_document_command(history));
    expect_chart(history.document.charts.size() == 1);
    require_chart(redo_document_command(history));
    expect_chart(history.document.charts.back().id == *created.payload.chart);
}
TEST_CASE("Missing selected creation timing sources reject without adding a chart", "[core][chart-editor][timing-links]") {
    auto history = document_history_t::from(chart_song());
    timing_link_selection_t timing;
    timing.sources[static_cast<std::size_t>(timing_category_t::offset)] = chart_id_t{999};
    const auto created = apply_add_chart_command(history, chart_id_t{2}, nullptr, timing);
    expect_chart(!created.command.applied);
    expect_chart(!created.payload.chart);
    expect_chart(history.document.charts.size() == 1);
    expect_chart(history.undo.empty());
}
