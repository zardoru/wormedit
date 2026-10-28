#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/timing.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

auto cross_measure_hold_song(wormedit::stepmania_profile_t profile)
    -> wormedit::song_document_t {
    const std::string header = profile == wormedit::stepmania_profile_t::sm
        ? "#NOTES:dance-single::Hard:1:0,0,0,0,0:"
        : "#VERSION:0.83;#NOTEDATA:;#STEPSTYPE:dance-single;#DIFFICULTY:Hard;#METER:1;#NOTES:";
    const std::string source = "#TITLE:Cross measure holds;#BPMS:0=120;" + header +
        "\n2400\n0000\n0000\n0000\n,\n0000\n0000\n3300\n0000\n;";
    auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}), {.profile = profile});
    REQUIRE_FALSE(!parsed.ok());
    REQUIRE(parsed.value.has_value());
    return std::move(*parsed.value);
}

void check_cross_measure_hold(const wormedit::note_t& note,
                              wormedit::measure_id_t head, wormedit::measure_id_t tail) {
    CHECK(note.position.measure == head);
    REQUIRE(note.end.has_value());
    CHECK(note.end->measure == tail);
}

void check_cross_measure_holds(const wormedit::song_document_t& song) {
    REQUIRE(wormedit::validate_document(song).empty());
    const auto& chart = song.charts.front();
    REQUIRE(chart.measures.size() == 2);
    REQUIRE(chart.measures.front().notes.size() == 2);
    CHECK(chart.measures.back().notes.empty());
    const auto& notes = chart.measures.front().notes;
    CHECK(notes[0].kind == wormedit::note_kind_t::hold);
    CHECK(notes[1].kind == wormedit::note_kind_t::roll);
    for (const auto& note : notes)
        check_cross_measure_hold(note, chart.measures.front().id, chart.measures.back().id);
}

auto read_binary(const std::filesystem::path& path) -> std::vector<std::byte> {
    std::ifstream stream(path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes;
    bytes.reserve(source.size());
    for (const auto character : source)
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    return bytes;
}

auto fixture(std::string_view name, wormedit::stepmania_profile_t profile,
             wormedit::stepmania_timing_syntax_t syntax =
                 wormedit::stepmania_timing_syntax_t::unspecified,
             wormedit::stepmania_implementation_profile_t implementation =
                 wormedit::stepmania_implementation_profile_t::unspecified)
    -> wormedit::stepmania_parse_outcome_t {
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = profile;
    options.source_timing_syntax = syntax;
    options.source_implementation_profile = implementation;
    return wormedit::parse_stepmania_bytes(
        read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "stepmania" / name), options);
}

auto parse_ssc(std::string_view source,
               wormedit::stepmania_timing_syntax_t syntax =
                   wormedit::stepmania_timing_syntax_t::unspecified,
               wormedit::stepmania_implementation_profile_t implementation =
                   wormedit::stepmania_implementation_profile_t::unspecified)
    -> wormedit::stepmania_parse_outcome_t {
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::ssc;
    options.source_timing_syntax = syntax;
    options.source_implementation_profile = implementation;
    return wormedit::parse_stepmania_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                           options);
}

auto at(wormedit::local_position_t position, std::uint64_t measure, std::uint64_t row) -> bool {
    return position.measure.value == measure && position.row == row;
}

auto roundtrip(const wormedit::song_document_t& song, wormedit::stepmania_profile_t profile)
    -> wormedit::stepmania_parse_outcome_t {
    const auto output = wormedit::write_stepmania(song, {.profile = profile});
    REQUIRE(output.value.has_value());
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = profile;
    return wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{output.value->data(), output.value->size()}), options);
}

auto has_dance_single_arrows(const wormedit::chart_document_t& chart) -> bool {
    if (chart.lane_layouts.empty()) return false;
    const auto& lanes = chart.lane_layouts.front().layout.lanes;
    return lanes.size() == 4 && lanes[0].glyph == wormedit::lane_glyph_t::arrow_left &&
           lanes[1].glyph == wormedit::lane_glyph_t::arrow_down &&
           lanes[2].glyph == wormedit::lane_glyph_t::arrow_up &&
           lanes[3].glyph == wormedit::lane_glyph_t::arrow_right;
}

auto sm_music_round_trips_once() -> bool {
    constexpr std::string_view source =
        "#MUSIC:music\\song.ogg;#OFFSET:0.125;#BPMS:0=120;#NOTES:\n"
        "dance-single:\nTest:\nHard:\n5:\n0,0,0,0,0:\n1000\n0000\n0000\n0000\n;";
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::sm;
    const auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}), options);
    if (!parsed.value || parsed.value->resources.size() != 1 ||
        parsed.value->audio_assets.size() != 1)
        return false;
    const auto& resource = parsed.value->resources.front();
    if (resource.name != "music\\song.ogg" || resource.roles.size() != 1 ||
        resource.roles.front() != wormedit::resource_role_t::music ||
        parsed.value->audio_assets.front().source_path != "music/song.ogg" ||
        parsed.value->charts.empty() ||
        parsed.value->charts.front().beat_zero_audio_offset_seconds !=
            wormedit::signed_rational_t{-1, 8})
        return false;
    const auto written = wormedit::write_stepmania(
        *parsed.value, {.profile = wormedit::stepmania_profile_t::sm});
    if (!written.value || written.value->find("#MUSIC:music\\song.ogg;") == std::string::npos)
        return false;
    const auto first_music = written.value->find("#MUSIC:");
    if (written.value->find("#MUSIC:", first_music + 1) != std::string::npos)
        return false;
    const auto round_tripped = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{written.value->data(), written.value->size()}), options);
    return round_tripped.value && round_tripped.value->resources.size() == 1 &&
           round_tripped.value->audio_assets.size() == 1 &&
           round_tripped.value->audio_assets.front().source_path == "music/song.ogg";
}

auto dance_layout_case_folding_and_solo_glyphs() -> bool {
    constexpr std::string_view single =
        "#BPMS:0=120;#NOTES:\nDANCE-SINGLE:\nTest:\nHard:\n5:\n0,0,0,0,0:\n"
        "1000\n0000\n0000\n0000\n;";
    const auto parsed_single = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{single.data(), single.size()}));
    if (!parsed_single.value || !has_dance_single_arrows(parsed_single.value->charts.front()))
        return false;
    constexpr std::string_view solo =
        "#BPMS:0=120;#NOTES:\ndance-solo:\nTest:\nHard:\n5:\n0,0,0,0,0,0:\n"
        "100000\n000000\n000000\n000000\n;";
    const auto parsed_solo = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{solo.data(), solo.size()}));
    if (!parsed_solo.value) return false;
    const auto& lanes = parsed_solo.value->charts.front().lane_layouts.front().layout.lanes;
    constexpr std::array expected{wormedit::lane_glyph_t::arrow_left,
                                  wormedit::lane_glyph_t::arrow_up_left,
                                  wormedit::lane_glyph_t::arrow_down,
                                  wormedit::lane_glyph_t::arrow_up,
                                  wormedit::lane_glyph_t::arrow_up_right,
                                  wormedit::lane_glyph_t::arrow_right};
    return lanes.size() == expected.size() &&
           std::ranges::equal(lanes, expected, {}, &wormedit::lane_definition_t::glyph);
}

auto unsafe_stepmania_music_is_retained_unbound() -> bool {
    constexpr std::string_view source =
        "#MUSIC:../outside.ogg;#BPMS:0=120;#NOTES:\n"
        "dance-single:\nTest:\nHard:\n5:\n0,0,0,0,0:\n1000\n0000\n0000\n0000\n;";
    const auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}));
    if (!parsed.value || !parsed.value->resources.empty() || !parsed.value->audio_assets.empty())
        return false;
    const auto group = std::ranges::find(parsed.value->metadata.format_groups,
                                         wormedit::format_family_t::stepmania,
                                         &wormedit::format_metadata_t::family);
    if (group == parsed.value->metadata.format_groups.end()) return false;
    const auto music = std::ranges::find(group->ordered_fields, std::string{"MUSIC"},
                                         &std::pair<std::string, std::string>::first);
    if (music == group->ordered_fields.end() || music->second != "../outside.ogg") return false;
    const auto written = wormedit::write_stepmania(*parsed.value);
    if (!written.value) return false;
    constexpr std::string_view retained = "#MUSIC:../outside.ogg;";
    const auto first = written.value->find(retained);
    return first != std::string::npos && written.value->find(retained, first + 1) == std::string::npos;
}

auto sm_source_orders_allow_history_and_native_roundtrip(std::string_view source) -> bool {
    wormedit::stepmania_parse_options_t options;
    options.profile = wormedit::stepmania_profile_t::sm;
    const auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}), options);
    if (!parsed.value || parsed.value->charts.size() != 1 ||
        !wormedit::validate_document(*parsed.value).empty()) return false;
    auto bpm_history = wormedit::document_history_t::from(*parsed.value);
    const auto bpm_revision = bpm_history.document.revision;
    const auto& bpm = bpm_history.document.charts.front().bpms.front();
    const auto bpm_edit = wormedit::apply_bpm_edit(
        bpm_history, wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                         .chart = bpm_history.document.charts.front().id,
                         .id = bpm.id,
                         .value = wormedit::bpm_edit_value_t::from(
                             bpm.position, wormedit::timing_bpm_value_t{{110, 1}})});
    if (!bpm_edit || bpm_history.document.revision != bpm_revision + 1 ||
        !wormedit::undo_document_command(bpm_history)) return false;

    auto drag_history = wormedit::document_history_t::from(*parsed.value);
    const auto drag_revision = drag_history.document.revision;
    const auto& chart = drag_history.document.charts.front();
    const auto begun = wormedit::beat_interval_drag_t::propose(drag_history.document, chart.id,
        chart.bpms.front().id, {chart.measures.front().id, 240}, 48'000);
    if (!begun.value.has_value()) return false;
    const auto proposal = begun.value->revise(drag_history.document, {}, 48'000);
    if (!proposal.value.has_value()) return false;
    const auto committed = wormedit::apply_proposal(drag_history, *proposal.value);
    if (!committed || drag_history.document.revision != drag_revision + 1 ||
        !wormedit::undo_document_command(drag_history) ||
        drag_history.document.charts.front().bpms.front().beats_per_minute !=
            wormedit::signed_rational_t{120, 1}) return false;
    const auto native_bytes = wormedit::write_native_project(*parsed.value);
    return native_bytes.value &&
           wormedit::parse_native_project(*native_bytes.value).value.has_value();
}

} // namespace

