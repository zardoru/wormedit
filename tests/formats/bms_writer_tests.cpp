#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>

#include <wormedit/core/timing.h>
#include <wormedit/core/interaction.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <limits>
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

auto control_statement_at(const wormedit::bms_control_node_t& root, std::size_t source_order)
    -> const wormedit::bms_control_node_t* {
    const wormedit::bms_control_node_t* match{};
    const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& node) -> void {
        if (node.kind == wormedit::bms_control_kind_t::raw_statement &&
            node.source_order == source_order) {
            match = &node;
        }
        for (const auto& child : node.children) {
            self(self, child);
        }
    };
    visit(visit, root);
    return match;
}

auto control_node_at(const wormedit::bms_control_node_t& root, std::size_t source_order)
    -> const wormedit::bms_control_node_t* {
    const wormedit::bms_control_node_t* match{};
    const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& node) -> void {
        if (node.kind != wormedit::bms_control_kind_t::root &&
            node.kind != wormedit::bms_control_kind_t::if_group &&
            node.source_order == source_order) {
            match = &node;
        }
        for (const auto& child : node.children) {
            self(self, child);
        }
    };
    visit(visit, root);
    return match;
}

auto branch_edit_has_error(const wormedit::bms_branch_statement_edit_result_t& result,
                           wormedit::bms_branch_statement_edit_diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::error &&
               diagnostic.code == code;
    });
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

