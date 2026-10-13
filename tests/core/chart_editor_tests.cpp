#include <wormedit/core/chart_editor.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("Chart duplicate remaps internal graphs and shares resources", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(validate_document(history.document).empty()));
    const auto result = apply_duplicate_chart_command(history, 4, chart_id_t{2});
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
    require_chart(static_cast<bool>(parsed.song));
    auto& parsed_chart = parsed.song->charts.front();
    require_chart(!parsed_chart.measures.front().notes.empty() && !parsed_chart.active_branch_selection.empty());
    // Import retains branch ownership in source provenance; also exercise an
    // explicitly authored semantic branch reference in the document model.
    parsed_chart.measures.front().notes.front().branch = parsed_chart.active_branch_selection.front().branch;
    auto history = document_history_t::from(*parsed.song);
    const auto result = apply_duplicate_chart_command(history, history.document.revision, history.document.charts[0].id);
    require_chart(static_cast<bool>(result));
    expect_chart(static_cast<bool>(validate_document(history.document).empty()));
    const auto& original = history.document.charts.front();
    const auto& copy = history.document.charts.back();
    require_chart(static_cast<bool>(original.bms_control_tree));
    require_chart(static_cast<bool>(copy.bms_control_tree));
    expect_chart(static_cast<bool>(copy.bms_control_tree->id != original.bms_control_tree->id));
    expect_chart(static_cast<bool>(copy.bms_control_tree->children.front().id != original.bms_control_tree->children.front().id));
    expect_chart(static_cast<bool>(copy.bms_control_tree->children.front().original_text == original.bms_control_tree->children.front().original_text));
    require_chart(!copy.bms_bpm_projections.empty());
    expect_chart(copy.bms_bpm_projections.back().event != original.bms_bpm_projections.back().event);
    expect_chart(copy.bms_bpm_projections.back().imported_position.measure == copy.measures.front().id);
    require_chart(!copy.active_branch_selection.empty());
    expect_chart(copy.active_branch_selection.front().branch != original.active_branch_selection.front().branch);
    require_chart(!copy.measures.front().notes.empty());
    expect_chart(copy.measures.front().notes.front().branch != original.measures.front().notes.front().branch);
    expect_chart(copy.measures.front().notes.front().branch == copy.active_branch_selection.front().branch);
}

TEST_CASE("Chart management operations undo and redo atomically", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, 4, chart_id_t{2})));
    const auto duplicate = history.document.charts.back().id;
    require_chart(static_cast<bool>(apply_move_chart_group_command(history, history.document.revision, chart_id_t{2}, 1)));
    expect_chart(static_cast<bool>(history.document.output_groups[0].charts.empty()));
    require_chart(static_cast<bool>(undo_document_command(history)));
    expect_chart(static_cast<bool>(history.document.output_groups[0].charts == std::vector{chart_id_t{2}}));
    require_chart(static_cast<bool>(apply_reorder_chart_command(history, history.document.revision, duplicate, 0)));
    expect_chart(static_cast<bool>(history.document.charts.front().id == duplicate));
    require_chart(static_cast<bool>(undo_document_command(history)));
    expect_chart(static_cast<bool>(history.document.charts.front().id == chart_id_t{2}));
}

TEST_CASE("Chart delete removes group memberships and restores them on undo", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, 4, chart_id_t{2})));
    require_chart(static_cast<bool>(apply_delete_chart_command(history, history.document.revision, chart_id_t{2})));
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

TEST_CASE("Chart management rejects stale revisions and invalid destinations", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    const auto before = write_native_project(history.document);
    require_chart(before.bytes.has_value());
    expect_chart(!static_cast<bool>(apply_duplicate_chart_command(history, 3, chart_id_t{2})));
    expect_chart(!static_cast<bool>(apply_delete_chart_command(history, 3, chart_id_t{2})));
    expect_chart(!static_cast<bool>(apply_reorder_chart_command(history, 3, chart_id_t{2}, 0)));
    expect_chart(!static_cast<bool>(apply_move_chart_group_command(history, 3, chart_id_t{2}, 0)));
    expect_chart(!static_cast<bool>(apply_delete_chart_command(history, 4, chart_id_t{2})));
    expect_chart(!static_cast<bool>(apply_duplicate_chart_command(history, 4, chart_id_t{999})));
    expect_chart(!static_cast<bool>(apply_reorder_chart_command(history, 4, chart_id_t{2}, 1)));
    expect_chart(!static_cast<bool>(apply_move_chart_group_command(history, 4, chart_id_t{2}, 2)));
    expect_chart(static_cast<bool>(history.document.revision == 4));
    expect_chart(static_cast<bool>(history.undo.empty()));
    expect_chart(static_cast<bool>(history.document.charts.front().metadata.name == "Normal"));
    expect_chart(write_native_project(history.document).bytes == before.bytes);
}