TEST_CASE("StepMania matched timing and chart notes parse transactionally",
          "[unit][format][stepmania]") {
    SECTION("SM song timing scope") {
        const auto result = fixture("matched-timing.sm", wormedit::stepmania_profile_t::sm);
        REQUIRE(result.value.has_value());
        REQUIRE_FALSE(!result.ok());
        REQUIRE(result.value->charts.size() == 1);
        const auto& chart = result.value->charts.front();
        CHECK((chart.beat_zero_audio_offset_seconds == wormedit::signed_rational_t{-1, 8}));
        const auto timing = wormedit::timing_index_t::from(chart);
        REQUIRE(timing.value.has_value());
        const auto beat_zero = timing.value->seconds_at(0);
        REQUIRE(beat_zero.seconds.has_value());
        CHECK((*beat_zero.seconds == wormedit::signed_rational_t{-1, 8}));
        REQUIRE(chart.bpms.size() == 2);
        CHECK(at(chart.bpms[0].position, 1, 0));
        CHECK(at(chart.bpms[1].position, 3, 0));
        REQUIRE(chart.pauses.size() == 2);
        CHECK(chart.pauses[0].kind == wormedit::pause_kind_t::stop);
        CHECK(at(chart.pauses[0].position, 1, 480));
        CHECK(chart.pauses[1].kind == wormedit::pause_kind_t::delay);
        CHECK(at(chart.pauses[1].position, 1, 720));
        REQUIRE(chart.warps.size() == 1);
        CHECK(chart.warps[0].source_encoding == wormedit::warp_provenance_t::native);
        CHECK(at(chart.warps[0].span.begin, 2, 0));
        CHECK(at(chart.warps[0].span.end, 2, 480));
        REQUIRE(chart.scrolls.size() == 2);
        REQUIRE(chart.speeds.size() == 2);
        CHECK(chart.measures.size() == 3);
        CHECK(chart.measures[0].notes.size() == 3);
        CHECK(chart.measures[1].notes.size() == 1);
        CHECK(has_dance_single_arrows(chart));
    }
    SECTION("SSC song fields and chart block remain separately addressable") {
        const auto result = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
        REQUIRE(result.value.has_value());
        REQUIRE_FALSE(!result.ok());
        REQUIRE(result.value->metadata.format_groups.size() == 1);
        REQUIRE(result.value->charts.size() == 1);
        const auto& chart = result.value->charts.front();
        REQUIRE(chart.metadata.format_groups.size() == 1);
        CHECK(chart.metadata.format_groups.front().ordered_fields.front().first == "NOTEDATA");
        CHECK(chart.bpms.size() == 2);
        CHECK(chart.warps.size() == 1);
        CHECK(chart.speeds.size() == 2);
    }
}

TEST_CASE("single-chart SM source orders validate and support undoable timing edits",
          "[unit][format][stepmania][metadata][beat-fit]") {
    constexpr std::string_view source =
        "#MUSIC:song.ogg;#BPMS:0=120;#NOTES:\n"
        "dance-single:\nTest:\nHard:\n5:\n0,0,0,0,0:\n"
        "1000\n0000\n0000\n0000\n;";
    CHECK(sm_source_orders_allow_history_and_native_roundtrip(source));
}

TEST_CASE("StepMania dance layouts fold step type case and map solo arrows",
          "[unit][format][stepmania][layout]") {
    CHECK(dance_layout_case_folding_and_solo_glyphs());
}

TEST_CASE("SM MUSIC imports once and round-trips as the modeled music resource",
          "[unit][format][stepmania][resource]") {
    CHECK(sm_music_round_trips_once());
}

TEST_CASE("unsafe StepMania MUSIC is retained as metadata without binding a resource",
          "[unit][format][stepmania][resource]") {
    CHECK(unsafe_stepmania_music_is_retained_unbound());
}

TEST_CASE("StepMania writer omits removed MUSIC and emits retained MUSIC once",
          "[unit][format][stepmania][resource]") {
    constexpr std::string_view source =
        "#MUSIC:song.ogg;#BPMS:0=120;#NOTES:\n"
        "dance-single:\nTest:\nHard:\n5:\n0,0,0,0,0:\n1000\n0000\n0000\n0000\n;";
    auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}));
    REQUIRE(parsed.value);
    auto& song = *parsed.value;
    const auto unchanged = wormedit::write_stepmania(song);
    REQUIRE(unchanged.value);
    CHECK(unchanged.value->find("#MUSIC:song.ogg;") != std::string::npos);
    const auto music_at = unchanged.value->find("#MUSIC:");
    CHECK(unchanged.value->find("#MUSIC:", music_at + 1) == std::string::npos);
    song.resources.clear();
    song.audio_assets.clear();
    const auto removed = wormedit::write_stepmania(song);
    REQUIRE(removed.value);
    CHECK(removed.value->find("#MUSIC:") == std::string::npos);
}

TEST_CASE("StepMania fixture preserves 5.1 same-row timing phases",
          "[unit][format][stepmania][timing][discontinuity]") {
    const auto parsed = fixture("discontinuity-order.ssc", wormedit::stepmania_profile_t::ssc,
                                wormedit::stepmania_timing_syntax_t::native_warps);
    REQUIRE(parsed.value.has_value());
    REQUIRE_FALSE(!parsed.ok());
    const auto& chart = parsed.value->charts.front();
    const auto timing = wormedit::timing_index_t::from(chart);
    REQUIRE(timing.value);
    CHECK(timing.value->discontinuity_profile ==
          wormedit::timing_discontinuity_profile_t::stepmania_5_1);

    const auto built =
        wormedit::build_preview_schedule(chart, *timing.value, {.sample_rate = 48'000});
    REQUIRE(built);
    std::vector<wormedit::preview_discontinuity_kind_t> before_delay;
    std::vector<wormedit::preview_discontinuity_kind_t> at_event;
    for (const auto& event : built.schedule->discontinuities) {
        if (event.sample_frame == 48'000) {
            before_delay.push_back(event.kind);
        } else if (event.sample_frame == 60'000) {
            at_event.push_back(event.kind);
        }
    }
    REQUIRE(before_delay.size() == 2);
    CHECK(before_delay[0] == wormedit::preview_discontinuity_kind_t::bpm);
    CHECK(before_delay[1] == wormedit::preview_discontinuity_kind_t::delay);
    REQUIRE(at_event.size() == 2);
    CHECK(at_event[0] == wormedit::preview_discontinuity_kind_t::metronome);
    CHECK(at_event[1] == wormedit::preview_discontinuity_kind_t::stop);
}

TEST_CASE("StepMania legacy negative timing encodings become canonical beat warps",
          "[unit][format][stepmania]") {
    const auto bpm = fixture("legacy-negative-bpm.sm", wormedit::stepmania_profile_t::sm,
                             wormedit::stepmania_timing_syntax_t::legacy_negative_bpm);
    REQUIRE(bpm.value.has_value());
    REQUIRE_FALSE(!bpm.ok());
    REQUIRE(bpm.value->charts.front().bpms.size() == 2);
    REQUIRE(bpm.value->charts.front().warps.size() == 1);
    CHECK(bpm.value->charts.front().warps.front().source_encoding ==
          wormedit::warp_provenance_t::negative_bpm);
    CHECK(at(bpm.value->charts.front().warps.front().span.begin, 2, 0));
    CHECK(at(bpm.value->charts.front().warps.front().span.end, 3, 0));

    const auto stop = fixture("legacy-negative-stop.sm", wormedit::stepmania_profile_t::sm,
                              wormedit::stepmania_timing_syntax_t::legacy_negative_stop);
    REQUIRE(stop.value.has_value());
    REQUIRE_FALSE(!stop.ok());
    REQUIRE(stop.value->charts.front().warps.size() == 1);
    CHECK(stop.value->charts.front().warps.front().source_encoding ==
          wormedit::warp_provenance_t::negative_stop);
    CHECK(at(stop.value->charts.front().warps.front().span.begin, 2, 0));
    CHECK(at(stop.value->charts.front().warps.front().span.end, 3, 0));
}

TEST_CASE("StepMania source profiles gate legacy warp syntax and summarize diagnostics",
          "[unit][format][stepmania]") {
    using syntax_t = wormedit::stepmania_timing_syntax_t;
    const auto native = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc,
                                syntax_t::native_warps);
    REQUIRE(native.value.has_value());
    CHECK(native.value->charts.front().warps.front().source_encoding ==
          wormedit::warp_provenance_t::native);
    CHECK(native.diagnostics.empty());

    const auto native_as_legacy = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc,
                                          syntax_t::legacy_negative_bpm);
    CHECK_FALSE(native_as_legacy.value.has_value());
    REQUIRE(native_as_legacy.diagnostics.size() == 1);
    CHECK(native_as_legacy.diagnostics.front().code ==
          wormedit::stepmania_parse_diagnostic_code_t::implementation_profile_mismatch);

    const auto mismatched = fixture("legacy-negative-bpm.sm", wormedit::stepmania_profile_t::sm,
                                    syntax_t::native_warps);
    CHECK_FALSE(mismatched.value.has_value());
    REQUIRE(mismatched.diagnostics.size() == 1);
    CHECK(mismatched.diagnostics.front().code ==
          wormedit::stepmania_parse_diagnostic_code_t::implementation_profile_mismatch);
    const auto stop_mismatched =
        fixture("legacy-negative-stop.sm", wormedit::stepmania_profile_t::sm,
                syntax_t::legacy_negative_bpm);
    CHECK_FALSE(stop_mismatched.value.has_value());
    REQUIRE(stop_mismatched.diagnostics.size() == 1);
    CHECK(stop_mismatched.diagnostics.front().code ==
          wormedit::stepmania_parse_diagnostic_code_t::implementation_profile_mismatch);

    const auto repeated =
        fixture("legacy-negative-bpm-multiple.sm", wormedit::stepmania_profile_t::sm,
                syntax_t::legacy_negative_bpm);
    REQUIRE(repeated.value.has_value());
    const auto& chart = repeated.value->charts.front();
    REQUIRE(chart.warps.size() == 2);
    CHECK(at(chart.warps[0].span.begin, 2, 0));
    CHECK(at(chart.warps[0].span.end, 3, 0));
    CHECK(at(chart.warps[1].span.begin, 4, 0));
    CHECK(at(chart.warps[1].span.end, 5, 0));
    REQUIRE(repeated.diagnostics.size() == 1);
    CHECK(repeated.diagnostics.front().code ==
          wormedit::stepmania_parse_diagnostic_code_t::legacy_warp_encoding);
    CHECK(repeated.value->metadata.format_groups.front().ordered_fields.front().first == "TITLE");
    CHECK(std::ranges::find(repeated.value->metadata.format_groups.front().ordered_fields,
                            std::pair{std::string{"BPMS"},
                                      std::string{"0.000=120.000,4.000=-120.000,8.000=120.000,"
                                                  "12.000=-120.000,16.000=120.000"}}) !=
          repeated.value->metadata.format_groups.front().ordered_fields.end());
}

