#include <wormedit/core/sound_slice.h>
#include <wormedit/core/bgm_packing.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <set>
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

auto fixture(std::string_view name) -> wormedit::bmson_parse_result_t {
    return wormedit::parse_bmson_bytes(
        read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / name));
}

auto parse_text(std::string_view source, wormedit::bmson_parse_options_t options = {})
    -> wormedit::bmson_parse_result_t {
    return wormedit::parse_bmson_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                       std::move(options));
}

auto absolute(const wormedit::chart_document_t& chart, wormedit::local_position_t position)
    -> std::optional<std::uint64_t> {
    const auto index = wormedit::build_measure_index(chart);
    if (!std::holds_alternative<wormedit::measure_index_t>(index)) return std::nullopt;
    return wormedit::absolute_row(std::get<wormedit::measure_index_t>(index), position);
}

auto has_error_code(const wormedit::bmson_parse_result_t& result,
                    wormedit::bmson_parse_diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::error &&
               diagnostic.code == code;
    });
}

auto packing_preferences_are_deterministic() -> bool {
    using namespace wormedit;
    const auto key = bgm_packing_key_t{false, 7};
    const std::vector<bgm_packing_request_t> requests{{key, 10}, {key, 10}, {key, 11}};
    const std::vector<bgm_packing_existing_t> existing{
        {key, 100, 0}, {key, 101, 0}, {key, 102, 1},
        {key, 103, 2}, {key, 104, 2}, {key, 105, 2}};
    const auto result = allocate_bgm_columns(requests, existing, 3);
    if (!result || result.lanes != std::vector<std::size_t>{2, 0, 2}) return false;

    const std::vector<bgm_packing_existing_t> tied{
        {key, 100, 2}, {key, 101, 0}};
    const std::vector<bgm_packing_request_t> same_row{{key, 5}, {key, 5}};
    const auto tie = allocate_bgm_columns(same_row, tied, 3);
    return tie && tie.lanes == std::vector<std::size_t>{0, 2};
}

auto packing_groups_spills_and_silent_rows_are_stable() -> bool {
    using namespace wormedit;
    const auto plain = bgm_packing_key_t{false, 7};
    const auto grouped_key = bgm_packing_key_t{true, 7};
    const std::vector<bgm_packing_request_t> grouped{
        {plain, 5}, {plain, 5}, {plain, 6}, {plain, 6},
        {grouped_key, 8}, {plain, 9}};
    const auto result = allocate_bgm_columns(grouped);
    if (!result || result.lanes != std::vector<std::size_t>{0, 1, 0, 1, 0, 0}) return false;

    const std::vector<bgm_packing_existing_t> namespaces{
        {plain, 100, 1}, {grouped_key, 101, 2}};
    const std::vector<bgm_packing_request_t> same_value{{plain, 4}, {grouped_key, 4}};
    const auto separated = allocate_bgm_columns(same_value, namespaces, 3);
    if (!separated || separated.lanes != std::vector<std::size_t>{1, 2}) return false;

    const std::vector<bgm_packing_request_t> silent{{std::nullopt, 5}};
    const std::vector<bgm_packing_existing_t> occupied{{bgm_packing_key_t{false, 3}, 5, 0},
                                                        {bgm_packing_key_t{true, 3}, 6, 1}};
    const auto quiet = allocate_bgm_columns(silent, occupied, 2);
    return quiet && quiet.lanes == std::vector<std::size_t>{1};
}

auto packing_rejections_are_atomic() -> bool {
    using namespace wormedit;
    const std::vector<bgm_packing_request_t> requests{{bgm_packing_key_t{false, 9}, 4}};
    const std::vector<bgm_packing_existing_t> outside{{std::nullopt, 2, 1}};
    const auto out_of_range = allocate_bgm_columns(requests, outside, 1);
    const std::vector<bgm_packing_existing_t> duplicate{{std::nullopt, 2, 0},
                                                         {std::nullopt, 2, 0}};
    const std::vector<bgm_packing_request_t> at_stacked_row{{requests.front().key, 2}};
    const auto stacked = allocate_bgm_columns(at_stacked_row, duplicate, 1);
    const std::vector<bgm_packing_request_t> invalid{{bgm_packing_key_t{true, 0}, 4}};
    const auto zero_key = allocate_bgm_columns(invalid);
    const auto capped = allocate_bgm_columns(requests, {}, 0, 0);
    const auto initial = allocate_bgm_columns({}, {}, 2, 1);
    const auto is_rejected = [](const auto& result) {
        return !result && !result.error.empty() && result.lanes.empty();
    };
    return is_rejected(out_of_range) && stacked &&
        stacked.lanes == std::vector<std::size_t>{1} && is_rejected(zero_key) &&
        is_rejected(capped) && is_rejected(initial) && requests.front().row == 4 &&
        requests.front().key->value == 9 && outside.front().lane == 1 &&
        duplicate.size() == 2 && invalid.front().key->value == 0 &&
        duplicate.front().row == 2 && duplicate.back().row == 2 &&
        capped.lane_count == 0 && initial.lane_count == 2;
}