TEST_CASE("virtual BMS #999 remains sparse until an edit and round-trips its measure number",
          "[unit][format][bms][writer][virtual-canvas]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Virtual extent\n#WAV01 click.wav\n#00002:1/2\n#00011:01\n";
    auto imported = parse(source, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(imported.song);
    REQUIRE(imported.song->charts.size() == 1);
    REQUIRE(imported.song->charts.front().measures.size() == 1);
    const auto lane = imported.song->charts.front().measures.front().notes.front().lane;
    const auto sound = imported.song->charts.front().measures.front().notes.front().sound;
    REQUIRE(sound);
    const auto untouched = wormedit::write_bms(*imported.song);
    REQUIRE(untouched.bytes);
    CHECK(*untouched.bytes == source);

    auto history = wormedit::make_document_history(std::move(*imported.song));
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = wormedit::make_editor_interaction(
        history, history.document.charts.front().id, lane,
        {history.document.charts.front().measures.front().id, 0}, snaps, 4);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    const auto placed = wormedit::dispatch_editor_action(
        history, state, wormedit::place_virtual_bms_tap_action_t{958'560, lane, *sound});
    REQUIRE(placed);
    REQUIRE(history.document.charts.front().measures.size() == 1'000);
    REQUIRE(history.document.charts.front().measures.back().notes.front().sound == sound);

    const auto written = wormedit::write_bms(history.document);
    REQUIRE(written.bytes);
    CHECK_FALSE(written.has_errors());
    CHECK(written.bytes->find("#99911:01") != std::string::npos);
    CHECK(written.bytes->find("#998") == std::string::npos);
    CHECK(written.bytes->find("#500") == std::string::npos);
    const auto reloaded = parse(*written.bytes, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(reloaded.song);
    REQUIRE(reloaded.song->charts.front().measures.size() == 1'000);
    REQUIRE(reloaded.song->charts.front().measures.back().notes.size() == 1);
    CHECK(reloaded.song->charts.front().measures.back().notes.front().position.row == 0);
    CHECK((reloaded.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{1, 2}));
    REQUIRE(wormedit::undo_document_command(history));
    REQUIRE(history.document.charts.front().measures.size() == 1);
    const auto undone = wormedit::write_bms(history.document);
    REQUIRE(undone.bytes);
    CHECK(*undone.bytes == source);
}

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

TEST_CASE("BMS inactive branch statements are edited directly without flattening the tree",
          "[unit][format][bms][writer][branch][direct-edit]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Direct branch edit\n#BPM 120\n"
        "#WAV01 active.wav\n#WAV02 inactive.wav\n#WAV03 replacement.wav\n"
        "#RANDOM 2\n#IF 1\n#00011:0100\n#ELSE\n#00011:0200\n"
        "#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE_FALSE(imported.has_errors());
    const auto& chart = imported.song->charts.front();
    REQUIRE(chart.bms_control_tree.has_value());
    const auto inactive_order = line_order(source, "#00011:0200");
    const auto* inactive = control_statement_at(*chart.bms_control_tree, inactive_order);
    REQUIRE(inactive != nullptr);

    wormedit::bms_branch_statement_edit_t edit{
        .statement = inactive->id,
        .expected_source_order = inactive->source_order,
        .expected_source_span = inactive->source_span,
        .expected_original_text = inactive->original_text,
        .replacement_text = "#00011:0300",
    };
    const auto edited = wormedit::edit_bms_inactive_branch_statement(*imported.song, edit);
    REQUIRE(edited.song.has_value());
    CHECK_FALSE(edited.has_errors());
    CHECK(control_statement_at(*imported.song->charts.front().bms_control_tree, inactive_order)
              ->original_text == "#00011:0200");
    CHECK(control_statement_at(*edited.song->charts.front().bms_control_tree, inactive_order)
              ->original_text == "#00011:0300");

    const auto written =
        wormedit::write_bms(*edited.song, {.lane_profile_id = parse_options.lane_profile_id});
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(occurrences(*written.bytes, "#RANDOM 2") == 1);
    CHECK(occurrences(*written.bytes, "#IF 1") == 1);
    CHECK(occurrences(*written.bytes, "#ELSE") == 1);
    CHECK(occurrences(*written.bytes, "#ENDIF") == 1);
    CHECK(occurrences(*written.bytes, "#ENDRANDOM") == 1);
    CHECK(occurrences(*written.bytes, "#00011:0100") == 1);
    CHECK(occurrences(*written.bytes, "#00011:0300") == 1);
    CHECK(written.bytes->find("#00011:0200") == std::string::npos);

    parse_options.branch_choices = {
        {.control_source_order = line_order(*written.bytes, "#RANDOM 2"), .selected_value = 1}};
    const auto active_reimport = parse(*written.bytes, parse_options);
    REQUIRE(active_reimport.song.has_value());
    REQUIRE(active_reimport.song->charts.front().measures.front().notes.size() == 1);
    const auto& active_note = active_reimport.song->charts.front().measures.front().notes.front();
    REQUIRE(active_note.sound.has_value());
    CHECK(std::ranges::find(active_reimport.song->resources, *active_note.sound,
                            &wormedit::resource_entry_t::id)
              ->name == "active.wav");

    parse_options.branch_choices.front().selected_value = 2;
    const auto edited_reimport = parse(*written.bytes, parse_options);
    REQUIRE(edited_reimport.song.has_value());
    REQUIRE(edited_reimport.song->charts.front().measures.front().notes.size() == 1);
    const auto& edited_note = edited_reimport.song->charts.front().measures.front().notes.front();
    REQUIRE(edited_note.sound.has_value());
    CHECK(std::ranges::find(edited_reimport.song->resources, *edited_note.sound,
                            &wormedit::resource_entry_t::id)
              ->name == "replacement.wav");
}

TEST_CASE("BMS statements can be inserted into a provenance-checked inactive leaf",
          "[unit][format][bms][writer][branch][direct-insert]") {
    constexpr std::string_view source = "#PLAYER 1\n#TITLE Leaf insertion\n#BPM 120\n"
                                        "#WAV01 active.wav\n#WAV02 inactive.wav\n"
                                        "#RANDOM 2\n#IF 1\n#00011:0100\n#ELSE\n#00011:0200\n"
                                        "#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 2}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE_FALSE(imported.has_errors());
    const auto& chart = imported.song->charts.front();
    REQUIRE(chart.bms_control_tree.has_value());
    const auto* leaf = control_node_at(*chart.bms_control_tree, line_order(source, "#IF 1"));
    const auto* anchor =
        control_statement_at(*chart.bms_control_tree, line_order(source, "#00011:0100"));
    REQUIRE(leaf != nullptr);
    REQUIRE(anchor != nullptr);

    const wormedit::bms_branch_statement_insert_t insertion{
        .leaf_branch = leaf->id,
        .expected_leaf_source_order = leaf->source_order,
        .expected_leaf_source_span = leaf->source_span,
        .expected_leaf_original_text = leaf->original_text,
        .after_statement = anchor->id,
        .expected_statement_source_order = anchor->source_order,
        .expected_statement_source_span = anchor->source_span,
        .expected_statement_original_text = anchor->original_text,
        .statement_text = "#00012:01",
    };
    auto inserted = wormedit::insert_bms_inactive_branch_statement(*imported.song, insertion);
    REQUIRE(inserted.song.has_value());
    CHECK_FALSE(inserted.has_errors());
    CHECK(inserted.song->charts.front().measures.front().notes.size() == 1);
    const auto active_resource = std::ranges::find(
        inserted.song->resources, std::string("active.wav"), &wormedit::resource_entry_t::name);
    REQUIRE(active_resource != inserted.song->resources.end());
    inserted.song->charts.front().measures.front().notes.front().sound = active_resource->id;

    const auto written =
        wormedit::write_bms(*inserted.song, {.lane_profile_id = parse_options.lane_profile_id});
    INFO((written.diagnostics.empty() ? std::string{} : written.diagnostics.back().message));
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(occurrences(*written.bytes, "#RANDOM 2") == 1);
    CHECK(occurrences(*written.bytes, "#IF 1") == 1);
    CHECK(occurrences(*written.bytes, "#ELSE") == 1);
    CHECK(occurrences(*written.bytes, "#ENDIF") == 1);
    CHECK(occurrences(*written.bytes, "#ENDRANDOM") == 1);
    CHECK(written.bytes->find("#00011:0100\n#00012:01\n#ELSE") != std::string::npos);
    CHECK(occurrences(*written.bytes, "#00011:0100") == 2);

    parse_options.branch_choices = {
        {.control_source_order = line_order(*written.bytes, "#RANDOM 2"), .selected_value = 1}};
    const auto original_branch = parse(*written.bytes, parse_options);
    REQUIRE(original_branch.song.has_value());
    CHECK(original_branch.song->charts.front().measures.front().notes.size() == 2);

    parse_options.branch_choices.front().selected_value = 2;
    const auto inserted_branch = parse(*written.bytes, parse_options);
    REQUIRE(inserted_branch.song.has_value());
    const auto& notes = inserted_branch.song->charts.front().measures.front().notes;
    REQUIRE(notes.size() == 1);
    CHECK(std::ranges::any_of(notes, [&](const auto& note) {
        if (!note.sound.has_value()) {
            return false;
        }
        const auto resource = std::ranges::find(inserted_branch.song->resources, *note.sound,
                                                &wormedit::resource_entry_t::id);
        return resource != inserted_branch.song->resources.end() && resource->name == "active.wav";
    }));

    auto ambiguous = *imported.song;
    wormedit::bms_control_node_t* mutable_leaf{};
    const auto find_mutable_leaf = [&](const auto& self,
                                       wormedit::bms_control_node_t& node) -> void {
        if (node.id == insertion.leaf_branch) {
            mutable_leaf = &node;
            return;
        }
        for (auto& child : node.children) {
            self(self, child);
            if (mutable_leaf != nullptr) {
                return;
            }
        }
    };
    find_mutable_leaf(find_mutable_leaf, *ambiguous.charts.front().bms_control_tree);
    REQUIRE(mutable_leaf != nullptr);
    const auto mutable_anchor =
        std::ranges::find(mutable_leaf->children, anchor->id, &wormedit::bms_control_node_t::id);
    REQUIRE(mutable_anchor != mutable_leaf->children.end());
    const auto duplicate_anchor = *mutable_anchor;
    mutable_leaf->children.push_back(duplicate_anchor);
    const auto ambiguous_result =
        wormedit::insert_bms_inactive_branch_statement(ambiguous, insertion);
    CHECK_FALSE(ambiguous_result.song.has_value());
    CHECK(branch_edit_has_error(
        ambiguous_result, wormedit::bms_branch_statement_edit_diagnostic_code_t::ambiguous_target));

    auto stale = insertion;
    ++stale.expected_statement_source_span.byte_begin;
    const auto stale_result = wormedit::insert_bms_inactive_branch_statement(*imported.song, stale);
    CHECK_FALSE(stale_result.song.has_value());
    CHECK(branch_edit_has_error(
        stale_result, wormedit::bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch));

    const auto* active_leaf = control_node_at(*chart.bms_control_tree, line_order(source, "#ELSE"));
    const auto* active_anchor =
        control_statement_at(*chart.bms_control_tree, line_order(source, "#00011:0200"));
    REQUIRE(active_leaf != nullptr);
    REQUIRE(active_anchor != nullptr);
    const auto active_insertion = wormedit::bms_branch_statement_insert_t{
        .leaf_branch = active_leaf->id,
        .expected_leaf_source_order = active_leaf->source_order,
        .expected_leaf_source_span = active_leaf->source_span,
        .expected_leaf_original_text = active_leaf->original_text,
        .after_statement = active_anchor->id,
        .expected_statement_source_order = active_anchor->source_order,
        .expected_statement_source_span = active_anchor->source_span,
        .expected_statement_original_text = active_anchor->original_text,
        .statement_text = "#00012:01",
    };
    const auto active_result =
        wormedit::insert_bms_inactive_branch_statement(*imported.song, active_insertion);
    CHECK_FALSE(active_result.song.has_value());
    CHECK(branch_edit_has_error(
        active_result, wormedit::bms_branch_statement_edit_diagnostic_code_t::active_statement));

    auto multiple_charts = *imported.song;
    auto second_chart = multiple_charts.charts.front();
    second_chart.id = wormedit::chart_id_t{9'999};
    multiple_charts.charts.push_back(std::move(second_chart));
    const auto multiple_result =
        wormedit::insert_bms_inactive_branch_statement(multiple_charts, insertion);
    CHECK_FALSE(multiple_result.song.has_value());
    CHECK(branch_edit_has_error(
        multiple_result,
        wormedit::bms_branch_statement_edit_diagnostic_code_t::unsupported_song_shape));

    auto exhausted = *imported.song;
    auto* exhausted_else = const_cast<wormedit::bms_control_node_t*>(
        control_node_at(*exhausted.charts.front().bms_control_tree, line_order(source, "#ELSE")));
    REQUIRE(exhausted_else != nullptr);
    exhausted_else->source_order = std::numeric_limits<std::size_t>::max();
    const auto exhausted_result =
        wormedit::insert_bms_inactive_branch_statement(exhausted, insertion);
    CHECK_FALSE(exhausted_result.song.has_value());
    CHECK(branch_edit_has_error(
        exhausted_result, wormedit::bms_branch_statement_edit_diagnostic_code_t::resource_limit));
}

TEST_CASE("BMS raw statements can be deleted from inactive leaves with full-tree preservation",
          "[unit][format][bms][writer][branch][direct-delete]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Leaf deletion\n#BPM 120\n#WAV01 click.wav\n"
        "#RANDOM 3\n#IF 1\n#00011:0100\n#ELSEIF 2\n#00011:0200\n"
        "#ELSE\n#00011:0300\n#ENDIF\n#ENDRANDOM\n"
        "#SWITCH 2\n#CASE 1\n#00011:0400\n#SKIP\n#DEF\n#00011:0500\n#ENDSW\n";
    struct deletion_case_t {
        std::string_view leaf_line;
        std::string_view statement_line;
        std::int64_t random_value;
        std::int64_t switch_value;
    };
    constexpr std::array deletion_cases{
        deletion_case_t{"#IF 1", "#00011:0100", 2, 1},
        deletion_case_t{"#ELSEIF 2", "#00011:0200", 1, 1},
        deletion_case_t{"#ELSE", "#00011:0300", 1, 1},
        deletion_case_t{"#CASE 1", "#00011:0400", 1, 2},
        deletion_case_t{"#DEF", "#00011:0500", 1, 1},
    };
    const auto tree_signature = [](const wormedit::bms_control_node_t& root) {
        std::string signature;
        const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& node) -> void {
            signature += std::to_string(static_cast<int>(node.kind));
            signature += ':';
            signature += node.original_text;
            signature += ':';
            if (node.close_text.has_value()) {
                signature += *node.close_text;
            }
            signature += '[';
            for (const auto& child : node.children) {
                self(self, child);
            }
            signature += ']';
        };
        visit(visit, root);
        return signature;
    };
    const auto tree_has_id = [](const wormedit::bms_control_node_t& root,
                                wormedit::branch_id_t id) {
        bool found{};
        const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& node) -> void {
            found = found || node.id == id;
            for (const auto& child : node.children) {
                self(self, child);
            }
        };
        visit(visit, root);
        return found;
    };

    for (const auto& deletion_case : deletion_cases) {
        INFO(deletion_case.leaf_line);
        wormedit::bms_parse_options_t parse_options;
        parse_options.lane_profile_id = "bme-7k-outside-scratch";
        parse_options.branch_choices = {
            {.control_source_order = line_order(source, "#RANDOM 3"),
             .selected_value = deletion_case.random_value},
            {.control_source_order = line_order(source, "#SWITCH 2"),
             .selected_value = deletion_case.switch_value},
        };
        const auto imported = parse(source, parse_options);
        REQUIRE(imported.song.has_value());
        REQUIRE_FALSE(imported.has_errors());
        const auto& chart = imported.song->charts.front();
        REQUIRE(chart.bms_control_tree.has_value());
        const auto leaf_lookup =
            deletion_case.leaf_line == "#ELSE" ? "#ELSE\n" : deletion_case.leaf_line;
        const auto* leaf =
            control_node_at(*chart.bms_control_tree, line_order(source, leaf_lookup));
        const auto* statement = control_statement_at(
            *chart.bms_control_tree, line_order(source, deletion_case.statement_line));
        REQUIRE(leaf != nullptr);
        REQUIRE(statement != nullptr);

        const wormedit::bms_branch_statement_delete_t deletion{
            .leaf_branch = leaf->id,
            .expected_leaf_source_order = leaf->source_order,
            .expected_leaf_source_span = leaf->source_span,
            .expected_leaf_original_text = leaf->original_text,
            .statement = statement->id,
            .expected_source_order = statement->source_order,
            .expected_source_span = statement->source_span,
            .expected_original_text = statement->original_text,
        };
        const auto deleted =
            wormedit::delete_bms_inactive_branch_statement(*imported.song, deletion);
        INFO((deleted.diagnostics.empty() ? std::string{} : deleted.diagnostics.back().message));
        REQUIRE(deleted.song.has_value());
        CHECK_FALSE(deleted.has_errors());
        REQUIRE(deleted.song->charts.front().bms_control_tree.has_value());
        CHECK_FALSE(tree_has_id(*deleted.song->charts.front().bms_control_tree, statement->id));

        const auto target_size =
            statement->source_span.byte_end - statement->source_span.byte_begin;
        const auto* original_switch =
            control_node_at(*chart.bms_control_tree, line_order(source, "#SWITCH 2"));
        REQUIRE(original_switch != nullptr);
        REQUIRE(original_switch->close_source_span.has_value());
        REQUIRE(original_switch->close_source_order.has_value());
        const auto find_by_id =
            [&](const auto& self,
                const wormedit::bms_control_node_t& node) -> const wormedit::bms_control_node_t* {
            if (node.id == original_switch->id) {
                return &node;
            }
            for (const auto& child : node.children) {
                if (const auto* found = self(self, child); found != nullptr) {
                    return found;
                }
            }
            return nullptr;
        };
        const auto* shifted_switch =
            find_by_id(find_by_id, *deleted.song->charts.front().bms_control_tree);
        REQUIRE(shifted_switch != nullptr);
        REQUIRE(shifted_switch->close_source_span.has_value());
        REQUIRE(shifted_switch->close_source_order.has_value());
        CHECK(shifted_switch->close_source_span->byte_begin + target_size ==
              original_switch->close_source_span->byte_begin);
        CHECK(shifted_switch->close_source_span->byte_end + target_size ==
              original_switch->close_source_span->byte_end);
        CHECK(*shifted_switch->close_source_order == *original_switch->close_source_order - 1);

        for (const auto& original_note : chart.measures.front().notes) {
            const auto& edited_notes = deleted.song->charts.front().measures.front().notes;
            const auto edited_note =
                std::ranges::find(edited_notes, original_note.id, &wormedit::note_t::id);
            REQUIRE(edited_note != edited_notes.end());
            CHECK(edited_note->provenance.source_order ==
                  (original_note.provenance.source_order > statement->source_order
                       ? original_note.provenance.source_order - 1
                       : original_note.provenance.source_order));
        }

        const auto bms_group = std::ranges::find_if(
            deleted.song->charts.front().metadata.format_groups,
            [](const auto& group) { return group.family == wormedit::format_family_t::bms; });
        REQUIRE(bms_group != deleted.song->charts.front().metadata.format_groups.end());
        REQUIRE(bms_group->encoding.original_bytes != nullptr);
        CHECK_FALSE(bms_group->encoding.unchanged_since_import);
        const auto& updated_bytes = *bms_group->encoding.original_bytes;
        const std::string updated_source(reinterpret_cast<const char*>(updated_bytes.data()),
                                         updated_bytes.size());
        auto expected_source = std::string(source);
        expected_source.erase(statement->source_span.byte_begin, target_size);
        CHECK(updated_source == expected_source);

        const auto written =
            wormedit::write_bms(*deleted.song, {.lane_profile_id = parse_options.lane_profile_id});
        INFO((written.diagnostics.empty() ? std::string{} : written.diagnostics.back().message));
        REQUIRE(written.bytes.has_value());
        CHECK_FALSE(written.has_errors());
        CHECK(written.bytes->find(deletion_case.statement_line) == std::string::npos);
        CHECK(occurrences(*written.bytes, "#RANDOM 3") == 1);
        CHECK(occurrences(*written.bytes, "#IF 1") == 1);
        CHECK(occurrences(*written.bytes, "#ELSEIF 2") == 1);
        CHECK(occurrences(*written.bytes, "#ELSE\n") == 1);
        CHECK(occurrences(*written.bytes, "#ENDIF") == 1);
        CHECK(occurrences(*written.bytes, "#ENDRANDOM") == 1);
        CHECK(occurrences(*written.bytes, "#SWITCH 2") == 1);
        CHECK(occurrences(*written.bytes, "#CASE 1") == 1);
        CHECK(occurrences(*written.bytes, "#SKIP") == 1);
        CHECK(occurrences(*written.bytes, "#DEF") == 1);
        CHECK(occurrences(*written.bytes, "#ENDSW") == 1);
        const auto tree_reimport = parse(*written.bytes);
        REQUIRE(tree_reimport.song.has_value());
        REQUIRE(tree_reimport.song->charts.front().bms_control_tree.has_value());
        CHECK(tree_signature(*deleted.song->charts.front().bms_control_tree) ==
              tree_signature(*tree_reimport.song->charts.front().bms_control_tree));

        for (std::int64_t random_value = 1; random_value <= 3; ++random_value) {
            for (std::int64_t switch_value = 1; switch_value <= 2; ++switch_value) {
                auto reimport_options = parse_options;
                reimport_options.branch_choices = {
                    {.control_source_order = line_order(*written.bytes, "#RANDOM 3"),
                     .selected_value = random_value},
                    {.control_source_order = line_order(*written.bytes, "#SWITCH 2"),
                     .selected_value = switch_value},
                };
                const auto reimported = parse(*written.bytes, reimport_options);
                REQUIRE(reimported.song.has_value());
                REQUIRE_FALSE(reimported.has_errors());
                const auto conditional_deleted = deletion_case.leaf_line == "#IF 1" ||
                                                 deletion_case.leaf_line == "#ELSEIF 2" ||
                                                 deletion_case.leaf_line == "#ELSE";
                const auto selected_deleted =
                    conditional_deleted
                        ? ((deletion_case.leaf_line == "#IF 1" && random_value == 1) ||
                           (deletion_case.leaf_line == "#ELSEIF 2" && random_value == 2) ||
                           (deletion_case.leaf_line == "#ELSE" && random_value == 3))
                        : ((deletion_case.leaf_line == "#CASE 1" && switch_value == 1) ||
                           (deletion_case.leaf_line == "#DEF" && switch_value == 2));
                CHECK(reimported.song->charts.front().measures.front().notes.size() ==
                      (selected_deleted ? 1U : 2U));
            }
        }
    }
}

TEST_CASE("BMS inactive branch deletion rejects stale active ambiguous and invalid targets",
          "[unit][format][bms][writer][branch][direct-delete]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Delete rejection\n#BPM 120\n#WAV01 one.wav\n"
        "#RANDOM 2\n#IF 1\n#00011:0100\n#ELSE\n#00011:0200\n"
        "#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE(imported.song->charts.front().bms_control_tree.has_value());
    const auto& tree = *imported.song->charts.front().bms_control_tree;
    const auto* active_leaf = control_node_at(tree, line_order(source, "#IF 1"));
    const auto* inactive_leaf = control_node_at(tree, line_order(source, "#ELSE"));
    const auto* active = control_statement_at(tree, line_order(source, "#00011:0100"));
    const auto* inactive = control_statement_at(tree, line_order(source, "#00011:0200"));
    const auto* structural = control_node_at(tree, line_order(source, "#ELSE"));
    const auto* random = control_node_at(tree, line_order(source, "#RANDOM 2"));
    REQUIRE(active_leaf != nullptr);
    REQUIRE(inactive_leaf != nullptr);
    REQUIRE(active != nullptr);
    REQUIRE(inactive != nullptr);
    REQUIRE(structural != nullptr);
    REQUIRE(random != nullptr);

    const auto request_for = [](const wormedit::bms_control_node_t& leaf,
                                const wormedit::bms_control_node_t& statement) {
        return wormedit::bms_branch_statement_delete_t{
            .leaf_branch = leaf.id,
            .expected_leaf_source_order = leaf.source_order,
            .expected_leaf_source_span = leaf.source_span,
            .expected_leaf_original_text = leaf.original_text,
            .statement = statement.id,
            .expected_source_order = statement.source_order,
            .expected_source_span = statement.source_span,
            .expected_original_text = statement.original_text,
        };
    };
    const auto expect_error = [](const wormedit::bms_branch_statement_edit_result_t& result,
                                 wormedit::bms_branch_statement_edit_diagnostic_code_t code) {
        CHECK_FALSE(result.song.has_value());
        CHECK(branch_edit_has_error(result, code));
    };

    const auto active_result = wormedit::delete_bms_inactive_branch_statement(
        *imported.song, request_for(*active_leaf, *active));
    expect_error(active_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::active_statement);

    auto stale = request_for(*inactive_leaf, *inactive);
    ++stale.expected_source_span.byte_begin;
    const auto stale_result = wormedit::delete_bms_inactive_branch_statement(*imported.song, stale);
    expect_error(stale_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch);

    const auto structural_result = wormedit::delete_bms_inactive_branch_statement(
        *imported.song, request_for(*inactive_leaf, *structural));
    expect_error(
        structural_result,
        wormedit::bms_branch_statement_edit_diagnostic_code_t::target_not_branch_statement);

    auto capped = request_for(*inactive_leaf, *inactive);
    capped.max_statement_bytes = 1;
    const auto capped_result =
        wormedit::delete_bms_inactive_branch_statement(*imported.song, capped);
    expect_error(capped_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::resource_limit);

    auto invalid_selection = *imported.song;
    invalid_selection.charts.front().active_branch_selection.push_back(
        invalid_selection.charts.front().active_branch_selection.front());
    const auto invalid_selection_result = wormedit::delete_bms_inactive_branch_statement(
        invalid_selection, request_for(*inactive_leaf, *inactive));
    expect_error(invalid_selection_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::invalid_branch_selection);

    auto ambiguous = *imported.song;
    wormedit::bms_control_node_t* mutable_leaf{};
    const auto find_mutable_leaf = [&](const auto& self,
                                       wormedit::bms_control_node_t& node) -> void {
        if (node.id == inactive_leaf->id) {
            mutable_leaf = &node;
            return;
        }
        for (auto& child : node.children) {
            self(self, child);
            if (mutable_leaf != nullptr) {
                return;
            }
        }
    };
    find_mutable_leaf(find_mutable_leaf, *ambiguous.charts.front().bms_control_tree);
    REQUIRE(mutable_leaf != nullptr);
    mutable_leaf->children.push_back(mutable_leaf->children.front());
    const auto ambiguous_result = wormedit::delete_bms_inactive_branch_statement(
        ambiguous, request_for(*inactive_leaf, *inactive));
    expect_error(ambiguous_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::ambiguous_target);

    auto invalid_span = *imported.song;
    wormedit::bms_control_node_t* mutable_target{};
    const auto find_mutable_target = [&](const auto& self,
                                         wormedit::bms_control_node_t& node) -> void {
        if (node.id == inactive->id) {
            mutable_target = &node;
            return;
        }
        for (auto& child : node.children) {
            self(self, child);
            if (mutable_target != nullptr) {
                return;
            }
        }
    };
    find_mutable_target(find_mutable_target, *invalid_span.charts.front().bms_control_tree);
    REQUIRE(mutable_target != nullptr);
    mutable_target->source_span = {std::numeric_limits<std::size_t>::max() - 2,
                                   std::numeric_limits<std::size_t>::max() - 1};
    auto invalid_request = request_for(*inactive_leaf, *inactive);
    invalid_request.expected_source_span = mutable_target->source_span;
    const auto invalid_span_result =
        wormedit::delete_bms_inactive_branch_statement(invalid_span, invalid_request);
    expect_error(invalid_span_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch);

    auto overlapping_span = *imported.song;
    auto* mutable_random = const_cast<wormedit::bms_control_node_t*>(
        control_node_at(*overlapping_span.charts.front().bms_control_tree, random->source_order));
    REQUIRE(mutable_random != nullptr);
    mutable_random->close_source_span = inactive->source_span;
    const auto overlapping_result = wormedit::delete_bms_inactive_branch_statement(
        overlapping_span, request_for(*inactive_leaf, *inactive));
    expect_error(overlapping_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch);

    auto conflicting_projection = *imported.song;
    REQUIRE_FALSE(conflicting_projection.charts.front().measures.front().notes.empty());
    conflicting_projection.charts.front().measures.front().notes.front().provenance.source_order =
        inactive->source_order;
    const auto conflicting_result = wormedit::delete_bms_inactive_branch_statement(
        conflicting_projection, request_for(*inactive_leaf, *inactive));
    expect_error(conflicting_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch);

    auto multiple_charts = *imported.song;
    auto second_chart = multiple_charts.charts.front();
    second_chart.id = wormedit::chart_id_t{9'999};
    multiple_charts.charts.push_back(std::move(second_chart));
    const auto multiple_result = wormedit::delete_bms_inactive_branch_statement(
        multiple_charts, request_for(*inactive_leaf, *inactive));
    expect_error(multiple_result,
                 wormedit::bms_branch_statement_edit_diagnostic_code_t::unsupported_song_shape);
}

TEST_CASE("BMS direct branch editing rejects stale active and structural targets",
          "[unit][format][bms][writer][branch][direct-edit]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Direct branch rejection\n#BPM 120\n#WAV01 one.wav\n"
        "#RANDOM 2\n#IF 1\n#00011:0100\n#ELSE\n#00011:0001\n"
        "#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE(imported.song->charts.front().bms_control_tree.has_value());
    const auto& tree = *imported.song->charts.front().bms_control_tree;
    const auto* active = control_statement_at(tree, line_order(source, "#00011:0100"));
    const auto* inactive = control_statement_at(tree, line_order(source, "#00011:0001"));
    REQUIRE(active != nullptr);
    REQUIRE(inactive != nullptr);

    const auto request_for = [](const wormedit::bms_control_node_t& node) {
        return wormedit::bms_branch_statement_edit_t{
            .statement = node.id,
            .expected_source_order = node.source_order,
            .expected_source_span = node.source_span,
            .expected_original_text = node.original_text,
            .replacement_text = "#00011:0101",
        };
    };
    const auto active_result =
        wormedit::edit_bms_inactive_branch_statement(*imported.song, request_for(*active));
    CHECK_FALSE(active_result.song.has_value());
    CHECK(branch_edit_has_error(
        active_result, wormedit::bms_branch_statement_edit_diagnostic_code_t::active_statement));

    auto stale = request_for(*inactive);
    ++stale.expected_source_span.byte_begin;
    const auto stale_result =
        wormedit::edit_bms_inactive_branch_statement(*imported.song, std::move(stale));
    CHECK_FALSE(stale_result.song.has_value());
    CHECK(branch_edit_has_error(
        stale_result, wormedit::bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch));

    auto changed_token = request_for(*inactive);
    changed_token.replacement_text = "#00012:0100";
    const auto changed_token_result =
        wormedit::edit_bms_inactive_branch_statement(*imported.song, std::move(changed_token));
    CHECK_FALSE(changed_token_result.song.has_value());
    CHECK(branch_edit_has_error(
        changed_token_result,
        wormedit::bms_branch_statement_edit_diagnostic_code_t::invalid_replacement));

    const auto* else_control = control_node_at(tree, line_order(source, "#ELSE"));
    REQUIRE(else_control != nullptr);
    const auto structural_result = wormedit::edit_bms_inactive_branch_statement(
        *imported.song, {.statement = else_control->id,
                         .expected_source_order = else_control->source_order,
                         .expected_source_span = else_control->source_span,
                         .expected_original_text = else_control->original_text,
                         .replacement_text = "#ELSE"});
    CHECK_FALSE(structural_result.song.has_value());
    CHECK(branch_edit_has_error(
        structural_result,
        wormedit::bms_branch_statement_edit_diagnostic_code_t::target_not_branch_statement));

    auto multiline = request_for(*inactive);
    multiline.replacement_text = "#00011:0100\n#ENDIF";
    const auto multiline_result =
        wormedit::edit_bms_inactive_branch_statement(*imported.song, std::move(multiline));
    CHECK_FALSE(multiline_result.song.has_value());
    CHECK(branch_edit_has_error(
        multiline_result,
        wormedit::bms_branch_statement_edit_diagnostic_code_t::invalid_replacement));

    auto capped = request_for(*inactive);
    capped.max_replacement_bytes = 4;
    const auto capped_result =
        wormedit::edit_bms_inactive_branch_statement(*imported.song, std::move(capped));
    CHECK_FALSE(capped_result.song.has_value());
    CHECK(branch_edit_has_error(
        capped_result, wormedit::bms_branch_statement_edit_diagnostic_code_t::resource_limit));

    auto invalid_selection = *imported.song;
    invalid_selection.charts.front().active_branch_selection.push_back(
        invalid_selection.charts.front().active_branch_selection.front());
    const auto invalid_selection_result =
        wormedit::edit_bms_inactive_branch_statement(invalid_selection, request_for(*inactive));
    CHECK_FALSE(invalid_selection_result.song.has_value());
    CHECK(branch_edit_has_error(
        invalid_selection_result,
        wormedit::bms_branch_statement_edit_diagnostic_code_t::invalid_branch_selection));

    auto ambiguous = *imported.song;
    wormedit::bms_control_node_t* duplicate_parent{};
    const auto find_parent = [&](const auto& self, wormedit::bms_control_node_t& node) -> void {
        if (std::ranges::any_of(node.children,
                                [&](const auto& child) { return child.id == inactive->id; })) {
            duplicate_parent = &node;
        }
        for (auto& child : node.children) {
            self(self, child);
        }
    };
    find_parent(find_parent, *ambiguous.charts.front().bms_control_tree);
    REQUIRE(duplicate_parent != nullptr);
    const auto duplicate = std::ranges::find(duplicate_parent->children, inactive->id,
                                             &wormedit::bms_control_node_t::id);
    REQUIRE(duplicate != duplicate_parent->children.end());
    const auto duplicate_node = *duplicate;
    duplicate_parent->children.push_back(duplicate_node);
    const auto ambiguous_result =
        wormedit::edit_bms_inactive_branch_statement(ambiguous, request_for(*inactive));
    CHECK_FALSE(ambiguous_result.song.has_value());
    CHECK(branch_edit_has_error(
        ambiguous_result, wormedit::bms_branch_statement_edit_diagnostic_code_t::ambiguous_target));
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
    REQUIRE(edited_branch_ratio.bytes.has_value());
    CHECK_FALSE(edited_branch_ratio.has_errors());
    CHECK(occurrences(*edited_branch_ratio.bytes, "#00002:1.75") == 1);
    CHECK(occurrences(*edited_branch_ratio.bytes, "#00002:1/2") == 1);
    CHECK(edited_branch_ratio.bytes->find("#00002:3/4") == std::string::npos);
    const auto ratio_random_order = line_order(*edited_branch_ratio.bytes, "#RANDOM 2");
    parse_options.branch_choices = {
        {.control_source_order = ratio_random_order, .selected_value = 1}};
    const auto edited_first_ratio = parse(*edited_branch_ratio.bytes, parse_options);
    REQUIRE(edited_first_ratio.song.has_value());
    CHECK((edited_first_ratio.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{7, 4}));
    parse_options.branch_choices.front().selected_value = 2;
    const auto untouched_second_ratio = parse(*edited_branch_ratio.bytes, parse_options);
    REQUIRE(untouched_second_ratio.song.has_value());
    CHECK((untouched_second_ratio.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{1, 2}));
    parse_options.branch_choices.clear();
    const auto untouched_root_ratio = parse(*edited_branch_ratio.bytes, parse_options);
    REQUIRE(untouched_root_ratio.song.has_value());
    CHECK((untouched_root_ratio.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{5, 4}));

    auto ambiguous_ratio_source = *metadata_import.song;
    ambiguous_ratio_source.charts.front().measures.front().definition.length = {7, 4};
    const auto sibling_ratio_order = line_order(metadata_source, "#00002:1/2");
    ambiguous_ratio_source.charts.front().bms_measure_length_projections.front().source_order =
        sibling_ratio_order;
    const auto rejected_ambiguous_ratio = wormedit::write_bms(ambiguous_ratio_source, options);
    CHECK_FALSE(rejected_ambiguous_ratio.bytes.has_value());
    CHECK(has_error(rejected_ambiguous_ratio,
                    wormedit::bms_write_diagnostic_code_t::branch_provenance));

    auto changed_retained_ratio = *metadata_import.song;
    changed_retained_ratio.charts.front().measures.front().definition.length = {7, 4};
    const auto active_ratio_node = std::ranges::find_if(
        changed_retained_ratio.charts.front().bms_control_tree->children,
        [](const auto& node) { return node.kind == wormedit::bms_control_kind_t::random; });
    REQUIRE(active_ratio_node !=
            changed_retained_ratio.charts.front().bms_control_tree->children.end());
    auto& active_ratio_children = active_ratio_node->children.front().children.front().children;
    const auto changed_line = std::ranges::find_if(active_ratio_children, [](const auto& node) {
        return node.kind == wormedit::bms_control_kind_t::raw_statement &&
               node.command_token == "00002";
    });
    REQUIRE(changed_line != active_ratio_children.end());
    changed_line->original_text = "#00002:9/4";
    const auto rejected_changed_source = wormedit::write_bms(changed_retained_ratio, options);
    CHECK_FALSE(rejected_changed_source.bytes.has_value());
    CHECK(has_error(rejected_changed_source,
                    wormedit::bms_write_diagnostic_code_t::branch_provenance));

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
    const auto branch_bpm_order = line_order(source, "#BPM 180");
    const auto branch_bpm =
        std::ranges::find_if(edited_timing.charts.front().bpms, [&](const auto& bpm) {
            return bpm.provenance.source_order == branch_bpm_order;
        });
    REQUIRE(branch_bpm != edited_timing.charts.front().bpms.end());
    branch_bpm->beats_per_minute = {181, 1};
    const auto edited_bpm = wormedit::write_bms(edited_timing, options);
    REQUIRE(edited_bpm.bytes.has_value());
    CHECK_FALSE(edited_bpm.has_errors());
    CHECK(occurrences(*edited_bpm.bytes, "#BPM 181") == 1);
    CHECK(occurrences(*edited_bpm.bytes, "#BPM 240") == 1);
    const auto edited_random_order = line_order(*edited_bpm.bytes, "#RANDOM 2");
    parse_options.branch_choices = {
        {.control_source_order = edited_random_order, .selected_value = 1}};
    const auto edited_branch_bpm = parse(*edited_bpm.bytes, parse_options);
    REQUIRE(edited_branch_bpm.song.has_value());
    CHECK(std::ranges::any_of(edited_branch_bpm.song->charts.front().bpms, [](const auto& bpm) {
        return bpm.beats_per_minute == wormedit::signed_rational_t{181, 1};
    }));
    parse_options.branch_choices.front().selected_value = 2;
    const auto untouched_sibling_bpm = parse(*edited_bpm.bytes, parse_options);
    REQUIRE(untouched_sibling_bpm.song.has_value());
    CHECK(std::ranges::any_of(untouched_sibling_bpm.song->charts.front().bpms, [](const auto& bpm) {
        return bpm.beats_per_minute == wormedit::signed_rational_t{240, 1};
    }));

    auto edited_root_channel_bpm = *imported.song;
    REQUIRE_FALSE(edited_root_channel_bpm.charts.front().bpms.empty());
    edited_root_channel_bpm.charts.front().bpms.front().beats_per_minute = {121, 1};
    const auto rejected_root_channel_bpm = wormedit::write_bms(edited_root_channel_bpm, options);
    CHECK_FALSE(rejected_root_channel_bpm.bytes.has_value());
    CHECK(has_error(rejected_root_channel_bpm,
                    wormedit::bms_write_diagnostic_code_t::branch_provenance));

    auto edited_pause = *imported.song;
    REQUIRE_FALSE(edited_pause.charts.front().pauses.empty());
    edited_pause.charts.front().pauses.front().duration = {49, 1};
    const auto rejected_pause = wormedit::write_bms(edited_pause, options);
    CHECK_FALSE(rejected_pause.bytes.has_value());
    CHECK(has_error(rejected_pause, wormedit::bms_write_diagnostic_code_t::branch_provenance));
}

TEST_CASE("BMS active flatten serializes projected BPM, STOP, and fractional measures",
          "[unit][format][bms][writer]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Flatten timing\n#BPM 120\n#BPM01 180\n#BPM02 240\n"
        "#STOP01 48\n#STOP02 96\n#RANDOM 2\n#IF 1\n#00002:0.75\n"
        "#00008:00010002\n#00009:00010002\n#ELSE\n#00002:1.25\n"
        "#00008:0003\n#00009:0002\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE_FALSE(imported.has_errors());
    const auto& projected = imported.song->charts.front();
    REQUIRE(projected.bpms.size() == 3);
    REQUIRE(projected.pauses.size() == 2);
    CHECK((projected.measures.front().definition.length == wormedit::rational_t{3, 4}));

    wormedit::bms_write_options_t preserve_options;
    preserve_options.lane_profile_id = "bme-7k-outside-scratch";
    preserve_options.preserve_unchanged_source = false;
    const auto preserved = wormedit::write_bms(*imported.song, preserve_options);
    REQUIRE(preserved.bytes.has_value());
    CHECK_FALSE(preserved.has_errors());
    CHECK(preserved.bytes->find("#RANDOM 2") != std::string::npos);
    CHECK(preserved.bytes->find("#ELSE") != std::string::npos);
    CHECK(preserved.bytes->find("#ENDRANDOM") != std::string::npos);
    CHECK(preserved.bytes->find("#00002:1.25") != std::string::npos);
    CHECK(preserved.bytes->find("#00008:0003") != std::string::npos);
    CHECK(preserved.bytes->find("#00009:0002") != std::string::npos);

    auto flatten_options = preserve_options;
    flatten_options.branch_policy = wormedit::bms_branch_policy_t::flatten_active;
    const auto flattened = wormedit::write_bms(*imported.song, flatten_options);
    INFO((flattened.diagnostics.empty() ? std::string{} : flattened.diagnostics.back().message));
    REQUIRE(flattened.bytes.has_value());
    CHECK_FALSE(flattened.has_errors());
    CHECK(has_warning(flattened, wormedit::bms_write_diagnostic_code_t::lossy_transform));
    CHECK(flattened.bytes->find("#RANDOM") == std::string::npos);
    CHECK(flattened.bytes->find("#ELSE") == std::string::npos);
    CHECK(flattened.bytes->find("#BPM 120") != std::string::npos);
    CHECK(flattened.bytes->find("#BPM01 180") != std::string::npos);
    CHECK(flattened.bytes->find("#BPM02 240") != std::string::npos);
    CHECK(flattened.bytes->find("#STOP01 48") != std::string::npos);
    CHECK(flattened.bytes->find("#STOP02 96") != std::string::npos);
    CHECK(flattened.bytes->find("#00002:0.75") != std::string::npos);
    CHECK(flattened.bytes->find("#00002:1.25") == std::string::npos);
    const auto flattened_again = wormedit::write_bms(*imported.song, flatten_options);
    REQUIRE(flattened_again.bytes.has_value());
    CHECK(*flattened_again.bytes == *flattened.bytes);

    const auto reparsed = parse(*flattened.bytes, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.has_errors());
    const auto& roundtrip = reparsed.song->charts.front();
    REQUIRE(roundtrip.bpms.size() == projected.bpms.size());
    REQUIRE(roundtrip.pauses.size() == projected.pauses.size());
    CHECK((roundtrip.measures.front().definition.length ==
           projected.measures.front().definition.length));

    const auto source_timing = wormedit::build_timing_index(projected);
    const auto roundtrip_timing = wormedit::build_timing_index(roundtrip);
    REQUIRE(source_timing.index.has_value());
    REQUIRE(roundtrip_timing.index.has_value());
    REQUIRE(source_timing.index->rows.size() == roundtrip_timing.index->rows.size());
    for (std::size_t row = 0; row < source_timing.index->rows.size(); ++row) {
        CHECK(source_timing.index->rows[row].effective_bpm ==
              roundtrip_timing.index->rows[row].effective_bpm);
        CHECK(source_timing.index->rows[row].before_delays_seconds ==
              roundtrip_timing.index->rows[row].before_delays_seconds);
        CHECK(source_timing.index->rows[row].event_seconds ==
              roundtrip_timing.index->rows[row].event_seconds);
        CHECK(source_timing.index->rows[row].after_stops_seconds ==
              roundtrip_timing.index->rows[row].after_stops_seconds);
    }

    auto non_bms_pause = *imported.song;
    non_bms_pause.charts.front().pauses.front().source_unit = wormedit::pause_unit_t::seconds;
    const auto rejected_pause = wormedit::write_bms(non_bms_pause, flatten_options);
    CHECK_FALSE(rejected_pause.bytes.has_value());
    CHECK(has_error(rejected_pause, wormedit::bms_write_diagnostic_code_t::unsupported_feature));

    auto nonterminating = *imported.song;
    nonterminating.charts.front().measures.push_back(
        {.id = wormedit::measure_id_t{9'999}, .definition = {.length = {1, 3}}});
    const auto rejected_ratio = wormedit::write_bms(nonterminating, flatten_options);
    CHECK_FALSE(rejected_ratio.bytes.has_value());
    CHECK(has_error(rejected_ratio, wormedit::bms_write_diagnostic_code_t::unrepresentable_number));
}

TEST_CASE("BMS writer patches active branch channel events and uniquely bound timing values",
          "[unit][format][bms][writer][branch][events]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Branch event edits\n#BPM 120\n"
        "#WAV01 original.wav\n#WAV02 sibling.wav\n#WAV03 edited.wav\n"
        "#BMP01 original.png\n#BMP02 replacement.png\n"
        "#RANDOM 2\n#IF 1\n#BPM01 150\n#STOP01 48\n"
        "#00011:0100\n#00001:0100\n#00004:0100\n#00003:6400\n"
        "#00008:0100\n#00009:0100\n#ELSE\n#BPM01 210\n#STOP01 96\n"
        "#00011:0200\n#00001:0200\n#00004:0200\n#00003:7800\n"
        "#00008:0100\n#00009:0100\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE_FALSE(imported.has_errors());

    auto edited = *imported.song;
    auto& chart = edited.charts.front();
    const auto half_measure = chart.divisions_per_standard_measure / 2;
    const auto replacement_wav = std::ranges::find_if(
        edited.resources, [](const auto& resource) { return resource.name == "edited.wav"; });
    const auto replacement_bmp = std::ranges::find_if(
        edited.resources, [](const auto& resource) { return resource.name == "replacement.png"; });
    REQUIRE(replacement_wav != edited.resources.end());
    REQUIRE(replacement_bmp != edited.resources.end());

    REQUIRE(chart.measures.front().notes.size() == 1);
    auto& note = chart.measures.front().notes.front();
    note.position.row = half_measure;
    note.sound = replacement_wav->id;
    REQUIRE(chart.measures.front().bgm_events.size() == 2);
    auto& bgm = chart.measures.front().bgm_events.front();
    bgm.sound = replacement_wav->id;
    REQUIRE(chart.bga_events.size() == 1);
    chart.bga_events.front().position.row = half_measure;
    chart.bga_events.front().resource = replacement_bmp->id;

    const auto direct_bpm = std::ranges::find_if(chart.bpms, [](const auto& bpm) {
        return bpm.provenance.source_field.starts_with("03 slot ");
    });
    const auto extended_bpm = std::ranges::find_if(chart.bpms, [](const auto& bpm) {
        return bpm.provenance.source_field.starts_with("08 slot ");
    });
    REQUIRE(direct_bpm != chart.bpms.end());
    REQUIRE(extended_bpm != chart.bpms.end());
    direct_bpm->position.row = half_measure;
    direct_bpm->beats_per_minute = {111, 1};
    extended_bpm->position.row = half_measure;
    extended_bpm->beats_per_minute = {155, 1};
    REQUIRE(chart.pauses.size() == 1);
    chart.pauses.front().position.row = half_measure;
    chart.pauses.front().duration = {60, 1};

    const auto written =
        wormedit::write_bms(edited, {.lane_profile_id = parse_options.lane_profile_id,
                                     .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(occurrences(*written.bytes, "#00011:0003") == 1);
    CHECK(occurrences(*written.bytes, "#00001:0300") == 1);
    CHECK(occurrences(*written.bytes, "#00004:0002") == 1);
    CHECK(occurrences(*written.bytes, "#00003:006F") == 1);
    CHECK(occurrences(*written.bytes, "#00008:0001") == 1);
    CHECK(occurrences(*written.bytes, "#00009:0001") == 1);
    CHECK(occurrences(*written.bytes, "#BPM01 155") == 1);
    CHECK(occurrences(*written.bytes, "#STOP01 60") == 1);
    CHECK(occurrences(*written.bytes, "#00011:0200") == 1);
    CHECK(occurrences(*written.bytes, "#00001:0200") == 1);
    CHECK(occurrences(*written.bytes, "#00004:0200") == 1);
    CHECK(occurrences(*written.bytes, "#00003:7800") == 1);
    CHECK(occurrences(*written.bytes, "#BPM01 210") == 1);
    CHECK(occurrences(*written.bytes, "#STOP01 96") == 1);
    CHECK(occurrences(*written.bytes, "#ELSE") == 1);
    CHECK(occurrences(*written.bytes, "#ENDIF") == 1);
    CHECK(occurrences(*written.bytes, "#ENDRANDOM") == 1);

    parse_options.branch_choices = {
        {.control_source_order = line_order(*written.bytes, "#RANDOM 2"), .selected_value = 1}};
    const auto first_branch = parse(*written.bytes, parse_options);
    REQUIRE(first_branch.song.has_value());
    CHECK_FALSE(first_branch.has_errors());
    const auto& reparsed_chart = first_branch.song->charts.front();
    REQUIRE(reparsed_chart.measures.front().notes.size() == 1);
    CHECK(reparsed_chart.measures.front().notes.front().position.row == half_measure);
    REQUIRE(reparsed_chart.measures.front().notes.front().sound.has_value());
    CHECK(std::ranges::find(first_branch.song->resources,
                            *reparsed_chart.measures.front().notes.front().sound,
                            &wormedit::resource_entry_t::id)
              ->name == "edited.wav");
    REQUIRE(reparsed_chart.measures.front().bgm_events.size() == 2);
    const auto edited_bgm =
        std::ranges::find_if(reparsed_chart.measures.front().bgm_events, [](const auto& event) {
            return event.position.row == 0 && event.sound.has_value();
        });
    REQUIRE(edited_bgm != reparsed_chart.measures.front().bgm_events.end());
    CHECK(std::ranges::find(first_branch.song->resources, *edited_bgm->sound,
                            &wormedit::resource_entry_t::id)
              ->name == "edited.wav");
    REQUIRE(reparsed_chart.bga_events.size() == 1);
    CHECK(reparsed_chart.bga_events.front().position.row == half_measure);
    CHECK(std::ranges::find(first_branch.song->resources,
                            reparsed_chart.bga_events.front().resource,
                            &wormedit::resource_entry_t::id)
              ->name == "replacement.png");
    CHECK(std::ranges::any_of(reparsed_chart.bpms, [&](const auto& bpm) {
        return bpm.position.row == half_measure &&
               bpm.beats_per_minute == wormedit::signed_rational_t{111, 1};
    }));
    CHECK(std::ranges::any_of(reparsed_chart.bpms, [&](const auto& bpm) {
        return bpm.position.row == half_measure &&
               bpm.beats_per_minute == wormedit::signed_rational_t{155, 1};
    }));
    CHECK(std::ranges::any_of(reparsed_chart.pauses, [&](const auto& pause) {
        return pause.position.row == half_measure &&
               pause.duration == wormedit::signed_rational_t{60, 1};
    }));

    parse_options.branch_choices.front().selected_value = 2;
    const auto second_branch = parse(*written.bytes, parse_options);
    REQUIRE(second_branch.song.has_value());
    CHECK_FALSE(second_branch.has_errors());
    const auto& sibling_chart = second_branch.song->charts.front();
    REQUIRE(sibling_chart.measures.front().notes.size() == 1);
    CHECK(sibling_chart.measures.front().notes.front().position.row == 0);
    CHECK(sibling_chart.measures.front().notes.front().sound.has_value());
    CHECK(std::ranges::any_of(sibling_chart.bpms, [](const auto& bpm) {
        return bpm.beats_per_minute == wormedit::signed_rational_t{120, 1};
    }));
    CHECK(std::ranges::any_of(sibling_chart.pauses, [](const auto& pause) {
        return pause.duration == wormedit::signed_rational_t{96, 1};
    }));
}

TEST_CASE("BMS writer rejects branch event edits without unique source attribution",
          "[unit][format][bms][writer][branch][events]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Ambiguous branch timing\n#BPM01 150\n#STOP01 48\n"
        "#RANDOM 2\n#IF 1\n#00011:0102\n#00008:0101\n#00009:0101\n"
        "#ELSE\n#00008:0100\n#00009:0100\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());

    wormedit::bms_write_options_t options;
    options.lane_profile_id = parse_options.lane_profile_id;
    options.preserve_unchanged_source = false;

    auto colliding_note = *imported.song;
    auto& notes = colliding_note.charts.front().measures.front().notes;
    REQUIRE(notes.size() == 2);
    notes.front().position.row = colliding_note.charts.front().divisions_per_standard_measure / 2;
    const auto collision = wormedit::write_bms(colliding_note, options);
    CHECK_FALSE(collision.bytes.has_value());
    CHECK(has_error(collision, wormedit::bms_write_diagnostic_code_t::branch_provenance));

    auto ambiguous_bpm = *imported.song;
    auto& bpms = ambiguous_bpm.charts.front().bpms;
    const auto first_extended = std::ranges::find_if(
        bpms, [](const auto& bpm) { return bpm.provenance.source_field.starts_with("08 slot "); });
    REQUIRE(first_extended != bpms.end());
    first_extended->beats_per_minute = {155, 1};
    const auto shared_bpm = wormedit::write_bms(ambiguous_bpm, options);
    CHECK_FALSE(shared_bpm.bytes.has_value());
    CHECK(has_error(shared_bpm, wormedit::bms_write_diagnostic_code_t::branch_provenance));

    auto ambiguous_pause = *imported.song;
    REQUIRE(ambiguous_pause.charts.front().pauses.size() == 2);
    ambiguous_pause.charts.front().pauses.front().duration = {60, 1};
    const auto shared_stop = wormedit::write_bms(ambiguous_pause, options);
    CHECK_FALSE(shared_stop.bytes.has_value());
    CHECK(has_error(shared_stop, wormedit::bms_write_diagnostic_code_t::branch_provenance));

    auto forged_provenance = *imported.song;
    forged_provenance.charts.front().measures.front().notes.front().provenance.source_spelling +=
        " forged";
    const auto forged = wormedit::write_bms(forged_provenance, options);
    CHECK_FALSE(forged.bytes.has_value());
    CHECK(has_error(forged, wormedit::bms_write_diagnostic_code_t::branch_provenance));
}

TEST_CASE("BMS writer protects root timing declarations shared with sibling branch events",
          "[unit][format][bms][writer][branch][events]") {
    constexpr std::string_view source = "#PLAYER 1\n#TITLE Shared declaration\n#BPM01 150\n"
                                        "#RANDOM 2\n#IF 1\n#00008:0100\n#ELSE\n#00008:0100\n"
                                        "#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    auto edited = *imported.song;
    const auto extended = std::ranges::find_if(edited.charts.front().bpms, [](const auto& bpm) {
        return bpm.provenance.source_field.starts_with("08 slot ");
    });
    REQUIRE(extended != edited.charts.front().bpms.end());
    extended->beats_per_minute = {155, 1};
    const auto rejected = wormedit::write_bms(
        edited, {.lane_profile_id = "bme-7k-outside-scratch", .preserve_unchanged_source = false});
    CHECK_FALSE(rejected.bytes.has_value());
    CHECK(has_error(rejected, wormedit::bms_write_diagnostic_code_t::branch_provenance));
}

TEST_CASE("BMS writer edits a retained branch hold through its paired source slots",
          "[unit][format][bms][writer][branch][events]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Branch hold\n#BPM 120\n#WAV01 original.wav\n#WAV02 edited.wav\n"
        "#RANDOM 2\n#IF 1\n#00051:0101\n#ELSE\n#00051:0101\n"
        "#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices = {
        {.control_source_order = line_order(source, "#RANDOM 2"), .selected_value = 1}};
    const auto imported = parse(source, parse_options);
    REQUIRE(imported.song.has_value());
    REQUIRE(imported.song->charts.front().measures.front().notes.size() == 1);
    REQUIRE(imported.song->charts.front().measures.front().notes.front().kind ==
            wormedit::note_kind_t::hold);
    const auto edited_wav =
        std::ranges::find_if(imported.song->resources,
                             [](const auto& resource) { return resource.name == "edited.wav"; });
    REQUIRE(edited_wav != imported.song->resources.end());

    auto edited = *imported.song;
    edited.charts.front().measures.front().notes.front().sound = edited_wav->id;
    const auto written =
        wormedit::write_bms(edited, {.lane_profile_id = parse_options.lane_profile_id,
                                     .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(occurrences(*written.bytes, "#00051:0201") == 1);
    CHECK(occurrences(*written.bytes, "#00051:0101") == 1);

    parse_options.branch_choices = {
        {.control_source_order = line_order(*written.bytes, "#RANDOM 2"), .selected_value = 1}};
    const auto first = parse(*written.bytes, parse_options);
    REQUIRE(first.song.has_value());
    REQUIRE(first.song->charts.front().measures.front().notes.size() == 1);
    const auto& first_hold = first.song->charts.front().measures.front().notes.front();
    CHECK(first_hold.kind == wormedit::note_kind_t::hold);
    REQUIRE(first_hold.sound.has_value());
    CHECK(
        std::ranges::find(first.song->resources, *first_hold.sound, &wormedit::resource_entry_t::id)
            ->name == "edited.wav");

    parse_options.branch_choices.front().selected_value = 2;
    const auto second = parse(*written.bytes, parse_options);
    REQUIRE(second.song.has_value());
    REQUIRE(second.song->charts.front().measures.front().notes.size() == 1);
    const auto& sibling_hold = second.song->charts.front().measures.front().notes.front();
    REQUIRE(sibling_hold.sound.has_value());
    CHECK(std::ranges::find(second.song->resources, *sibling_hold.sound,
                            &wormedit::resource_entry_t::id)
              ->name == "original.wav");
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

TEST_CASE("BMS export bakes recipes before reporting unsupported regular scroll events",
          "[unit][format][bms][writer][sv-recipe]") {
    const auto imported = simple_chart();
    REQUIRE(imported.song.has_value());
    auto song = *imported.song;
    auto& chart = song.charts.front();
    REQUIRE(chart.measures.size() >= 2);
    chart.sv_recipes.push_back({
        .id = wormedit::recipe_id_t{900004},
        .name = "export guard",
        .range = {{chart.measures[0].id, 0}, {chart.measures[1].id, 0}},
        .output = wormedit::sv_recipe_output_t::sv,
        .curve = wormedit::sv_recipe_curve_t::constant,
        .parameters = {.value_from = {2, 1}},
    });
    const auto written = wormedit::write_bms(song);
    CHECK_FALSE(written.bytes.has_value());
    CHECK(has_error(written, wormedit::bms_write_diagnostic_code_t::unsupported_feature));
}
