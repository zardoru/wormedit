#include "../../src/core/text_util.h"
#include <wormedit/formats/slice_spans.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
using wormedit::text_util::ascii_iequal;
using namespace wormedit;

void check_span(bool value) { CHECK(value); }
void require_span(bool value) { REQUIRE(value); }

struct slice_fixture_t {
    song_document_t song;
    std::vector<slice_span_source_t> sources;
    chart_id_t chart{chart_id_t{1}};
};

auto slice_fixture() -> slice_fixture_t {
    slice_fixture_t fixture;
    fixture.song.id = song_id_t{1};
    fixture.song.revision = 7;
    fixture.song.audio_assets.push_back({.id = asset_id_t{2}, .source_path = "stem.ogg"});
    fixture.song.resources.push_back({.id = resource_id_t{3}, .name = "Stem",
        .asset = asset_id_t{2}, .roles = {resource_role_t::sample, resource_role_t::bgm}});

    chart_document_t chart;
    chart.id = fixture.chart;
    chart.source_family = format_family_t::bmson;
    chart.divisions_per_standard_measure = 960;
    chart.measures.push_back({.id = measure_id_t{10}});
    chart.bpms.push_back({.id = object_id_t{11}, .position = {measure_id_t{10}, 0},
                          .beats_per_minute = {120, 1}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{12}, .name = "BGM"});
    const auto events = [](std::uint64_t first) {
        return std::vector<sound_channel_event_t>{
            {.id = object_id_t{first}, .position = {measure_id_t{10}, 0},
             .lane = lane_id_t{1}, .continuation = false},
            {.id = object_id_t{first + 1}, .position = {measure_id_t{10}, 240},
             .lane = lane_id_t{1}, .continuation = true},
            {.id = object_id_t{first + 2}, .position = {measure_id_t{10}, 480},
             .lane = lane_id_t{1}, .continuation = false}};
    };
    chart.sound_channels.push_back({.id = object_id_t{20}, .name = "Stem A",
        .source_clip = {.id = clip_id_t{21}, .source = asset_id_t{2}}, .events = events(30)});
    chart.sound_channels.push_back({.id = object_id_t{22}, .name = "Stem B",
        .source_clip = {.id = clip_id_t{23}, .source = asset_id_t{2}}, .events = events(40)});
    chart.measures.front().notes = {
        {.id = object_id_t{50}, .position = {measure_id_t{10}, 0}, .lane = lane_id_t{1},
         .sound = resource_id_t{3}, .sound_channel_event = object_id_t{30}},
        {.id = object_id_t{51}, .position = {measure_id_t{10}, 240}, .lane = lane_id_t{1},
         .sound = resource_id_t{3}},
    };
    chart.measures.front().bgm_events = {
        {.id = object_id_t{60}, .position = {measure_id_t{10}, 480},
         .source_lane = object_id_t{12}, .sound = resource_id_t{3},
         .sound_channel_event = object_id_t{42}},
        {.id = object_id_t{61}, .position = {measure_id_t{10}, 720},
         .source_lane = object_id_t{12}, .sound = resource_id_t{3}},
    };
    fixture.song.charts.push_back(std::move(chart));
    fixture.sources.push_back({.asset = asset_id_t{2}, .native_sample_rate = 100,
        .native_frame_count = 1'000, .output_frame_count = 441'000});
    return fixture;
}

auto find_mapping(const slice_span_plan_t& plan, slice_span_event_kind_t kind, object_id_t event)
    -> const slice_span_mapping_t* {
    const auto found = std::ranges::find_if(plan.mappings, [&](const auto& mapping) {
        return mapping.kind == kind && mapping.event == event;
    });
    return found == plan.mappings.end() ? nullptr : &*found;
}

auto mapped_span(const slice_span_plan_t& plan, slice_span_event_kind_t kind, object_id_t event)
    -> const slice_span_t* {
    const auto* mapping = find_mapping(plan, kind, event);
    if (!mapping || !mapping->span_index || *mapping->span_index >= plan.spans.size()) return nullptr;
    return &plan.spans[*mapping->span_index];
}