auto packing_handles_more_than_one_thousand_requests() -> bool {
    using namespace wormedit;
    std::vector<bgm_packing_request_t> requests;
    requests.reserve(1003);
    for (std::uint64_t index = 0; index < 1003; ++index)
        requests.push_back({bgm_packing_key_t{false, index % 13 + 1}, index});
    const auto result = allocate_bgm_columns(requests);
    if (!result || result.lanes.size() != requests.size() || result.lane_count != 1) return false;
    for (std::size_t index = 0; index < requests.size(); ++index)
        if (result.lanes[index] != 0) return false;
    return true;
}

auto fixture_text(std::string_view name) -> std::string {
    const auto bytes = read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / name);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

auto channel_events_are_unique(const wormedit::chart_document_t& chart,
                               std::size_t expected_count) -> bool {
    std::set<std::uint64_t> identities;
    std::size_t count{};
    for (const auto& channel : chart.sound_channels)
        for (const auto& event : channel.events) {
            ++count;
            if (!event.id || !identities.insert(event.id.value).second) return false;
        }
    return count == expected_count;
}

auto packed_events_use_chart_lanes(const wormedit::chart_document_t& chart) -> bool {
    for (const auto& measure : chart.measures)
        for (const auto& event : measure.bgm_events)
            if (std::ranges::none_of(chart.bgm_source_lanes, [&](const auto& lane) {
                    return lane.id == event.source_lane;
                })) return false;
    return true;
}

auto raw_channel_note_count(const nlohmann::json& document) -> std::size_t {
    std::size_t count{};
    for (const auto& channel : document.at("sound_channels"))
        count += channel.at("notes").size();
    return count;
}

auto hybrid_fixture_is_classified_and_lossless() -> bool {
    using namespace wormedit;
    const auto source = fixture_text("hybrid-three-stems-200-keysounds.bmson");
    const auto original_json = nlohmann::json::parse(source);
    const auto parsed = parse_text(source);
    if (!parsed.song || parsed.song->charts.empty()) return false;
    const auto& chart = parsed.song->charts.front();
    const auto stems = std::ranges::count_if(chart.bgm_source_lanes,
        [](const auto& lane) { return lane.stem_channel.has_value(); });
    const auto packed_columns = chart.bgm_source_lanes.size() - static_cast<std::size_t>(stems);
    const auto stem_resources = std::ranges::count_if(parsed.song->resources,
        [](const auto& resource) { return resource.sound_kind == sound_kind_t::stem; });
    const auto keysounds = std::ranges::count_if(parsed.song->resources,
        [](const auto& resource) { return resource.sound_kind == sound_kind_t::keysound; });
    if (chart.sound_channels.size() != 203 || stems != 3 || packed_columns != 1 ||
        stem_resources != 3 || keysounds != 200 ||
        !channel_events_are_unique(chart, raw_channel_note_count(original_json)) ||
        !packed_events_use_chart_lanes(chart)) return false;
    const auto written = write_bmson(*parsed.song);
    return written.json && !written.has_errors() && *written.json == source &&
        nlohmann::json::parse(*written.json) == original_json;
}

auto continuation_controls_stem_classification() -> bool {
    using namespace wormedit;
    const auto parsed = parse_text(R"({"version":"1.0.0","info":{"init_bpm":120},
      "sound_channels":[
        {"name":"playable.wav","notes":[{"x":1,"y":0,"c":false}]},
        {"name":"stem.wav","notes":[{"x":1,"y":0,"c":false},
          {"x":1,"y":240,"c":true}]}
      ]})");
    if (!parsed.song || parsed.song->charts.empty()) return false;
    const auto& chart = parsed.song->charts.front();
    if (chart.sound_channels.size() != 2 || chart.bgm_source_lanes.size() != 1 ||
        chart.bgm_source_lanes.front().stem_channel != chart.sound_channels[1].id) return false;
    const auto playable = std::ranges::find(parsed.song->resources, std::string{"playable.wav"},
        &resource_entry_t::name);
    const auto stem = std::ranges::find(parsed.song->resources, std::string{"stem.wav"},
        &resource_entry_t::name);
    return playable != parsed.song->resources.end() && stem != parsed.song->resources.end() &&
        playable->sound_kind == sound_kind_t::keysound && stem->sound_kind == sound_kind_t::stem;
}

} // namespace

namespace {
template <typename T>
void check_bmson_condition(const T& condition) { CHECK(static_cast<bool>(condition)); }
template <typename T>
void require_bmson_condition(const T& condition) { REQUIRE(static_cast<bool>(condition)); }
} // namespace

TEST_CASE("BGM column packing prefers established groups deterministically",
          "[unit][format][bmson][bgm-packing]") {
    check_bmson_condition(packing_preferences_are_deterministic());
}

