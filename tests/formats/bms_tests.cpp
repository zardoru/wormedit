#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/timing.h>
#include <wormedit/formats/bms.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

auto read_binary(const std::filesystem::path& path) -> std::vector<std::byte> {
    std::ifstream stream(path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes;
    bytes.reserve(source.size());
    for (const auto character : source) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return bytes;
}

auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto parse(std::string_view source, wormedit::bms_parse_options_t options = {})
    -> wormedit::bms_parse_result_t {
    return wormedit::parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                     options);
}

auto fixture(std::string_view relative, wormedit::bms_parse_options_t options = {})
    -> wormedit::bms_parse_result_t {
    const auto bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / relative);
    return wormedit::parse_bms_bytes(bytes, std::move(options));
}

auto has_detection_evidence(const wormedit::bms_profile_detection_result_t& detection,
                            wormedit::bms_detection_evidence_source_t source) -> bool {
    return std::ranges::any_of(detection.evidence,
                               [source](const auto& item) { return item.source == source; });
}

auto find_kind(const wormedit::bms_control_node_t& node, wormedit::bms_control_kind_t kind)
    -> std::vector<const wormedit::bms_control_node_t*> {
    std::vector<const wormedit::bms_control_node_t*> matches;
    const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& current) -> void {
        if (current.kind == kind) {
            matches.push_back(&current);
        }
        for (const auto& child : current.children) {
            self(self, child);
        }
    };
    visit(visit, node);
    return matches;
}

auto source_lines(const wormedit::bms_control_node_t& node) -> std::vector<std::string> {
    std::vector<std::string> lines;
    const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& current) -> void {
        if (current.kind != wormedit::bms_control_kind_t::root) {
            lines.push_back(current.original_text);
        }
        if (current.close_text.has_value()) {
            lines.push_back(*current.close_text);
        }
        for (const auto& child : current.children) {
            self(self, child);
        }
    };
    visit(visit, node);
    return lines;
}

auto physical_source_spans(const wormedit::bms_control_node_t& node)
    -> std::vector<wormedit::bms_source_span_t> {
    std::vector<wormedit::bms_source_span_t> spans;
    const auto visit = [&](const auto& self, const wormedit::bms_control_node_t& current) -> void {
        if (current.kind != wormedit::bms_control_kind_t::root &&
            current.kind != wormedit::bms_control_kind_t::if_group) {
            spans.push_back(current.source_span);
        }
        if (current.close_source_span.has_value()) {
            spans.push_back(*current.close_source_span);
        }
        for (const auto& child : current.children) {
            self(self, child);
        }
    };
    visit(visit, node);
    std::ranges::sort(spans, {}, &wormedit::bms_source_span_t::byte_begin);
    return spans;
}

} // namespace

TEST_CASE("BMS reader imports metadata, source bytes, measures and resource declarations",
          "[unit][format][bms]") {
    auto result = fixture("bms/repeated-bgm-ratio-stop-collision.bms");
    REQUIRE(result.song.has_value());
    CHECK_FALSE(result.has_errors());
    CHECK(result.encoding.detected == wormedit::text_encoding_t::ascii);
    CHECK(result.encoding.unchanged_since_import);
    REQUIRE(result.encoding.original_bytes);
    CHECK_FALSE(result.encoding.original_bytes->empty());

    const auto& song = *result.song;
    CHECK(song.metadata.title == "Fixture: layers, ratio, timing collision");
    CHECK(song.metadata.artist == "Wormedit fixture authors");
    REQUIRE(song.metadata.format_groups.size() == 1);
    CHECK(song.metadata.format_groups[0].encoding.original_bytes == result.encoding.original_bytes);
    REQUIRE(song.charts.size() == 1);
    const auto& chart = song.charts.front();
    REQUIRE(chart.metadata.format_groups.size() == 1);
    CHECK(chart.metadata.format_groups[0].encoding.original_bytes ==
          result.encoding.original_bytes);
    REQUIRE(chart.bms_control_tree.has_value());
    REQUIRE(chart.measures.size() == 2);
    CHECK((chart.measures[0].definition.length == wormedit::rational_t{3, 4}));
    CHECK((chart.measures[1].definition.length == wormedit::rational_t{5, 4}));
    CHECK(song.audio_assets.size() == 4);
    CHECK(song.resources.size() == 4);
    CHECK(song.resources[0].name == "bgm-a.wav");
    CHECK(song.audio_assets[0].source_path == std::filesystem::path("bgm-a.wav"));
    CHECK(song.resources[0].target_identities[0].identifier == "01");
    // Two BGM columns: the busiest measure has two `#mmm01` lines.
    REQUIRE(chart.bgm_source_lanes.size() == 2);
    // `00` slots are padding, not events: only the three non-empty slots remain.
    REQUIRE(chart.measures[0].bgm_events.size() == 3);
    REQUIRE(chart.measures[1].bgm_events.size() == 1);
    CHECK(chart.measures[0].bgm_events[0].sound == song.resources[0].id);
    CHECK(chart.measures[0].bgm_events[0].structural_slot == 0);
    CHECK(chart.measures[0].bgm_events[1].sound == song.resources[1].id);
    CHECK(chart.measures[0].bgm_events[1].structural_slot == 3);
    CHECK(chart.measures[0].bgm_events[2].sound == song.resources[2].id);
    CHECK(chart.measures[0].bgm_events[2].structural_slot == 2);
    CHECK(chart.measures[0].bgm_events[2].source_lane !=
          chart.measures[0].bgm_events[0].source_lane);
    CHECK(chart.measures[1].bgm_events[0].source_lane ==
          chart.measures[0].bgm_events[0].source_lane);
    CHECK(std::ranges::all_of(chart.measures[0].bgm_events,
                              [](const auto& event) { return event.sound.has_value(); }));
    REQUIRE(chart.bpms.size() == 3);
    CHECK((chart.bpms[0].beats_per_minute == wormedit::signed_rational_t{120, 1}));
    CHECK((chart.bpms[1].beats_per_minute == wormedit::signed_rational_t{120, 1}));
    CHECK((chart.bpms[2].beats_per_minute == wormedit::signed_rational_t{180, 1}));
    REQUIRE(chart.pauses.size() == 1);
    CHECK((chart.pauses[0].duration == wormedit::signed_rational_t{48, 1}));
    CHECK(chart.pauses[0].source_unit == wormedit::pause_unit_t::bms_stop_ticks);
    CHECK(chart.measures[0].notes.empty());

    const auto raw = source_lines(*chart.bms_control_tree);
    CHECK(std::ranges::find(raw, "#00001:0100000200") != raw.end());
    CHECK(std::ranges::find(raw, "#00001:0000030000") != raw.end());
    const auto ratios =
        find_kind(*chart.bms_control_tree, wormedit::bms_control_kind_t::raw_statement);
    const auto ratio_line = std::ranges::find_if(
        ratios, [](const auto* node) { return node->original_text == "#00002:0.75"; });
    REQUIRE(ratio_line != ratios.end());
    CHECK((*ratio_line)->command_token == "00002");
    CHECK((*ratio_line)->raw_value == "0.75");
    CHECK((*ratio_line)->source_order == 10);
    CHECK((*ratio_line)->source_span.byte_end > (*ratio_line)->source_span.byte_begin);
    CHECK((*ratio_line)->line_ending == wormedit::bms_line_ending_t::lf);
    CHECK(wormedit::validate_document(song).empty());
}

