#include <wormedit/formats/bms_clipboard.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace {
auto target_document() -> wormedit::song_document_t {
    using namespace wormedit;
    song_document_t song;
    song.id = song_id_t{1};
    chart_document_t chart;
    chart.id = chart_id_t{2};
    chart.divisions_per_standard_measure = 1344;
    chart.measures.push_back({.id = measure_id_t{10}});
    lane_layout_t layout;
    constexpr const char* channels[]{"16", "11", "12", "13", "14", "15", "18", "19"};
    for (std::size_t index = 0; index < std::size(channels); ++index) {
        lane_definition_t lane;
        lane.id = lane_id_t{100 + index};
        lane.role = index == 0 ? lane_role_t::scratch : lane_role_t::playable;
        lane.display_order = static_cast<std::uint32_t>(index);
        lane.bms_channels.visible = channels[index];
        layout.lanes.push_back(std::move(lane));
    }
    chart.lane_layouts.push_back({.id = object_id_t{3}, .position = {measure_id_t{10}, 0},
        .layout = std::move(layout)});
    chart.bgm_source_lanes.push_back({.id = object_id_t{4}});
    song.charts.push_back(std::move(chart));
    for (unsigned index = 1; index <= 6; ++index) {
        resource_entry_t resource;
        resource.id = resource_id_t{200 + index};
        resource.asset = asset_id_t{300 + index};
        song.audio_assets.push_back({.id = resource.asset});
        resource.roles = {resource_role_t::sample, resource_role_t::bgm};
        resource.target_identities.push_back({.family = format_family_t::bms,
            .identifier = "0" + std::to_string(index), .namespace_name = "WAV"});
        song.resources.push_back(std::move(resource));
    }
    return song;
}

auto decode(std::string_view text, wormedit::bms_clipboard_decode_options_t options = {})
    -> wormedit::chart_clipboard_outcome_t {
    const auto song = target_document();
    return wormedit::parse_bms_chart_clipboard(text, song, song.charts.front(),
        song.charts.front().lane_layouts.front().layout, options);
}

auto fixture(const char* name) -> std::string {
    const auto path = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "bms/clipboard" / name;
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

auto fixture_name() -> const char* {
    return GENERATE("bmse-3.0-mixed.clip", "bmse-legacy-mixed.clip",
        "ibmsc-3.5.5.16-mixed.clip", "ibmsc-3.5.5.16-xnt-mixed.clip");
}

auto note_object(const wormedit::chart_clipboard_t& clipboard, std::size_t lane,
                 wormedit::note_kind_t kind) -> const wormedit::chart_clipboard_object_t* {
    const auto found = std::ranges::find_if(clipboard.objects, [=](const auto& object) {
        const auto* note = std::get_if<wormedit::note_t>(&object.payload);
        return note && note->kind == kind && object.lane_index == lane;
    });
    return found == clipboard.objects.end() ? nullptr : &*found;
}

template <class Payload>
auto first_object(const wormedit::chart_clipboard_t& clipboard)
    -> const wormedit::chart_clipboard_object_t* {
    const auto found = std::ranges::find_if(clipboard.objects, [](const auto& object) {
        return std::holds_alternative<Payload>(object.payload);
    });
    return found == clipboard.objects.end() ? nullptr : &*found;
}
} // namespace

TEST_CASE("bms_clipboard four pinned grammars normalize the ten endpoint scenario", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    CHECK(result->source_song == wormedit::song_id_t{1});
    CHECK(result->source_chart == wormedit::chart_id_t{2});
    CHECK(result->objects.size() == 9);
}

TEST_CASE("bms_clipboard free grid positions recover exact fractions", "[formats]") {
    const auto* name = fixture_name();
    const auto result = decode(fixture(name));
    REQUIRE(result);
    const auto* note = note_object(*result, 5, wormedit::note_kind_t::tap);
    REQUIRE(note);
    const auto expected = std::string_view{name}.starts_with("bmse-legacy")
        ? wormedit::rational_t{9, 64} : wormedit::rational_t{1, 7};
    CHECK(note->offset == expected);
    CHECK(std::get<wormedit::note_t>(note->payload).sound == wormedit::resource_id_t{206});
}

TEST_CASE("bms_clipboard paired and length long notes become exact native holds", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    const auto* hold = note_object(*result, 2, wormedit::note_kind_t::hold);
    REQUIRE(hold);
    CHECK((hold->offset == wormedit::rational_t{0, 1}));
    CHECK((hold->duration == wormedit::rational_t{1, 2}));
    CHECK(std::get<wormedit::note_t>(hold->payload).sound == wormedit::resource_id_t{204});
}