TEST_CASE("BGM column packing groups, spills and handles silent rows",
          "[unit][format][bmson][bgm-packing]") {
    check_bmson_condition(packing_groups_spills_and_silent_rows_are_stable());
}

TEST_CASE("BGM column packing rejects invalid input atomically",
          "[unit][format][bmson][bgm-packing]") {
    check_bmson_condition(packing_rejections_are_atomic());
}

TEST_CASE("BGM column packing handles more than one thousand requests",
          "[unit][format][bmson][bgm-packing]") {
    check_bmson_condition(packing_handles_more_than_one_thousand_requests());
}

TEST_CASE("BMSON hybrid fixture classifies stems and round-trips packed keysounds",
          "[unit][format][bmson][hybrid-lanes]") {
    check_bmson_condition(hybrid_fixture_is_classified_and_lossless());
}

TEST_CASE("BMSON continuation markers alone make a channel a stem",
          "[unit][format][bmson][hybrid-lanes]") {
    check_bmson_condition(continuation_controls_stem_classification());
}

TEST_CASE("BMSON 1.0 fixtures retain source bytes and valid document structure",
          "[unit][format][bmson]") {
    for (const auto name : {"slicing-and-mixed-sounds.bmson", "published-vox-slicing.bmson",
                            "conflicting-continuation-flags.bmson", "zero-resolution-default.bmson",
                            "null-resolution-default.bmson", "omitted-resolution-default.bmson"}) {
        DYNAMIC_SECTION(name) {
            const auto result = fixture(name);
            REQUIRE(result.song.has_value());
            CHECK_FALSE(result.has_errors());
            CHECK(wormedit::validate_document(*result.song).empty());
            REQUIRE(result.song->metadata.format_groups.size() == 1);
            const auto& song_source = result.song->metadata.format_groups.front().encoding;
            CHECK(song_source.detected == wormedit::text_encoding_t::utf8);
            CHECK(song_source.unchanged_since_import);
            REQUIRE(song_source.original_bytes);
            REQUIRE(result.song->charts.size() == 1);
            REQUIRE(result.song->charts.front().metadata.format_groups.size() == 1);
            CHECK(result.song->charts.front().metadata.format_groups.front().encoding.original_bytes ==
                  song_source.original_bytes);
        }
    }
}

TEST_CASE("BMSON pulse geometry, bar lines, timing and mixed sound slices are materialized",
          "[unit][format][bmson]") {
    const auto result = fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(result.song.has_value());
    const auto& chart = result.song->charts.front();
    CHECK(chart.divisions_per_standard_measure == 960);
    REQUIRE(chart.measures.size() == 3);
    CHECK((chart.measures[0].definition.length == wormedit::rational_t{1, 1}));
    CHECK((chart.measures[1].definition.length == wormedit::rational_t{3, 4}));
    CHECK((chart.measures[2].definition.length == wormedit::rational_t{1, 1}));
    REQUIRE(chart.bpms.size() == 2);
    CHECK((chart.bpms[0].beats_per_minute == wormedit::signed_rational_t{120, 1}));
    CHECK((chart.bpms[1].beats_per_minute == wormedit::signed_rational_t{150, 1}));
    REQUIRE(chart.pauses.size() == 1);
    CHECK(chart.pauses[0].kind == wormedit::pause_kind_t::stop);
    CHECK(chart.pauses[0].source_unit == wormedit::pause_unit_t::beats);
    CHECK((chart.pauses[0].duration == wormedit::signed_rational_t{1, 2}));
    REQUIRE(chart.sound_channels.size() == 2);
    CHECK(chart.sound_channels[0].events.size() == 4);
    // x=0 at pulse 240 remains in the source channel, but has no semantic BGM event
    // because that slice also has x=3.
    CHECK(chart.sound_channels[1].events.size() == 3);
    CHECK(std::ranges::any_of(chart.sound_channels[1].events, [](const auto& event) {
        return !event.playable && event.position.row == 240;
    }));
    CHECK(chart.measures[0].bgm_events.empty());
    REQUIRE(chart.measures[0].notes.size() == 4);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            REQUIRE(note.sound_channel_event.has_value());
            const auto linked = std::ranges::find_if(chart.sound_channels, [&](const auto& channel) {
                return std::ranges::any_of(channel.events, [&](const auto& event) {
                    return event.id == *note.sound_channel_event && event.playable &&
                           event.position == note.position && event.end == note.end &&
                           event.lane == note.lane;
                });
            });
            CHECK(linked != chart.sound_channels.end());
        }
    }
    CHECK(absolute(chart, chart.measures[0].notes[0].position) == 0);
    CHECK(absolute(chart, chart.measures[0].notes[1].position) == 240);
    REQUIRE(chart.measures[1].notes.size() == 2);
    const auto hold = std::ranges::find_if(chart.measures[1].notes, [](const auto& note) {
        return note.kind == wormedit::note_kind_t::hold;
    });
    REQUIRE(hold != chart.measures[1].notes.end());
    CHECK(absolute(chart, hold->position) == 960);
    REQUIRE(hold->end.has_value());
    CHECK(absolute(chart, *hold->end) == 1440);
    CHECK(result.song->visual_assets.size() == 1);
    REQUIRE(chart.bga_events.size() == 1);
    CHECK(chart.bga_events.front().kind == wormedit::visual_event_kind_t::background);

    auto mismatched_resource = *result.song;
    REQUIRE(mismatched_resource.resources.size() >= 2);
    auto& mismatched_note = mismatched_resource.charts.front().measures.front().notes.front();
    const auto original_resource = *mismatched_note.sound;
    const auto replacement = std::ranges::find_if(
        mismatched_resource.resources,
        [&](const auto& resource) { return resource.id != original_resource; });
    REQUIRE(replacement != mismatched_resource.resources.end());
    mismatched_note.sound = replacement->id;
    const auto mismatch_diagnostics = wormedit::validate_document(mismatched_resource);
    CHECK(std::ranges::any_of(mismatch_diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::diagnostic_code_t::dangling_resource;
    }));
}