TEST_CASE("BMS fixture preserves presentation-first discontinuity order",
          "[unit][format][bms][timing][discontinuity]") {
    const auto parsed = fixture("bms/repeated-bgm-ratio-stop-collision.bms");
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.has_errors());
    const auto& chart = parsed.song->charts.front();
    const auto timing = wormedit::build_timing_index(chart);
    REQUIRE(timing);
    CHECK(timing.index->discontinuity_profile ==
          wormedit::timing_discontinuity_profile_t::bms_legacy);

    const auto built = wormedit::build_preview_schedule(
        chart, *timing.index, {.sample_rate = 48'000, .include_metronome = false});
    REQUIRE(built);
    std::vector<wormedit::preview_discontinuity_kind_t> at_zero;
    for (const auto& event : built.schedule->discontinuities) {
        if (event.sample_frame == 0) {
            at_zero.push_back(event.kind);
        }
    }
    const auto first_bpm = std::ranges::find(at_zero, wormedit::preview_discontinuity_kind_t::bpm);
    const auto first_stop =
        std::ranges::find(at_zero, wormedit::preview_discontinuity_kind_t::stop);
    REQUIRE(first_bpm != at_zero.end());
    REQUIRE(first_stop != at_zero.end());
    CHECK(std::ranges::find(at_zero.begin(), first_bpm,
                            wormedit::preview_discontinuity_kind_t::bgm) != first_bpm);
    CHECK(std::ranges::find(first_bpm, at_zero.end(),
                            wormedit::preview_discontinuity_kind_t::bgm) == at_zero.end());
    CHECK(std::ranges::find(first_stop, at_zero.end(),
                            wormedit::preview_discontinuity_kind_t::bpm) == at_zero.end());
}

TEST_CASE("BMS reader retains extended metadata fields in source order",
          "[unit][format][bms][metadata]") {
    constexpr std::string_view source = "#PLAYER 1\n"
                                        "#GENRE Artcore\n"
                                        "#TITLE Main title\n"
                                        "#SUBTITLE [Another]\n"
                                        "#ARTIST Primary artist\n"
                                        "#SUBARTIST feat. Wormedit\n"
                                        "#PLAYLEVEL 12\n"
                                        "#STAGEFILE stage.png\n"
                                        "#BANNER banner.png\n"
                                        "#PREVIEW preview.ogg\n"
                                        "#CUSTOM_EXTENSION retained value\n"
                                        "#BPM 150\n";

    const auto result = parse(source);
    REQUIRE(result.song.has_value());
    CHECK_FALSE(result.has_errors());
    CHECK(result.song->metadata.title == "Main title");
    CHECK(result.song->metadata.artist == "Primary artist");
    REQUIRE(result.song->metadata.format_groups.size() == 1);
    CHECK(result.song->metadata.format_groups.front().ordered_fields ==
          std::vector<std::pair<std::string, std::string>>{
              {"PLAYER", "1"},
              {"GENRE", "Artcore"},
              {"TITLE", "Main title"},
              {"SUBTITLE", "[Another]"},
              {"ARTIST", "Primary artist"},
              {"SUBARTIST", "feat. Wormedit"},
              {"PLAYLEVEL", "12"},
              {"STAGEFILE", "stage.png"},
              {"BANNER", "banner.png"},
              {"PREVIEW", "preview.ogg"},
              {"CUSTOM_EXTENSION", "retained value"},
          });
}

TEST_CASE("BMS channel materialization respects explicit profiles and exact grids",
          "[unit][format][bms][events]") {
    constexpr std::string_view source =
        "#PLAYER 1\n#BPM 120\n#WAV01 key.wav\n#00011:0100\n#00031:0001\n#000D1:0100\n";
    wormedit::bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    auto selected = parse(source, options);
    REQUIRE(selected.song.has_value());
    REQUIRE(selected.song->charts.size() == 1);
    const auto& chart = selected.song->charts.front();
    REQUIRE(chart.lane_layouts.size() == 1);
    REQUIRE(chart.measures.front().notes.size() == 3);
    CHECK(chart.measures.front().notes[0].kind == wormedit::note_kind_t::tap);
    CHECK(chart.measures.front().notes[0].sound == selected.song->resources.front().id);
    CHECK(chart.measures.front().notes[1].kind == wormedit::note_kind_t::invisible);
    CHECK(chart.measures.front().notes[1].position.row == 480);
    CHECK(chart.measures.front().notes[2].kind == wormedit::note_kind_t::mine);
    CHECK_FALSE(chart.measures.front().notes[2].sound.has_value());
    CHECK_FALSE(selected.has_errors());

    auto seven_slots = std::string{"#BPM 120\n"};
    for (unsigned int index = 1; index <= 7; ++index) {
        const auto identifier = index < 10 ? "0" + std::to_string(index) : std::to_string(index);
        seven_slots += "#WAV" + identifier + " sample-" + identifier + ".wav\n";
    }
    seven_slots += "#00001:01020304050607\n";
    auto subdivided = parse(seven_slots);
    REQUIRE(subdivided.song.has_value());
    const auto& subdivided_chart = subdivided.song->charts.front();
    CHECK(subdivided_chart.divisions_per_standard_measure == 6720);
    REQUIRE(subdivided_chart.measures.front().bgm_events.size() == 7);
    for (std::size_t slot = 0; slot < 7; ++slot) {
        CHECK(subdivided_chart.measures.front().bgm_events[slot].position.row == slot * 960);
    }

    auto denominator_only = parse("#00002:1/997\n");
    REQUIRE(denominator_only.song.has_value());
    CHECK(denominator_only.song->charts.front().divisions_per_standard_measure == 957120);
    CHECK((denominator_only.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{1, 997}));

    wormedit::bms_parse_options_t bounded;
    bounded.max_chart_resolution = 960;
    auto capped = parse("#00002:1/997\n", bounded);
    REQUIRE(capped.song.has_value());
    CHECK(capped.song->charts.front().divisions_per_standard_measure == 960);
    CHECK((capped.song->charts.front().measures.front().definition.length ==
           wormedit::rational_t{1, 1}));
    CHECK(std::ranges::any_of(capped.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_measure &&
               diagnostic.message.find("remains raw") != std::string::npos;
    }));

    auto capped_subdivision = parse(seven_slots, bounded);
    REQUIRE(capped_subdivision.song.has_value());
    CHECK(capped_subdivision.song->charts.front().divisions_per_standard_measure == 960);
    CHECK(capped_subdivision.song->charts.front().bgm_source_lanes.empty());
    CHECK(capped_subdivision.song->charts.front().measures.front().bgm_events.empty());
    CHECK(std::ranges::any_of(capped_subdivision.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_measure &&
               diagnostic.message.find("channel subdivisions") != std::string::npos;
    }));
}

