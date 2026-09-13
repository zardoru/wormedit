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

TEST_CASE("BMS writer rejects holds and events outside the current semantic slice",
          "[unit][format][bms][writer]") {
    auto imported = simple_chart();
    REQUIRE(imported.song.has_value());
    auto song = *imported.song;
    auto& note = song.charts.front().measures.front().notes.front();
    note.kind = wormedit::note_kind_t::hold;
    note.end = wormedit::local_position_t{note.position.measure, note.position.row + 120};
    wormedit::bms_write_options_t options;
    options.preserve_unchanged_source = false;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto hold = wormedit::write_bms(song, options);
    CHECK_FALSE(hold.bytes.has_value());
    CHECK(has_error(hold, wormedit::bms_write_diagnostic_code_t::unsupported_feature));

    auto bgm = *imported.song;
    bgm.charts.front().bgm_source_lanes.push_back({
        .id = wormedit::object_id_t{91},
        .name = "BGM source",
        .source_order = 0,
    });
    bgm.charts.front().measures.front().bgm_events.push_back({
        .id = wormedit::object_id_t{90},
        .position = {bgm.charts.front().measures.front().id, 0},
        .source_lane = wormedit::object_id_t{91},
        .sound = bgm.resources.front().id,
    });
    const auto bgm_result = wormedit::write_bms(bgm, options);
    CHECK_FALSE(bgm_result.bytes.has_value());
    CHECK(has_error(bgm_result, wormedit::bms_write_diagnostic_code_t::unsupported_feature));
}

TEST_CASE("BMS writer diagnoses branch projections and unsafe retained metadata tokens",
          "[unit][format][bms][writer]") {
    constexpr std::string_view branch_source = "#RANDOM 1\n"
                                               "#IF 1\n"
                                               "#PLAYER 1\n"
                                               "#TITLE Branch\n"
                                               "#BPM 120\n"
                                               "#WAV01 click.wav\n"
                                               "#00011:01\n"
                                               "#ENDIF\n"
                                               "#ENDRANDOM\n";
    wormedit::bms_parse_options_t parse_options;
    parse_options.lane_profile_id = "bme-7k-outside-scratch";
    parse_options.branch_choices.push_back({.control_source_order = 0, .selected_value = 1});
    auto imported = parse(branch_source, parse_options);
    REQUIRE(imported.song.has_value());

    wormedit::bms_write_options_t options;
    options.preserve_unchanged_source = false;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto branch = wormedit::write_bms(*imported.song, options);
    CHECK_FALSE(branch.bytes.has_value());
    CHECK(has_error(branch, wormedit::bms_write_diagnostic_code_t::unsupported_feature));

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