TEST_CASE("Versioned SSC metadata remains editable without losing format-only fields",
          "[unit][format][stepmania]") {
    auto parsed = fixture("versioned-metadata.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(parsed.value.has_value());
    REQUIRE_FALSE(!parsed.ok());
    CHECK(parsed.value->metadata.title == "Versioned metadata fixture");
    CHECK(parsed.value->metadata.artist == "Wormedit fixture authors");
    REQUIRE(parsed.value->charts.size() == 1);
    auto& chart = parsed.value->charts.front();
    CHECK(chart.metadata.name == "Editable chart name");
    CHECK(chart.metadata.author == "Chart Author");
    CHECK((chart.metadata.rating == wormedit::signed_rational_t{11, 1}));

    const auto& song_fields = parsed.value->metadata.format_groups.front().ordered_fields;
    CHECK(std::ranges::find(song_fields, std::pair{std::string{"VERSION"}, std::string{"0.83"}}) !=
          song_fields.end());
    CHECK(std::ranges::find(song_fields,
                            std::pair{std::string{"SUBTITLE"}, std::string{"Retained subtitle"}}) !=
          song_fields.end());
    CHECK(std::ranges::find(song_fields, std::pair{std::string{"TITLETRANSLIT"},
                                                   std::string{"Versioned Metadata Fixture"}}) !=
          song_fields.end());
    CHECK(std::ranges::find(song_fields, std::pair{std::string{"ARTISTTRANSLIT"},
                                                   std::string{"Wormedit Fixture Authors"}}) !=
          song_fields.end());

    parsed.value->metadata.title = "Edited title";
    chart.metadata.name = "Edited chart";
    chart.metadata.author = "Edited Author";
    chart.metadata.rating = {25, 2};
    const auto written =
        wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(written.value.has_value());
    CHECK(written.value->find("#VERSION:0.83;") != std::string::npos);
    CHECK(written.value->find("#SUBTITLE:Retained subtitle;") != std::string::npos);
    CHECK(written.value->find("#TITLETRANSLIT:Versioned Metadata Fixture;") != std::string::npos);
    CHECK(written.value->find("#ARTISTTRANSLIT:Wormedit Fixture Authors;") != std::string::npos);
    CHECK(written.value->find("#GENRE:Test Genre;") != std::string::npos);
    CHECK(written.value->find("#ORIGIN:Wormedit;") != std::string::npos);
    CHECK(written.value->find("#CREDIT:Fixture maintainers;") != std::string::npos);
    CHECK(written.value->find("#BANNER:banner.png;") != std::string::npos);
    CHECK(written.value->find("#BACKGROUND:background.png;") != std::string::npos);
    CHECK(written.value->find("#CDTITLE:cdtitle.png;") != std::string::npos);
    CHECK(written.value->find("#SAMPLESTART:12.500;") != std::string::npos);
    CHECK(written.value->find("#SAMPLELENGTH:15.000;") != std::string::npos);
    CHECK(written.value->find("#SELECTABLE:YES;") != std::string::npos);
    CHECK(written.value->find("#DISPLAYBPM:120:180;") != std::string::npos);
    CHECK(written.value->find("#TITLE:Edited title;") != std::string::npos);
    CHECK(written.value->find("#DESCRIPTION:Retained chart description;") != std::string::npos);
    CHECK(written.value->find("#CHARTNAME:Edited chart;") != std::string::npos);
    CHECK(written.value->find("#CREDIT:Edited Author;") != std::string::npos);
    CHECK(written.value->find("#CREDIT:Chart Author;") == std::string::npos);
    CHECK(written.value->find("#MUSIC:chart.ogg;") != std::string::npos);
    CHECK(written.value->find("#METER:12.5;") != std::string::npos);

    const auto reimported = parse_ssc(*written.value);
    REQUIRE(reimported.value.has_value());
    REQUIRE_FALSE(!reimported.ok());
    const auto& reparsed_chart = reimported.value->charts.front();
    CHECK(reparsed_chart.metadata.name == "Edited chart");
    CHECK(reparsed_chart.metadata.author == "Edited Author");
    CHECK((reparsed_chart.metadata.rating == wormedit::signed_rational_t{25, 2}));
}

TEST_CASE("SSC parsing and writing are bounded to the audited StepMania 5.1 version profile",
          "[unit][format][stepmania][version]") {
    SECTION("an omitted version uses the upstream 0.83 default and writes it explicitly") {
        const auto parsed = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
        REQUIRE(parsed.value.has_value());
        REQUIRE_FALSE(!parsed.ok());

        const auto written = wormedit::write_stepmania(
            *parsed.value, {.profile = wormedit::stepmania_profile_t::ssc});
        REQUIRE(written.value.has_value());
        const auto first = written.value->find("#VERSION:0.83;");
        REQUIRE(first != std::string::npos);
        CHECK(written.value->find("#VERSION:0.83;", first + 1) == std::string::npos);
        CHECK(first == 0);
    }

    SECTION("an older version is rejected before its different warp semantics are interpreted") {
        const auto parsed =
            fixture("negative-version-0.69.ssc", wormedit::stepmania_profile_t::ssc);
        CHECK_FALSE(parsed.value.has_value());
        REQUIRE(parsed.diagnostics.size() == 1);
        CHECK(parsed.diagnostics.front().code ==
              wormedit::stepmania_parse_diagnostic_code_t::unsupported_version);
    }

    SECTION("malformed and duplicate version declarations reject transactionally") {
        const auto malformed =
            parse_ssc("#VERSION:future;#NOTEDATA:;#STEPSTYPE:dance-single;#NOTES:\n0000\n;");
        CHECK_FALSE(malformed.value.has_value());
        REQUIRE(malformed.diagnostics.size() == 1);
        CHECK(malformed.diagnostics.front().code ==
              wormedit::stepmania_parse_diagnostic_code_t::unsupported_version);

        const auto duplicate =
            parse_ssc("#VERSION:0.83;#VERSION:0.83;#NOTEDATA:;#STEPSTYPE:dance-single;"
                      "#NOTES:\n0000\n;");
        CHECK_FALSE(duplicate.value.has_value());
        REQUIRE(duplicate.diagnostics.size() == 1);
        CHECK(duplicate.diagnostics.front().code ==
              wormedit::stepmania_parse_diagnostic_code_t::duplicate_field);
    }

    SECTION("SM does not inherit the SSC version gate") {
        constexpr std::string_view source = "#VERSION:0.69;#BPMS:0=120;"
                                            "#NOTES:dance-single:Legacy:Edit:1:0,0,0,0,0:\n0000\n;";
        auto options = wormedit::stepmania_parse_options_t{};
        options.profile = wormedit::stepmania_profile_t::sm;
        const auto parsed = wormedit::parse_stepmania_bytes(
            std::as_bytes(std::span{source.data(), source.size()}), options);
        REQUIRE(parsed.value.has_value());
        REQUIRE_FALSE(!parsed.ok());

        const auto written =
            wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::sm});
        REQUIRE(written.value.has_value());
        CHECK(written.value->find("#VERSION:0.69;") != std::string::npos);
    }
}