TEST_CASE("BMSON published slicing derives exact shared source-frame ranges",
          "[unit][format][bmson][slicing]") {
    const auto parsed = fixture("published-vox-slicing.bmson");
    REQUIRE(parsed.song.has_value());
    const auto& chart = parsed.song->charts.front();
    REQUIRE(chart.sound_channels.size() == 1);

    const auto slices = wormedit::derive_sound_channel_slices(
        chart, chart.sound_channels.front(), 48'000, 200'000);
    REQUIRE(slices);
    REQUIRE(slices.slices.size() == 6);
    // The restart at pulse 720 rewinds the source; the preceding slice still
    // ends at frame 48,000, measured from the first anchor at pulse 240.
    const std::array expected_begin{0ULL, 12'000ULL, 0ULL, 12'000ULL, 48'000ULL, 96'000ULL};
    const std::array<std::optional<std::uint64_t>, 6> expected_end{
        12'000ULL, 48'000ULL, 12'000ULL, 48'000ULL, 96'000ULL, 200'000ULL};
    const std::array expected_restart{true, false, true, false, false, false};
    const std::array<std::size_t, 6> expected_event_count{1, 2, 2, 2, 1, 1};
    for (std::size_t index = 0; index < slices.slices.size(); ++index) {
        CHECK(slices.slices[index].source_begin_frame == expected_begin[index]);
        CHECK(slices.slices[index].source_end_frame == expected_end[index]);
        CHECK(slices.slices[index].restart == expected_restart[index]);
        CHECK(slices.slices[index].events.size() == expected_event_count[index]);
        for (const auto event : slices.slices[index].events) {
            CHECK(wormedit::find_sound_slice(slices, event) == &slices.slices[index]);
        }
    }

    auto clipped = chart.sound_channels.front();
    clipped.source_clip.begin_frame = 100;
    clipped.source_clip.end_frame = 150'100;
    const auto clipped_slices =
        wormedit::derive_sound_channel_slices(chart, clipped, 48'000, 300'000);
    REQUIRE(clipped_slices);
    CHECK(clipped_slices.slices.front().source_begin_frame == 100);
    CHECK(clipped_slices.slices.back().source_end_frame == 150'100);

    CHECK_FALSE(wormedit::derive_sound_channel_slices(chart, clipped, 0));
    CHECK_FALSE(wormedit::derive_sound_channel_slices(
        chart, clipped, 48'000, 300'000, {.maximum_events = 8}));
}

TEST_CASE("BMSON continuation collisions normalize to restart while preserving source spelling",
          "[unit][format][bmson]") {
    const auto result = fixture("conflicting-continuation-flags.bmson");
    REQUIRE(result.song.has_value());
    const auto& channel = result.song->charts.front().sound_channels.front();
    REQUIRE(channel.events.size() == 3);
    CHECK_FALSE(channel.events[0].continuation);
    CHECK_FALSE(channel.events[1].continuation);
    CHECK(channel.events[2].continuation);
    CHECK(channel.events[0].provenance.source_spelling.find("\"c\": true") != std::string::npos);
    CHECK(channel.events[1].provenance.source_spelling.find("\"c\": false") != std::string::npos);
}

TEST_CASE("BMSON resolution zero null and omission all select 240 and retain spelling",
          "[unit][format][bmson]") {
    for (const auto [name, spelling] : {
             std::pair{"zero-resolution-default.bmson", "0"},
             std::pair{"null-resolution-default.bmson", "null"},
             std::pair{"omitted-resolution-default.bmson", "<omitted>"}}) {
        const auto result = fixture(name);
        REQUIRE(result.song.has_value());
        const auto& chart = result.song->charts.front();
        CHECK(chart.divisions_per_standard_measure == 960);
        const auto& fields = chart.metadata.format_groups.front().ordered_fields;
        const auto resolution = std::ranges::find(fields, std::string("resolution"),
                                                   &std::pair<std::string, std::string>::first);
        REQUIRE(resolution != fields.end());
        CHECK(resolution->second == spelling);
    }
}

TEST_CASE("BMSON same-source channels share one audio asset while retaining independent resources",
          "[unit][format][bmson]") {
    const auto result = parse_text(R"({
      "version":"1.0.0", "info":{"title":"shared","artist":"a","init_bpm":120},
      "sound_channels":[
        {"name":"same.wav","notes":[{"x":0,"y":0,"c":false}]},
        {"name":"same.wav","notes":[{"x":0,"y":240,"c":true}]}
      ]
    })");
    REQUIRE(result.song.has_value());
    CHECK_FALSE(result.has_errors());
    CHECK(result.song->audio_assets.size() == 1);
    CHECK(result.song->resources.size() == 2);
    const auto& chart = result.song->charts.front();
    REQUIRE(chart.measures.front().bgm_events.size() == 2);
    REQUIRE(chart.measures[0].bgm_events[0].sound.has_value());
    REQUIRE(chart.measures[0].bgm_events[1].sound.has_value());
    const auto& bgm1 = chart.measures[0].bgm_events[0];
    const auto& bgm2 = chart.measures[0].bgm_events[1];
    REQUIRE(bgm1.sound_channel_event.has_value());
    REQUIRE(bgm2.sound_channel_event.has_value());
    CHECK(bgm1.sound_channel_event != bgm2.sound_channel_event);
    const auto resource_asset = [&](wormedit::resource_id_t resource) {
        const auto found = std::ranges::find(result.song->resources, resource,
                                             &wormedit::resource_entry_t::id);
        REQUIRE(found != result.song->resources.end());
        return found->asset;
    };
    CHECK(resource_asset(*bgm1.sound) == resource_asset(*bgm2.sound));
    CHECK(wormedit::validate_document(*result.song).empty());
}

