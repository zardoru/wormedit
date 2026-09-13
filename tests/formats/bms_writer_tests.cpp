#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

    wormedit::bms_write_options_t normalized_options;
    normalized_options.lane_profile_id = "bme-7k-outside-scratch";
    normalized_options.output_encoding = wormedit::text_encoding_t::ascii;
    const auto normalized = wormedit::write_bms(*imported.song, normalized_options);
    REQUIRE(normalized.bytes.has_value());
    CHECK_FALSE(normalized.has_errors());
    CHECK(*normalized.bytes != source);
    CHECK(normalized.bytes->find('\r') == std::string::npos);
}

TEST_CASE("BMS writer selects normalized UTF-8 or ASCII output explicitly",
          "[unit][format][bms][writer][encoding]") {
    auto imported = simple_chart();
    REQUIRE(imported.song.has_value());

    wormedit::bms_write_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.output_encoding = wormedit::text_encoding_t::ascii;
    const auto ascii = wormedit::write_bms(*imported.song, options);
    REQUIRE(ascii.bytes.has_value());
    CHECK_FALSE(ascii.has_errors());
    CHECK_FALSE(ascii.bytes->starts_with("\xef\xbb\xbf"));
    const auto reparsed_ascii = parse(*ascii.bytes, {.lane_profile_id = options.lane_profile_id});
    REQUIRE(reparsed_ascii.song.has_value());
    CHECK(reparsed_ascii.encoding.detected == wormedit::text_encoding_t::ascii);
    CHECK(reparsed_ascii.song->metadata.title == "Original title");

    auto unicode_song = *imported.song;
    unicode_song.metadata.title = "Snow / 雪";
    options.output_encoding = wormedit::text_encoding_t::utf8;
    const auto utf8 = wormedit::write_bms(unicode_song, options);
    REQUIRE(utf8.bytes.has_value());
    CHECK_FALSE(utf8.has_errors());
    CHECK(utf8.bytes->starts_with("\xef\xbb\xbf"));
    const auto reparsed_utf8 = parse(*utf8.bytes, {.lane_profile_id = options.lane_profile_id});
    REQUIRE(reparsed_utf8.song.has_value());
    CHECK(reparsed_utf8.encoding.detected == wormedit::text_encoding_t::utf8);
    CHECK(reparsed_utf8.song->metadata.title == "Snow / 雪");

    options.output_encoding = wormedit::text_encoding_t::ascii;
    const auto unrepresentable = wormedit::write_bms(unicode_song, options);
    CHECK_FALSE(unrepresentable.bytes.has_value());
    CHECK(has_error(unrepresentable, wormedit::bms_write_diagnostic_code_t::unrepresentable_text));

    options.output_encoding = wormedit::text_encoding_t::unknown;
    const auto unknown = wormedit::write_bms(*imported.song, options);
    CHECK_FALSE(unknown.bytes.has_value());
    CHECK(has_error(unknown, wormedit::bms_write_diagnostic_code_t::unsupported_encoding));
}

