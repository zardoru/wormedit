#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

namespace {

auto parse(std::string_view source, wormedit::bms_parse_options_t options = {})
    -> wormedit::bms_parse_result_t {
    return wormedit::parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                     std::move(options));
}

auto has_error(const wormedit::bms_write_result_t& result,
               wormedit::bms_write_diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::error &&
               diagnostic.code == code;
    });
}

auto has_warning(const wormedit::bms_write_result_t& result,
                 wormedit::bms_write_diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
               diagnostic.code == code;
    });
}

auto line_order(std::string_view text, std::string_view line) -> std::size_t {
    const auto position = text.find(line);
    if (position == std::string_view::npos)
        return std::string_view::npos;
    return static_cast<std::size_t>(std::ranges::count(text.substr(0, position), '\n'));
}

auto occurrences(std::string_view text, std::string_view needle) -> std::size_t {
    std::size_t count{};
    for (std::size_t position = 0;
         (position = text.find(needle, position)) != std::string_view::npos;
         position += needle.size()) {
        ++count;
    }
    return count;
}

auto simple_chart() -> wormedit::bms_parse_result_t {
    constexpr std::string_view source = "#PLAYER 1\n"
                                        "#TITLE Original title\n"
                                        "#SUBTITLE Side title\n"
                                        "#ARTIST Original artist\n"
                                        "#SUBARTIST Guest artist\n"
                                        "#PLAYLEVEL 12\n"
                                        "#RANK 2\n"
                                        "#STAGEFILE stage.png\n"
                                        "#BANNER banner.png\n"
                                        "#PREVIEW preview.ogg\n"
                                        "#GENRE Example\n"
                                        "#CUSTOM_EXTENSION retained value\n"
                                        "#BPM 120\n"
                                        "#WAV01 click.wav\n"
                                        "#00002:3/4\n"
                                        "#00102:5/4\n"
                                        "#00011:0101\n";
    wormedit::bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    return parse(source, std::move(options));
}

} // namespace

TEST_CASE("BMS writer preserves unchanged source bytes exactly", "[unit][format][bms][writer]") {
    constexpr std::string_view source = "#TITLE Keep bytes\r\n#BPM 120\r\n";
    const auto imported = parse(source);
    REQUIRE(imported.song.has_value());
    const auto written = wormedit::write_bms(*imported.song);
    REQUIRE(written.bytes.has_value());
    CHECK(*written.bytes == source);
    CHECK_FALSE(written.has_errors());
}