TEST_CASE("BMSON non-playable source-channel events use lane zero but playable events cannot",
          "[unit][format][bmson]") {
    const auto bgm = fixture("published-vox-slicing.bmson");
    REQUIRE(bgm.song.has_value());
    const auto& chart = bgm.song->charts.front();
    const auto& bgm_channel = chart.sound_channels.front();
    const auto bgm_event = std::ranges::find_if(bgm_channel.events,
                                                [](const auto& event) { return !event.playable; });
    REQUIRE(bgm_event != bgm_channel.events.end());
    CHECK_FALSE(bgm_event->lane);
    const auto semantic_bgm = std::ranges::find_if(chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    REQUIRE(semantic_bgm != chart.measures.end());
    REQUIRE(semantic_bgm->bgm_events.front().sound_channel_event.has_value());
    CHECK(*semantic_bgm->bgm_events.front().sound_channel_event == bgm_event->id);
    CHECK(wormedit::validate_document(*bgm.song).empty());

    auto invalid = *bgm.song;
    auto& playable = invalid.charts.front().sound_channels.front().events.front();
    REQUIRE(playable.playable);
    playable.lane = {};
    const auto diagnostics = wormedit::validate_document(invalid);
    CHECK(std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::diagnostic_code_t::unknown_lane;
    }));

    auto dangling_link = *bgm.song;
    const auto linked_measure = std::ranges::find_if(dangling_link.charts.front().measures,
                                                     [](const auto& measure) {
                                                         return !measure.bgm_events.empty();
                                                     });
    REQUIRE(linked_measure != dangling_link.charts.front().measures.end());
    linked_measure->bgm_events.front().sound_channel_event = wormedit::object_id_t{999999};
    const auto link_diagnostics = wormedit::validate_document(dangling_link);
    CHECK(std::ranges::any_of(link_diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::diagnostic_code_t::invalid_id;
    }));

    auto wrong_role = *bgm.song;
    const auto playable_event = std::ranges::find_if(
        wrong_role.charts.front().sound_channels.front().events,
        [](const auto& event) { return event.playable; });
    REQUIRE(playable_event != wrong_role.charts.front().sound_channels.front().events.end());
    const auto wrong_role_measure = std::ranges::find_if(wrong_role.charts.front().measures,
                                                        [](const auto& measure) {
                                                            return !measure.bgm_events.empty();
                                                        });
    REQUIRE(wrong_role_measure != wrong_role.charts.front().measures.end());
    wrong_role_measure->bgm_events.front().sound_channel_event = playable_event->id;
    const auto role_diagnostics = wormedit::validate_document(wrong_role);
    CHECK(std::ranges::any_of(role_diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::diagnostic_code_t::invalid_id;
    }));

    auto duplicate_link = *bgm.song;
    const auto duplicate_measure = std::ranges::find_if(duplicate_link.charts.front().measures,
                                                        [](const auto& measure) {
                                                            return !measure.bgm_events.empty();
                                                        });
    REQUIRE(duplicate_measure != duplicate_link.charts.front().measures.end());
    auto duplicate_event = duplicate_measure->bgm_events.front();
    duplicate_event.id = wormedit::object_id_t{999998};
    duplicate_measure->bgm_events.push_back(std::move(duplicate_event));
    const auto duplicate_diagnostics = wormedit::validate_document(duplicate_link);
    CHECK(std::ranges::any_of(duplicate_diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::diagnostic_code_t::duplicate_id;
    }));
}