TEST_CASE("every built-in BMS lane profile imports its fixture-backed visible channel map",
          "[unit][format][bms][events][profile][fixture]") {
    struct fixture_case_t {
        std::string_view file;
        std::string_view profile_id;
        std::size_t lane_count;
    };
    constexpr std::array cases{
        fixture_case_t{"bms-5k-free-zone.bms", "bms-5k-outside-scratch-free-zone", 7},
        fixture_case_t{"bms-5k-foot-pedal.bms", "bms-5k-outside-scratch-foot-pedal", 7},
        fixture_case_t{"bme-7k.bme", "bme-7k-outside-scratch", 8},
        fixture_case_t{"bme-7k-foot-pedal.bme", "bme-7k-outside-scratch-foot-pedal", 9},
        fixture_case_t{"bms-10k-standard.bms", "bms-10k-left-outside-right-outside", 12},
        fixture_case_t{"bms-10k-free-zone.bms", "bms-10k-left-outside-right-outside-free-zone", 14},
        fixture_case_t{"bms-10k-foot-pedal.bms", "bms-10k-left-outside-right-outside-foot-pedal",
                       14},
        fixture_case_t{"bms-10k-scratchless.bms", "bms-10k-scratchless", 10},
        fixture_case_t{"bme-14k.bme", "bme-14k-left-outside-right-outside", 16},
        fixture_case_t{"bme-14k-foot-pedal.bme", "bme-14k-left-outside-right-outside-foot-pedal",
                       18},
        fixture_case_t{"pms-5button.pms", "pms-standard-5button", 5},
        fixture_case_t{"pms-9button.pms", "pms-standard-9button", 9},
        fixture_case_t{"pms-bme-9button.pms", "pms-bme-style-9button", 9},
        fixture_case_t{"pms-bme-18button.pms", "pms-bme-style-18button", 18},
        fixture_case_t{"double-scratch-fpp.bms", "bms-double-scratch-fpp", 9},
        fixture_case_t{"octave-foot-pedal.bms", "bms-octave-foot-pedal", 16},
        fixture_case_t{"project2dx-7k.bms", "project2dx-7k", 8},
    };

    const auto root = std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bms" / "profiles";
    const auto inventory = read_text(root / "README.md");
    REQUIRE_FALSE(inventory.empty());
    const auto catalog = wormedit::built_in_bms_lane_profiles();
    REQUIRE(catalog.size() == cases.size());

    for (const auto& test : cases) {
        CAPTURE(test.file, test.profile_id);
        CHECK(inventory.find(test.file) != std::string::npos);
        CHECK(inventory.find(test.profile_id) != std::string::npos);

        const auto descriptor = std::ranges::find_if(catalog, [&](const auto& candidate) {
            return candidate.profile.layout.profile_id == test.profile_id;
        });
        REQUIRE(descriptor != catalog.end());
        REQUIRE(descriptor->profile.layout.lanes.size() == test.lane_count);

        wormedit::bms_parse_options_t options;
        options.lane_profile_id = test.profile_id;
        auto result = fixture("bms/profiles/" + std::string(test.file), std::move(options));
        REQUIRE(result.song.has_value());
        CHECK_FALSE(result.has_errors());
        REQUIRE(result.lane_profile_detection.has_value());
        CHECK(result.lane_profile_detection->status ==
              wormedit::bms_profile_detection_status_t::selected);
        REQUIRE(result.lane_profile_detection->selected_profile.has_value());
        CHECK(result.lane_profile_detection->selected_profile->layout.profile_id ==
              test.profile_id);
        CHECK(result.lane_profile_detection->confidence == 1.0);
        CHECK(has_detection_evidence(*result.lane_profile_detection,
                                     wormedit::bms_detection_evidence_source_t::explicit_profile));
        REQUIRE(result.song->charts.size() == 1);
        const auto& chart = result.song->charts.front();
        REQUIRE(chart.lane_layouts.size() == 1);
        const auto& imported_layout = chart.lane_layouts.front().layout;
        CHECK(imported_layout.profile_id == test.profile_id);
        REQUIRE(imported_layout.lanes.size() == test.lane_count);
        REQUIRE(chart.measures.front().notes.size() == test.lane_count);

        for (const auto& expected_lane : descriptor->profile.layout.lanes) {
            REQUIRE(expected_lane.bms_channels.visible.has_value());
            const auto note =
                std::ranges::find_if(chart.measures.front().notes, [&](const auto& item) {
                    return item.lane == expected_lane.id;
                });
            REQUIRE(note != chart.measures.front().notes.end());
            CHECK(note->kind == wormedit::note_kind_t::tap);
            CHECK(note->sound.has_value());
            CHECK(note->provenance.profile == test.profile_id);
            CHECK(note->provenance.source_field.starts_with(*expected_lane.bms_channels.visible));

            const auto imported_lane = std::ranges::find(imported_layout.lanes, expected_lane.id,
                                                         &wormedit::lane_definition_t::id);
            REQUIRE(imported_lane != imported_layout.lanes.end());
            CHECK(imported_lane->role == expected_lane.role);
            CHECK(imported_lane->stage == expected_lane.stage);
        }
    }
}

TEST_CASE("BMS import forwards extension and active headers into conservative profile detection",
          "[unit][format][bms][events][profile][detection][fixture]") {
    wormedit::bms_parse_options_t pms_options;
    pms_options.file_extension = ".pms";
    auto pms = fixture("bms/profiles/pms-bme-18button.pms", pms_options);
    REQUIRE(pms.song.has_value());
    REQUIRE(pms.lane_profile_detection.has_value());
    CHECK(pms.lane_profile_detection->status == wormedit::bms_profile_detection_status_t::selected);
    REQUIRE(pms.lane_profile_detection->selected_profile.has_value());
    CHECK(pms.lane_profile_detection->selected_profile->layout.profile_id ==
          "pms-bme-style-18button");
    CHECK(has_detection_evidence(*pms.lane_profile_detection,
                                 wormedit::bms_detection_evidence_source_t::file_extension));
    CHECK(has_detection_evidence(*pms.lane_profile_detection,
                                 wormedit::bms_detection_evidence_source_t::player_header));
    REQUIRE(pms.song->charts.front().lane_layouts.size() == 1);
    CHECK(pms.song->charts.front().measures.front().notes.size() == 18);

    wormedit::bms_parse_options_t octave_options;
    octave_options.file_extension = "bms";
    auto octave = fixture("bms/profiles/octave-foot-pedal.bms", octave_options);
    REQUIRE(octave.song.has_value());
    REQUIRE(octave.lane_profile_detection.has_value());
    CHECK(octave.lane_profile_detection->status ==
          wormedit::bms_profile_detection_status_t::selected);
    REQUIRE(octave.lane_profile_detection->selected_profile.has_value());
    CHECK(octave.lane_profile_detection->selected_profile->layout.profile_id ==
          "bms-octave-foot-pedal");
    CHECK(has_detection_evidence(*octave.lane_profile_detection,
                                 wormedit::bms_detection_evidence_source_t::octave_fp_header));
    REQUIRE(octave.song->charts.front().lane_layouts.size() == 1);
    CHECK(octave.song->charts.front().measures.front().notes.size() == 16);

    wormedit::bms_parse_options_t ambiguous_options;
    ambiguous_options.file_extension = ".bms";
    auto ambiguous = fixture("bms/profiles/bms-5k-free-zone.bms", ambiguous_options);
    REQUIRE(ambiguous.song.has_value());
    REQUIRE(ambiguous.lane_profile_detection.has_value());
    CHECK(ambiguous.lane_profile_detection->status ==
          wormedit::bms_profile_detection_status_t::ambiguous);
    CHECK_FALSE(ambiguous.lane_profile_detection->selected_profile.has_value());
    CHECK(ambiguous.lane_profile_detection->custom_fallback.channels.size() == 7);
    CHECK(ambiguous.song->charts.front().lane_layouts.empty());
    CHECK(ambiguous.song->charts.front().measures.front().notes.empty());
    CHECK_FALSE(ambiguous.has_errors());

    wormedit::bms_parse_options_t contradictory_options;
    contradictory_options.file_extension = ".pms";
    contradictory_options.lane_profile_id = "project2dx-7k";
    auto contradictory = fixture("bms/profiles/pms-bme-18button.pms", contradictory_options);
    REQUIRE(contradictory.song.has_value());
    REQUIRE(contradictory.lane_profile_detection.has_value());
    CHECK(contradictory.lane_profile_detection->status ==
          wormedit::bms_profile_detection_status_t::custom_required);
    CHECK_FALSE(contradictory.lane_profile_detection->selected_profile.has_value());
    CHECK(has_detection_evidence(*contradictory.lane_profile_detection,
                                 wormedit::bms_detection_evidence_source_t::explicit_profile));
    CHECK(contradictory.song->charts.front().lane_layouts.empty());
    CHECK(contradictory.song->charts.front().measures.front().notes.empty());
}