TEST_CASE("SM global CREDIT maps to chart authors and conflicting edits reject",
          "[unit][format][stepmania]") {
    constexpr std::string_view source =
        "#TITLE:Credit scope;#CREDIT:Global Author;#BPMS:0=120;"
        "#NOTES:dance-single:Easy chart:Easy:2:0,0,0,0,0:\n1000\n0000\n0000\n0000\n;"
        "#NOTES:dance-single:Hard chart:Hard:8:0,0,0,0,0:\n0100\n0000\n0000\n0000\n;";
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::sm;
    auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}), options);
    REQUIRE(parsed.value.has_value());
    REQUIRE(parsed.value->charts.size() == 2);
    CHECK(parsed.value->charts[0].metadata.author == "Global Author");
    CHECK(parsed.value->charts[1].metadata.author == "Global Author");

    auto written =
        wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::sm});
    REQUIRE(written.value.has_value());
    CHECK(written.value->find("#CREDIT:Global Author;") != std::string::npos);

    parsed.value->charts[1].metadata.author = "Another Author";
    written =
        wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::sm});
    CHECK_FALSE(written.value.has_value());
    CHECK(std::ranges::any_of(written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code ==
                   wormedit::stepmania_write_diagnostic_code_t::conflicting_song_metadata &&
               diagnostic.location == "CREDIT";
    }));
}

TEST_CASE("Native warp timing, notes, and unknown metadata write deterministically and reimport",
          "[unit][format][stepmania]") {
    const auto input = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(input.value.has_value());
    const auto output =
        wormedit::write_stepmania(*input.value, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(output.value.has_value());
    CHECK(output.value->find("#OFFSET:0.125;") != std::string::npos);
    CHECK(output.value->find("#WARPS:4=2;") != std::string::npos);
    const auto again = roundtrip(*input.value, wormedit::stepmania_profile_t::ssc);
    REQUIRE(again.value.has_value());
    const auto& original = input.value->charts.front();
    const auto& reimported = again.value->charts.front();
    CHECK((reimported.beat_zero_audio_offset_seconds == original.beat_zero_audio_offset_seconds));
    CHECK(reimported.bpms.size() == original.bpms.size());
    CHECK(reimported.pauses.size() == original.pauses.size());
    CHECK(reimported.warps.size() == original.warps.size());
    CHECK(reimported.scrolls.size() == original.scrolls.size());
    CHECK(reimported.speeds.size() == original.speeds.size());
    CHECK(reimported.measures[0].notes.size() == original.measures[0].notes.size());
}

namespace {
void expect_scientific_timing(bool condition,
                              std::source_location source = std::source_location::current()) {
    INFO("Scientific timing expectation at " << source.line());
    REQUIRE(condition);
}
} // namespace

TEST_CASE("StepMania scientific notation is exact across timing event fields",
          "[unit][format][stepmania][numeric-text]") {
    const auto parsed = parse_ssc(
        "#OFFSET:-1.25e-1;#BPMS:0E+0=1.2E+2;#STOPS:1e0=2.5e-1;"
        "#DELAYS:2E+0=5E-2;#WARPS:4e0=2E+0;#SCROLLS:0=-1.25E-1;"
        "#SPEEDS:0=2.5E+0=1.25E-1=1;"
        "#NOTEDATA:;#STEPSTYPE:dance-single;#NOTES:\n0000\n0000\n0000\n0000\n;");
    expect_scientific_timing(parsed.value.has_value());
    expect_scientific_timing(parsed.ok());
    const auto& chart = parsed.value->charts.front();
    expect_scientific_timing(chart.beat_zero_audio_offset_seconds == wormedit::signed_rational_t{1, 8});
    expect_scientific_timing(chart.bpms.size() == 1);
    expect_scientific_timing(chart.bpms.front().beats_per_minute == wormedit::signed_rational_t{120, 1});
    expect_scientific_timing(chart.pauses.size() == 2);
    expect_scientific_timing(chart.pauses[0].duration == wormedit::signed_rational_t{1, 4});
    expect_scientific_timing(chart.pauses[1].duration == wormedit::signed_rational_t{1, 20});
    expect_scientific_timing(chart.warps.size() == 1);
    expect_scientific_timing(chart.scrolls.size() == 1);
    expect_scientific_timing(chart.scrolls.front().multiplier == wormedit::signed_rational_t{-1, 8});
    expect_scientific_timing(chart.speeds.size() == 1);
    expect_scientific_timing(chart.speeds.front().multiplier == wormedit::signed_rational_t{5, 2});
    expect_scientific_timing(chart.speeds.front().transition_beats == wormedit::rational_t{1, 8});
    expect_scientific_timing(chart.speeds.front().transition_is_seconds);
    const auto reimported = roundtrip(*parsed.value, wormedit::stepmania_profile_t::ssc);
    expect_scientific_timing(reimported.value.has_value());
    expect_scientific_timing(reimported.value->charts.front().speeds.front().multiplier == chart.speeds.front().multiplier);
}

TEST_CASE("StepMania empty timing lists and common column mappings are accepted",
          "[unit][format][stepmania]") {
    const auto empty =
        parse_ssc("#BPMS:;#STOPS:;#DELAYS:;#WARPS:;#SCROLLS:;#SPEEDS:;"
                  "#NOTEDATA:;#STEPSTYPE:dance-single;#NOTES:\n0000\n0000\n0000\n0000\n;");
    REQUIRE(empty.value.has_value());
    REQUIRE_FALSE(!empty.ok());
    CHECK(empty.value->charts.front().bpms.empty());
    CHECK(empty.value->charts.front().speeds.empty());

    const auto parse_columns = [](std::string_view step_type, std::size_t columns) {
        const std::string row(columns, '0');
        const std::string source = "#NOTEDATA:;#STEPSTYPE:" + std::string(step_type) +
                                   ";#NOTES:\n" + row + "\n" + row + "\n" + row + "\n" + row +
                                   "\n;";
        return parse_ssc(source);
    };
    for (const auto [type, count] :
         {std::pair{"dance-threepanel", 3U}, std::pair{"dance-single", 4U},
          std::pair{"pump-single", 5U}, std::pair{"ez2-single", 5U}, std::pair{"dance-solo", 6U},
          std::pair{"pump-halfdouble", 6U}, std::pair{"ez2-real", 7U},
          std::pair{"dance-double", 8U}, std::pair{"dance-couple", 8U},
          std::pair{"pump-double", 10U}, std::pair{"pump-couple", 10U},
          std::pair{"ez2-double", 10U}}) {
        const auto parsed = parse_columns(type, count);
        REQUIRE(parsed.value.has_value());
        REQUIRE(parsed.value->charts.front().lane_layouts.size() == 1);
        CHECK(parsed.value->charts.front().lane_layouts.front().layout.lanes.size() == count);
    }
    CHECK_FALSE(parse_columns("made-up", 4).value.has_value());
}

TEST_CASE("SSC shared song timing remains at song scope across charts",
          "[unit][format][stepmania]") {
    constexpr std::string_view source = "#OFFSET:0.125;#BPMS:0=120;"
                                        "#NOTEDATA:;#STEPSTYPE:dance-single;#DIFFICULTY:Easy;"
                                        "#NOTES:\n1000\n0000\n0000\n0000\n;"
                                        "#NOTEDATA:;#STEPSTYPE:dance-single;#DIFFICULTY:Hard;"
                                        "#NOTES:\n0100\n0000\n0000\n0000\n;";
    const auto parsed = parse_ssc(source);
    REQUIRE(parsed.value.has_value());
    REQUIRE(parsed.value->charts.size() == 2);
    const auto written =
        wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(written.value.has_value());
    const auto first_chart = written.value->find("#NOTEDATA:");
    REQUIRE(first_chart != std::string::npos);
    CHECK(written.value->find("#OFFSET:0.125;") < first_chart);
    CHECK(written.value->find("#BPMS:0=120;") < first_chart);
    CHECK(written.value->find("#BPMS:", first_chart) == std::string::npos);
    CHECK(written.value->find("#OFFSET:", first_chart) == std::string::npos);
}

TEST_CASE("SM writer rejects timing it cannot faithfully represent", "[unit][format][stepmania]") {
    const auto parsed = fixture("matched-timing.sm", wormedit::stepmania_profile_t::sm);
    REQUIRE(parsed.value.has_value());
    const auto native =
        wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::sm});
    CHECK_FALSE(native.value.has_value());
    CHECK(std::ranges::any_of(native.diagnostics, [](const auto& diagnostic) {
        return diagnostic.location == "WARPS" &&
               diagnostic.code == wormedit::stepmania_write_diagnostic_code_t::unsupported_feature;
    }));
    CHECK(std::ranges::any_of(native.diagnostics, [](const auto& diagnostic) {
        return diagnostic.location == "SCROLLS" || diagnostic.location == "SPEEDS";
    }));

    auto legacy_song = *parsed.value;
    legacy_song.charts.front().scrolls.clear();
    legacy_song.charts.front().speeds.clear();
    const auto legacy = wormedit::write_stepmania(
        legacy_song, {.profile = wormedit::stepmania_profile_t::sm,
                      .warp_encoding = wormedit::warp_encoding_t::negative_stop});
    REQUIRE(legacy.value.has_value());
    CHECK(legacy.value->find("#WARPS:") == std::string::npos);
    CHECK(legacy.value->find("4=-1") != std::string::npos);
}

TEST_CASE("StepMania groups repeated note metadata warnings", "[unit][format][stepmania]") {
    auto parsed = parse_ssc("#BPMS:0=120;#NOTEDATA:;#STEPSTYPE:dance-single;#DIFFICULTY:Easy;"
                            "#NOTES:\n1000\n0100\n0010\n0001\n;");
    REQUIRE(parsed.value);
    for (auto& note : parsed.value->charts.front().measures.front().notes) note.label = "label";
    const auto written = wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(written.ok());
    REQUIRE(written.value);
    CHECK(std::ranges::count_if(written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::stepmania_write_diagnostic_code_t::unsupported_feature &&
            diagnostic.location == "NOTES";
    }) == 1);
}