TEST_CASE("BMSON malformed pulse and resolution fixtures produce typed errors",
          "[unit][format][bmson]") {
    const auto wrong_resolution = fixture("negative-wrong-resolution-type.bmson");
    CHECK_FALSE(wrong_resolution.song.has_value());
    CHECK(has_error_code(wrong_resolution, wormedit::bmson_parse_diagnostic_code_t::invalid_type));
    for (const auto name : {"negative-pulse-position.bmson", "negative-fractional-pulse-position.bmson"}) {
        const auto result = fixture(name);
        CHECK_FALSE(result.song.has_value());
        CHECK_FALSE(result.diagnostics.empty());
        CHECK(result.diagnostics.front().byte_offset.has_value());
    }
    const auto overflow = fixture("negative-overflow-pulse-position.bmson");
    CHECK_FALSE(overflow.song.has_value());
    CHECK(has_error_code(overflow, wormedit::bmson_parse_diagnostic_code_t::integer_out_of_range));
    const auto negative_resolution = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120,"resolution":-240}})");
    REQUIRE(negative_resolution.song.has_value());
    CHECK(negative_resolution.song->charts.front().divisions_per_standard_measure == 960);
    const auto& fields = negative_resolution.song->charts.front().metadata.format_groups.front().ordered_fields;
    const auto resolution = std::ranges::find(fields, std::string("resolution"),
                                               &std::pair<std::string, std::string>::first);
    REQUIRE(resolution != fields.end());
    CHECK(resolution->second == "-240");
}

TEST_CASE("BMSON reader enforces source bounds and rejects malformed JSON without partial documents",
          "[unit][format][bmson]") {
    wormedit::bmson_parse_options_t options;
    options.max_source_bytes = 4;
    const auto too_large = parse_text("{\"x\":1}", options);
    CHECK_FALSE(too_large.song.has_value());
    CHECK(has_error_code(too_large, wormedit::bmson_parse_diagnostic_code_t::invalid_value));

    const auto malformed = parse_text("{\"version\":\"1.0.0\",}");
    CHECK_FALSE(malformed.song.has_value());
    CHECK(has_error_code(malformed, wormedit::bmson_parse_diagnostic_code_t::invalid_json));

    const auto enormous_number = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":1e1000000}})");
    CHECK_FALSE(enormous_number.song.has_value());
    CHECK_FALSE(enormous_number.diagnostics.empty());
}

TEST_CASE("BMSON JSON parser retains exact spans and enforces structural limits",
          "[unit][format][bmson]") {
    constexpr std::string_view exact_source =
        R"({"version":"1.0.0","info":{"init_bpm":120},"bpm_events":[ { "y" : 0, "bpm" : 1.20e2 } ]})";
    const auto exact = parse_text(exact_source);
    REQUIRE(exact.song.has_value());
    const auto& chart = exact.song->charts.front();
    REQUIRE(chart.bpms.size() == 1);
    CHECK(chart.bpms.front().provenance.source_spelling == R"({ "y" : 0, "bpm" : 1.20e2 })");
    CHECK((chart.bpms.front().beats_per_minute == wormedit::signed_rational_t{120, 1}));

    const std::string duplicate_source =
        R"({"version":"1.0.0","info":{"init_bpm":120,"init_bpm":121}})";
    const auto duplicate = parse_text(duplicate_source);
    CHECK_FALSE(duplicate.song.has_value());
    CHECK(has_error_code(duplicate, wormedit::bmson_parse_diagnostic_code_t::invalid_json));
    REQUIRE_FALSE(duplicate.diagnostics.empty());
    REQUIRE(duplicate.diagnostics.front().byte_offset.has_value());
    CHECK(*duplicate.diagnostics.front().byte_offset == duplicate_source.rfind("\"init_bpm\""));

    wormedit::bmson_parse_options_t depth_options;
    depth_options.max_nesting_depth = 0;
    const auto too_deep = parse_text(R"({"version":"1.0.0","info":{"init_bpm":120}})",
                                     depth_options);
    CHECK_FALSE(too_deep.song.has_value());
    CHECK(has_error_code(too_deep, wormedit::bmson_parse_diagnostic_code_t::invalid_json));

    wormedit::bmson_parse_options_t value_options;
    value_options.max_json_values = 1;
    const auto too_many = parse_text(R"({"version":"1.0.0","info":{"init_bpm":120}})",
                                     value_options);
    CHECK_FALSE(too_many.song.has_value());
    CHECK(has_error_code(too_many, wormedit::bmson_parse_diagnostic_code_t::invalid_json));

    std::string invalid_utf8 = R"({"version":"1.0.0","info":{"init_bpm":120},"x":" )";
    invalid_utf8.push_back(static_cast<char>(0xff));
    const auto invalid_encoding = parse_text(invalid_utf8);
    CHECK_FALSE(invalid_encoding.song.has_value());
    CHECK(has_error_code(invalid_encoding, wormedit::bmson_parse_diagnostic_code_t::invalid_json));
}