TEST_CASE("dense standard BMS-family fixtures auto-select only separated profile evidence",
          "[unit][format][bms][events][profile][detection][coverage][fixture]") {
    struct selected_case_t {
        std::string_view file;
        std::string_view extension;
        std::string_view profile_id;
        std::size_t note_count;
    };
    constexpr std::array selected_cases{
        selected_case_t{"bme-7k.bme", ".bme", "bme-7k-outside-scratch", 8},
        selected_case_t{"bme-14k.bme", ".bme", "bme-14k-left-outside-right-outside", 16},
        selected_case_t{"bme-14k-foot-pedal.bme", ".bme",
                        "bme-14k-left-outside-right-outside-foot-pedal", 18},
        selected_case_t{"pms-5button.pms", ".pms", "pms-standard-5button", 5},
        selected_case_t{"pms-9button.pms", ".pms", "pms-standard-9button", 9},
        selected_case_t{"pms-bme-9button.pms", ".pms", "pms-bme-style-9button", 9},
    };
    for (const auto& test : selected_cases) {
        CAPTURE(test.file, test.profile_id);
        wormedit::bms_parse_options_t options;
        options.file_extension = test.extension;
        const auto result = fixture("bms/profiles/" + std::string(test.file), options);
        REQUIRE(result.song.has_value());
        REQUIRE_FALSE(result.has_errors());
        REQUIRE(result.lane_profile_detection.has_value());
        const auto& detection = *result.lane_profile_detection;
        REQUIRE(detection.status == wormedit::bms_profile_detection_status_t::selected);
        REQUIRE(detection.selected_profile.has_value());
        CHECK(detection.selected_profile->layout.profile_id == test.profile_id);
        const auto candidate = std::ranges::find_if(detection.candidates, [&](const auto& item) {
            return item.profile.layout.profile_id == test.profile_id;
        });
        REQUIRE(candidate != detection.candidates.end());
        CHECK(candidate->complete_channel_family_match);
        REQUIRE(result.song->charts.front().lane_layouts.size() == 1);
        CHECK(result.song->charts.front().measures.front().notes.size() == test.note_count);
    }

    struct ambiguous_case_t {
        std::string_view file;
        std::string_view extension;
    };
    constexpr std::array ambiguous_cases{
        ambiguous_case_t{"bms-5k-free-zone.bms", ".bms"},
        ambiguous_case_t{"bms-10k-foot-pedal.bms", ".bms"},
        ambiguous_case_t{"bme-7k-foot-pedal.bme", ".bme"},
        ambiguous_case_t{"double-scratch-fpp.bms", ".bms"},
        ambiguous_case_t{"project2dx-7k.bms", ".bms"},
    };
    for (const auto& test : ambiguous_cases) {
        CAPTURE(test.file);
        wormedit::bms_parse_options_t options;
        options.file_extension = test.extension;
        const auto result = fixture("bms/profiles/" + std::string(test.file), options);
        REQUIRE(result.song.has_value());
        REQUIRE_FALSE(result.has_errors());
        REQUIRE(result.lane_profile_detection.has_value());
        CHECK(result.lane_profile_detection->status ==
              wormedit::bms_profile_detection_status_t::ambiguous);
        CHECK_FALSE(result.lane_profile_detection->selected_profile.has_value());
        CHECK(result.song->charts.front().lane_layouts.empty());
        CHECK(result.song->charts.front().measures.front().notes.empty());
    }
}

TEST_CASE("BMS long-note channel endpoints pair across lines and measures",
          "[unit][format][bms][events][long-note]") {
    constexpr std::string_view source = "#WAV01 head.wav\n#WAV02 tail.wav\n"
                                        "#00051:0100\n#00051:0002\n#00151:0100\n#00251:0100\n";
    wormedit::bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    auto result = parse(source, options);
    REQUIRE(result.song.has_value());
    const auto& song = *result.song;
    const auto& chart = song.charts.front();
    REQUIRE(chart.measures.size() == 3);
    REQUIRE(chart.measures[0].notes.size() == 1);
    REQUIRE(chart.measures[1].notes.size() == 1);
    const auto& within_measure = chart.measures[0].notes.front();
    CHECK(within_measure.kind == wormedit::note_kind_t::hold);
    CHECK(within_measure.position.row == 0);
    REQUIRE(within_measure.end.has_value());
    CHECK(within_measure.end->measure == chart.measures[0].id);
    CHECK(within_measure.end->row == 480);
    CHECK(within_measure.sound == song.resources[0].id);
    CHECK(within_measure.provenance.source_field.find("paired with") != std::string::npos);
    CHECK(within_measure.provenance.source_spelling.find("#00051:0100") != std::string::npos);
    CHECK(within_measure.provenance.source_spelling.find("#00051:0002") != std::string::npos);

    const auto& across_measures = chart.measures[1].notes.front();
    CHECK(across_measures.kind == wormedit::note_kind_t::hold);
    CHECK(across_measures.position.measure == chart.measures[1].id);
    REQUIRE(across_measures.end.has_value());
    CHECK(across_measures.end->measure == chart.measures[2].id);
    CHECK(across_measures.sound == song.resources[0].id);
    CHECK(wormedit::validate_document(song).empty());

    auto legacy = fixture("bms/legacy-long-note.bml", options);
    REQUIRE(legacy.song.has_value());
    const auto& legacy_song = *legacy.song;
    const auto& legacy_chart = legacy_song.charts.front();
    REQUIRE(legacy_chart.measures.front().notes.size() == 1);
    const auto& legacy_hold = legacy_chart.measures.front().notes.front();
    CHECK(legacy_hold.kind == wormedit::note_kind_t::hold);
    CHECK(legacy_hold.position.row == 0);
    REQUIRE(legacy_hold.end.has_value());
    CHECK(legacy_hold.end->row == 480);
    CHECK(legacy_hold.sound == legacy_song.resources[0].id);
    CHECK(wormedit::validate_document(legacy_song).empty());
}

TEST_CASE("BMS leaves odd long-note endpoints raw and reports them",
          "[unit][format][bms][events][long-note][recovery]") {
    constexpr std::string_view source = "#WAV01 head.wav\n#00051:0100\n";
    wormedit::bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    auto result = parse(source, options);
    REQUIRE(result.song.has_value());
    const auto& song = *result.song;
    const auto& chart = song.charts.front();
    CHECK(chart.measures.front().notes.empty());
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unpaired_long_note &&
               diagnostic.message.find("no matching partner") != std::string::npos;
    }));
    const auto raw = source_lines(*chart.bms_control_tree);
    CHECK(std::ranges::find(raw, "#00051:0100") != raw.end());
    CHECK(wormedit::validate_document(song).empty());

    constexpr std::string_view incompatible_source = "#WAV01 head.wav\n#00051:0100\n#00151:0100\n";
    options.lane_profile_id = "pms-standard-5button";
    auto incompatible = parse(incompatible_source, options);
    REQUIRE(incompatible.song.has_value());
    CHECK(incompatible.song->charts.front().measures[0].notes.empty());
    CHECK(std::ranges::any_of(incompatible.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unsupported_channel;
    }));
    CHECK(wormedit::validate_document(*incompatible.song).empty());

    constexpr std::string_view capped_source =
        "#WAV01 head.wav\n#00051:01020304050607\n#00151:01\n#00251:01\n";
    wormedit::bms_parse_options_t capped_options;
    capped_options.lane_profile_id = "bme-7k-outside-scratch";
    capped_options.max_chart_resolution = 960;
    auto capped = parse(capped_source, capped_options);
    REQUIRE(capped.song.has_value());
    for (const auto& measure : capped.song->charts.front().measures) {
        CHECK(measure.notes.empty());
    }
    CHECK(std::ranges::any_of(capped.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_measure &&
               diagnostic.message.find("channel subdivisions") != std::string::npos;
    }));
    CHECK(wormedit::validate_document(*capped.song).empty());

    constexpr std::string_view capped_measure_source =
        "#WAV01 head.wav\n#00002:0.001\n#00051:0101\n";
    auto capped_measure = parse(capped_measure_source, capped_options);
    REQUIRE(capped_measure.song.has_value());
    CHECK(capped_measure.song->charts.front().measures.front().notes.empty());
    CHECK(std::ranges::any_of(capped_measure.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unpaired_long_note &&
               diagnostic.message.find("fallback measure geometry") != std::string::npos;
    }));
    CHECK(wormedit::validate_document(*capped_measure.song).empty());
}