TEST_CASE("BMS writer emits ordered metadata, exact measures, resources, and taps",
          "[unit][format][bms][writer]") {
    auto imported = simple_chart();
    REQUIRE(imported.song.has_value());
    auto song = *imported.song;
    song.metadata.title = "Edited title";
    song.metadata.artist = "Edited artist";

    wormedit::bms_write_options_t options;
    options.preserve_unchanged_source = false;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto first = wormedit::write_bms(song, options);
    REQUIRE(first.bytes.has_value());
    CHECK_FALSE(first.has_errors());
    CHECK(*first.bytes == *wormedit::write_bms(song, options).bytes);

    const auto player = first.bytes->find("#PLAYER 1");
    const auto title = first.bytes->find("#TITLE Edited title");
    const auto subtitle = first.bytes->find("#SUBTITLE Side title");
    const auto artist = first.bytes->find("#ARTIST Edited artist");
    const auto subartist = first.bytes->find("#SUBARTIST Guest artist");
    const auto stagefile = first.bytes->find("#STAGEFILE stage.png");
    REQUIRE(player != std::string::npos);
    REQUIRE(title != std::string::npos);
    REQUIRE(subtitle != std::string::npos);
    REQUIRE(artist != std::string::npos);
    REQUIRE(subartist != std::string::npos);
    REQUIRE(stagefile != std::string::npos);
    CHECK(first.bytes->find("#PLAYER 1", player + 1) == std::string::npos);
    CHECK(player < title);
    CHECK(title < subtitle);
    CHECK(subtitle < artist);
    CHECK(artist < subartist);
    CHECK(subartist < stagefile);
    CHECK(first.bytes->find("#WAV01 click.wav") != std::string::npos);
    CHECK(first.bytes->find("#00002:0.75") != std::string::npos);
    CHECK(first.bytes->find("#00102:1.25") != std::string::npos);
    CHECK(first.bytes->find("#BPM 120\n") != std::string::npos);
    CHECK(first.bytes->find("#GENRE Example") != std::string::npos);
    CHECK(first.bytes->find("#PLAYLEVEL 12") != std::string::npos);
    CHECK(first.bytes->find("#RANK 2") != std::string::npos);
    CHECK(first.bytes->find("#BANNER banner.png") != std::string::npos);
    CHECK(first.bytes->find("#PREVIEW preview.ogg") != std::string::npos);
    CHECK(first.bytes->find("#CUSTOM_EXTENSION retained value") != std::string::npos);
    CHECK(std::ranges::count(*first.bytes, '#') >= 10);

    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    const auto reparsed = parse(*first.bytes, std::move(parse_options));
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.has_errors());
    CHECK(reparsed.song->metadata.title == "Edited title");
    CHECK(reparsed.song->metadata.artist == "Edited artist");
    REQUIRE(reparsed.song->resources.size() == 1);
    CHECK(reparsed.song->resources.front().name == "click.wav");
    REQUIRE(reparsed.song->charts.front().measures.size() == 2);
    CHECK((reparsed.song->charts.front().measures[0].definition.length ==
           wormedit::rational_t{3, 4}));
    CHECK((reparsed.song->charts.front().measures[1].definition.length ==
           wormedit::rational_t{5, 4}));
    REQUIRE(reparsed.song->charts.front().measures.front().notes.size() == 2);
    CHECK(reparsed.song->charts.front().measures.front().notes[0].position.row == 0);
    CHECK(reparsed.song->charts.front().measures.front().notes[1].position.row ==
          imported.song->charts.front().measures.front().notes[1].position.row);
    const auto& fields = reparsed.song->metadata.format_groups.front().ordered_fields;
    const auto& original_fields = imported.song->metadata.format_groups.front().ordered_fields;
    REQUIRE(fields.size() == original_fields.size());
    for (std::size_t index = 0; index < fields.size(); ++index) {
        CHECK(fields[index].first == original_fields[index].first);
        const auto expected = fields[index].first == "TITLE"    ? std::string("Edited title")
                              : fields[index].first == "ARTIST" ? std::string("Edited artist")
                                                                : original_fields[index].second;
        CHECK(fields[index].second == expected);
    }
    CHECK(std::ranges::any_of(fields, [](const auto& field) {
        return field.first == "SUBTITLE" && field.second.find("Side title") != std::string::npos;
    }));
    CHECK(std::ranges::any_of(fields, [](const auto& field) {
        return field.first == "SUBARTIST" && field.second.find("Guest artist") != std::string::npos;
    }));
    CHECK(std::ranges::any_of(fields, [](const auto& field) {
        return field.first == "STAGEFILE" && field.second.find("stage.png") != std::string::npos;
    }));
}

TEST_CASE("BMS writer flattens note layers and omits bookmarks with warnings",
          "[unit][format][bms][writer]") {
    auto imported = simple_chart();
    REQUIRE(imported.song.has_value());
    auto song = *imported.song;
    auto& chart = song.charts.front();
    chart.semantic_layers.push_back({.id = wormedit::layer_id_t{77}, .name = "Vocal"});
    chart.measures.front().notes.front().semantic_layers = {wormedit::layer_id_t{77}};
    chart.bookmarks.push_back({
        .id = wormedit::object_id_t{78},
        .label = "Section marker",
        .anchor = wormedit::marker_anchor_t::beat,
        .position = chart.measures.front().notes.front().position,
    });
    wormedit::bms_write_options_t options;
    options.preserve_unchanged_source = false;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto written = wormedit::write_bms(song, options);
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(has_warning(written, wormedit::bms_write_diagnostic_code_t::lossy_transform));
    const auto reparsed = parse(*written.bytes, {
                                                    .lane_profile_id = "bme-7k-outside-scratch",
                                                });
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->charts.front().semantic_layers.empty());
    CHECK(reparsed.song->charts.front().measures.front().notes.size() == 2);
}