TEST_CASE("Chart duplication rejects exhausted IDs and invalid proposed documents", "[unit][core][chart-editor]") {
    auto song = chart_song();
    song.charts.front().id = chart_id_t{std::numeric_limits<std::uint64_t>::max()};
    song.output_groups.clear();
    auto history = document_history_t::from(song);
    expect_chart(!static_cast<bool>(apply_duplicate_chart_command(history, 4, song.charts.front().id)));
    expect_chart(static_cast<bool>(history.undo.empty()));
    history = document_history_t::from(chart_song());
    history.document.resources.clear();
    expect_chart(!static_cast<bool>(apply_duplicate_chart_command(history, 4, chart_id_t{2})));
    expect_chart(static_cast<bool>(history.undo.empty()));
    expect_chart(static_cast<bool>(history.document.charts.size() == 1));
}

TEST_CASE("Chart duplication does not reuse IDs retained by undo and redo history", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, 4, chart_id_t{2})));
    const auto first_copy = history.document.charts.back();
    require_chart(undo_document_command(history));
    require_chart(static_cast<bool>(apply_duplicate_chart_command(history, history.document.revision, chart_id_t{2})));
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
    const auto result = apply_new_chart_command(history, 4);
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
    const auto second = apply_new_chart_command(history, history.document.revision);
    require_chart(static_cast<bool>(second));
    expect_chart(second.payload.chart->value > first_id.value);
}

TEST_CASE("New chart rejects stale invalid and exhausted requests without changing history", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(chart_song());
    const auto original = history.document;
    expect_chart(!apply_new_chart_command(history, 3));
    auto invalid = make_bms_7k_profile();
    invalid.layout.lanes[0].id = lane_id_t{};
    expect_chart(!apply_new_chart_command(history, 4, std::move(invalid)));
    expect_chart(history.document.revision == original.revision && history.document.charts.size() == 1);
    expect_chart(history.undo.empty() && history.redo.empty());
    history.document.audio_assets[0].id = asset_id_t{std::numeric_limits<std::uint64_t>::max()};
    expect_chart(!apply_new_chart_command(history, 4));
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
    const auto added = apply_new_chart_command(history, 4, make_bms_14k_profile(), format_family_t::bms);
    require_chart(static_cast<bool>(added));
    expect_chart(history.document.charts.size() == 258);
    expect_chart(added.payload.chart == history.document.charts.back().id);
    expect_chart(history.document.charts.back().source_family == format_family_t::bms);
    expect_chart(history.document.charts.back().lane_layouts[0].layout.split_stage);
    expect_chart(validate_document(history.document).empty());
}

namespace {
auto four_key_template_song() -> song_document_t {
    constexpr std::string_view bytes = "#VERSION:0.83;\n#TITLE:Template;\n#BPMS:0=120;\n#NOTEDATA:;\n#CHARTNAME:Active4K;\n#STEPSTYPE:dance-single;\n#DIFFICULTY:Hard;\n#METER:8;\n#NOTES:\n1000\n0000\n0100\n0000\n;\n";
    auto parsed = parse_stepmania_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}),
        {.profile = stepmania_profile_t::ssc});
    require_chart(parsed.song.has_value() && !parsed.has_errors());
    auto song = std::move(*parsed.song);
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
    const auto result = apply_add_chart_command(history, 4, source.id);
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
    const auto replacement = apply_add_chart_command(history, history.document.revision, source.id);
    require_chart(static_cast<bool>(replacement));
    expect_chart(replacement.payload.chart->value > created.id.value);
}

TEST_CASE("New chart template rejects stale missing and exhausted requests atomically", "[unit][core][chart-editor]") {
    auto history = document_history_t::from(four_key_template_song());
    const auto source = history.document.charts[0].id;
    expect_chart(!apply_add_chart_command(history, 3, source));
    expect_chart(!apply_add_chart_command(history, 4, chart_id_t{9999}));
    expect_chart(history.document.charts.size() == 1 && history.document.revision == 4 && history.undo.empty());
    history.document.id = song_id_t{std::numeric_limits<std::uint64_t>::max()};
    expect_chart(!apply_add_chart_command(history, 4, source));
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
    const auto created = apply_add_chart_command(history, 4, history.document.charts[0].id);
    require_chart(static_cast<bool>(created));
    const auto& chart = history.document.charts.back();
    expect_chart(chart.measures.size() == 1 && chart.lane_layouts.size() == 1);
    expect_chart(chart.lane_layouts[0].layout.profile_id == "stepmania:dance-single");
    expect_chart(chart.input_profile.bindings.size() == 4);
    expect_chart(validate_document(history.document).empty());
}