TEST_CASE("StepMania writer rejects unsafe metadata and invisible notes",
          "[unit][format][stepmania]") {
    auto parsed = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(parsed.value.has_value());
    parsed.value->metadata.title = "bad;#NOTES:injected";
    const auto unsafe =
        wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::ssc});
    CHECK_FALSE(unsafe.value.has_value());
    CHECK(std::ranges::any_of(unsafe.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::stepmania_write_diagnostic_code_t::invalid_metadata;
    }));

    parsed = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(parsed.value.has_value());
    REQUIRE_FALSE(parsed.value->charts.front().measures.front().notes.empty());
    parsed.value->charts.front().measures.front().notes.front().kind =
        wormedit::note_kind_t::invisible;
    const auto invisible =
        wormedit::write_stepmania(*parsed.value, {.profile = wormedit::stepmania_profile_t::ssc});
    CHECK_FALSE(invisible.value.has_value());
    CHECK(std::ranges::any_of(invisible.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::stepmania_write_diagnostic_code_t::invalid_note_layout;
    }));
}

TEST_CASE("StepMania negative timing and malformed charts fail atomically",
          "[unit][format][stepmania]") {
    const auto invalid = fixture("negative-invalid-timing.ssc", wormedit::stepmania_profile_t::ssc);
    CHECK_FALSE(invalid.value.has_value());
    CHECK(!invalid.ok());

    constexpr std::string_view missing_tail =
        "#BPMS:0=120;#NOTEDATA:;#STEPSTYPE:dance-single;#NOTES:\n2000\n;";
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::ssc;
    const auto malformed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{missing_tail.data(), missing_tail.size()}), options);
    CHECK_FALSE(malformed.value.has_value());
    CHECK(!malformed.ok());
}

TEST_CASE("Legacy warp writers diagnose and preserve representable conversions",
          "[unit][format][stepmania]") {
    const auto input = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(input.value.has_value());
    const auto legacy_bpm = wormedit::write_stepmania(
        *input.value, {.profile = wormedit::stepmania_profile_t::ssc,
                      .warp_encoding = wormedit::warp_encoding_t::negative_bpm});
    REQUIRE(legacy_bpm.value.has_value());
    CHECK(legacy_bpm.value->find("= -") == std::string::npos);
    CHECK(legacy_bpm.value->find("#BPMS:0=120,4=-120,8=120;") != std::string::npos);

    const auto legacy_stop = wormedit::write_stepmania(
        *input.value, {.profile = wormedit::stepmania_profile_t::ssc,
                      .warp_encoding = wormedit::warp_encoding_t::negative_stop});
    REQUIRE(legacy_stop.value.has_value());
    CHECK(legacy_stop.value->find("#STOPS:4=-2;") != std::string::npos);
}

TEST_CASE("StepMania target timing syntax profiles choose exact warp encodings",
          "[unit][format][stepmania]") {
    using syntax_t = wormedit::stepmania_timing_syntax_t;
    const auto input = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc,
                               syntax_t::native_warps);
    REQUIRE(input.value.has_value());

    const auto native_output = wormedit::write_stepmania(
        *input.value, {.profile = wormedit::stepmania_profile_t::ssc,
                      .target_timing_syntax = syntax_t::native_warps});
    REQUIRE(native_output.value.has_value());
    CHECK(native_output.value->find("#WARPS:4=4;") != std::string::npos);
    CHECK(native_output.value->find("#STOPS:") == std::string::npos);

    const auto legacy_bpm = wormedit::write_stepmania(
        *input.value, {.profile = wormedit::stepmania_profile_t::ssc,
                      .target_timing_syntax = syntax_t::legacy_negative_bpm});
    REQUIRE(legacy_bpm.value.has_value());
    CHECK(legacy_bpm.value->find("#WARPS:") == std::string::npos);
    CHECK(legacy_bpm.value->find("#BPMS:0=120,4=-120,8=120;") != std::string::npos);
    const auto bpm_reimported = parse_ssc(*legacy_bpm.value, syntax_t::legacy_negative_bpm);
    REQUIRE(bpm_reimported.value.has_value());
    const auto& bpm_warp = bpm_reimported.value->charts.front().warps.front();
    CHECK(bpm_warp.source_encoding == wormedit::warp_provenance_t::negative_bpm);
    CHECK(at(bpm_warp.span.begin, 2, 0));
    CHECK(at(bpm_warp.span.end, 3, 0));

    const auto legacy_stop = wormedit::write_stepmania(
        *input.value, {.profile = wormedit::stepmania_profile_t::ssc,
                      .target_timing_syntax = syntax_t::legacy_negative_stop});
    REQUIRE(legacy_stop.value.has_value());
    CHECK(legacy_stop.value->find("#WARPS:") == std::string::npos);
    CHECK(legacy_stop.value->find("#STOPS:4=-2;") != std::string::npos);
    const auto stop_reimported =
        parse_ssc(*legacy_stop.value, syntax_t::legacy_negative_stop);
    REQUIRE(stop_reimported.value.has_value());
    const auto& stop_warp = stop_reimported.value->charts.front().warps.front();
    CHECK(stop_warp.source_encoding == wormedit::warp_provenance_t::negative_stop);
    CHECK(at(stop_warp.span.begin, 2, 0));
    CHECK(at(stop_warp.span.end, 3, 0));

    const auto repeated =
        fixture("legacy-negative-bpm-multiple.sm", wormedit::stepmania_profile_t::sm,
                syntax_t::legacy_negative_bpm);
    REQUIRE(repeated.value.has_value());
    const auto repeated_output = wormedit::write_stepmania(
        *repeated.value, {.profile = wormedit::stepmania_profile_t::ssc,
                         .target_timing_syntax = syntax_t::legacy_negative_stop});
    REQUIRE(repeated_output.value.has_value());
    CHECK(repeated_output.value->find("#STOPS:4=-2,12=-2;") != std::string::npos);
    CHECK(std::ranges::count_if(repeated_output.diagnostics, [](const auto& diagnostic) {
              return diagnostic.code ==
                         wormedit::stepmania_write_diagnostic_code_t::legacy_warp_conversion &&
                     diagnostic.severity == wormedit::diagnostic_severity_t::warning;
          }) == 1);
    const auto repeated_reimported =
        parse_ssc(*repeated_output.value, syntax_t::legacy_negative_stop);
    REQUIRE(repeated_reimported.value.has_value());
    REQUIRE(repeated_reimported.value->charts.front().warps.size() == 2);
    CHECK(at(repeated_reimported.value->charts.front().warps[0].span.begin, 2, 0));
    CHECK(at(repeated_reimported.value->charts.front().warps[0].span.end, 3, 0));
    CHECK(at(repeated_reimported.value->charts.front().warps[1].span.begin, 4, 0));
    CHECK(at(repeated_reimported.value->charts.front().warps[1].span.end, 5, 0));
}

TEST_CASE("Named StepMania implementations expose only their audited SSC 0.83 subset",
          "[unit][format][stepmania][implementation-profile]") {
    using implementation_t = wormedit::stepmania_implementation_profile_t;
    using syntax_t = wormedit::stepmania_timing_syntax_t;
    constexpr std::array implementations{
        implementation_t::stepmania_5_1,
        implementation_t::etterna_0_75_1,
    };
    const auto unscoped = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc,
                                  syntax_t::native_warps);
    REQUIRE(unscoped.value.has_value());
    const auto legacy_source = wormedit::write_stepmania(
        *unscoped.value,
        {.profile = wormedit::stepmania_profile_t::ssc,
         .target_timing_syntax = syntax_t::legacy_negative_bpm});
    REQUIRE(legacy_source.value.has_value());
    REQUIRE(legacy_source.value->find("#BPMS:0=120,4=-120,8=120;") != std::string::npos);

    for (const auto implementation : implementations) {
        CAPTURE(implementation);
        const auto parsed = fixture("native-warp.ssc", wormedit::stepmania_profile_t::ssc,
                                    syntax_t::native_warps, implementation);
        REQUIRE(parsed.value.has_value());
        REQUIRE_FALSE(!parsed.ok());
        REQUIRE(parsed.value->charts.front().warps.size() == 1);

        const auto written = wormedit::write_stepmania(
            *parsed.value,
            {.profile = wormedit::stepmania_profile_t::ssc,
             .target_implementation_profile = implementation});
        REQUIRE(written.value.has_value());
        REQUIRE_FALSE(!written.ok());
        CHECK(written.value->starts_with("#VERSION:0.83;\n"));
        CHECK(written.value->find("#WARPS:4=4;") != std::string::npos);

        const auto legacy_forced_native = wormedit::write_stepmania(
            *parsed.value,
            {.profile = wormedit::stepmania_profile_t::ssc,
             .target_implementation_profile = implementation,
             .warp_encoding = wormedit::warp_encoding_t::negative_bpm});
        REQUIRE(legacy_forced_native.value.has_value());
        CHECK(legacy_forced_native.value->find("#WARPS:4=4;") != std::string::npos);
        CHECK(legacy_forced_native.value->find("=-120") == std::string::npos);

        const auto reparsed = parse_ssc(*written.value, syntax_t::native_warps, implementation);
        REQUIRE(reparsed.value.has_value());
        REQUIRE_FALSE(!reparsed.ok());
        REQUIRE(reparsed.value->charts.front().warps.size() == 1);
        CHECK(at(reparsed.value->charts.front().warps.front().span.begin, 2, 0));
        CHECK(at(reparsed.value->charts.front().warps.front().span.end, 3, 0));

        const auto conflicting_source = fixture(
            "native-warp.ssc", wormedit::stepmania_profile_t::ssc,
            syntax_t::legacy_negative_bpm, implementation);
        CHECK_FALSE(conflicting_source.value.has_value());
        CHECK(!conflicting_source.ok());

        const auto unsupported_legacy =
            parse_ssc(*legacy_source.value, syntax_t::unspecified, implementation);
        CHECK_FALSE(unsupported_legacy.value.has_value());
        CHECK(!unsupported_legacy.ok());

        const auto conflicting_target = wormedit::write_stepmania(
            *parsed.value,
            {.profile = wormedit::stepmania_profile_t::ssc,
             .target_timing_syntax = syntax_t::legacy_negative_stop,
             .target_implementation_profile = implementation});
        CHECK_FALSE(conflicting_target.value.has_value());
        CHECK(!conflicting_target.ok());

        const auto sm_source = fixture("matched-timing.sm", wormedit::stepmania_profile_t::sm,
                                       syntax_t::unspecified, implementation);
        CHECK_FALSE(sm_source.value.has_value());
        CHECK(!sm_source.ok());
    }
}