TEST_CASE("BMS writer round trips holds, repeated BGM lanes, structural zeros, and BGA",
          "[unit][format][bms][writer]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Event round trip\n#BPM 120\n"
        "#WAV01 click.wav\n#WAV02 music.wav\n"
        "#BMP01 bg.png\n#BMP02 poor.png\n#BMP03 layer-a.png\n#BMP04 layer-b.png\n"
        "#00056:010001\n"
        "#00001:0100\n#00001:0002\n"
        "#00004:0100\n#00006:0200\n#00007:0300\n#0000A:0400\n";
    const auto imported = parse(source, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(imported.song.has_value());
    REQUIRE_FALSE(imported.has_errors());
    const auto& original_chart = imported.song->charts.front();
    REQUIRE(original_chart.bgm_source_lanes.size() == 2);
    REQUIRE(original_chart.bga_events.size() == 4);
    REQUIRE(original_chart.measures.front().notes.size() == 1);
    CHECK(original_chart.measures.front().notes.front().kind == wormedit::note_kind_t::hold);

    const auto written =
        wormedit::write_bms(*imported.song, {.lane_profile_id = "bme-7k-outside-scratch",
                                             .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(written.bytes->find("#00056:010001") != std::string::npos);
    CHECK(written.bytes->find("#00004:01") != std::string::npos);
    CHECK(written.bytes->find("#00006:02") != std::string::npos);
    CHECK(written.bytes->find("#00007:03") != std::string::npos);
    CHECK(written.bytes->find("#0000A:04") != std::string::npos);
    CHECK(written.bytes->find("#00001:0100") != std::string::npos);
    CHECK(written.bytes->find("#00001:0002") != std::string::npos);

    const auto reparsed = parse(*written.bytes, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.has_errors());
    const auto& chart = reparsed.song->charts.front();
    REQUIRE(chart.measures.front().notes.size() == 1);
    CHECK(chart.measures.front().notes.front().kind == wormedit::note_kind_t::hold);
    REQUIRE(chart.measures.front().notes.front().end.has_value());
    CHECK(chart.measures.front().notes.front().end->row ==
          original_chart.measures.front().notes.front().end->row);
    REQUIRE(chart.bgm_source_lanes.size() == 2);
    REQUIRE(chart.measures.front().bgm_events.size() == 4);
    CHECK(chart.measures.front().bgm_events[0].structural_slot == 0);
    CHECK(chart.measures.front().bgm_events[1].structural_slot == 1);
    CHECK(chart.measures.front().bgm_events[2].structural_slot == 0);
    CHECK(chart.measures.front().bgm_events[3].structural_slot == 1);
    CHECK(chart.measures.front().bgm_events[1].sound == std::nullopt);
    CHECK(chart.measures.front().bgm_events[2].sound == std::nullopt);
    REQUIRE(chart.bga_events.size() == 4);
    CHECK(std::ranges::count_if(chart.bga_events, [](const auto& event) {
              return event.kind == wormedit::visual_event_kind_t::background;
          }) == 1);
    CHECK(std::ranges::count_if(chart.bga_events, [](const auto& event) {
              return event.kind == wormedit::visual_event_kind_t::poor;
          }) == 1);
    CHECK(std::ranges::count_if(chart.bga_events, [](const auto& event) {
              return event.kind == wormedit::visual_event_kind_t::layer;
          }) == 2);
}

TEST_CASE("BMS writer supports authored BGM lanes across measures", "[unit][format][bms][writer]") {
    auto song = *simple_chart().song;
    auto& chart = song.charts.front();
    chart.bgm_source_lanes.push_back({
        .id = wormedit::object_id_t{9001},
        .name = "Authored BGM",
        .source_order = 1,
    });
    chart.bgm_source_lanes.push_back({
        .id = wormedit::object_id_t{9002},
        .name = "Empty editor lane",
        .source_order = 2,
    });
    chart.measures[0].bgm_events.push_back({
        .id = wormedit::object_id_t{9003},
        .position = {chart.measures[0].id, 0},
        .source_lane = wormedit::object_id_t{9001},
        .sound = song.resources.front().id,
    });
    chart.measures[1].bgm_events.push_back({
        .id = wormedit::object_id_t{9004},
        .position = {chart.measures[1].id, chart.measures[1].definition.length.numerator * 120},
        .source_lane = wormedit::object_id_t{9001},
        .sound = song.resources.front().id,
    });

    const auto written = wormedit::write_bms(
        song, {.lane_profile_id = "bme-7k-outside-scratch", .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(has_warning(written, wormedit::bms_write_diagnostic_code_t::lossy_transform));
    CHECK(written.bytes->find("#00001:01") != std::string::npos);
    CHECK(written.bytes->find("#00101:0001") != std::string::npos);

    const auto reparsed = parse(*written.bytes, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.has_errors());
    CHECK(reparsed.song->charts.front().measures[0].bgm_events.size() == 1);
    CHECK(reparsed.song->charts.front().measures[1].bgm_events.size() == 2);
}

TEST_CASE("BMS writer reports unrepresentable hold and visual-event semantics",
          "[unit][format][bms][writer]") {
    constexpr std::string_view source = "#PLAYER 1\n#TITLE Events\n#BPM 120\n#WAV01 click.wav\n"
                                        "#BMP01 layer.png\n#00056:010001\n#00007:01\n#0000A:01\n";
    const auto imported = parse(source, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(imported.song.has_value());
    wormedit::bms_write_options_t options{.lane_profile_id = "bme-7k-outside-scratch",
                                          .preserve_unchanged_source = false};

    auto roll = *imported.song;
    roll.charts.front().measures.front().notes.front().kind = wormedit::note_kind_t::roll;
    const auto roll_result = wormedit::write_bms(roll, options);
    CHECK_FALSE(roll_result.bytes.has_value());
    CHECK(has_error(roll_result, wormedit::bms_write_diagnostic_code_t::unsupported_feature));

    auto video = *imported.song;
    video.charts.front().bga_events.front().kind = wormedit::visual_event_kind_t::video;
    const auto video_result = wormedit::write_bms(video, options);
    CHECK_FALSE(video_result.bytes.has_value());
    CHECK(has_error(video_result, wormedit::bms_write_diagnostic_code_t::unsupported_feature));

    auto crowded_layers = *imported.song;
    auto third_layer = crowded_layers.charts.front().bga_events.front();
    third_layer.id = wormedit::object_id_t{9001};
    third_layer.kind = wormedit::visual_event_kind_t::layer;
    crowded_layers.charts.front().bga_events.push_back(third_layer);
    const auto layer_result = wormedit::write_bms(crowded_layers, options);
    CHECK_FALSE(layer_result.bytes.has_value());
    CHECK(has_error(layer_result, wormedit::bms_write_diagnostic_code_t::duplicate_event));
}

TEST_CASE("BMS writer preserves nested control trees and edits root content outside them",
          "[unit][format][bms][writer]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Root title\n#BPM 120\n#WAV01 root.wav\n#00011:01\n"
        "#RANDOM 2\n#IF 1\n#SETSWITCH 1\n#SWITCH 2\n#CASE 1\n#RANDOM 1\n"
        "#IF 1\n#WAV03 branch-b.wav\n#00011:03\n#ENDIF\n#ENDRANDOM\n#SKIP\n"
        "#DEF\n#WAV04 branch-default.wav\n#00011:04\n#ENDSW\n#ELSE\n"
        "#WAV02 outer-else.wav\n#00011:02\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {{.control_source_order = 5, .selected_value = 1},
                                    {.control_source_order = 8, .selected_value = 1},
                                    {.control_source_order = 10, .selected_value = 1}};
    auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE_FALSE(imported.has_errors());
    auto song = *imported.song;
    song.metadata.title = "Edited root title";
    auto& notes = song.charts.front().measures.front().notes;
    REQUIRE(notes.size() == 2);
    auto root_note = std::ranges::find_if(
        notes, [](const auto& note) { return note.provenance.source_order == 4; });
    REQUIRE(root_note != notes.end());
    root_note->position.row = 120;

    wormedit::bms_write_options_t options;
    options.preserve_unchanged_source = false;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto written = wormedit::write_bms(song, options);
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(written.bytes->find("#TITLE Edited root title") != std::string::npos);
    CHECK(written.bytes->find("#RANDOM 2") != std::string::npos);
    CHECK(written.bytes->find("#IF 1") != std::string::npos);
    CHECK(written.bytes->find("#SETSWITCH 1") != std::string::npos);
    CHECK(written.bytes->find("#SWITCH 2") != std::string::npos);
    CHECK(written.bytes->find("#RANDOM 1") != std::string::npos);
    CHECK(written.bytes->find("#CASE 1") != std::string::npos);
    CHECK(written.bytes->find("#SKIP") != std::string::npos);
    CHECK(written.bytes->find("#DEF") != std::string::npos);
    CHECK(written.bytes->find("#ENDSW") != std::string::npos);
    CHECK(written.bytes->find("#ENDIF") != std::string::npos);
    CHECK(written.bytes->find("#ENDRANDOM") != std::string::npos);
    CHECK(written.bytes->find("#WAV03 branch-b.wav") != std::string::npos);
    CHECK(written.bytes->find("#WAV04 branch-default.wav") != std::string::npos);
    CHECK(written.bytes->find("#WAV02 outer-else.wav") != std::string::npos);
    CHECK(occurrences(*written.bytes, "#00011:") == 4);

    const auto random_order = line_order(*written.bytes, "#RANDOM 2");
    const auto nested_random_order = line_order(*written.bytes, "#RANDOM 1");
    const auto switch_order = line_order(*written.bytes, "#SWITCH 2");
    REQUIRE(random_order != std::string_view::npos);
    REQUIRE(nested_random_order != std::string_view::npos);
    REQUIRE(switch_order != std::string_view::npos);
    wormedit::bms_parse_options_t reparse_options;
    reparse_options.lane_profile_id = "bme-7k-outside-scratch";
    reparse_options.branch_choices = {
        {.control_source_order = random_order, .selected_value = 1},
        {.control_source_order = switch_order, .selected_value = 1},
        {.control_source_order = nested_random_order, .selected_value = 1}};
    const auto reparsed = parse(*written.bytes, reparse_options);
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.has_errors());
    const auto& reparsed_notes = reparsed.song->charts.front().measures.front().notes;
    REQUIRE(reparsed_notes.size() == 2);
    CHECK(std::ranges::any_of(reparsed_notes, [](const auto& note) {
        return note.position.row == 120 && note.sound.has_value();
    }));
    CHECK(std::ranges::any_of(reparsed_notes, [](const auto& note) {
        return note.position.row == 0 && note.sound.has_value() &&
               note.provenance.source_field.starts_with("11 slot 0 object 03");
    }));

    options.branch_policy = wormedit::bms_branch_policy_t::flatten_active;
    const auto flatten = wormedit::write_bms(song, options);
    CHECK_FALSE(flatten.bytes.has_value());
    CHECK(has_error(flatten, wormedit::bms_write_diagnostic_code_t::unsupported_branch_policy));
}

TEST_CASE("BMS writer rejects unsafe branch metadata and root/branch ordering ambiguity",
          "[unit][format][bms][writer]") {
    constexpr std::string_view metadata_source =
        "#PLAYER 1\n#RANDOM 1\n#IF 1\n#TITLE Branch title\n#WAV01 click.wav\n"
        "#00011:01\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices.push_back({.control_source_order = 1, .selected_value = 1});
    const auto metadata_import = parse(metadata_source, parse_options);
    REQUIRE(metadata_import.song.has_value());
    const auto metadata_result =
        wormedit::write_bms(*metadata_import.song, {.lane_profile_id = "bme-7k-outside-scratch",
                                                    .preserve_unchanged_source = false});
    CHECK_FALSE(metadata_result.bytes.has_value());
    CHECK(has_error(metadata_result, wormedit::bms_write_diagnostic_code_t::branch_provenance));

    constexpr std::string_view straddled_source =
        "#PLAYER 1\n#TITLE Order\n#BPM 120\n#WAV01 click.wav\n#00011:01\n"
        "#RANDOM 1\n#IF 1\n#WAV02 branch.wav\n#00011:02\n#ENDIF\n#ENDRANDOM\n"
        "#WAV03 after.wav\n";
    parse_options.branch_choices = {{.control_source_order = 5, .selected_value = 1}};
    const auto straddled_import = parse(straddled_source, parse_options);
    REQUIRE(straddled_import.song.has_value());
    const auto order_result =
        wormedit::write_bms(*straddled_import.song, {.lane_profile_id = "bme-7k-outside-scratch",
                                                     .preserve_unchanged_source = false});
    CHECK_FALSE(order_result.bytes.has_value());
    CHECK(has_error(order_result, wormedit::bms_write_diagnostic_code_t::branch_provenance));
}

TEST_CASE("BMS writer diagnoses unsafe retained metadata tokens", "[unit][format][bms][writer]") {
    wormedit::bms_write_options_t options;
    options.preserve_unchanged_source = false;
    options.lane_profile_id = "bme-7k-outside-scratch";

    auto unsafe = *simple_chart().song;
    unsafe.metadata.format_groups.front().ordered_fields.front().first = "TITLE\n#WAV01";
    const auto unsafe_result = wormedit::write_bms(unsafe, options);
    CHECK_FALSE(unsafe_result.bytes.has_value());
    CHECK(has_error(unsafe_result, wormedit::bms_write_diagnostic_code_t::invalid_document));

    auto extension = *simple_chart().song;
    extension.metadata.format_groups.front().ordered_fields.emplace_back("CUSTOM_EXTENSION",
                                                                         "value");
    const auto extension_result = wormedit::write_bms(extension, options);
    REQUIRE(extension_result.bytes.has_value());
    CHECK(extension_result.bytes->find("#CUSTOM_EXTENSION value") != std::string::npos);

    auto nonterminating = *simple_chart().song;
    nonterminating.charts.front().measures.front().definition.length = {1, 3};
    nonterminating.charts.front().measures.front().definition.meter.reset();
    nonterminating.charts.front().measures.front().notes.clear();
    const auto nonterminating_result = wormedit::write_bms(nonterminating, options);
    CHECK_FALSE(nonterminating_result.bytes.has_value());
    CHECK(has_error(nonterminating_result,
                    wormedit::bms_write_diagnostic_code_t::unrepresentable_number));
}

TEST_CASE("BMS normalized output diagnoses omitted raw source lines",
          "[unit][format][bms][writer]") {
    constexpr std::string_view source =
        "; retained source comment\n#PLAYER 1\n#TITLE Commented\n#BPM 120\n";
    const auto imported = parse(source);
    REQUIRE(imported.song.has_value());
    const auto written =
        wormedit::write_bms(*imported.song, {.lane_profile_id = "bme-7k-outside-scratch",
                                             .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(has_warning(written, wormedit::bms_write_diagnostic_code_t::lossy_transform));
    CHECK(written.bytes->find("retained source comment") == std::string::npos);
}

TEST_CASE("BMS writer emits every retained song and chart metadata group",
          "[unit][format][bms][writer]") {
    auto song = *simple_chart().song;
    song.metadata.format_groups.push_back({
        .family = wormedit::format_family_t::bms,
        .ordered_fields = {{"SECOND_GROUP", "song value"}},
    });
    song.charts.front().metadata.format_groups.push_back({
        .family = wormedit::format_family_t::bms,
        .ordered_fields = {{"CHART_GROUP", "chart value"}},
    });

    const auto written = wormedit::write_bms(
        song, {.lane_profile_id = "bme-7k-outside-scratch", .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(written.bytes->find("#SECOND_GROUP song value") != std::string::npos);
    CHECK(written.bytes->find("#CHART_GROUP chart value") != std::string::npos);
}

TEST_CASE("BMS writer validates only the selected chart", "[unit][format][bms][writer]") {
    auto song = *simple_chart().song;
    const auto selected_chart = song.charts.front().id;
    auto unrelated = song.charts.front();
    unrelated.id = wormedit::chart_id_t{999};
    unrelated.divisions_per_standard_measure = 0;
    song.charts.push_back(std::move(unrelated));

    const auto written = wormedit::write_bms(song, {.chart = selected_chart,
                                                    .lane_profile_id = "bme-7k-outside-scratch",
                                                    .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
}