TEST_CASE("Output group CRUD is atomic and undoable without altering chart content", "[unit][output-groups]") {
    auto history = document_history_t::from(chart_song());
    history.document.output_groups.clear();
    const auto before = write_native_project(history.document).bytes;
    const auto initial_revision = history.document.revision;
    output_group_t group{.name = "Release", .family = format_family_t::bmson,
        .charts = {chart_id_t{2}}, .relative_output_path = "exports/chart.bmson"};
    require_chart(apply_set_output_group_command(history, history.document.revision, std::nullopt, group).applied);
    expect_chart(history.document.output_groups.size() == 1);
    group.name = "Renamed";
    group.family = format_family_t::stepmania;
    group.relative_output_path = "release/song.ssc";
    require_chart(apply_set_output_group_command(history, history.document.revision, 0, group).applied);
    expect_chart(history.document.output_groups[0].name == "Renamed");
    require_chart(undo_document_command(history));
    expect_chart(history.document.output_groups[0].name == "Release");
    require_chart(redo_document_command(history));
    expect_chart(history.document.output_groups[0].relative_output_path == "release/song.ssc");
    require_chart(apply_delete_output_group_command(history, history.document.revision, 0).applied);
    expect_chart(history.document.output_groups.empty());
    require_chart(undo_document_command(history));
    expect_chart(history.document.output_groups[0].charts == group.charts);
    auto unchanged = history.document;
    unchanged.output_groups.clear();
    unchanged.revision = initial_revision;
    expect_chart(write_native_project(unchanged).bytes == before);
}

TEST_CASE("Output group edits reject unsafe paths stale contexts and invalid memberships atomically", "[unit][output-groups]") {
    auto history = document_history_t::from(chart_song());
    history.document.output_groups.clear();
    const auto revision = history.document.revision;
    output_group_t group{.name = "Release", .family = format_family_t::bmson,
        .charts = {chart_id_t{2}}, .relative_output_path = "exports/chart.bmson"};
    for (const auto* path : {"", "/tmp/chart.bmson", "../chart.bmson", "exports/../chart.bmson", "C:\\chart.bmson", "exports\\chart.bmson"}) {
        group.relative_output_path = path;
        expect_chart(!apply_set_output_group_command(history, revision, std::nullopt, group));
    }
    group.relative_output_path = "exports/chart.bmson";
    group.charts = {chart_id_t{999}};
    expect_chart(!apply_set_output_group_command(history, revision, std::nullopt, group));
    group.charts = {chart_id_t{2}, chart_id_t{2}};
    expect_chart(!apply_set_output_group_command(history, revision, std::nullopt, group));
    group.charts = {chart_id_t{2}};
    group.family = format_family_t::quaver;
    expect_chart(!apply_set_output_group_command(history, revision, std::nullopt, group));
    group.family = format_family_t::bmson;
    expect_chart(!apply_set_output_group_command(history, revision + 1, std::nullopt, group));
    expect_chart(!apply_set_output_group_command(history, revision, 99, group));
    expect_chart(!apply_delete_output_group_command(history, revision, 0));
    expect_chart(history.document.revision == revision);
    expect_chart(history.undo.empty());
    expect_chart(history.document.output_groups.empty());
}

TEST_CASE("Output groups permit drafts and multiple memberships with format-aware file cardinality", "[unit][output-groups]") {
    auto history = document_history_t::from(chart_song());
    history.document.output_groups.clear();
    const auto duplicate = apply_duplicate_chart_command(history, history.document.revision, chart_id_t{2});
    require_chart(static_cast<bool>(duplicate));
    output_group_t group{.name = "Draft", .family = format_family_t::bmson,
        .relative_output_path = "exports/charts"};
    require_chart(apply_set_output_group_command(history, history.document.revision, std::nullopt, group).applied);
    group.name = "Both charts";
    group.charts = {chart_id_t{2}, *duplicate.payload.chart};
    group.relative_output_path = "exports/song.bmson";
    expect_chart(!apply_set_output_group_command(history, history.document.revision, std::nullopt, group));
    group.relative_output_path = "exports/charts";
    require_chart(apply_set_output_group_command(history, history.document.revision, std::nullopt, group).applied);
    expect_chart(history.document.output_groups[1].charts.size() == 2);
    group.name = "Shared SSC";
    group.family = format_family_t::stepmania;
    group.relative_output_path = "exports/song.ssc";
    require_chart(apply_set_output_group_command(history, history.document.revision, std::nullopt, group).applied);
    expect_chart(history.document.output_groups[1].charts == history.document.output_groups[2].charts);
    expect_chart(!apply_set_output_group_command(history, history.document.revision, std::nullopt, group));
}