TEST_CASE("StepMania export policy keeps file syntax and implementation choices separate",
          "[unit][format][stepmania][export-policy]") {
    using error_t = wormedit::failure_code_t;
    using implementation_t = wormedit::stepmania_implementation_profile_t;
    using profile_t = wormedit::stepmania_profile_t;
    using syntax_t = wormedit::stepmania_timing_syntax_t;

    const auto syntax_only = wormedit::stepmania_write_options_t::from(
        {.file_profile = profile_t::sm,
         .timing_syntax = syntax_t::legacy_negative_bpm,
         .implementation_profile = implementation_t::unspecified});
    REQUIRE(syntax_only);
    CHECK(syntax_only->profile == profile_t::sm);
    CHECK(syntax_only->target_timing_syntax == syntax_t::legacy_negative_bpm);
    CHECK(syntax_only->target_implementation_profile ==
          implementation_t::unspecified);
    CHECK(syntax_only->warp_encoding == wormedit::warp_encoding_t::negative_bpm);

    const auto named = wormedit::stepmania_write_options_t::from(
        {.file_profile = profile_t::ssc,
         .timing_syntax = syntax_t::native_warps,
         .implementation_profile = implementation_t::stepmania_5_1});
    REQUIRE(named);
    CHECK(named->profile == profile_t::ssc);
    CHECK(named->target_implementation_profile == implementation_t::stepmania_5_1);
    CHECK(named->warp_encoding == wormedit::warp_encoding_t::native);

    const auto missing_syntax = wormedit::stepmania_write_options_t::from(
        {.file_profile = profile_t::ssc,
         .timing_syntax = syntax_t::unspecified,
         .implementation_profile = implementation_t::unspecified});
    CHECK_FALSE(missing_syntax);
    CHECK(missing_syntax.error().code == error_t::invalid_request);

    const auto named_sm = wormedit::stepmania_write_options_t::from(
        {.file_profile = profile_t::sm,
         .timing_syntax = syntax_t::native_warps,
         .implementation_profile = implementation_t::etterna_0_75_1});
    CHECK_FALSE(named_sm);
    CHECK(named_sm.error().code == error_t::unsupported);

    const auto named_legacy = wormedit::stepmania_write_options_t::from(
        {.file_profile = profile_t::ssc,
         .timing_syntax = syntax_t::legacy_negative_stop,
         .implementation_profile = implementation_t::etterna_0_75_1});
    CHECK_FALSE(named_legacy);
    CHECK(named_legacy.error().code == error_t::conflict);

    wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::stepmania,
        .stepmania = wormedit::stepmania_export_policy_t{
            .file_profile = profile_t::ssc,
            .timing_syntax = syntax_t::native_warps,
            .implementation_profile = implementation_t::etterna_0_75_1},
    };
    REQUIRE(request.stepmania.has_value());
    CHECK(request.stepmania->file_profile == profile_t::ssc);
    CHECK(request.stepmania->timing_syntax == syntax_t::native_warps);
    CHECK(request.stepmania->implementation_profile == implementation_t::etterna_0_75_1);
}

TEST_CASE("StepMania export bakes parametric SV recipes into SCROLLS",
          "[unit][format][stepmania][sv-recipe]") {
    const auto parsed = fixture("matched-timing.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(parsed.value.has_value());
    auto song = *parsed.value;
    auto& chart = song.charts.front();
    REQUIRE(chart.measures.size() >= 2);
    chart.scrolls.clear();
    chart.sv_recipes.push_back({
        .id = wormedit::recipe_id_t{900001},
        .name = "export guard",
        .range = {{chart.measures[0].id, 0}, {chart.measures[1].id, 0}},
        .output = wormedit::sv_recipe_output_t::sv,
        .curve = wormedit::sv_recipe_curve_t::constant,
        .parameters = {.value_from = {2, 1}},
    });
    const auto written =
        wormedit::write_stepmania(song, {.profile = wormedit::stepmania_profile_t::ssc});
    REQUIRE(written.value.has_value());
    CHECK(written.value->find("#SCROLLS:0=2,4=1;") != std::string::npos);
    CHECK(chart.scrolls.empty());
    CHECK(chart.sv_recipes.size() == 1);
}

TEST_CASE("StepMania imports bind lanes without changing exported chart bytes", "[stepmania][default-input]") {
    auto parsed = fixture("matched-timing.sm", wormedit::stepmania_profile_t::sm);
    REQUIRE(parsed.value);
    const auto& input = parsed.value->charts.front().input_profile;
    CHECK(input.bindings.size() == 4);
    CHECK(input.bindings.front().key == wormedit::physical_key_t::digit_1);
    const auto bound = wormedit::write_stepmania(*parsed.value);
    parsed.value->charts.front().input_profile = {};
    const auto unbound = wormedit::write_stepmania(*parsed.value);
    CHECK(bound.value == unbound.value);
}

namespace {
void check_offset_roundtrip(const wormedit::song_document_t& song,
                           wormedit::stepmania_profile_t profile) {
    auto parsed = roundtrip(song, profile);
    REQUIRE_FALSE(!parsed.ok());
    REQUIRE(parsed.value.has_value());
    check_cross_measure_holds(*parsed.value);
}

void check_cross_measure_offset_edit(wormedit::stepmania_profile_t profile) {
    auto history = wormedit::document_history_t::from(cross_measure_hold_song(profile));
    check_cross_measure_holds(history.document);
    const auto chart = history.document.charts.front().id;
    const auto edited = wormedit::apply_audio_offset_edit(history, chart, {1, 4});
    REQUIRE(edited.applied);
    const wormedit::signed_rational_t expected{1, 4};
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds == expected);
    check_cross_measure_holds(history.document);
    check_offset_roundtrip(history.document, profile);
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds.numerator == 0);
    check_cross_measure_holds(history.document);
}
}

TEST_CASE("StepMania cross-measure holds remain editable through exact audio offset changes",
          "[formats][stepmania][audio-offset]") {
    for (const auto profile : {wormedit::stepmania_profile_t::sm, wormedit::stepmania_profile_t::ssc})
        check_cross_measure_offset_edit(profile);
}

TEST_CASE("pump-single and dance-solo fixtures import named arrow lanes and keep their step type",
          "[unit][format][stepmania][lane-profile]") {
    struct step_case_t {
        const char* stem;
        const char* step_type;
        std::size_t columns;
    };
    for (const auto& item : {step_case_t{"pump-single", "pump-single", 5},
                             step_case_t{"dance-solo", "dance-solo", 6}}) {
        const auto specs = wormedit::stepmania_lane_specs(item.step_type);
        REQUIRE(specs.size() == item.columns);
        for (const auto profile : {wormedit::stepmania_profile_t::sm, wormedit::stepmania_profile_t::ssc}) {
            const bool ssc = profile == wormedit::stepmania_profile_t::ssc;
            const auto path = std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "stepmania" /
                              (std::string(item.stem) + (ssc ? ".ssc" : ".sm"));
            const auto parsed = wormedit::parse_stepmania_bytes(read_binary(path), {.profile = profile});
            INFO(path.string());
            REQUIRE_FALSE(!parsed.ok());
            REQUIRE(parsed.value.has_value());
            const auto& chart = parsed.value->charts.front();
            const auto& lanes = chart.lane_layouts.front().layout.lanes;
            REQUIRE(lanes.size() == item.columns);
            CHECK(chart.lane_layouts.front().layout.profile_id ==
                  std::string("stepmania:") + item.step_type);
            for (std::size_t index = 0; index < lanes.size(); ++index) {
                CHECK(lanes[index].name == specs[index].name);
                CHECK(lanes[index].glyph == specs[index].glyph);
            }
            std::size_t taps = 0;
            for (const auto& measure : chart.measures)
                for (const auto& note : measure.notes) {
                    // Column n carries its single tap on row n of the first measure.
                    CHECK(note.lane == lanes[taps].id);
                    ++taps;
                }
            CHECK(taps == item.columns);

            const auto written = wormedit::write_stepmania(*parsed.value, {.profile = profile});
            REQUIRE(written.value.has_value());
            CHECK(written.value->find(item.step_type) != std::string::npos);
            const auto again = wormedit::parse_stepmania_bytes(
                std::as_bytes(std::span{written.value->data(), written.value->size()}),
                {.profile = profile});
            REQUIRE(again.value.has_value());
            CHECK(again.value->charts.front().lane_layouts.front().layout.lanes.size() ==
                  item.columns);
        }
    }
}

namespace {

auto parse_inline(const std::string& source, wormedit::stepmania_profile_t profile)
    -> wormedit::stepmania_parse_outcome_t {
    return wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}), {.profile = profile});
}

auto sample_resources(const wormedit::song_document_t& song)
    -> std::vector<const wormedit::resource_entry_t*> {
    std::vector<const wormedit::resource_entry_t*> result;
    for (const auto& resource : song.resources)
        if (std::ranges::count(resource.roles, wormedit::resource_role_t::sample))
            result.push_back(&resource);
    return result;
}