TEST_CASE("BMS projects explicitly selected RANDOM and reachable SWITCH branches",
          "[unit][format][bms][control][events]") {
    constexpr std::string_view random_source =
        "#WAV01 key.wav\n#RANDOM 2\n#IF 1\n#00011:01\n#ELSE\n#00111:01\n"
        "#ENDIF\n#ENDRANDOM\n#00211:01\n";
    wormedit::bms_parse_options_t random_options;
    random_options.lane_profile_id = "bme-7k-outside-scratch";
    random_options.branch_choices = {{1, 1}}; // #RANDOM is physical source line 2.
    auto random_result = parse(random_source, random_options);
    REQUIRE(random_result.song.has_value());
    const auto& random_song = *random_result.song;
    const auto& random_chart = random_song.charts.front();
    CHECK(random_chart.measures[0].notes.size() == 1);
    CHECK(random_chart.measures[1].notes.empty());
    CHECK(random_chart.measures[2].notes.size() == 1);
    REQUIRE(random_chart.active_branch_selection.size() == 1);
    CHECK(random_chart.active_branch_selection.front().selected_value == 1);
    CHECK(wormedit::validate_document(random_song).empty());
    const auto random_raw = source_lines(*random_chart.bms_control_tree);
    CHECK(std::ranges::find(random_raw, "#00111:01") != random_raw.end());

    constexpr std::string_view switch_source =
        "#WAV01 key.wav\n#RANDOM 2\n#IF 2\n#SWITCH 2\n#CASE 1\n#00011:01\n"
        "#SKIP\n#CASE 2\n#00111:01\n#SKIP\n#DEF\n#00211:01\n#ENDSW\n"
        "#ENDIF\n#ENDRANDOM\n#00311:01\n";
    wormedit::bms_parse_options_t switch_options;
    switch_options.lane_profile_id = "bme-7k-outside-scratch";
    switch_options.branch_choices = {{1, 2}, {3, 2}};
    auto switch_result = parse(switch_source, switch_options);
    REQUIRE(switch_result.song.has_value());
    const auto& switch_song = *switch_result.song;
    const auto& switch_chart = switch_song.charts.front();
    CHECK(switch_chart.measures[0].notes.empty());
    CHECK(switch_chart.measures[1].notes.size() == 1);
    CHECK(switch_chart.measures[2].notes.empty());
    CHECK(switch_chart.measures[3].notes.size() == 1);
    REQUIRE(switch_chart.active_branch_selection.size() == 2);
    CHECK(switch_chart.active_branch_selection[0].selected_value == 2);
    CHECK(switch_chart.active_branch_selection[1].selected_value == 2);
    CHECK(wormedit::validate_document(switch_song).empty());

    wormedit::bms_parse_options_t fallthrough_options;
    fallthrough_options.lane_profile_id = "bme-7k-outside-scratch";
    fallthrough_options.branch_choices = {{22, 1}}; // #SWITCH is physical source line 23.
    auto fallthrough = fixture("bms/control-flow-mixed.bms", fallthrough_options);
    REQUIRE(fallthrough.song.has_value());
    const auto& fallthrough_song = *fallthrough.song;
    const auto& fallthrough_chart = fallthrough_song.charts.front();
    REQUIRE(fallthrough_chart.measures.front().notes.size() == 3);
    CHECK(fallthrough_chart.measures.front().notes[0].sound == fallthrough_song.resources[0].id);
    CHECK(fallthrough_chart.measures.front().notes[1].sound == fallthrough_song.resources[5].id);
    CHECK(fallthrough_chart.measures.front().notes[2].sound == fallthrough_song.resources[6].id);
    CHECK(fallthrough_chart.active_branch_selection.size() == 1);
    CHECK(wormedit::validate_document(fallthrough_song).empty());

    constexpr std::string_view invalid_choice_source =
        "#WAV01 key.wav\n#RANDOM 2\n#IF 1\n#00011:01\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t invalid_choice_options;
    invalid_choice_options.lane_profile_id = "bme-7k-outside-scratch";
    invalid_choice_options.branch_choices = {{1, 3}};
    auto invalid_choice = parse(invalid_choice_source, invalid_choice_options);
    REQUIRE(invalid_choice.song.has_value());
    const auto& invalid_choice_song = *invalid_choice.song;
    CHECK(invalid_choice_song.charts.front().measures.front().notes.empty());
    CHECK(invalid_choice_song.charts.front().active_branch_selection.empty());
    CHECK(std::ranges::any_of(invalid_choice.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_branch_selection;
    }));
    CHECK(wormedit::validate_document(invalid_choice_song).empty());

    constexpr std::string_view capped_branch_source =
        "#RANDOM 1\n#IF 1\n#00001:01020304050607\n#ENDIF\n#ENDRANDOM\n";
    wormedit::bms_parse_options_t capped_branch_options;
    capped_branch_options.max_chart_resolution = 960;
    capped_branch_options.branch_choices = {{0, 1}};
    auto capped_branch = parse(capped_branch_source, capped_branch_options);
    REQUIRE(capped_branch.song.has_value());
    CHECK(capped_branch.song->charts.front().bgm_source_lanes.empty());
    CHECK(capped_branch.song->charts.front().measures.front().bgm_events.empty());
    CHECK(std::ranges::any_of(capped_branch.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_measure &&
               diagnostic.message.find("channel subdivisions") != std::string::npos;
    }));
    CHECK(wormedit::validate_document(*capped_branch.song).empty());
}

TEST_CASE("BMS fixed RANDOM and SWITCH values drive selected branch projection",
          "[unit][format][bms][control][events]") {
    wormedit::bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    auto result = fixture("bms/control-flow-fixed-values.bms", options);
    REQUIRE(result.song.has_value());
    const auto& song = *result.song;
    const auto& chart = song.charts.front();
    REQUIRE(chart.active_branch_selection.size() == 2);
    CHECK(chart.active_branch_selection[0].selected_value == 2);
    CHECK(chart.active_branch_selection[1].selected_value == 1);
    REQUIRE(chart.measures.front().notes.size() == 3);
    CHECK(chart.measures.front().notes[1].sound == song.resources[1].id);
    CHECK(chart.measures.front().notes[2].sound == song.resources[3].id);
    CHECK(wormedit::validate_document(song).empty());
}

TEST_CASE("BMS notes need a compatible selected profile and conditional payloads stay raw",
          "[unit][format][bms][events]") {
    constexpr std::string_view playable = "#WAV01 key.wav\n#00011:0100\n";
    auto unselected = parse(playable);
    REQUIRE(unselected.song.has_value());
    CHECK(unselected.song->charts.front().measures.front().notes.empty());
    CHECK(std::ranges::any_of(unselected.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unsupported_channel;
    }));

    wormedit::bms_parse_options_t incompatible_options;
    incompatible_options.lane_profile_id = "pms-standard-5button";
    auto incompatible = parse(playable, incompatible_options);
    REQUIRE(incompatible.song.has_value());
    CHECK(incompatible.song->charts.front().measures.front().notes.empty());
    CHECK(std::ranges::any_of(incompatible.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unsupported_channel;
    }));

    auto conditional = parse("#RANDOM 1\n#00001:01\n#ENDRANDOM\n");
    REQUIRE(conditional.song.has_value());
    CHECK(conditional.song->charts.front().bgm_source_lanes.empty());
    CHECK(conditional.song->charts.front().measures.front().bgm_events.empty());
    const auto& tree = *conditional.song->charts.front().bms_control_tree;
    const auto statements = find_kind(tree, wormedit::bms_control_kind_t::raw_statement);
    CHECK(std::ranges::any_of(statements,
                              [](const auto* node) { return node->original_text == "#00001:01"; }));
}

TEST_CASE("BMS unresolved channel references retain unbound source slots",
          "[unit][format][bms][events]") {
    auto result = parse("#BPM 120\n#00001:01\n#00003:7Z\n#00008:02\n#00009:03\n");
    REQUIRE(result.song.has_value());
    const auto& chart = result.song->charts.front();
    CHECK(result.song->resources.empty());
    REQUIRE(chart.measures.front().bgm_events.size() == 1);
    CHECK_FALSE(chart.measures.front().bgm_events.front().sound.has_value());
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::unresolved_reference;
    }));
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_number;
    }));
    CHECK(chart.bpms.size() == 1);
    CHECK(chart.pauses.empty());
}