TEST_CASE("BMSON fixed beat modes use canonical x positions and reject out-of-range notes",
          "[unit][format][bmson]") {
    const auto default_mode = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[{"name":"a.wav","notes":[{"x":8,"y":0}]}]})");
    require_bmson_condition(default_mode.song.has_value());
    const auto& default_chart = default_mode.song->charts.front();
    require_bmson_condition(default_chart.lane_layouts.size() == 1);
    check_bmson_condition(default_chart.lane_layouts.front().layout.display_name == "beat-7k");
    require_bmson_condition(default_chart.lane_layouts.front().layout.lanes.size() == 8);
    check_bmson_condition(default_chart.lane_layouts.front().layout.lanes.front().role == wormedit::lane_role_t::scratch);
    check_bmson_condition(default_chart.lane_layouts.front().layout.lanes.front().display_order == 0);
    require_bmson_condition(default_chart.measures.front().notes.size() == 1);
    check_bmson_condition(default_chart.measures.front().notes.front().lane.value == 100008);

    const auto default_out_of_range = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[{"name":"a.wav","notes":[{"x":9,"y":0}]}]})");
    CHECK_FALSE(default_out_of_range.song.has_value());
    check_bmson_condition(has_error_code(default_out_of_range, wormedit::bmson_parse_diagnostic_code_t::invalid_value));

    const auto beat_5k = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":"beat-5k"},"sound_channels":[{"name":"a.wav","notes":[{"x":8,"y":0}]}]})");
    require_bmson_condition(beat_5k.song.has_value());
    const auto& beat_5k_layout = beat_5k.song->charts.front().lane_layouts.front().layout;
    require_bmson_condition(beat_5k_layout.lanes.size() == 6);
    check_bmson_condition(beat_5k_layout.lanes.front().id.value == 100008);
    check_bmson_condition(beat_5k_layout.lanes.front().role == wormedit::lane_role_t::scratch);
    check_bmson_condition(beat_5k_layout.lanes[1].id.value == 100001);
    check_bmson_condition(beat_5k_layout.lanes[1].display_order == 1);

    const auto beat_10k = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":"beat-10k"},"sound_channels":[{"name":"a.wav","notes":[{"x":16,"y":0}]}]})");
    require_bmson_condition(beat_10k.song.has_value());
    const auto& beat_10k_chart = beat_10k.song->charts.front();
    require_bmson_condition(beat_10k_chart.lane_layouts.front().layout.lanes.size() == 12);
    require_bmson_condition(beat_10k_chart.measures.front().notes.size() == 1);
    check_bmson_condition(beat_10k_chart.measures.front().notes.front().lane.value == 100016);
    check_bmson_condition(beat_10k_chart.lane_layouts.front().layout.lanes.back().role == wormedit::lane_role_t::scratch);
    check_bmson_condition(beat_10k_chart.lane_layouts.front().layout.lanes.front().id.value == 100008);
    check_bmson_condition(beat_10k_chart.lane_layouts.front().layout.lanes[6].id.value == 100009);
    check_bmson_condition(beat_10k_chart.lane_layouts.front().layout.lanes[6].display_order == 6);
}

TEST_CASE("BMSON imported beat and popn modes receive their display colors",
          "[unit][format][bmson][layout]") {
    using color = wormedit::lane_key_color_t;
    const auto alternating = [](std::size_t count) {
        std::vector<color> result{color::scratch};
        for (std::size_t index = 0; index < count; ++index)
            result.push_back(index % 2 == 0 ? color::white : color::black);
        return result;
    };
    auto beat_10 = alternating(5);
    for (std::size_t index = 0; index < 5; ++index)
        beat_10.push_back(index % 2 == 0 ? color::white : color::black);
    beat_10.push_back(color::scratch);
    auto beat_14 = alternating(7);
    for (std::size_t index = 0; index < 7; ++index)
        beat_14.push_back(index % 2 == 0 ? color::white : color::black);
    beat_14.push_back(color::scratch);
    const std::vector<std::pair<std::string_view, std::vector<color>>> cases{
        {"beat-5k", alternating(5)},
        {"beat-7k", alternating(7)},
        {"beat-10k", beat_10},
        {"beat-14k", beat_14},
        {"popn-9k", std::vector<color>{color::pms_white, color::pms_yellow, color::pms_green,
                                       color::pms_blue, color::pms_red, color::pms_blue,
                                       color::pms_green, color::pms_yellow, color::pms_white}},
    };
    for (const auto& [hint, expected] : cases) {
        CAPTURE(hint);
        auto source = std::string{R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":")"};
        source.append(hint);
        source.append(R"("},"sound_channels":[]})");
        const auto parsed = parse_text(source);
        require_bmson_condition(parsed.song);
        const auto& lanes = parsed.song->charts.front().lane_layouts.front().layout.lanes;
        require_bmson_condition(lanes.size() == expected.size());
        for (std::size_t index = 0; index < expected.size(); ++index) {
            check_bmson_condition(lanes[index].display_order == index);
            check_bmson_condition(lanes[index].key_color == expected[index]);
        }
    }
}