auto resource_named(const wormedit::song_document_t& song, wormedit::resource_id_t id)
    -> std::string {
    const auto found = std::ranges::find(song.resources, id, &wormedit::resource_entry_t::id);
    return found == song.resources.end() ? std::string{} : found->name;
}

auto warning_count(const wormedit::stepmania_parse_outcome_t& result) -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count(
        result.diagnostics, wormedit::diagnostic_severity_t::warning,
        &wormedit::stepmania_parse_diagnostic_t::severity));
}

}  // namespace

TEST_CASE("StepMania SM keysounds bind per-note indices to sample resources",
          "[unit][format][stepmania][keysounds]") {
    const auto result = fixture("keysounds.sm", wormedit::stepmania_profile_t::sm);
    REQUIRE(result.value.has_value());
    REQUIRE_FALSE(!result.ok());
    const auto& song = *result.value;
    REQUIRE(wormedit::validate_document(song).empty());
    const auto samples = sample_resources(song);
    REQUIRE(samples.size() == 3);
    CHECK(samples[0]->name == "kick.wav");
    CHECK(samples[1]->name == "snare.wav");
    CHECK(samples[2]->name == "hat.wav");
    CHECK(samples[0]->missing);
    CHECK(samples[0]->provenance.source_field == "KEYSOUNDS");

    const auto& chart = song.charts.front();
    REQUIRE(chart.measures.size() == 2);
    const auto& first = chart.measures[0].notes;
    REQUIRE(first.size() == 3);
    CHECK(first[0].kind == wormedit::note_kind_t::tap);
    CHECK(first[0].sound == samples[0]->id);
    CHECK(first[0].provenance.source_spelling == "1[0]");
    CHECK(first[1].kind == wormedit::note_kind_t::hold);
    CHECK(first[1].sound == samples[1]->id);
    CHECK(first[1].provenance.source_spelling == "hold[1]");
    CHECK(first[2].kind == wormedit::note_kind_t::lift);
    CHECK(first[2].sound == samples[2]->id);
    const auto& second = chart.measures[1].notes;
    REQUIRE(second.size() == 2);
    CHECK(second[0].kind == wormedit::note_kind_t::mine);
    CHECK(second[0].sound == samples[0]->id);
    // Index 7 is outside the three-entry list: the note stays, soundless, with one warning.
    CHECK(second[1].kind == wormedit::note_kind_t::tap);
    CHECK_FALSE(second[1].sound.has_value());
    CHECK(second[1].provenance.source_spelling == "1[7]");
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().severity == wormedit::diagnostic_severity_t::warning);
    CHECK(result.diagnostics.front().message.find("measure 2 row 2") != std::string::npos);
}

TEST_CASE("StepMania SSC keysounds honor the backslash-hash escape",
          "[unit][format][stepmania][keysounds]") {
    const auto result = fixture("keysounds.ssc", wormedit::stepmania_profile_t::ssc);
    REQUIRE(result.value.has_value());
    REQUIRE_FALSE(!result.ok());
    CHECK(result.diagnostics.empty());
    const auto& song = *result.value;
    const auto samples = sample_resources(song);
    REQUIRE(samples.size() == 2);
    CHECK(samples[0]->name == "#first.wav");
    CHECK(samples[1]->name == "second.wav");
    const auto& notes = song.charts.front().measures.front().notes;
    REQUIRE(notes.size() == 3);
    CHECK(resource_named(song, *notes[0].sound) == "second.wav");
    CHECK(resource_named(song, *notes[1].sound) == "#first.wav");
    CHECK(resource_named(song, *notes[2].sound) == "second.wav");
    CHECK(notes[0].sound == notes[2].sound);
}

TEST_CASE("StepMania keysound lists keep placeholders and register only used files",
          "[unit][format][stepmania][keysounds]") {
    const std::string source =
        "#KEYSOUNDS:a.wav,,/abs.wav,b.wav;#BPMS:0=120;"
        "#NOTES:dance-single::Hard:1:0,0,0,0,0:\n1[3]000\n0[1]100\n0001\n0000\n;";
    const auto result = parse_inline(source, wormedit::stepmania_profile_t::sm);
    REQUIRE(result.value.has_value());
    CHECK(warning_count(result) == 2);
    const auto samples = sample_resources(*result.value);
    REQUIRE(samples.size() == 1);
    CHECK(samples.front()->name == "b.wav");
    const auto& notes = result.value->charts.front().measures.front().notes;
    REQUIRE(notes.size() == 3);
    CHECK(notes[0].sound == samples.front()->id);
    CHECK_FALSE(notes[1].sound.has_value());
    CHECK(notes[1].provenance.source_spelling == "1");
}

TEST_CASE("StepMania malformed keysound suffixes reject the chart",
          "[unit][format][stepmania][keysounds]") {
    for (const char* row : {"1[x]00", "1[]000", "1[2000", "1[-1]000", "1[0]x00"}) {
        const std::string source = std::string("#KEYSOUNDS:a.wav;#NOTES:dance-single::Hard:1:0:\n") +
                                   row + "\n;";
        const auto result = parse_inline(source, wormedit::stepmania_profile_t::sm);
        INFO(row);
        CHECK_FALSE(result.value.has_value());
        REQUIRE(!result.ok());
        CHECK(result.diagnostics.front().code ==
              wormedit::stepmania_parse_diagnostic_code_t::invalid_notes);
    }
    SECTION("a suffix does not hide a column count mismatch") {
        const auto result = parse_inline(
            "#KEYSOUNDS:a.wav;#NOTES:dance-single::Hard:1:0:\n1[0]00\n;",
            wormedit::stepmania_profile_t::sm);
        CHECK_FALSE(result.value.has_value());
    }
}

TEST_CASE("StepMania charts without keysounds import without sample resources or warnings",
          "[unit][format][stepmania][keysounds]") {
    for (const auto* name : {"pump-single.sm", "dance-solo.sm", "matched-timing.sm"}) {
        const auto result = fixture(name, wormedit::stepmania_profile_t::sm);
        INFO(name);
        REQUIRE(result.value.has_value());
        CHECK(result.diagnostics.empty());
        CHECK(sample_resources(*result.value).empty());
    }
    SECTION("an unreferenced list registers no resource") {
        const auto result = parse_inline(
            "#KEYSOUNDS:a.wav,b.wav;#NOTES:dance-single::Hard:1:0:\n1000\n0100\n0010\n0001\n;",
            wormedit::stepmania_profile_t::sm);
        REQUIRE(result.value.has_value());
        CHECK(result.diagnostics.empty());
        CHECK(sample_resources(*result.value).empty());
    }
}

TEST_CASE("StepMania K autokeysounds import as BGM events without judged notes",
          "[unit][format][stepmania][keysounds]") {
    const std::string source =
        "#KEYSOUNDS:a.wav,b.wav;#BPMS:0=120;"
        "#NOTES:dance-single::Hard:1:0,0,0,0,0:\n"
        "K[0]K[1]00\n0200\n0K[1]00\n0300\n,\n"
        "K000\nK[9]000\n1K[0]00\n0000\n;";
    const auto result = parse_inline(source, wormedit::stepmania_profile_t::sm);
    REQUIRE(result.value.has_value());
    REQUIRE_FALSE(!result.ok());
    REQUIRE(wormedit::validate_document(*result.value).empty());
    CHECK(warning_count(result) == 2);
    const auto& chart = result.value->charts.front();
    const auto samples = sample_resources(*result.value);
    REQUIRE(samples.size() == 2);
    // Judged notes: only the hold and the tap; K cells add none.
    CHECK(chart.measures[0].notes.size() == 1);
    CHECK(chart.measures[1].notes.size() == 1);
    REQUIRE(chart.bgm_source_lanes.size() == 2);
    const auto& first = chart.measures[0].bgm_events;
    REQUIRE(first.size() == 3);
    CHECK(first[0].source_lane == chart.bgm_source_lanes[0].id);
    CHECK(first[1].source_lane == chart.bgm_source_lanes[1].id);
    CHECK(first[0].position == first[1].position);
    CHECK(first[0].sound == samples[0]->id);
    CHECK(first[1].sound == samples[1]->id);
    CHECK(first[0].provenance.source_spelling == "K[0] column 0");
    CHECK(first[1].provenance.source_spelling == "K[1] column 1");
    // This K sits on a column whose hold body is open in the same lane.
    CHECK(first[2].source_lane == chart.bgm_source_lanes[0].id);
    CHECK(first[2].provenance.source_spelling == "K[1] column 1");
    const auto& second = chart.measures[1].bgm_events;
    REQUIRE(second.size() == 1);
    CHECK(second.front().source_lane == chart.bgm_source_lanes[0].id);
    CHECK(second.front().sound == samples[0]->id);
}

namespace {

auto read_fixture_text(std::string_view name) -> std::string {
    const auto bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "stepmania" / name);
    std::string text;
    for (const auto byte : bytes) text.push_back(static_cast<char>(byte));
    return text;
}

auto export_text(const wormedit::song_document_t& song, wormedit::stepmania_profile_t profile)
    -> wormedit::stepmania_write_outcome_t {
    return wormedit::write_stepmania(song, {.profile = profile});
}

auto warning_count_of(const wormedit::stepmania_write_outcome_t& result) -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count(
        result.diagnostics, wormedit::diagnostic_severity_t::warning,
        &wormedit::stepmania_write_diagnostic_t::severity));
}

void check_unedited_round_trip(const std::string& source, wormedit::stepmania_profile_t profile) {
    const auto parsed = parse_inline(source, profile);
    REQUIRE(parsed.value.has_value());
    const auto written = export_text(*parsed.value, profile);
    for (const auto& diagnostic : written.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE_FALSE(!written.ok());
    REQUIRE(written.value.has_value());
    CHECK(*written.value == source);
}

}  // namespace