TEST_CASE("BMS BGA channels materialize with typed events and relinkable missing BMPs",
          "[unit][format][bms][events][resource]") {
    auto result = parse("#BMP01 background.png\n"
                        "#BMP02 poor.png\n"
                        "#BMP03 layer-one.png\n"
                        "#BMP04 layer-two.png\n"
                        "#00004:0100\n"
                        "#00006:0200\n"
                        "#00007:0300\n"
                        "#0000A:0400\n"
                        "#00107:7F00\n"
                        "#0020A:7F00\n");
    REQUIRE(result.song.has_value());
    auto& song = *result.song;
    auto& chart = song.charts.front();
    REQUIRE(chart.bga_events.size() == 6);
    CHECK(chart.bga_events[0].kind == wormedit::visual_event_kind_t::background);
    CHECK(chart.bga_events[1].kind == wormedit::visual_event_kind_t::poor);
    CHECK(chart.bga_events[2].kind == wormedit::visual_event_kind_t::layer);
    CHECK(chart.bga_events[3].kind == wormedit::visual_event_kind_t::layer);
    CHECK(chart.bga_events[4].kind == wormedit::visual_event_kind_t::layer);
    CHECK(chart.bga_events[5].kind == wormedit::visual_event_kind_t::layer);
    CHECK(chart.bga_events[0].resource == song.resources[0].id);
    CHECK(chart.bga_events[1].resource == song.resources[1].id);
    CHECK(chart.bga_events[2].resource == song.resources[2].id);
    CHECK(chart.bga_events[3].resource == song.resources[3].id);
    CHECK(chart.bga_events[4].resource == chart.bga_events[5].resource);
    CHECK(chart.bga_events[0].provenance.source_field.starts_with("04 slot 0"));
    CHECK(chart.bga_events[3].provenance.source_field.starts_with("0A slot 0"));
    CHECK(chart.bga_events[4].position.measure == chart.measures[1].id);
    CHECK(chart.bga_events[5].position.measure == chart.measures[2].id);

    REQUIRE(song.resources.size() == 5);
    const auto& missing = song.resources.back();
    CHECK(missing.name == "Missing BMS BMP 7F");
    CHECK(missing.missing);
    CHECK(missing.roles == std::vector{wormedit::resource_role_t::bga});
    REQUIRE(missing.target_identities.size() == 1);
    CHECK(missing.target_identities.front().identifier == "7F");
    CHECK(missing.target_identities.front().namespace_name == "BMP");
    const auto asset =
        std::ranges::find(song.visual_assets, missing.asset, &wormedit::visual_asset_t::id);
    REQUIRE(asset != song.visual_assets.end());
    CHECK(asset->kind == wormedit::asset_kind_t::image);
    CHECK(asset->source_path.empty());
    CHECK(wormedit::validate_document(song).empty());

    REQUIRE(chart.bms_control_tree.has_value());
    const auto& tree = *chart.bms_control_tree;
    const auto raw = find_kind(tree, wormedit::bms_control_kind_t::raw_statement);
    for (const auto* expected : {"#00004:0100", "#00006:0200", "#00007:0300", "#0000A:0400",
                                 "#00107:7F00", "#0020A:7F00"}) {
        CHECK(std::ranges::any_of(raw, [expected](const auto* statement) {
            return statement->original_text == expected &&
                   statement->source_span.byte_end > statement->source_span.byte_begin;
        }));
    }
    CHECK(std::ranges::count_if(result.diagnostics, [](const auto& diagnostic) {
              return diagnostic.code ==
                         wormedit::bms_parse_diagnostic_code_t::unresolved_reference &&
                     diagnostic.message.find("BMP7F") != std::string::npos;
          }) == 1);
}

TEST_CASE("BMS reader grows measures through the largest channel index", "[unit][format][bms]") {
    auto result = fixture("bms/custom-channel.bme");
    REQUIRE(result.song.has_value());
    REQUIRE(result.song->charts.size() == 1);
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.measures.size() == 1);
    CHECK((chart.measures[0].definition.length == wormedit::rational_t{1, 1}));

    auto high_measure = parse("#TITLE high\n#99911:01\n");
    REQUIRE(high_measure.song.has_value());
    REQUIRE(high_measure.song->charts[0].measures.size() == 1000);
    CHECK((high_measure.song->charts[0].measures[999].definition.length ==
           wormedit::rational_t{1, 1}));
}

TEST_CASE("BMS control flow is retained as nested source nodes with closing provenance",
          "[unit][format][bms][control]") {
    auto result = fixture("bms/control-flow-mixed.bms");
    REQUIRE(result.song.has_value());
    REQUIRE(result.song->charts[0].bms_control_tree.has_value());
    const auto& root = *result.song->charts[0].bms_control_tree;
    const auto randoms = find_kind(root, wormedit::bms_control_kind_t::random);
    REQUIRE(randoms.size() == 2);
    CHECK(randoms[0]->value == 2);
    CHECK(randoms[0]->close_text == "#ENDRANDOM");
    CHECK(randoms[0]->close_source_order.has_value());
    CHECK(randoms[0]->close_line_ending == wormedit::bms_line_ending_t::lf);

    const auto groups = find_kind(root, wormedit::bms_control_kind_t::if_group);
    REQUIRE(groups.size() == 2);
    const auto branches = find_kind(root, wormedit::bms_control_kind_t::if_branch);
    REQUIRE(branches.size() == 2);
    CHECK(branches[0]->value == 1);
    const auto else_if = find_kind(root, wormedit::bms_control_kind_t::else_if_branch);
    REQUIRE(else_if.size() == 1);
    CHECK(else_if[0]->value == 2);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::else_branch).size() == 1);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::switch_block).size() == 1);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::case_branch).size() == 2);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::default_branch).size() == 1);
    CHECK(find_kind(root, wormedit::bms_control_kind_t::skip).size() == 1);

    auto fixed = fixture("bms/control-flow-fixed-values.bms");
    REQUIRE(fixed.song.has_value());
    const auto& fixed_root = *fixed.song->charts[0].bms_control_tree;
    REQUIRE(find_kind(fixed_root, wormedit::bms_control_kind_t::set_random).size() == 1);
    REQUIRE(find_kind(fixed_root, wormedit::bms_control_kind_t::set_switch).size() == 1);

    const auto source_bytes =
        read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bms/control-flow-mixed.bms");
    const auto spans = physical_source_spans(root);
    REQUIRE_FALSE(spans.empty());
    std::size_t cursor{};
    for (const auto span : spans) {
        CHECK(span.byte_begin == cursor);
        CHECK(span.byte_end > span.byte_begin);
        cursor = span.byte_end;
    }
    CHECK(cursor == source_bytes.size());
}

TEST_CASE("BMS reader preserves malformed and orphan source with located diagnostics",
          "[unit][format][bms][recovery]") {
    auto malformed = fixture("bms/negative-orphan-control.bms");
    REQUIRE(malformed.song.has_value());
    CHECK(std::ranges::any_of(malformed.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::malformed_control &&
               diagnostic.line > 0 && diagnostic.source_span.has_value();
    }));
    const auto& malformed_root = *malformed.song->charts[0].bms_control_tree;
    const auto malformed_source = source_lines(malformed_root);
    CHECK(std::ranges::find(malformed_source, "#ENDRANDOM") != malformed_source.end());

    constexpr std::string_view source = "#TITLE recover\r\nnot a command\n#00002:bad\r";
    auto recovered = parse(source);
    REQUIRE(recovered.song.has_value());
    CHECK(recovered.has_errors());
    CHECK_FALSE(std::ranges::any_of(recovered.diagnostics,
                                    [](const auto& diagnostic) { return diagnostic.line == 2; }));
    CHECK(std::ranges::any_of(recovered.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::invalid_measure &&
               diagnostic.line == 3;
    }));
    const auto& root = *recovered.song->charts[0].bms_control_tree;
    const auto raw = find_kind(root, wormedit::bms_control_kind_t::raw_statement);
    const auto retained = std::ranges::find_if(
        raw, [](const auto* node) { return node->original_text == "not a command"; });
    REQUIRE(retained != raw.end());
    CHECK((*retained)->line_ending == wormedit::bms_line_ending_t::lf);
    CHECK((*retained)->source_span.byte_end - (*retained)->source_span.byte_begin == 14);
    const auto endings = find_kind(root, wormedit::bms_control_kind_t::raw_statement);
    CHECK(std::ranges::any_of(endings, [](const auto* node) {
        return node->original_text == "#TITLE recover" &&
               node->line_ending == wormedit::bms_line_ending_t::crlf;
    }));

    auto odd_pairs = fixture("bms/negative-malformed-pairs.bms");
    REQUIRE(odd_pairs.song.has_value());
    CHECK(std::ranges::any_of(odd_pairs.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::malformed_line &&
               diagnostic.message.find("odd number") != std::string::npos;
    }));
}