TEST_CASE("BMSON generic 24K and 48K modes retain their fixed keyboard geometry",
          "[unit][format][bmson][layout]") {
    const auto generic_24 = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":"generic-24keys"},"sound_channels":[{"name":"a.wav","notes":[{"x":24,"y":0}]}]})");
    REQUIRE(generic_24.song.has_value());
    const auto& layout_24 = generic_24.song->charts.front().lane_layouts.front().layout;
    CHECK(layout_24.profile_id == "bmson-generic-24keys");
    CHECK(layout_24.lanes.size() == 24);
    CHECK(layout_24.bmson_generic_keys_only);
    CHECK_FALSE(layout_24.split_stage);

    const auto generic_48 = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":"generic-48keys"},"sound_channels":[{"name":"a.wav","notes":[{"x":48,"y":0}]}]})");
    REQUIRE(generic_48.song.has_value());
    const auto& layout_48 = generic_48.song->charts.front().lane_layouts.front().layout;
    CHECK(layout_48.profile_id == "bmson-generic-48keys");
    CHECK(layout_48.lanes.size() == 48);
    CHECK(layout_48.bmson_generic_keys_only);
    CHECK(layout_48.split_stage);
    CHECK(layout_48.split_after_column == 24);
    CHECK(layout_48.lanes[24].stage == wormedit::stage_group_t::right);

    const auto invalid = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":"generic-24keys"},"sound_channels":[{"name":"a.wav","notes":[{"x":25,"y":0}]}]})");
    CHECK_FALSE(invalid.song.has_value());
}

TEST_CASE("BMSON x null is BGM and resource paths normalize both separators safely",
          "[unit][format][bmson]") {
    const auto bgm = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[{"name":"sub\\folder/tone.wav","notes":[{"x":null,"y":0}]}]})");
    REQUIRE(bgm.song.has_value());
    REQUIRE(bgm.song->audio_assets.size() == 1);
    CHECK(bgm.song->audio_assets.front().source_path.generic_string() == "sub/folder/tone.wav");
    REQUIRE(bgm.song->resources.size() == 1);
    CHECK(bgm.song->resources.front().provenance.source_spelling ==
          R"("sub\\folder/tone.wav")");
    REQUIRE(bgm.song->charts.front().measures.front().bgm_events.size() == 1);
    const auto& source_event = bgm.song->charts.front().sound_channels.front().events.front();
    CHECK_FALSE(source_event.playable);
    CHECK_FALSE(source_event.lane);

    const auto nul_path = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[{"name":"tone\u0000.wav","notes":[]}]})");
    CHECK_FALSE(nul_path.song.has_value());
    CHECK(has_error_code(nul_path, wormedit::bmson_parse_diagnostic_code_t::invalid_value));

    const auto traversal = parse_text(
        R"({"version":"1.0.0","info":{"init_bpm":120},"sound_channels":[{"name":"sub\\..\\tone.wav","notes":[]}]})");
    CHECK_FALSE(traversal.song.has_value());
    CHECK(has_error_code(traversal, wormedit::bmson_parse_diagnostic_code_t::invalid_value));
}

TEST_CASE("unknown BMSON modes retain generic piano colors", "[unit][format][bmson][layout]") {
    const auto parsed = parse_text(R"({"version":"1.0.0","info":{"init_bpm":120,
        "mode_hint":"unknown"},"sound_channels":[{"name":"a.wav","notes":[{"x":3,"y":0}]}]})");
    require_bmson_condition(parsed.song);
    const auto& layout = parsed.song->charts.front().lane_layouts.front().layout;
    check_bmson_condition(layout.profile_id == "bmson-generic-nkeys");
    check_bmson_condition(layout.lanes[0].key_color == wormedit::lane_key_color_t::white);
    check_bmson_condition(layout.lanes[1].key_color == wormedit::lane_key_color_t::black);
    check_bmson_condition(layout.lanes[2].key_color == wormedit::lane_key_color_t::white);
}