TEST_CASE("StepMania keysound fixtures round-trip byte-for-byte when unedited",
          "[unit][format][stepmania][keysounds]") {
    check_unedited_round_trip(read_fixture_text("keysounds.sm"), wormedit::stepmania_profile_t::sm);
    check_unedited_round_trip(read_fixture_text("keysounds.ssc"), wormedit::stepmania_profile_t::ssc);
}

TEST_CASE("StepMania autokeysounds and placeholders round-trip byte-for-byte",
          "[unit][format][stepmania][keysounds]") {
    const std::string source =
        "#KEYSOUNDS:a.wav,,b.wav;\n#BPMS:0.000=120.000;\n#NOTES:\n"
        "dance-single:\n:\nEdit:\n1:\n0,0,0,0,0:\n"
        "K[0]K[2]00\n02[2]00\n0K[2]00\n0300\n,\n"
        "0001[1]\n0000\n0000\n0000\n;\n";
    const auto parsed = parse_inline(source, wormedit::stepmania_profile_t::sm);
    REQUIRE(parsed.value.has_value());
    const auto written = export_text(*parsed.value, wormedit::stepmania_profile_t::sm);
    REQUIRE(written.value.has_value());
    // Re-export of the re-import is a fixed point, and the keysound content survives.
    const auto again = parse_inline(*written.value, wormedit::stepmania_profile_t::sm);
    REQUIRE(again.value.has_value());
    const auto second = export_text(*again.value, wormedit::stepmania_profile_t::sm);
    CHECK(second.value == written.value);
    CHECK(written.value->find("#KEYSOUNDS:a.wav,,b.wav;") != std::string::npos);
    CHECK(written.value->find("K[0]K[2]00\n") != std::string::npos);
    CHECK(written.value->find("0K[2]00\n") != std::string::npos);
    CHECK(written.value->find("0001[1]\n") != std::string::npos);
}

TEST_CASE("StepMania export moves an autokeysound to a free column and reports a full row",
          "[unit][format][stepmania][keysounds]") {
    const std::string source =
        "#KEYSOUNDS:a.wav,b.wav;#BPMS:0=120;"
        "#NOTES:dance-single::Hard:1:0,0,0,0,0:\nK[0]K[1]00\n0000\n0000\n0000\n;";
    auto parsed = parse_inline(source, wormedit::stepmania_profile_t::sm);
    REQUIRE(parsed.value.has_value());
    auto& chart = parsed.value->charts.front();
    auto& measure = chart.measures.front();
    const auto add_tap = [&](std::size_t lane, std::uint64_t id) {
        measure.notes.push_back({.id = wormedit::object_id_t{id},
                                 .position = measure.bgm_events.front().position,
                                 .lane = chart.lane_layouts.front().layout.lanes[lane].id});
    };
    SECTION("a taken source column falls back to the first free column") {
        measure.bgm_events.pop_back();
        add_tap(0, 900);
        add_tap(1, 901);
        const auto written = export_text(*parsed.value, wormedit::stepmania_profile_t::sm);
        REQUIRE(written.value.has_value());
        CHECK(written.value->find("\n11K[0]0\n") != std::string::npos);
        CHECK(warning_count_of(written) == 0);
    }
    SECTION("a full row drops the event with a warning-level loss") {
        for (std::size_t lane = 0; lane < 4; ++lane) add_tap(lane, 900 + lane);
        const auto written = export_text(*parsed.value, wormedit::stepmania_profile_t::sm);
        REQUIRE(written.value.has_value());
        CHECK(written.value->find("K[") == std::string::npos);
        CHECK(warning_count_of(written) == 1);
    }
}

TEST_CASE("StepMania shared parse warnings remain independent of song presence", "[unit][stepmania][results]") {
    using namespace wormedit;
    stepmania_parse_outcome_t parsed{.diagnostics = {{.severity = diagnostic_severity_t::warning, .message = "warning"}}};
    CHECK(parsed.ok());
    const auto legacy = std::move(parsed);
    CHECK(legacy.ok());
    CHECK_FALSE(legacy.value);
    CHECK(legacy.diagnostics.front().message == "warning");
}

TEST_CASE("StepMania shared write warnings retain text", "[unit][stepmania][results]") {
    using namespace wormedit;
    stepmania_write_outcome_t written{.value = std::string{"text"},
        .diagnostics = {{.severity = diagnostic_severity_t::warning, .message = "warning"}}};
    CHECK(written.ok());
    const auto legacy = std::move(written);
    CHECK(legacy.ok());
    CHECK(legacy.value == "text");
    CHECK(legacy.diagnostics.front().message == "warning");
}

TEST_CASE("StepMania primary export policy retains required syntax rejection", "[unit][stepmania][results]") {
    using namespace wormedit;
    const auto resolved = stepmania_write_options_t::from(
        stepmania_export_policy_t{.timing_syntax = stepmania_timing_syntax_t::unspecified});
    REQUIRE_FALSE(resolved);
    CHECK(resolved.error().code == failure_code_t::invalid_request);
    CHECK(resolved.error().message == "Select a StepMania timing syntax explicitly");
}

namespace {
void expect_background(bool condition, std::source_location at = std::source_location::current()) {
    INFO("Background assertion at line " << at.line());
    REQUIRE(condition);
}
}

TEST_CASE("StepMania static backgrounds are editable and removal cannot revive retained text",
          "[unit][format][stepmania][background]") {
    auto parsed = fixture("versioned-metadata.ssc", wormedit::stepmania_profile_t::ssc);
    expect_background(parsed.value.has_value());
    expect_background(parsed.value->visual_assets.size() == 1);
    expect_background(parsed.value->charts.front().bga_events.size() == 1);
    parsed.value->visual_assets.front().source_path = "images/replacement.png";
    auto written = wormedit::write_stepmania(*parsed.value);
    expect_background(written.value.has_value());
    expect_background(written.value->find("#BACKGROUND:images/replacement.png;") != std::string::npos);
    expect_background(written.value->find("#BACKGROUND:background.png;") == std::string::npos);
    parsed.value->charts.front().bga_events.clear();
    written = wormedit::write_stepmania(*parsed.value);
    expect_background(written.value.has_value());
    expect_background(written.value->find("#BACKGROUND:") == std::string::npos);
}

TEST_CASE("StepMania refuses unsupported or unsafe background events",
          "[unit][format][stepmania][background]") {
    using namespace wormedit;
    for (int variant = 0; variant < 5; ++variant) {
        INFO(variant);
        auto parsed = fixture("versioned-metadata.ssc", stepmania_profile_t::ssc);
        expect_background(parsed.value.has_value());
        auto& chart = parsed.value->charts.front();
        auto& event = chart.bga_events.front();
        switch (variant) {
        case 0: event.position.row = 1; break;
        case 1: event.kind = visual_event_kind_t::layer; break;
        case 2: chart.bga_events.push_back(event); break;
        case 3: parsed.value->visual_assets.front().source_path = "../escape.png"; break;
        case 4: parsed.value->visual_assets.clear(); break;
        }
        const auto written = write_stepmania(*parsed.value);
        expect_background(!written.value.has_value());
        expect_background(!written.diagnostics.empty());
        expect_background(written.diagnostics.front().code == (variant < 3
            ? stepmania_write_diagnostic_code_t::unsupported_feature
            : stepmania_write_diagnostic_code_t::invalid_metadata));
    }
}

TEST_CASE("StepMania song output refuses different chart backgrounds",
          "[unit][format][stepmania][background]") {
    using namespace wormedit;
    auto parsed = fixture("versioned-metadata.ssc", stepmania_profile_t::ssc);
    expect_background(parsed.value.has_value());
    auto other = parsed.value->charts.front();
    other.id = chart_id_t{9999};
    other.bga_events.clear();
    parsed.value->charts.push_back(std::move(other));
    const auto written = write_stepmania(*parsed.value, {.profile = stepmania_profile_t::ssc});
    expect_background(!written.value.has_value());
    expect_background(!written.diagnostics.empty());
    expect_background(written.diagnostics.front().code == stepmania_write_diagnostic_code_t::conflicting_song_metadata);
}

TEST_CASE("StepMania background import obeys the event budget",
          "[unit][format][stepmania][background]") {
    const std::string source = "#BACKGROUND:bg.png;#BPMS:0=120;#NOTES:dance-single::Edit:1:0,0,0,0,0:0000;";
    auto parsed = wormedit::parse_stepmania_bytes(
        std::as_bytes(std::span{source.data(), source.size()}), {.max_events = 1});
    expect_background(!parsed.value.has_value());
    expect_background(!parsed.diagnostics.empty());
    expect_background(parsed.diagnostics.back().code == wormedit::stepmania_parse_diagnostic_code_t::event_limit);
}

TEST_CASE("StepMania retains backgrounds stored by older native projects",
          "[unit][format][stepmania][background]") {
    auto parsed = fixture("versioned-metadata.ssc", wormedit::stepmania_profile_t::ssc);
    expect_background(parsed.value.has_value());
    parsed.value->charts.front().bga_events.clear();
    const auto asset = parsed.value->visual_assets.front().id;
    std::erase_if(parsed.value->resources, [asset](const auto& resource) { return resource.asset == asset; });
    parsed.value->visual_assets.clear();
    parsed.value->metadata.format_groups.front().ordered_fields.emplace_back("BACKGROUND", "legacy.png");
    const auto written = wormedit::write_stepmania(*parsed.value);
    expect_background(written.value.has_value());
    expect_background(written.value->find("#BACKGROUND:legacy.png;") != std::string::npos);
}