TEST_CASE("BMS selected branches activate source-ordered definitions and ratios",
          "[unit][format][bms][control][events]") {
    constexpr std::string_view source =
        "#TITLE root\n#BPM 120\n#WAV99 root.wav\n#RANDOM 2\n#IF 1\n"
        "#TITLE selected\n#WAV01 selected.wav\n#BPM01 180\n#STOP01 48\n"
        "#00002:0.75\n#00001:01\n#00008:01\n#00009:01\n#00011:01\n#ELSE\n"
        "#TITLE alternate\n#WAV01 alternate.wav\n#BPM01 210\n#STOP01 72\n"
        "#00002:0.5\n#00001:01\n#00008:01\n#00009:01\n#00011:01\n"
        "#ENDIF\n#ENDRANDOM\n";

    wormedit::bms_parse_options_t selected_options;
    selected_options.lane_profile_id = "bme-7k-outside-scratch";
    selected_options.branch_choices = {{3, 1}};
    auto selected = parse(source, selected_options);
    REQUIRE(selected.song.has_value());
    const auto& selected_song = *selected.song;
    const auto& selected_chart = selected_song.charts.front();
    CHECK(selected_song.metadata.title == "selected");
    REQUIRE(selected_song.resources.size() == 2);
    CHECK(selected_song.resources[0].name == "root.wav");
    CHECK(selected_song.resources[1].name == "selected.wav");
    REQUIRE(selected_chart.measures.size() == 1);
    CHECK((selected_chart.measures[0].definition.length == wormedit::rational_t{3, 4}));
    REQUIRE(selected_chart.measures[0].bgm_events.size() == 1);
    CHECK(selected_chart.measures[0].bgm_events.front().sound == selected_song.resources[1].id);
    REQUIRE(selected_chart.measures[0].notes.size() == 1);
    CHECK(selected_chart.measures[0].notes.front().sound == selected_song.resources[1].id);
    const auto selected_bpm = std::ranges::find_if(selected_chart.bpms, [](const auto& event) {
        return event.beats_per_minute == wormedit::signed_rational_t{180, 1};
    });
    REQUIRE(selected_bpm != selected_chart.bpms.end());
    REQUIRE(selected_chart.pauses.size() == 1);
    CHECK((selected_chart.pauses.front().duration == wormedit::signed_rational_t{48, 1}));
    CHECK(wormedit::validate_document(selected_song).empty());
    CHECK_FALSE(std::ranges::any_of(selected.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("occurs inside conditional control flow and remains raw") !=
               std::string::npos;
    }));

    auto alternate_options = selected_options;
    alternate_options.branch_choices = {{3, 2}};
    auto alternate = parse(source, alternate_options);
    REQUIRE(alternate.song.has_value());
    const auto& alternate_song = *alternate.song;
    const auto& alternate_chart = alternate_song.charts.front();
    CHECK(alternate_song.metadata.title == "alternate");
    REQUIRE(alternate_song.resources.size() == 2);
    CHECK(alternate_song.resources[1].name == "alternate.wav");
    CHECK((alternate_chart.measures[0].definition.length == wormedit::rational_t{1, 2}));
    REQUIRE(alternate_chart.measures[0].bgm_events.size() == 1);
    CHECK(alternate_chart.measures[0].bgm_events.front().sound == alternate_song.resources[1].id);
    const auto alternate_bpm = std::ranges::find_if(alternate_chart.bpms, [](const auto& event) {
        return event.beats_per_minute == wormedit::signed_rational_t{210, 1};
    });
    REQUIRE(alternate_bpm != alternate_chart.bpms.end());
    REQUIRE(alternate_chart.pauses.size() == 1);
    CHECK((alternate_chart.pauses.front().duration == wormedit::signed_rational_t{72, 1}));
    CHECK(wormedit::validate_document(alternate_song).empty());

    auto unselected_options = selected_options;
    unselected_options.branch_choices.clear();
    auto unselected = parse(source, unselected_options);
    REQUIRE(unselected.song.has_value());
    const auto& unselected_song = *unselected.song;
    const auto& unselected_chart = unselected_song.charts.front();
    CHECK(unselected_song.metadata.title == "root");
    REQUIRE(unselected_song.resources.size() == 1);
    CHECK(unselected_song.resources.front().name == "root.wav");
    CHECK((unselected_chart.measures[0].definition.length == wormedit::rational_t{1, 1}));
    CHECK(unselected_chart.measures[0].bgm_events.empty());
    CHECK(unselected_chart.pauses.empty());
    CHECK(std::ranges::none_of(unselected_chart.bpms, [](const auto& event) {
        return event.beats_per_minute == wormedit::signed_rational_t{180, 1} ||
               event.beats_per_minute == wormedit::signed_rational_t{210, 1};
    }));
    CHECK(wormedit::validate_document(unselected_song).empty());
}

TEST_CASE("BMS parser source, line and nesting limits are configurable",
          "[unit][format][bms][limits]") {
    auto source_limited = parse("#TITLE a\n", {.max_source_bytes = 3});
    CHECK_FALSE(source_limited.song.has_value());
    CHECK_FALSE(source_limited.encoding.original_bytes);

    auto line_limited = parse("#TITLE too long\n", {.max_line_bytes = 4});
    CHECK_FALSE(line_limited.song.has_value());
    CHECK(line_limited.diagnostics[0].code ==
          wormedit::bms_parse_diagnostic_code_t::malformed_line);

    auto nesting_limited =
        parse("#RANDOM 1\n#RANDOM 2\n#ENDRANDOM\n#ENDRANDOM\n", {.max_nesting_depth = 1});
    REQUIRE(nesting_limited.song.has_value());
    CHECK(std::ranges::any_of(nesting_limited.diagnostics, [](const auto& diagnostic) {
        return diagnostic.message.find("depth limit") != std::string::npos;
    }));

    auto lines_limited = parse("\n\n", {.max_lines = 1});
    CHECK_FALSE(lines_limited.song.has_value());
    REQUIRE_FALSE(lines_limited.diagnostics.empty());
    CHECK(lines_limited.diagnostics[0].message.find("physical line limit") != std::string::npos);
}

TEST_CASE("BMS parser retains malformed terminal branch ordering as raw source",
          "[unit][format][bms][control][recovery]") {
    constexpr std::string_view source = "#IF 1\n#ELSE\n#ELSE\n#ELIF 2\n#ENDIF\n"
                                        "#SWITCH 2\n#DEF\n#DEF\n#CASE 1\n#ENDSW\n";
    auto result = parse(source);
    REQUIRE(result.song.has_value());
    const auto& root = *result.song->charts[0].bms_control_tree;
    const auto raw = source_lines(root);
    CHECK(std::ranges::count(raw, "#ELSE") == 2);
    CHECK(std::ranges::find(raw, "#ELIF 2") != raw.end());
    CHECK(std::ranges::count(raw, "#DEF") == 2);
    CHECK(std::ranges::find(raw, "#CASE 1") != raw.end());
    CHECK(std::ranges::count_if(result.diagnostics, [](const auto& diagnostic) {
              return diagnostic.code == wormedit::bms_parse_diagnostic_code_t::malformed_control;
          }) == 4);
}