TEST_CASE("bms_clipboard a valid single iBMSC record is not lost to the foreign reader bug", "[formats]") {
    const auto result = decode("iBMSC Clipboard Data\r\n6 0 10000 0 0 0");
    REQUIRE(result);
    CHECK(result->objects.size() == 1);
}

TEST_CASE("bms_clipboard malformed single records reject as a whole", "[formats]") {
    const auto text = GENERATE("", "iBMSC Clipboard Data\r\n6 0 10000 0 0",
        "iBMSC Clipboard Data\r\n6 27,4285714285714 10000 0 0 0",
        "iBMSC Clipboard Data\r\n6 nan 10000 0 0 0",
        "iBMSC Clipboard Data\r\n4 0 10000 0 0 0",
        "iBMSC Clipboard Data\r\n6 0 70000 0 0 0",
        "iBMSC Clipboard Data\r\n6 0 10000 -1 0 0",
        "iBMSC Clipboard Data xNT\r\n6 0 10000 -1 0 0",
        "iBMSC Clipboard Data\n6 0 10000 0 0 0");
    const auto result = decode(text);
    CHECK_FALSE(result);
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("bms_clipboard byte and record limits reject before partial publication", "[formats]") {
    const auto text = "iBMSC Clipboard Data\r\n6 0 10000 0 0 0";
    CHECK_FALSE(decode(text, {.maximum_bytes = 1}));
    CHECK_FALSE(decode(text, {.maximum_records = 0}));
}

TEST_CASE("bms_clipboard fixtures preserve invisible semantics", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    const auto* note = note_object(*result, 3, wormedit::note_kind_t::invisible);
    REQUIRE(note);
    CHECK((note->offset == wormedit::rational_t{1, 8}));
    CHECK(std::get<wormedit::note_t>(note->payload).sound == wormedit::resource_id_t{205});
}

TEST_CASE("bms_clipboard fixtures preserve mine semantics", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    const auto* note = note_object(*result, 4, wormedit::note_kind_t::mine);
    REQUIRE(note);
    CHECK((note->offset == wormedit::rational_t{3, 8}));
    CHECK_FALSE(std::get<wormedit::note_t>(note->payload).sound);
}

TEST_CASE("bms_clipboard fixtures preserve visible key assignments", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    const auto* note = note_object(*result, 1, wormedit::note_kind_t::tap);
    REQUIRE(note);
    CHECK((note->offset == wormedit::rational_t{0, 1}));
    CHECK(std::get<wormedit::note_t>(note->payload).sound == wormedit::resource_id_t{201});
}

TEST_CASE("bms_clipboard fixtures preserve scratch assignments", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    const auto* note = note_object(*result, 0, wormedit::note_kind_t::tap);
    REQUIRE(note);
    CHECK((note->offset == wormedit::rational_t{1, 4}));
    CHECK(std::get<wormedit::note_t>(note->payload).sound == wormedit::resource_id_t{202});
}

TEST_CASE("bms_clipboard fixtures preserve exact BPM", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    const auto* bpm = first_object<wormedit::bpm_event_t>(*result);
    REQUIRE(bpm);
    CHECK((bpm->offset == wormedit::rational_t{0, 1}));
    CHECK((std::get<wormedit::bpm_event_t>(bpm->payload).beats_per_minute == wormedit::signed_rational_t{150, 1}));
}

TEST_CASE("bms_clipboard fixtures preserve BMS STOP ticks", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    const auto* stop = first_object<wormedit::pause_event_t>(*result);
    REQUIRE(stop);
    CHECK((stop->offset == wormedit::rational_t{3, 4}));
    CHECK((std::get<wormedit::pause_event_t>(stop->payload).duration == wormedit::signed_rational_t{48, 1}));
    CHECK(std::get<wormedit::pause_event_t>(stop->payload).source_unit == wormedit::pause_unit_t::bms_stop_ticks);
}

TEST_CASE("bms_clipboard fixtures preserve BGM lane and sound", "[formats]") {
    const auto result = decode(fixture(fixture_name()));
    REQUIRE(result);
    const auto* bgm = first_object<wormedit::bgm_event_t>(*result);
    REQUIRE(bgm);
    CHECK((bgm->offset == wormedit::rational_t{1, 2}));
    CHECK(bgm->lane_index == 0);
    CHECK(std::get<wormedit::bgm_event_t>(bgm->payload).sound == wormedit::resource_id_t{203});
}

TEST_CASE("bms_clipboard rejects unknown duplicate and unrepresentable records", "[formats]") {
    const auto text = GENERATE("unknown header\r\n6 0 10000 0 0 0",
        "iBMSC Clipboard Data\r\n999 0 10000 0 0 0",
        "iBMSC Clipboard Data\r\n6 0 10000 0 0 0\r\n6 0 10000 0 0 0",
        "iBMSC Clipboard Data\r\n6 0 10000 0 0 0\r\n06 0.0 10000 0 0 0",
        "iBMSC Clipboard Data\r\n6 0.000001 10000 0 0 0",
        "BMSE3.0 ClipBoard Object Data Format\r\nZZZ000000000000000001\r\n",
        "BMSE3.0 ClipBoard Object Data Format\r\n0110ZZZZZZZZZZZZZZZZ1\r\n",
        "BMSE3.0 ClipBoard Object Data Format\r\n0110" "0000000000000000" "1\r\n\r\n",
        "BMSE ClipBoard Object Data Format\r\n011000000001");
    CHECK_FALSE(decode(text));
}

TEST_CASE("bms_clipboard rejects a real batch one record over its bound", "[formats]") {
    const auto text = "iBMSC Clipboard Data\r\n6 0 10000 0 0 0\r\n6 48 10000 0 0 0";
    CHECK_FALSE(decode(text, {.maximum_records = 1}));
    CHECK_FALSE(decode(text, {.maximum_bytes = std::char_traits<char>::length(text) - 1}));
}

TEST_CASE("bms_clipboard hexadecimal resource numbering is explicit", "[formats]") {
    auto song = target_document();
    song.resources.front().target_identities.front().identifier = "10";
    const auto& chart = song.charts.front();
    const auto& layout = chart.lane_layouts.front().layout;
    const auto text = "BMSE3.0 ClipBoard Object Data Format\r\n0110000000000000000016\r\n";
    const auto result = wormedit::parse_bms_chart_clipboard(text, song, chart, layout,
        {.object_numbering = wormedit::bms_clipboard_numbering_t::base16});
    REQUIRE(result);
    REQUIRE(result->objects.size() == 1);
    CHECK(std::get<wormedit::note_t>(result->objects.front().payload).sound == wormedit::resource_id_t{201});
    CHECK_FALSE(wormedit::parse_bms_chart_clipboard(text, song, chart, layout));
}

TEST_CASE("bms_clipboard base62 resource numbering is explicit", "[formats]") {
    auto song = target_document();
    song.resources.front().target_identities.front().identifier = "0a";
    const auto& chart = song.charts.front();
    const auto& layout = chart.lane_layouts.front().layout;
    const auto text = "BMSE3.0 ClipBoard Object Data Format\r\n0110000000000000000036\r\n";
    const auto result = wormedit::parse_bms_chart_clipboard(text, song, chart, layout,
        {.object_numbering = wormedit::bms_clipboard_numbering_t::base62});
    REQUIRE(result);
    REQUIRE(result->objects.size() == 1);
    CHECK(std::get<wormedit::note_t>(result->objects.front().payload).sound == wormedit::resource_id_t{201});
    CHECK_FALSE(wormedit::parse_bms_chart_clipboard(text, song, chart, layout));
}

TEST_CASE("bms_clipboard ambiguous resource identities reject instead of choosing one", "[formats]") {
    auto song = target_document();
    auto duplicate = song.resources.front();
    duplicate.id = wormedit::resource_id_t{299};
    song.resources.push_back(std::move(duplicate));
    const auto& chart = song.charts.front();
    CHECK_FALSE(wormedit::parse_bms_chart_clipboard("iBMSC Clipboard Data\r\n6 0 10000 0 0 0",
        song, chart, chart.lane_layouts.front().layout));
}

TEST_CASE("bms_clipboard ambiguous target lane channels reject instead of choosing one", "[formats]") {
    auto song = target_document();
    auto& chart = song.charts.front();
    auto& layout = chart.lane_layouts.front().layout;
    layout.lanes[2].bms_channels.visible = "11";
    CHECK_FALSE(wormedit::parse_bms_chart_clipboard("iBMSC Clipboard Data\r\n6 0 10000 0 0 0",
        song, chart, layout));
}

TEST_CASE("bms_clipboard a missing target BGM lane rejects without fabricating geometry", "[formats]") {
    auto song = target_document();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes.clear();
    CHECK_FALSE(wormedit::parse_bms_chart_clipboard("iBMSC Clipboard Data\r\n27 0 30000 0 0 0",
        song, chart, chart.lane_layouts.front().layout));
}

TEST_CASE("bms_clipboard pairs reordered iBMSC long-note endpoints", "[formats]") {
    const auto result = decode("iBMSC Clipboard Data\r\n7 96 40000 -1 0 0\r\n7 0 40000 -1 0 0");
    REQUIRE(result);
    REQUIRE(result->objects.size() == 1);
    const auto& object = result->objects.front();
    CHECK((object.offset == wormedit::rational_t{0, 1}));
    CHECK((object.duration == wormedit::rational_t{1, 2}));
    CHECK(std::get<wormedit::note_t>(object.payload).kind == wormedit::note_kind_t::hold);
    CHECK(std::get<wormedit::note_t>(object.payload).sound == wormedit::resource_id_t{204});
}

TEST_CASE("bms_clipboard retains fractional iBMSC BPM exactly", "[formats]") {
    const auto result = decode("iBMSC Clipboard Data\r\n2 24 1234560 0 0 0");
    REQUIRE(result);
    const auto* bpm = first_object<wormedit::bpm_event_t>(*result);
    REQUIRE(bpm);
    CHECK((bpm->offset == wormedit::rational_t{1, 8}));
    CHECK((std::get<wormedit::bpm_event_t>(bpm->payload).beats_per_minute ==
          wormedit::signed_rational_t{15432, 125}));
}

TEST_CASE("bms_clipboard decodes BMSE scroll and speed controls", "[formats]") {
    const auto text = "BMSE3.0 ClipBoard Object Data Format\r\n"
                      "0SC0" "4038000000000000" "1.5\r\n"
                      "0SP0" "4048000000000000" "2.5\r\n";
    const auto result = decode(text);
    REQUIRE(result);
    const auto* scroll = first_object<wormedit::scroll_event_t>(*result);
    const auto* speed = first_object<wormedit::speed_event_t>(*result);
    REQUIRE(scroll);
    REQUIRE(speed);
    CHECK((scroll->offset == wormedit::rational_t{1, 8}));
    CHECK((std::get<wormedit::scroll_event_t>(scroll->payload).multiplier ==
          wormedit::signed_rational_t{3, 2}));
    CHECK((std::get<wormedit::speed_event_t>(speed->payload).multiplier ==
          wormedit::signed_rational_t{5, 2}));
}

TEST_CASE("bms_clipboard diagnoses unsupported BGA clipboard objects", "[formats]") {
    const auto text = "iBMSC Clipboard Data\r\n23 0 10000 0 0 0";
    const auto result = decode(text);
    CHECK_FALSE(result);
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("bms_clipboard mutated fields reject deterministically without a partial snippet", "[formats]") {
    const auto record = GENERATE("6 -1 10000 0 0 0", "6 inf 10000 0 0 0",
        "6 0 9223372036854775808 0 0 0", "6 0 10001 0 0 0",
        "6 0 10000 1 0 0", "6 0 10000 0 1 0", "6 0 10000 0 0 1",
        "6 0 10000 -1 -1 0", "6 0 10000 0 -1 -1", "6  0 10000 0 0 0");
    const auto text = std::string{"iBMSC Clipboard Data\r\n6 48 10000 0 0 0\r\n"} + record;
    const auto first = decode(text);
    const auto second = decode(text);
    REQUIRE_FALSE(first);
    REQUIRE_FALSE(second);
    CHECK_FALSE(first.error().empty());
    CHECK(first.error() == second.error());
}

TEST_CASE("bms_clipboard impossible endpoint pairs reject as a batch", "[formats]") {
    const auto records = GENERATE("7 0 40000 -1 0 0\r\n7 96 50000 -1 0 0",
        "7 0 40000 -1 0 0\r\n7 0 40000 -1 0 0",
        "7 0 40000 -1 0 0\r\n7 96 40000 -1 0 0\r\n7 144 40000 -1 0 0",
        "7 0 40000 -1 0 0\r\n7 24 50000 -1 0 0\r\n7 48 40000 -1 0 0\r\n7 72 50000 -1 0 0");
    const auto result = decode(std::string{"iBMSC Clipboard Data\r\n"} + records);
    CHECK_FALSE(result);
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("bms_clipboard decoded holds enter the native exact paste proposal", "[formats]") {
    const auto song = target_document();
    const auto& chart = song.charts.front();
    const auto decoded = wormedit::parse_bms_chart_clipboard(
        "iBMSC Clipboard Data xNT\r\n7 0 40000 96 0 0", song, chart,
        chart.lane_layouts.front().layout);
    REQUIRE(decoded);
    const auto proposal = wormedit::propose_chart_paste(song, chart.id, *decoded,
        {wormedit::measure_id_t{10}, 0}, song.revision);
    INFO(std::string{proposal.message()});
    REQUIRE(proposal.is_valid());
    REQUIRE(proposal.objects.size() == 1);
    const auto& hold = std::get<wormedit::note_t>(proposal.objects.front());
    CHECK((hold.end == wormedit::local_position_t{wormedit::measure_id_t{10}, 672}));
    CHECK(hold.lane == wormedit::lane_id_t{102});
    CHECK(hold.sound == wormedit::resource_id_t{204});
}

TEST_CASE("bms_clipboard paired duration remains the exact difference of recovered endpoints", "[formats]") {
    const auto result = decode("iBMSC Clipboard Data\r\n7 1.5 40000 -1 0 0\r\n"
                               "7 1.51181102362205 40000 -1 0 0");
    REQUIRE(result);
    REQUIRE(result->objects.size() == 1);
    const auto& hold = result->objects.front();
    CHECK((hold.offset == wormedit::rational_t{1, 128}));
    CHECK((hold.duration == wormedit::rational_t{1, 16256}));
}

TEST_CASE("bms_clipboard BMSE scientific values retain exact decimal meaning", "[formats]") {
    const auto result = decode("BMSE3.0 ClipBoard Object Data Format\r\n"
                               "0080" "0000000000000000" "1.25E+2\r\n");
    REQUIRE(result);
    const auto* bpm = first_object<wormedit::bpm_event_t>(*result);
    REQUIRE(bpm);
    CHECK((std::get<wormedit::bpm_event_t>(bpm->payload).beats_per_minute ==
           wormedit::signed_rational_t{125, 1}));
}

TEST_CASE("bms_clipboard standard BMS identities retain their case insensitive meaning", "[formats]") {
    auto song = target_document();
    auto& identity = song.resources.front().target_identities.front();
    identity.identifier = "0a";
    identity.namespace_name = "wav";
    const auto& chart = song.charts.front();
    const auto result = wormedit::parse_bms_chart_clipboard(
        "iBMSC Clipboard Data\r\n6 0 100000 0 0 0", song, chart,
        chart.lane_layouts.front().layout);
    REQUIRE(result);
    REQUIRE(result->objects.size() == 1);
    CHECK(std::get<wormedit::note_t>(result->objects.front().payload).sound ==
          wormedit::resource_id_t{201});
}

TEST_CASE("bms_clipboard zero participates in bounded nearest position recovery", "[formats]") {
    const auto result = decode("iBMSC Clipboard Data\r\n6 0.000000001 10000 0 0 0");
    REQUIRE(result);
    REQUIRE(result->objects.size() == 1);
    CHECK((result->objects.front().offset == wormedit::rational_t{0, 1}));
}

TEST_CASE("bms_clipboard duplicate aliases resolve to the same sound before rejection", "[formats]") {
    auto song = target_document();
    song.resources.front().target_identities.push_back({.family = wormedit::format_family_t::bms,
        .identifier = "0A", .namespace_name = "WAV"});
    const auto& chart = song.charts.front();
    const auto result = wormedit::parse_bms_chart_clipboard(
        "iBMSC Clipboard Data\r\n6 0 10000 0 0 0\r\n6 0 100000 0 0 0", song, chart,
        chart.lane_layouts.front().layout);
    CHECK_FALSE(result);
    CHECK_FALSE(result.error().empty());
}

TEST_CASE("bms_clipboard decimal reduction precedes magnitude overflow", "[formats][helpers]") {
    const auto result = decode("BMSE3.0 ClipBoard Object Data Format\r\n"
        "0080" "0000000000000000" "92233720368547758070e-1\r\n");
    REQUIRE(result);
    const auto* bpm = first_object<wormedit::bpm_event_t>(*result);
    REQUIRE(bpm);
    CHECK((std::get<wormedit::bpm_event_t>(bpm->payload).beats_per_minute ==
           wormedit::signed_rational_t{INT64_MAX, 1}));
}