void require_span_range(const slice_span_t* span, asset_id_t asset,
                        std::uint64_t begin, std::uint64_t end) {
    require_span(span != nullptr);
    check_span(span->asset == asset);
    check_span(span->begin_frame == begin);
    check_span(span->end_frame == end);
    check_span(!span->filename.empty());
}

auto same_channel(const sound_channel_t& left, const sound_channel_t& right) -> bool {
    if (left.id != right.id || left.source_clip.id != right.source_clip.id ||
        left.source_clip.source != right.source_clip.source ||
        left.source_clip.begin_frame != right.source_clip.begin_frame ||
        left.source_clip.end_frame != right.source_clip.end_frame ||
        left.events.size() != right.events.size()) return false;
    for (std::size_t index = 0; index < left.events.size(); ++index) {
        const auto& a = left.events[index];
        const auto& b = right.events[index];
        if (a.id != b.id || a.position != b.position || a.continuation != b.continuation ||
            a.playable != b.playable) return false;
    }
    return true;
}

auto same_note(const note_t& left, const note_t& right) -> bool {
    return left.id == right.id && left.position == right.position && left.lane == right.lane &&
        left.sound == right.sound && left.sound_channel_event == right.sound_channel_event;
}
auto same_bgm(const bgm_event_t& left, const bgm_event_t& right) -> bool {
    return left.id == right.id && left.position == right.position && left.source_lane == right.source_lane &&
        left.sound == right.sound && left.sound_channel_event == right.sound_channel_event;
}
auto same_measure(const measure_chunk_t& left, const measure_chunk_t& right) -> bool {
    return left.id == right.id && std::ranges::equal(left.notes, right.notes, same_note) &&
        std::ranges::equal(left.bgm_events, right.bgm_events, same_bgm);
}
auto same_bgm_lane(const bgm_source_lane_t& left, const bgm_source_lane_t& right) -> bool {
    return left.id == right.id && left.name == right.name && left.source_order == right.source_order &&
        left.stem_channel == right.stem_channel;
}
auto same_chart(const chart_document_t& left, const chart_document_t& right) -> bool {
    return left.id == right.id && left.source_family == right.source_family &&
        left.divisions_per_standard_measure == right.divisions_per_standard_measure &&
        std::ranges::equal(left.sound_channels, right.sound_channels, same_channel) &&
        std::ranges::equal(left.bgm_source_lanes, right.bgm_source_lanes, same_bgm_lane) &&
        std::ranges::equal(left.measures, right.measures, same_measure);
}
auto same_input(const song_document_t& left, const song_document_t& right) -> bool {
    const auto same_asset = [](const audio_asset_t& a, const audio_asset_t& b) {
        return a.id == b.id && a.source_path == b.source_path;
    };
    const auto same_resource = [](const resource_entry_t& a, const resource_entry_t& b) {
        return a.id == b.id && a.name == b.name && a.asset == b.asset && a.roles == b.roles;
    };
    return left.id == right.id && left.revision == right.revision &&
        std::ranges::equal(left.audio_assets, right.audio_assets, same_asset) &&
        std::ranges::equal(left.resources, right.resources, same_resource) &&
        std::ranges::equal(left.charts, right.charts, same_chart);
}

void require_rejected(const slice_fixture_t& fixture, chart_id_t chart,
                      std::span<const slice_span_source_t> sources,
                      const slice_span_options_t& options = {}) {
    const auto before = fixture.song;
    const auto plan = build_slice_span_plan(fixture.song, chart, sources, options);
    check_span(!plan);
    check_span(!plan.diagnostics.empty());
    check_span(plan.spans.empty());
    check_span(plan.mappings.empty());
    check_span(same_input(fixture.song, before));
}

void check_rejected_sources(const slice_fixture_t& fixture,
                           std::span<const slice_span_source_t> sources) {
    require_rejected(fixture, fixture.chart, sources);
}

} // namespace