TEST_CASE("BMS reader handles ASCII, BOM UTF-8, and caller-selected legacy codecs",
          "[unit][format][bms][encoding]") {
    auto ascii = fixture("bms/encoding/ascii-ambiguous.bms");
    REQUIRE(ascii.song.has_value());
    CHECK(ascii.encoding.detected == wormedit::text_encoding_t::ascii);
    CHECK_FALSE(ascii.encoding.user_override.has_value());

    auto utf8 = fixture("bms/encoding/utf8-bom.bms");
    REQUIRE(utf8.song.has_value());
    CHECK(utf8.encoding.detected == wormedit::text_encoding_t::utf8);
    CHECK(utf8.song->metadata.title == "UTF-8 BOM / 雪");
    REQUIRE(utf8.encoding.original_bytes);
    CHECK((*utf8.encoding.original_bytes)[0] == std::byte{0xef});

    const auto parse_with = [](std::string_view name, wormedit::text_encoding_t encoding) {
        const auto bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / name);
        return wormedit::parse_bms_bytes(bytes, {.encoding_override = encoding});
    };
    auto cp932 = parse_with("bms/encoding/cp932-shift-jis.bms", wormedit::text_encoding_t::cp932);
    if (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::cp932)) {
        REQUIRE(cp932.song.has_value());
        CHECK(cp932.song->metadata.title == "日本語譜面");
        CHECK(cp932.song->metadata.artist == "音楽家");
        CHECK(cp932.encoding.detected == wormedit::text_encoding_t::cp932);
        REQUIRE(cp932.encoding.original_bytes);
        CHECK_FALSE(cp932.encoding.original_bytes->empty());
    } else {
        CHECK_FALSE(cp932.song.has_value());
        REQUIRE_FALSE(cp932.diagnostics.empty());
        CHECK(cp932.diagnostics[0].code ==
              wormedit::bms_parse_diagnostic_code_t::unsupported_encoding);
    }

    auto euc_kr = parse_with("bms/encoding/euc-kr.bms", wormedit::text_encoding_t::euc_kr);
    if (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::euc_kr)) {
        REQUIRE(euc_kr.song.has_value());
        CHECK(euc_kr.song->metadata.title == "한국어");
    } else {
        CHECK_FALSE(euc_kr.song.has_value());
    }
    auto cp949 = parse_with("bms/encoding/cp949-extension.bms", wormedit::text_encoding_t::cp949);
    if (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::cp949)) {
        REQUIRE(cp949.song.has_value());
        CHECK(cp949.song->metadata.title == "뷁");
    } else {
        CHECK_FALSE(cp949.song.has_value());
    }
    auto windows =
        parse_with("bms/encoding/windows-1252.bms", wormedit::text_encoding_t::windows_1252);
    if (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::windows_1252)) {
        REQUIRE(windows.song.has_value());
        CHECK(windows.song->metadata.title == "café déjà");
        CHECK(windows.song->metadata.artist == "Björk");
    } else {
        CHECK_FALSE(windows.song.has_value());
    }

    const auto ambiguous = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) /
                                       "bms/encoding/cp932-shift-jis.bms");
    auto no_override = wormedit::parse_bms_bytes(ambiguous);
    CHECK_FALSE(no_override.song.has_value());
    REQUIRE(no_override.encoding.original_bytes);
    CHECK(*no_override.encoding.original_bytes == ambiguous);
    CHECK(no_override.diagnostics[0].code ==
          wormedit::bms_parse_diagnostic_code_t::invalid_encoding);
}

TEST_CASE("Malformed BOM UTF-8 and explicit legacy sequences fail without fallback",
          "[unit][format][bms][encoding]") {
    auto invalid_utf8 = fixture("bms/encoding/invalid-utf8-after-bom.bms");
    CHECK_FALSE(invalid_utf8.song.has_value());
    REQUIRE_FALSE(invalid_utf8.diagnostics.empty());
    CHECK(invalid_utf8.diagnostics[0].code ==
          wormedit::bms_parse_diagnostic_code_t::invalid_encoding);
    REQUIRE(invalid_utf8.encoding.original_bytes);
    CHECK(invalid_utf8.encoding.original_bytes->size() > 3);

    const auto cp932_bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) /
                                         "bms/encoding/invalid-cp932-truncated-lead.bms");
    auto invalid_cp932 = wormedit::parse_bms_bytes(
        cp932_bytes, {.encoding_override = wormedit::text_encoding_t::cp932});
    CHECK_FALSE(invalid_cp932.song.has_value());
    REQUIRE_FALSE(invalid_cp932.diagnostics.empty());
    CHECK(invalid_cp932.diagnostics[0].code ==
          (wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::cp932)
               ? wormedit::bms_parse_diagnostic_code_t::invalid_encoding
               : wormedit::bms_parse_diagnostic_code_t::unsupported_encoding));

    constexpr std::array bom_bytes{std::byte{0xef}, std::byte{0xbb}, std::byte{0xbf},
                                   std::byte{'#'},  std::byte{'T'},  std::byte{'I'},
                                   std::byte{'T'},  std::byte{'L'},  std::byte{'E'},
                                   std::byte{' '},  std::byte{0xe9}};
    auto conflicting_override = wormedit::parse_bms_bytes(
        bom_bytes, {.encoding_override = wormedit::text_encoding_t::windows_1252});
    CHECK_FALSE(conflicting_override.song.has_value());
    CHECK(conflicting_override.diagnostics[0].code ==
          wormedit::bms_parse_diagnostic_code_t::invalid_encoding);
}

TEST_CASE("WAV and BMP declarations retain unresolved relative asset paths",
          "[unit][format][bms][resource]") {
    auto result = parse("#TITLE art\n#WAV01 clips/voice.ogg\n#BMP01 bg/picture.png\n#00011:01\n");
    REQUIRE(result.song.has_value());
    REQUIRE(result.song->audio_assets.size() == 1);
    REQUIRE(result.song->visual_assets.size() == 1);
    REQUIRE(result.song->resources.size() == 2);
    CHECK(result.song->audio_assets[0].source_path == std::filesystem::path("clips/voice.ogg"));
    CHECK(result.song->visual_assets[0].source_path == std::filesystem::path("bg/picture.png"));
    CHECK_FALSE(result.song->resources[0].missing);
    CHECK(result.song->resources[1].roles == std::vector{wormedit::resource_role_t::bga});
    REQUIRE(result.song->resources[0].target_identities.size() == 1);
    REQUIRE(result.song->resources[1].target_identities.size() == 1);
    CHECK(result.song->resources[0].target_identities[0].namespace_name == "WAV");
    CHECK(result.song->resources[1].target_identities[0].namespace_name == "BMP");

    auto unicode =
        parse("#WAV01 caf\xc3\xa9.ogg\n", {.encoding_override = wormedit::text_encoding_t::utf8});
    REQUIRE(unicode.song.has_value());
    REQUIRE(unicode.song->audio_assets.size() == 1);
    CHECK(unicode.song->audio_assets[0].source_path ==
          std::filesystem::path(std::u8string{u8"café.ogg"}));
}

TEST_CASE("BMS import materializes complete primary keys and scratch without channel 17",
          "[unit][formats][bms][regression]") {
    const auto result = parse("#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n",
        {.file_extension = ".bms"});
    REQUIRE(result.song);
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.lane_layouts.size() == 1);
    CHECK(chart.lane_layouts.front().layout.profile_id == "bms-5k-outside-scratch-free-zone");
    CHECK(chart.measures.front().notes.size() == 6);
    CHECK(chart.measures.back().notes.size() == 1);
}

TEST_CASE("BML import materializes the standard nine-button channel pattern",
          "[unit][formats][bms][regression]") {
    const auto result = parse("#PLAYER 3\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n"
        "#00022:01\n#00023:01\n#00024:01\n#00025:01\n#04011:01\n",
        {.file_extension = ".bml"});
    REQUIRE(result.song);
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.lane_layouts.size() == 1);
    CHECK(chart.lane_layouts.front().layout.profile_id == "pms-standard-9button");
    CHECK(chart.measures.front().notes.size() == 9);
    CHECK(chart.measures.back().notes.size() == 1);
}