TEST_CASE("BMS writer round-trips each available selected legacy output encoding",
          "[unit][format][bms][writer][encoding]") {
    auto imported = simple_chart();
    REQUIRE(imported.song.has_value());
    struct legacy_case_t {
        wormedit::text_encoding_t encoding;
        std::string_view title;
    };
    constexpr std::array cases{
        legacy_case_t{wormedit::text_encoding_t::cp932, "日本語譜面"},
        legacy_case_t{wormedit::text_encoding_t::euc_kr, "한국어"},
        legacy_case_t{wormedit::text_encoding_t::cp949, "뷁"},
        legacy_case_t{wormedit::text_encoding_t::windows_1252, "Café – résumé"},
    };
    for (const auto& test : cases) {
        CAPTURE(static_cast<int>(test.encoding));
        auto song = *imported.song;
        song.metadata.title = test.title;
        wormedit::bms_write_options_t options;
        options.lane_profile_id = "bme-7k-outside-scratch";
        options.output_encoding = test.encoding;
        const auto written = wormedit::write_bms(song, options);
        if (!wormedit::is_bms_text_encoding_supported(test.encoding)) {
            CHECK_FALSE(written.bytes.has_value());
            CHECK(has_error(written, wormedit::bms_write_diagnostic_code_t::unsupported_encoding));
            continue;
        }
        REQUIRE(written.bytes.has_value());
        CHECK_FALSE(written.has_errors());
        CHECK_FALSE(written.bytes->starts_with("\xef\xbb\xbf"));

        wormedit::bms_parse_options_t parse_options;
        parse_options.encoding_override = test.encoding;
        parse_options.lane_profile_id = options.lane_profile_id;
        const auto reparsed = parse(*written.bytes, std::move(parse_options));
        REQUIRE(reparsed.song.has_value());
        CHECK_FALSE(reparsed.has_errors());
        CHECK(reparsed.song->metadata.title == test.title);
    }
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
    INFO((flatten.diagnostics.empty() ? std::string{} : flatten.diagnostics.back().message));
    REQUIRE(flatten.bytes.has_value());
    CHECK_FALSE(flatten.has_errors());
    CHECK(has_warning(flatten, wormedit::bms_write_diagnostic_code_t::lossy_transform));
    CHECK(flatten.bytes->find("#RANDOM") == std::string::npos);
    CHECK(flatten.bytes->find("#IF") == std::string::npos);
    CHECK(flatten.bytes->find("branch-b.wav") != std::string::npos);
    CHECK(flatten.bytes->find("branch-default.wav") == std::string::npos);
    CHECK(flatten.bytes->find("outer-else.wav") == std::string::npos);
    const auto flattened_reparse =
        parse(*flatten.bytes, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(flattened_reparse.song.has_value());
    CHECK(flattened_reparse.song->charts.front().active_branch_selection.empty());
    CHECK(flattened_reparse.song->charts.front().measures.front().notes.size() == 2);

    auto unresolved = song;
    unresolved.charts.front().active_branch_selection.clear();
    const auto unresolved_flatten = wormedit::write_bms(unresolved, options);
    CHECK_FALSE(unresolved_flatten.bytes.has_value());
    CHECK(has_error(unresolved_flatten,
                    wormedit::bms_write_diagnostic_code_t::unsupported_branch_policy));
}

TEST_CASE("BMS writer preserves branches and exact ratios while editing the active projection",
          "[unit][format][bms][writer]") {
    constexpr std::string_view metadata_source =
        "#PLAYER 1\n#TITLE Root title\n#00002:5/4\n#WAV03 root.wav\n#00011:03\n"
        "#RANDOM 2\n#IF 1\n#TITLE Branch one\n#00002:3/4\n#WAV01 one.wav\n"
        "#00011:0001\n#ELSE\n#TITLE Branch two\n#00002:1/2\n#WAV02 two.wav\n"
        "#00011:000002\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices.push_back(
        {.control_source_order = line_order(metadata_source, "#RANDOM 2"), .selected_value = 1});
    const auto metadata_import = parse(metadata_source, parse_options);
    REQUIRE(metadata_import.song.has_value());
    auto edited_song = *metadata_import.song;
    edited_song.metadata.title = "Edited root title";
    auto& chart = edited_song.charts.front();
    const auto root_note =
        std::ranges::find_if(chart.measures.front().notes, [&](const auto& note) {
            return note.provenance.source_order == line_order(metadata_source, "#00011:03");
        });
    REQUIRE(root_note != chart.measures.front().notes.end());
    root_note->position.row = 120;

    wormedit::bms_write_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.preserve_unchanged_source = false;
    const auto written = wormedit::write_bms(edited_song, options);
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(written.bytes->find("#TITLE Edited root title") != std::string::npos);
    CHECK(occurrences(*written.bytes, "#TITLE Edited root title") == 1);
    CHECK(occurrences(*written.bytes, "#TITLE Branch two") == 1);
    CHECK(occurrences(*written.bytes, "#00002:5/4") == 1);
    CHECK(occurrences(*written.bytes, "#00002:3/4") == 1);
    CHECK(occurrences(*written.bytes, "#00002:1/2") == 1);
    CHECK(line_order(*written.bytes, "#TITLE Edited root title") >
          line_order(*written.bytes, "#RANDOM 2"));

    const auto written_random_order = line_order(*written.bytes, "#RANDOM 2");
    parse_options.branch_choices = {
        {.control_source_order = written_random_order, .selected_value = 1}};
    const auto first_branch = parse(*written.bytes, parse_options);
    REQUIRE(first_branch.song.has_value());
    CHECK(first_branch.song->metadata.title == "Edited root title");
    CHECK((first_branch.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{3, 4}));
    CHECK(std::ranges::any_of(first_branch.song->charts.front().measures.front().notes,
                              [](const auto& note) { return note.position.row == 120; }));

    parse_options.branch_choices.front().selected_value = 2;
    const auto second_branch = parse(*written.bytes, parse_options);
    REQUIRE(second_branch.song.has_value());
    CHECK(second_branch.song->metadata.title == "Branch two");
    CHECK((second_branch.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{1, 2}));

    parse_options.branch_choices.clear();
    const auto root_projection = parse(*written.bytes, parse_options);
    REQUIRE(root_projection.song.has_value());
    CHECK(root_projection.song->metadata.title == "Root title");
    CHECK((root_projection.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{5, 4}));

    auto flatten_options = options;
    flatten_options.branch_policy = wormedit::bms_branch_policy_t::flatten_active;
    const auto flattened = wormedit::write_bms(*metadata_import.song, flatten_options);
    std::string flatten_diagnostics;
    for (const auto& diagnostic : flattened.diagnostics) {
        flatten_diagnostics += diagnostic.message + " at " + diagnostic.path + "\n";
    }
    INFO(flatten_diagnostics);
    REQUIRE(flattened.bytes.has_value());
    CHECK(has_warning(flattened, wormedit::bms_write_diagnostic_code_t::lossy_transform));
    CHECK(flattened.bytes->find("#RANDOM") == std::string::npos);
    CHECK(occurrences(*flattened.bytes, "#TITLE Branch one") == 1);
    CHECK(flattened.bytes->find("#TITLE Branch two") == std::string::npos);
    CHECK(flattened.bytes->find("two.wav") == std::string::npos);
    const auto flattened_projection = parse(*flattened.bytes);
    REQUIRE(flattened_projection.song.has_value());
    CHECK(flattened_projection.song->metadata.title == "Branch one");
    CHECK((flattened_projection.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{3, 4}));

    edited_song.charts.front().measures.front().definition.length = {7, 4};
    const auto edited_branch_ratio = wormedit::write_bms(edited_song, options);
    CHECK_FALSE(edited_branch_ratio.bytes.has_value());
    CHECK(has_error(edited_branch_ratio, wormedit::bms_write_diagnostic_code_t::branch_provenance));

    auto edited_branch_metadata = *metadata_import.song;
    auto& metadata_fields = edited_branch_metadata.metadata.format_groups.front();
    const auto branch_title_index =
        std::ranges::find(metadata_fields.ordered_field_source_orders,
                          line_order(metadata_source, "#TITLE Branch one"));
    REQUIRE(branch_title_index != metadata_fields.ordered_field_source_orders.end());
    metadata_fields
        .ordered_fields[static_cast<std::size_t>(
            branch_title_index - metadata_fields.ordered_field_source_orders.begin())]
        .second = "Edited branch title";
    const auto edited_branch_title = wormedit::write_bms(edited_branch_metadata, options);
    REQUIRE(edited_branch_title.bytes.has_value());
    CHECK_FALSE(edited_branch_title.has_errors());
    CHECK(occurrences(*edited_branch_title.bytes, "#TITLE Edited branch title") == 1);
    CHECK(occurrences(*edited_branch_title.bytes, "#TITLE Branch two") == 1);
    const auto edited_random_order = line_order(*edited_branch_title.bytes, "#RANDOM 2");
    parse_options.branch_choices = {
        {.control_source_order = edited_random_order, .selected_value = 1}};
    const auto edited_branch_reimport = parse(*edited_branch_title.bytes, parse_options);
    REQUIRE(edited_branch_reimport.song.has_value());
    CHECK(edited_branch_reimport.song->metadata.title == "Edited branch title");
    parse_options.branch_choices.front().selected_value = 2;
    const auto unchanged_other_branch = parse(*edited_branch_title.bytes, parse_options);
    REQUIRE(unchanged_other_branch.song.has_value());
    CHECK(unchanged_other_branch.song->metadata.title == "Branch two");

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

TEST_CASE("BMS writer keeps branch-only metadata inside its branch",
          "[unit][format][bms][writer]") {
    constexpr std::string_view source =
        "#RANDOM 1\n#IF 1\n#TITLE Branch-only title\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.branch_choices.push_back(
        {.control_source_order = line_order(source, "#RANDOM 1"), .selected_value = 1});
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE(imported.song->metadata.title == "Branch-only title");

    const auto written =
        wormedit::write_bms(*imported.song, {.lane_profile_id = "bme-7k-outside-scratch",
                                             .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK(occurrences(*written.bytes, "#TITLE Branch-only title") == 1);
    CHECK(line_order(*written.bytes, "#RANDOM 1") <
          line_order(*written.bytes, "#TITLE Branch-only title"));

    const auto reparsed = parse(*written.bytes);
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->metadata.title.empty());
}

TEST_CASE("BMS writer edits projected metadata in place across branch choices",
          "[unit][format][bms][writer]") {
    constexpr std::string_view source =
        "#TITLE Root title\n#RANDOM 2\n#IF 1\n#TITLE First title\n"
        "#ARTIST First artist\n#SUBTITLE First subtitle\n#ELSE\n"
        "#TITLE Second title\n#ARTIST Second artist\n#SUBTITLE Second subtitle\n"
        "#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    auto first = parse(source, parse_options);
    REQUIRE(first.song.has_value());
    auto ambiguous_provenance = *first.song;
    const auto branch_title_order = line_order(source, "#TITLE First title");
    const auto branch_title_source = std::ranges::find(
        ambiguous_provenance.metadata.format_groups.front().ordered_field_source_orders,
        branch_title_order);
    REQUIRE(branch_title_source !=
            ambiguous_provenance.metadata.format_groups.front().ordered_field_source_orders.end());
    *branch_title_source = line_order(source, "#IF 1");
    const auto ambiguous_write =
        wormedit::write_bms(ambiguous_provenance, {
                                                      .lane_profile_id = "bme-7k-outside-scratch",
                                                      .preserve_unchanged_source = false,
                                                  });
    CHECK_FALSE(ambiguous_write.bytes.has_value());
    CHECK(has_error(ambiguous_write, wormedit::bms_write_diagnostic_code_t::branch_provenance));

    first.song->metadata.title = "Edited first title";
    auto& first_fields = first.song->metadata.format_groups.front();
    const auto first_subtitle_order = line_order(source, "#SUBTITLE First subtitle");
    const auto first_subtitle =
        std::ranges::find(first_fields.ordered_field_source_orders, first_subtitle_order);
    REQUIRE(first_subtitle != first_fields.ordered_field_source_orders.end());
    first_fields
        .ordered_fields[static_cast<std::size_t>(first_subtitle -
                                                 first_fields.ordered_field_source_orders.begin())]
        .second = "Edited first subtitle";

    wormedit::bms_write_options_t write_options;
    write_options.lane_profile_id = "bme-7k-outside-scratch";
    write_options.preserve_unchanged_source = false;
    write_options.output_encoding = wormedit::text_encoding_t::ascii;
    const auto first_written = wormedit::write_bms(*first.song, write_options);
    REQUIRE(first_written.bytes.has_value());
    CHECK_FALSE(first_written.has_errors());
    CHECK(occurrences(*first_written.bytes, "#TITLE Root title") == 1);
    CHECK(occurrences(*first_written.bytes, "#TITLE Edited first title") == 1);
    CHECK(occurrences(*first_written.bytes, "#SUBTITLE Edited first subtitle") == 1);
    CHECK(occurrences(*first_written.bytes, "#TITLE Second title") == 1);
    CHECK(occurrences(*first_written.bytes, "#ARTIST Second artist") == 1);
    CHECK(occurrences(*first_written.bytes, "#SUBTITLE Second subtitle") == 1);
    CHECK(occurrences(*first_written.bytes, "#ELSE") == 1);
    CHECK(occurrences(*first_written.bytes, "#ENDIF") == 1);
    CHECK(occurrences(*first_written.bytes, "#ENDRANDOM") == 1);

    const auto first_random_order = line_order(*first_written.bytes, "#RANDOM 2");
    parse_options.branch_choices = {
        {.control_source_order = first_random_order, .selected_value = 1}};
    const auto first_reimport = parse(*first_written.bytes, parse_options);
    REQUIRE(first_reimport.song.has_value());
    CHECK(first_reimport.song->metadata.title == "Edited first title");
    CHECK(std::ranges::any_of(
        first_reimport.song->metadata.format_groups.front().ordered_fields, [](const auto& field) {
            return field.first == "SUBTITLE" && field.second == "Edited first subtitle";
        }));

    parse_options.branch_choices.front().selected_value = 2;
    auto second = parse(*first_written.bytes, parse_options);
    REQUIRE(second.song.has_value());
    second.song->metadata.artist = "Edited second artist";
    auto& second_fields = second.song->metadata.format_groups.front();
    const auto second_subtitle_order =
        line_order(*first_written.bytes, "#SUBTITLE Second subtitle");
    const auto second_subtitle =
        std::ranges::find(second_fields.ordered_field_source_orders, second_subtitle_order);
    REQUIRE(second_subtitle != second_fields.ordered_field_source_orders.end());
    second_fields
        .ordered_fields[static_cast<std::size_t>(second_subtitle -
                                                 second_fields.ordered_field_source_orders.begin())]
        .second = "Edited second subtitle";
    const auto second_written = wormedit::write_bms(*second.song, write_options);
    REQUIRE(second_written.bytes.has_value());
    CHECK_FALSE(second_written.has_errors());
    CHECK(occurrences(*second_written.bytes, "#ARTIST Edited second artist") == 1);
    CHECK(occurrences(*second_written.bytes, "#SUBTITLE Edited second subtitle") == 1);
    CHECK(occurrences(*second_written.bytes, "#TITLE Edited first title") == 1);

    const auto second_random_order = line_order(*second_written.bytes, "#RANDOM 2");
    parse_options.branch_choices = {
        {.control_source_order = second_random_order, .selected_value = 1}};
    const auto preserved_first = parse(*second_written.bytes, parse_options);
    REQUIRE(preserved_first.song.has_value());
    CHECK(preserved_first.song->metadata.title == "Edited first title");
    parse_options.branch_choices.front().selected_value = 2;
    const auto edited_second = parse(*second_written.bytes, parse_options);
    REQUIRE(edited_second.song.has_value());
    CHECK(edited_second.song->metadata.title == "Second title");
    CHECK(edited_second.song->metadata.artist == "Edited second artist");
    CHECK(std::ranges::any_of(
        edited_second.song->metadata.format_groups.front().ordered_fields, [](const auto& field) {
            return field.first == "SUBTITLE" && field.second == "Edited second subtitle";
        }));

    parse_options.branch_choices.clear();
    const auto root_projection = parse(*second_written.bytes, parse_options);
    REQUIRE(root_projection.song.has_value());
    CHECK(root_projection.song->metadata.title == "Root title");
}

TEST_CASE("BMS writer patches one branch resource path without changing sibling branches",
          "[unit][format][bms][writer][branch][resource]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Branch resources\n#BPM 120\n#RANDOM 2\n#IF 1\n"
        "#WAV01  first.wav\n#00011:01\n#ELSE\n#WAV01 second.wav\n#00011:01\n"
        "#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    const auto first_resource_order = line_order(source, "#WAV01  first.wav");
    const auto resource = std::ranges::find_if(imported.song->resources, [&](const auto& entry) {
        return entry.provenance.source_order == first_resource_order;
    });
    REQUIRE(resource != imported.song->resources.end());

    wormedit::bms_write_options_t write_options;
    write_options.lane_profile_id = parse_options.lane_profile_id;
    write_options.preserve_unchanged_source = false;
    const auto unchanged = wormedit::write_bms(*imported.song, write_options);
    REQUIRE(unchanged.bytes.has_value());
    CHECK(occurrences(*unchanged.bytes, "#WAV01  first.wav") == 1);

    resource->name = "edited-first.wav";
    const auto written = wormedit::write_bms(*imported.song, write_options);
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(occurrences(*written.bytes, "#WAV01 edited-first.wav") == 1);
    CHECK(occurrences(*written.bytes, "#WAV01 second.wav") == 1);
    CHECK(occurrences(*written.bytes, "#RANDOM 2") == 1);
    CHECK(occurrences(*written.bytes, "#IF 1") == 1);
    CHECK(occurrences(*written.bytes, "#ELSE") == 1);
    CHECK(occurrences(*written.bytes, "#ENDIF") == 1);
    CHECK(occurrences(*written.bytes, "#ENDRANDOM") == 1);

    const auto random_order = line_order(*written.bytes, "#RANDOM 2");
    parse_options.branch_choices = {{.control_source_order = random_order, .selected_value = 1}};
    const auto first = parse(*written.bytes, parse_options);
    REQUIRE(first.song.has_value());
    REQUIRE(first.song->resources.size() == 1);
    CHECK(first.song->resources.front().name == "edited-first.wav");

    parse_options.branch_choices.front().selected_value = 2;
    const auto second = parse(*written.bytes, parse_options);
    REQUIRE(second.song.has_value());
    REQUIRE(second.song->resources.size() == 1);
    CHECK(second.song->resources.front().name == "second.wav");

    auto ambiguous = *imported.song;
    ambiguous.resources.front().provenance.source_order = line_order(source, "#WAV01 second.wav");
    const auto rejected = wormedit::write_bms(ambiguous, write_options);
    CHECK_FALSE(rejected.bytes.has_value());
    CHECK(has_error(rejected, wormedit::bms_write_diagnostic_code_t::branch_provenance));
}

TEST_CASE("BMS writer preserves mixed root and branch timing statements",
          "[unit][format][bms][writer]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Mixed timing\n#BPM 120\n#BPM01 150\n#STOP01 48\n"
        "#00008:0001\n#00009:0001\n#RANDOM 2\n#IF 1\n#BPM 180\n#BPM02 200\n"
        "#STOP02 96\n#00008:0002\n#00009:0002\n#ELSE\n#BPM 240\n#BPM03 220\n"
        "#STOP03 144\n#00008:0003\n#00009:0003\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices.push_back(
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1});
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());

    auto edited = *imported.song;
    edited.metadata.title = "Edited mixed timing";
    wormedit::bms_write_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.preserve_unchanged_source = false;
    const auto written = wormedit::write_bms(edited, options);
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(occurrences(*written.bytes, "#BPM 120") == 1);
    CHECK(occurrences(*written.bytes, "#BPM 180") == 1);
    CHECK(occurrences(*written.bytes, "#BPM 240") == 1);
    CHECK(occurrences(*written.bytes, "#BPM01 150") == 1);
    CHECK(occurrences(*written.bytes, "#BPM02 200") == 1);
    CHECK(occurrences(*written.bytes, "#BPM03 220") == 1);
    CHECK(occurrences(*written.bytes, "#STOP01 48") == 1);
    CHECK(occurrences(*written.bytes, "#STOP02 96") == 1);
    CHECK(occurrences(*written.bytes, "#STOP03 144") == 1);
    CHECK(occurrences(*written.bytes, "#00008:0001") == 1);
    CHECK(occurrences(*written.bytes, "#00008:0002") == 1);
    CHECK(occurrences(*written.bytes, "#00008:0003") == 1);
    CHECK(occurrences(*written.bytes, "#00009:0001") == 1);
    CHECK(occurrences(*written.bytes, "#00009:0002") == 1);
    CHECK(occurrences(*written.bytes, "#00009:0003") == 1);

    const auto random_order = line_order(*written.bytes, "#RANDOM 2");
    parse_options.branch_choices = {{.control_source_order = random_order, .selected_value = 1}};
    const auto first = parse(*written.bytes, parse_options);
    REQUIRE(first.song.has_value());
    CHECK(std::ranges::any_of(first.song->charts.front().bpms, [](const auto& bpm) {
        return bpm.beats_per_minute == wormedit::signed_rational_t{180, 1};
    }));
    CHECK(std::ranges::any_of(first.song->charts.front().pauses, [](const auto& pause) {
        return pause.duration == wormedit::signed_rational_t{96, 1};
    }));

    parse_options.branch_choices.front().selected_value = 2;
    const auto second = parse(*written.bytes, parse_options);
    REQUIRE(second.song.has_value());
    CHECK(std::ranges::any_of(second.song->charts.front().bpms, [](const auto& bpm) {
        return bpm.beats_per_minute == wormedit::signed_rational_t{240, 1};
    }));
    CHECK(std::ranges::any_of(second.song->charts.front().pauses, [](const auto& pause) {
        return pause.duration == wormedit::signed_rational_t{144, 1};
    }));

    auto edited_timing = *imported.song;
    REQUIRE_FALSE(edited_timing.charts.front().bpms.empty());
    edited_timing.charts.front().bpms.front().beats_per_minute = {181, 1};
    const auto rejected_bpm = wormedit::write_bms(edited_timing, options);
    CHECK_FALSE(rejected_bpm.bytes.has_value());
    CHECK(has_error(rejected_bpm, wormedit::bms_write_diagnostic_code_t::branch_provenance));

    auto edited_pause = *imported.song;
    REQUIRE_FALSE(edited_pause.charts.front().pauses.empty());
    edited_pause.charts.front().pauses.front().duration = {49, 1};
    const auto rejected_pause = wormedit::write_bms(edited_pause, options);
    CHECK_FALSE(rejected_pause.bytes.has_value());
    CHECK(has_error(rejected_pause, wormedit::bms_write_diagnostic_code_t::branch_provenance));
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