TEST_CASE("slice_spans plan deduplicates repeated continuations and maps semantic sounds",
          "[format][slice_spans]") {
    auto fixture = slice_fixture();
    const auto plan = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(plan));
    check_span(plan.sources == fixture.sources);
    check_span(plan.spans.size() == 3);
    check_span(plan.mappings.size() == 10);
    require_span_range(mapped_span(plan, slice_span_event_kind_t::sound_channel_event,
                                    object_id_t{30}), asset_id_t{2}, 0, 22'050);
    require_span_range(mapped_span(plan, slice_span_event_kind_t::sound_channel_event,
                                    object_id_t{31}), asset_id_t{2}, 22'050, 44'100);
    require_span_range(mapped_span(plan, slice_span_event_kind_t::sound_channel_event,
                                    object_id_t{32}), asset_id_t{2}, 0, 441'000);
    const auto* repeated = find_mapping(plan, slice_span_event_kind_t::sound_channel_event,
                                        object_id_t{30});
    const auto* duplicate = find_mapping(plan, slice_span_event_kind_t::sound_channel_event,
                                         object_id_t{40});
    require_span(repeated != nullptr && duplicate != nullptr);
    check_span(repeated->span_index == duplicate->span_index);
    require_span_range(mapped_span(plan, slice_span_event_kind_t::note, object_id_t{50}),
                       asset_id_t{2}, 0, 22'050);
    const auto* linked_bgm = find_mapping(plan, slice_span_event_kind_t::bgm_event,
                                          object_id_t{60});
    const auto* unlinked_note = find_mapping(plan, slice_span_event_kind_t::note,
                                             object_id_t{51});
    const auto* unlinked_bgm = find_mapping(plan, slice_span_event_kind_t::bgm_event,
                                            object_id_t{61});
    require_span(linked_bgm && unlinked_note && unlinked_bgm);
    check_span(linked_bgm->span_index == unlinked_note->span_index);
    check_span(linked_bgm->span_index == unlinked_bgm->span_index);
    require_span_range(mapped_span(plan, slice_span_event_kind_t::bgm_event, object_id_t{60}),
                       asset_id_t{2}, 0, 441'000);
    check_span(same_input(fixture.song, slice_fixture().song));
}

TEST_CASE("slice_spans plan filenames and wrapper output are deterministic",
          "[format][slice_spans]") {
    const auto fixture = slice_fixture();
    const auto first = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    const auto second = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    const auto wrapped = plan_conversion_slice_spans(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(first));
    check_span(first.spans == second.spans);
    check_span(first.mappings == second.mappings);
    check_span(first.sources == second.sources);
    check_span(first.spans == wrapped.spans);
    check_span(first.mappings == wrapped.mappings);

    auto occupied = slice_span_options_t{};
    auto collision = first.spans.front().filename;
    std::ranges::transform(collision, collision.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    occupied.occupied_filenames.push_back(collision);
    const auto skipped = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources, occupied);
    require_span(static_cast<bool>(skipped));
    check_span(std::ranges::none_of(skipped.spans, [&](const auto& span) {
        return ascii_iequal(span.filename, collision);
    }));
    const auto repeated = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources, occupied);
    check_span(skipped.spans == repeated.spans);
}

TEST_CASE("slice_spans plan rejects ambiguous or invalid inputs atomically", "[format][slice_spans]") {
    auto fixture = slice_fixture();
    const auto sources = fixture.sources;
    require_rejected(fixture, chart_id_t{999}, sources);

    fixture.song.charts.push_back(fixture.song.charts.front());
    require_rejected(fixture, fixture.chart, sources);
    fixture = slice_fixture();
    const std::vector<slice_span_source_t> missing_source;
    check_rejected_sources(fixture, missing_source);

    auto duplicate_sources = sources;
    duplicate_sources.push_back(sources.front());
    check_rejected_sources(fixture, duplicate_sources);
    auto invalid_geometry = sources;
    invalid_geometry.front().native_sample_rate = 0;
    check_rejected_sources(fixture, invalid_geometry);

    fixture.song.resources.clear();
    require_rejected(fixture, fixture.chart, sources);
    fixture = slice_fixture();
    fixture.song.resources.push_back(fixture.song.resources.front());
    require_rejected(fixture, fixture.chart, sources);
    fixture = slice_fixture();
    fixture.song.charts.front().sound_channels.front().source_clip.end_frame = 10;
    fixture.song.charts.front().sound_channels.front().source_clip.begin_frame = 20;
    require_rejected(fixture, fixture.chart, sources);

    fixture = slice_fixture();
    fixture.song.charts.front().divisions_per_standard_measure = 0;
    require_rejected(fixture, fixture.chart, sources);

    fixture = slice_fixture();
    fixture.song.charts.front().sound_channels.front().source_clip.begin_frame = 1'000;
    require_rejected(fixture, fixture.chart, sources);
}

TEST_CASE("slice_spans plan rejects stale links and configured limits atomically", "[format][slice_spans]") {
    auto fixture = slice_fixture();
    const auto sources = fixture.sources;
    fixture.song.charts.front().measures.front().notes.front().sound_channel_event = object_id_t{999};
    require_rejected(fixture, fixture.chart, sources);

    fixture = slice_fixture();
    auto options = slice_span_options_t{};
    options.maximum_events = 1;
    require_rejected(fixture, fixture.chart, sources, options);
    options = {};
    options.maximum_spans = 1;
    require_rejected(fixture, fixture.chart, sources, options);

    fixture = slice_fixture();
    fixture.song.charts.front().sound_channels.front().events[1].id = object_id_t{30};
    require_rejected(fixture, fixture.chart, sources);
}

TEST_CASE("slice_spans plan uses ties-to-even at normalized source boundaries", "[format][slice_spans]") {
    auto fixture = slice_fixture();
    auto& chart = fixture.song.charts.front();
    chart.sound_channels.clear();
    chart.measures.front().notes = {{.id = object_id_t{70}, .position = {measure_id_t{10}, 0},
                                     .lane = lane_id_t{1}, .sound = resource_id_t{3}}};
    chart.measures.front().bgm_events.clear();
    fixture.sources = {{.asset = asset_id_t{2}, .native_sample_rate = 88'200,
        .native_frame_count = 3, .output_frame_count = 2}};
    const auto before = fixture.song;
    const auto plan = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(plan));
    require_span(plan.spans.size() == 1);
    check_span(plan.spans.front().begin_frame == 0);
    check_span(plan.spans.front().end_frame == 2);
    check_span(same_input(fixture.song, before));
}

TEST_CASE("slice_spans packed channel markers reuse the complete source clip", "[format][slice_spans]") {
    auto fixture = slice_fixture();
    auto& channel = fixture.song.charts.front().sound_channels.front();
    channel.source_clip.begin_frame = 100;
    channel.source_clip.end_frame = 900;
    for (auto& event : channel.events) event.continuation = false;
    const auto plan = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(plan));
    require_span(plan.spans.size() == 4);
    const auto* first = find_mapping(plan, slice_span_event_kind_t::sound_channel_event,
                                    object_id_t{30});
    const auto* second = find_mapping(plan, slice_span_event_kind_t::sound_channel_event,
                                     object_id_t{31});
    const auto* third = find_mapping(plan, slice_span_event_kind_t::sound_channel_event,
                                    object_id_t{32});
    require_span(first && second && third);
    check_span(first->span_index == second->span_index && second->span_index == third->span_index);
    const auto& span = plan.spans[*first->span_index];
    check_span(span.begin_frame == 44'100 && span.end_frame == 396'900);
}

TEST_CASE("slice_spans native clip boundaries round half frames to even", "[format][slice_spans]") {
    auto fixture = slice_fixture();
    fixture.sources = {{asset_id_t{2}, 88'200, 8, 4}};
    auto& channels = fixture.song.charts.front().sound_channels;
    channels[0].source_clip.begin_frame = 1;
    channels[0].source_clip.end_frame = 5;
    channels[1].source_clip.begin_frame = 3;
    channels[1].source_clip.end_frame = 7;
    for (auto& channel : channels)
        for (auto& event : channel.events) event.continuation = false;
    const auto before = fixture.song;
    const auto plan = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(plan));
    require_span_range(mapped_span(plan, slice_span_event_kind_t::sound_channel_event, object_id_t{30}), asset_id_t{2}, 0, 2);
    require_span_range(mapped_span(plan, slice_span_event_kind_t::sound_channel_event, object_id_t{40}), asset_id_t{2}, 2, 4);
    check_span(same_input(fixture.song, before));
}

TEST_CASE("slice_spans EOF continuations retain silent source and gameplay mappings", "[format][slice_spans]") {
    auto fixture = slice_fixture();
    fixture.sources = {{asset_id_t{2}, 100, 100, 44'100}};
    auto& chart = fixture.song.charts.front();
    chart.sound_channels.resize(1);
    auto& events = chart.sound_channels.front().events;
    events.back().continuation = true;
    events.push_back({.id = object_id_t{33}, .position = {measure_id_t{10}, 720}, .continuation = true});
    chart.measures.front().bgm_events.front().sound_channel_event = object_id_t{32};
    const auto before = fixture.song;
    const auto plan = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(plan));
    check_span(plan.spans.size() == 3 && plan.mappings.size() == 8);
    for (const auto event : {object_id_t{32}, object_id_t{33}}) {
        const auto* mapping = find_mapping(plan, slice_span_event_kind_t::sound_channel_event, event);
        require_span(mapping != nullptr);
        check_span(!mapping->span_index);
    }
    const auto* bgm = find_mapping(plan, slice_span_event_kind_t::bgm_event, object_id_t{60});
    require_span(bgm != nullptr);
    check_span(!bgm->span_index);
    check_span(same_input(fixture.song, before));
}

TEST_CASE("slice_spans explicit restart-only stem differs from a packed full clip", "[format][slice_spans]") {
    auto fixture = slice_fixture();
    auto& chart = fixture.song.charts.front();
    for (auto& event : chart.sound_channels.front().events) event.continuation = false;
    const auto packed = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(packed));
    require_span_range(mapped_span(packed, slice_span_event_kind_t::sound_channel_event, object_id_t{30}), asset_id_t{2}, 0, 441'000);
    chart.bgm_source_lanes.front().stem_channel = object_id_t{20};
    const auto before = fixture.song;
    const auto stem = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(stem));
    require_span_range(mapped_span(stem, slice_span_event_kind_t::sound_channel_event, object_id_t{30}), asset_id_t{2}, 0, 22'050);
    require_span_range(mapped_span(stem, slice_span_event_kind_t::sound_channel_event, object_id_t{31}), asset_id_t{2}, 0, 22'050);
    require_span_range(mapped_span(stem, slice_span_event_kind_t::sound_channel_event, object_id_t{32}), asset_id_t{2}, 0, 441'000);
    check_span(same_input(fixture.song, before));
}

TEST_CASE("slice_spans unbound gameplay objects retain explicit no-audio mappings", "[format][slice_spans]") {
    auto fixture = slice_fixture();
    auto& chart = fixture.song.charts.front();
    chart.sound_channels.clear();
    chart.bgm_source_lanes.front().stem_channel.reset();
    fixture.song.audio_assets.clear();
    fixture.song.resources.clear();
    fixture.sources.clear();
    for (auto& note : chart.measures.front().notes) {
        note.sound.reset();
        note.sound_channel_event.reset();
    }
    for (auto& bgm : chart.measures.front().bgm_events) {
        bgm.sound.reset();
        bgm.sound_channel_event.reset();
    }
    const auto before = fixture.song;
    const auto plan = build_slice_span_plan(fixture.song, fixture.chart, fixture.sources);
    require_span(static_cast<bool>(plan));
    check_span(plan.spans.empty() && plan.mappings.size() == 4);
    check_span(std::ranges::all_of(plan.mappings, [](const auto& mapping) { return !mapping.span_index; }));
    check_span(same_input(fixture.song, before));
}
