// BMSON stem, channel, packed-slice and Draw-source projection.
#include "frame_timing_fixtures.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

TEST_CASE("BMSON channel switches sharing an asset preserve independent timing and bounded padded rows",
          "[sys][waveform-projection][bmson]") {
    channel_projection_fixture_t fixture;
    fixture.project_channel();
    expect_gpu_work(!fixture.cache.waveform.channel_projection.view.value().rows.empty());
    const auto first = fixture.cache.waveform.channel_projection.view.value().rows.front().y_pixels;
    fixture.flattened.view.compose.selected_bmson_bgm_event_id.value = 74001;
    fixture.project_channel();
    expect_gpu_work(std::get<2>(*fixture.cache.waveform.channel_projection.ring.key()).channel == 71001);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 202);
    expect_gpu_work(!fixture.cache.waveform.channel_projection.view.value().rows.empty());
    expect_gpu_work(fixture.cache.waveform.channel_projection.view.value().rows.front().y_pixels > first + 40);
    fixture.interaction.viewport_scroll_pixels = .75;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 0);
    fixture.interaction.viewport_scroll_pixels = 1.25;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 1);
    fixture.history.document.charts.front().lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::bar;
    fixture.flattened.view.compose.downscroll = true;
    fixture.cache.waveform.channel_projection.view.invalidate();
    fixture.interaction.viewport_scroll_pixels = 0;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.channel_projection.view.value().rows.front().y_pixels > 140);
}

namespace {
void check_swapped_stem_publication(const wormedit::ui::compose_view_t& view) {
    REQUIRE(view.stem_waveforms.size() == 1);
    const auto& swapped = view.stem_waveforms.front();
    CHECK(swapped.stem_key == 71000);
    CHECK(swapped.source_lane_id == object_id_t{3});
    CHECK(!swapped.rows.empty());
}
} // namespace

TEST_CASE("BMSON stems other than the selected one get their own lane rows or a pending band",
          "[sys][waveform-projection][bmson][stems]") {
    channel_projection_fixture_t fixture;
    std::shared_ptr<const audio::waveform_analysis_result_t> stem_result = fixture.channel_analysis;
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history, fixture.workspace,
        &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources), {fixture.asset, fixture.channel_analysis},
        {fixture.asset, fixture.channel_analysis}, 1,
        [&](asset_id_t) { return stem_result; }};

    // Channel 71000 is selected through its marker, so only 71001 is a stem.
    populate_bmson_waveform_projection(context);
    CHECK(fixture.flattened.view.compose.stem_waveforms.size() == 1);
    const auto& stem = fixture.flattened.view.compose.stem_waveforms[0];
    CHECK(stem.source_lane_id == object_id_t{4});
    expect_gpu_work(stem.stem_key == 71001);
    CHECK((!stem.pending && stem.rows.size() > 0));
    CHECK(stem.rows.size() <= fixture.flattened.view.compose.viewport_height_pixels + 2);

    // Selecting the other marker swaps which stem is the extra one.
    fixture.flattened.view.compose.selected_bmson_bgm_event_id.value = 74001;
    populate_bmson_waveform_projection(context);
    check_swapped_stem_publication(fixture.flattened.view.compose);
    // The selected channel keeps an entry too, for its field taps and cut lines.
    CHECK(fixture.cache.stems.stem_waveforms.size() == 2);
    CHECK(fixture.cache.stems.stem_waveforms.front().channel == object_id_t{71000});

    // Audio that has not finished decoding is a placeholder, never a blocked frame.
    stem_result.reset();
    const bmson_waveform_projection_context_t pending_context{fixture.flattened, fixture.history, fixture.workspace,
        &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources), {fixture.asset, nullptr},
        {std::nullopt, nullptr}, 1, [&](asset_id_t) { return stem_result; }};
    populate_bmson_waveform_projection(pending_context);
    CHECK(fixture.flattened.view.compose.stem_waveforms.size() == 1);
    CHECK(fixture.flattened.view.compose.stem_waveforms[0].pending);
    expect_gpu_work(fixture.flattened.view.compose.stem_waveforms[0].stem_key == 71000);
    CHECK(fixture.flattened.view.compose.stem_waveforms[0].rows.size() == 0);
}

namespace {
auto project_stable_stem_keys(channel_projection_fixture_t& fixture, bool pending = false)
    -> std::vector<std::uint64_t> {
    fixture.flattened.view.compose.selected_bmson_bgm_event_id.value = 0;
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history,
        fixture.workspace, &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources),
        {fixture.asset, fixture.channel_analysis}, {std::nullopt, nullptr}, 1,
        [&](asset_id_t) { return pending ? nullptr : fixture.channel_analysis; }};
    populate_bmson_waveform_projection(context);
    std::vector<std::uint64_t> keys;
    for (std::size_t i = 0; i < fixture.flattened.view.compose.stem_waveforms.size(); ++i) {
        const auto& view = fixture.flattened.view.compose.stem_waveforms[i];
        expect_gpu_work(view.pending == pending);
        expect_gpu_work((view.rows.size() == 0) == pending);
        keys.push_back(view.stem_key);
    }
    std::ranges::sort(keys);
    return keys;
}
} // namespace

TEST_CASE("BMSON stem keys retain channel identity when visible channel order changes",
          "[sys][waveform-projection][bmson][stem-key]") {
    channel_projection_fixture_t fixture;
    const auto before = project_stable_stem_keys(fixture);
    expect_gpu_work(before == std::vector<std::uint64_t>{71000, 71001});
    auto& chart = fixture.history.document.charts.front();
    std::ranges::reverse(chart.sound_channels);
    ++fixture.history.document.revision;
    expect_gpu_work(project_stable_stem_keys(fixture) == before);
}

namespace {
void populate_three_hundred_stems(channel_projection_fixture_t& fixture) {
    auto& chart = fixture.history.document.charts.front();
    chart.sound_channels.clear();
    chart.bgm_source_lanes.clear();
    chart.measures.front().bgm_events.clear();
    for (std::uint64_t index = 0; index < 300; ++index) {
        chart.sound_channels.push_back({.id = object_id_t{71000 + index},
            .source_clip = {clip_id_t{72000 + index}, asset_id_t{7}},
            .events = {{.id = object_id_t{73000 + index}, .position = {measure_id_t{1}, 0},
                        .lane = lane_id_t{3}, .playable = false}}});
        chart.bgm_source_lanes.push_back({.id = object_id_t{50000 + index},
            .source_order = index, .stem_channel = object_id_t{71000 + index}});
        chart.measures.front().bgm_events.push_back({.id = object_id_t{74000 + index},
            .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{50000 + index},
            .sound_channel_event = object_id_t{73000 + index}});
    }
    ++fixture.history.document.revision;
}
} // namespace

TEST_CASE("BMSON projection publishes all 300 visible stems with one stable piece per channel",
          "[sys][waveform-projection][bmson][stem-key][stem-300]") {
    channel_projection_fixture_t fixture;
    populate_three_hundred_stems(fixture);
    const auto before = project_stable_stem_keys(fixture);
    expect_gpu_work(before.size() == 300);
    expect_gpu_work(std::ranges::adjacent_find(before) == before.end());
    expect_gpu_work(before.front() == 71000 && before.back() == 71299);
    std::ranges::reverse(fixture.history.document.charts.front().sound_channels);
    ++fixture.history.document.revision;
    expect_gpu_work(project_stable_stem_keys(fixture) == before);
    expect_gpu_work(project_stable_stem_keys(fixture, true) == before);
}

namespace {
void register_prepared_stem_source(channel_projection_fixture_t& fixture,
                                  std::string_view filename, std::string_view version) {
    fixture.sources.clear();
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "fixtures" / "audio" / filename;
    REQUIRE(fixture.sources.create_source({*fixture.asset, path,
        {"prepared-span", std::string(version)}}));
}

void configure_prepared_stem_frame(channel_projection_fixture_t& fixture) {
    fixture.history.document.audio_assets.back().stream = {};
    register_prepared_stem_source(fixture, "sliced-seek-source.ogg", "long");
    auto& chart = fixture.history.document.charts.front();
    chart.sound_channels[0].events.front().position.measure = measure_id_t{1};
    chart.sound_channels[1].events.front().position.measure = measure_id_t{1};
    fixture.interaction.cursor = {measure_id_t{4}, 0};
    fixture.flattened.view.compose.pixels_per_standard_measure = 200;
    fixture.flattened.view.compose.viewport_height_pixels = 200;
}

struct prepared_stem_frame_t {
    std::vector<const sound_channel_t*> channels;
    std::vector<object_id_t> ids;
    std::vector<audio::decoded_source_binding_t> bindings;
    std::uint64_t selection_build_count{};
};

auto prepare_stem_frame(channel_projection_fixture_t& fixture) -> prepared_stem_frame_t {
    const auto bindings = fixture.sources.bindings();
    const auto& chart = fixture.history.document.charts.front();
    const auto prepared = indexed_visible_stem_channels(fixture.cache.stems.stem_index,
        fixture.history.document, chart, fixture.interaction.cursor.measure, 0,
        bindings, wormedit::sys::stem_geometry_indices_t{.timing = &*fixture.cache.timing.marker_timing_index});
    REQUIRE(prepared.size() == 2);
    CHECK(fixture.cache.stems.stem_index.value().selection.key()->first == measure_id_t{4});
    CHECK(fixture.cache.stems.stem_index.value().selection.key()->second == 0);
    return {prepared, {prepared[0]->id, prepared[1]->id},
            fixture.cache.stems.stem_index.key()->bindings, fixture.cache.stems.stem_index.value().selection.build_count()};
}

auto shorten_prepared_stem_source(channel_projection_fixture_t& fixture,
                                 const prepared_stem_frame_t& prepared)
    -> std::vector<audio::decoded_source_binding_t> {
    register_prepared_stem_source(fixture, "silence.ogg", "short");
    const auto bindings = fixture.sources.bindings();
    REQUIRE(bindings != prepared.bindings);
    REQUIRE(bindings.front().asset == *fixture.asset);
    REQUIRE(bindings.front().source_frame_count != prepared.bindings.front().source_frame_count);
    return bindings;
}

void consume_prepared_stem_frame(channel_projection_fixture_t& fixture,
                                const prepared_stem_frame_t& prepared) {
    const auto channels = std::span<const sound_channel_t* const>(prepared.channels);
    runtime::waveform_analysis_t runtime{fixture.sources};
    runtime.service_compose_stem_waveforms(fixture.history.document, fixture.workspace,
        fixture.cache, &fixture.interaction, channels);
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history,
        fixture.workspace, &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources),
        {fixture.asset, fixture.channel_analysis}, {fixture.asset, fixture.channel_analysis}, 1,
        [](asset_id_t) { return std::shared_ptr<const audio::waveform_analysis_result_t>{}; },
        channels};
    populate_bmson_waveform_projection(context);
}

void check_prepared_stem_channels(const channel_projection_fixture_t& fixture,
                                  const prepared_stem_frame_t& prepared) {
    REQUIRE(fixture.cache.stems.stem_waveforms.size() == prepared.ids.size());
    for (std::size_t i = 0; i < prepared.ids.size(); ++i)
        CHECK(fixture.cache.stems.stem_waveforms[i].channel == prepared.ids[i]);
}

void check_prepared_stem_cache(const channel_projection_fixture_t& fixture,
                               const prepared_stem_frame_t& prepared) {
    CHECK(fixture.cache.stems.stem_index.key()->bindings == prepared.bindings);
    CHECK(fixture.cache.stems.stem_index.value().selection.value().size() == prepared.ids.size());
    CHECK(fixture.cache.stems.stem_index.value().selection.key()->first == measure_id_t{4});
    CHECK(fixture.cache.stems.stem_index.value().selection.key()->second == 0);
    CHECK(fixture.cache.stems.stem_index.value().selection.build_count() == prepared.selection_build_count);
}
} // namespace

TEST_CASE("prepared BMSON stem list stays authoritative across consumers and binding changes",
          "[sys][waveform-projection][bmson][stems][frame-timing]") {
    channel_projection_fixture_t fixture;
    configure_prepared_stem_frame(fixture);
    const auto prepared = prepare_stem_frame(fixture);
    const auto changed_bindings = shorten_prepared_stem_source(fixture, prepared);
    consume_prepared_stem_frame(fixture, prepared);
    check_prepared_stem_channels(fixture, prepared);
    check_prepared_stem_cache(fixture, prepared);

    stem_index_t next_frame_index;
    const auto& chart = fixture.history.document.charts.front();
    const auto next_frame = indexed_visible_stem_channels(next_frame_index,
        fixture.history.document, chart, fixture.interaction.cursor.measure, 0,
        changed_bindings, wormedit::sys::stem_geometry_indices_t{.timing = &*fixture.cache.timing.marker_timing_index});
    CHECK(next_frame.empty());
}
#endif

namespace {
void link_window_channels(chart_document_t& chart) {
    chart.source_family = format_family_t::bmson;
    for (std::size_t index = 0; index < chart.sound_channels.size(); ++index)
        chart.bgm_source_lanes.push_back({.id = object_id_t{90000 + index},
            .source_order = index, .stem_channel = chart.sound_channels[index].id});
}
} // namespace

TEST_CASE("stem waveforms cover channels with events near the cursor or audio reaching it",
          "[sys][waveform-projection][bmson][stems]") {
    song_document_t song;
    song.audio_assets = {{.id = asset_id_t{1}, .stream = {48'000, 2, 48'000 * 4, true}},
                         {.id = asset_id_t{2}, .stream = {48'000, 2, 48'000 * 60, true}}};
    auto& chart = song.charts.emplace_back();
    for (std::uint64_t id = 1; id <= 80; ++id) chart.measures.push_back({.id = measure_id_t{id}});
    chart.bpms = {{.id = object_id_t{900}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}};
    const auto channel = [](std::uint64_t id, std::uint64_t measure, std::uint64_t asset) {
        return sound_channel_t{.id = object_id_t{id}, .source_clip = {.source = asset_id_t{asset}},
            .events = {{.id = object_id_t{id + 1000}, .position = {measure_id_t{measure}, 0}}}};
    };
    // 120 BPM is two seconds per measure: the 4 s clip reaches about 3 measures, the 60 s clip 31.
    chart.sound_channels = {channel(1, 2, 1), channel(2, 40, 1), channel(3, 12, 1), channel(4, 80, 1),
                            channel(5, 2, 2), channel(6, 70, 1)};
    link_window_channels(chart);
    const auto at = [&](std::uint64_t measure) {
        std::vector<std::uint64_t> ids;
        for (const auto* found : sys::visible_stem_channels(song, chart, measure_id_t{measure}, 10))
            ids.push_back(found->id.value);
        return ids;
    };
    CHECK(at(1) == (std::vector<std::uint64_t>{1, 5}));
    // The long clip started at measure 2 is still audible 30 measures later, the short ones are not.
    CHECK(at(30) == (std::vector<std::uint64_t>{2, 5}));
    CHECK(at(40) == (std::vector<std::uint64_t>{2, 5}));
    CHECK(at(45) == (std::vector<std::uint64_t>{2}));
    CHECK(at(60) == (std::vector<std::uint64_t>{6}));
    // A cursor that is not in the chart behaves like the start.
    CHECK(at(9999) == at(1));
}

namespace {
auto configure_mixed_projection(channel_projection_fixture_t& fixture)
    -> std::shared_ptr<const audio::waveform_analysis_result_t> {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "fixtures" / "audio" / "keysound-c4.ogg";
    const auto packed_source = fixture.sources.create_source(
        {asset_id_t{8}, path, {"waveform:packed", "immutable:v1"}});
    expect_gpu_work(static_cast<bool>(packed_source));
    auto packed_asset = fixture.history.document.audio_assets.back();
    packed_asset.id = asset_id_t{8};
    fixture.history.document.audio_assets.push_back(std::move(packed_asset));
    auto& chart = fixture.history.document.charts.front();
    chart.sound_channels.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{90000}, .name = "BGM 1"}};
    chart.measures.front().bgm_events.clear();
    for (std::uint64_t index = 0; index < 1003; ++index) {
        const auto stem = index == 1 || index == 500 || index == 1001;
        const auto lane = object_id_t{stem ? 90001 + index : 90000};
        chart.sound_channels.push_back({.id = object_id_t{71000 + index},
            .source_clip = {clip_id_t{72000 + index}, asset_id_t{stem ? 7U : 8U}},
            .events = {{.id = object_id_t{73000 + index}, .position = {measure_id_t{1}, 0},
                        .playable = false}}});
        if (stem) chart.bgm_source_lanes.push_back({.id = lane,
            .source_order = index, .stem_channel = object_id_t{71000 + index}});
        chart.measures.front().bgm_events.push_back({.id = object_id_t{74000 + index},
            .position = {measure_id_t{1}, 0}, .source_lane = lane,
            .sound_channel_event = object_id_t{73000 + index}});
    }
    ++fixture.history.document.revision;
    fixture.workspace.transport.audio_asset.reset();
    fixture.flattened.view.compose.selected_bmson_bgm_event_id.value = 74000;
    auto packed_analysis = std::make_shared<audio::waveform_analysis_result_t>(*fixture.channel_analysis);
    packed_analysis->waveform->key.source = packed_source->source;
    return packed_analysis;
}

void check_mixed_production_projection(channel_projection_fixture_t& fixture,
    const std::shared_ptr<const audio::waveform_analysis_result_t>& packed_analysis, bool supply_all) {
    std::vector<const sound_channel_t*> all;
    for (const auto& channel : fixture.history.document.charts.front().sound_channels)
        all.push_back(&channel);
    std::optional<std::span<const sound_channel_t* const>> visible;
    if (supply_all) visible = all;
    std::size_t requests{};
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history,
        fixture.workspace, &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources),
        {std::nullopt, nullptr}, {asset_id_t{8}, packed_analysis}, 1,
        [&](asset_id_t) { ++requests; return fixture.channel_analysis; }, visible};
    populate_bmson_waveform_projection(context);
    INFO("requests=" << requests << " cache entries=" << fixture.cache.stems.stem_waveforms.size()
         << " projected views=" << fixture.flattened.view.compose.stem_waveforms.size());
    expect_gpu_work(requests == 3 && fixture.cache.stems.stem_waveforms.size() == 3);
    expect_gpu_work(fixture.flattened.view.compose.stem_waveforms.size() == 3);
    expect_gpu_work(fixture.cache.waveform.channel_projection.view.value().rows.empty());
    expect_gpu_work(!fixture.cache.waveform.channel_waveform_slices.value());
    const std::vector<std::uint64_t> expected{71001, 71500, 72001};
    std::vector<std::uint64_t> keys;
    for (std::size_t index = 0; index < fixture.flattened.view.compose.stem_waveforms.size(); ++index)
        keys.push_back(fixture.flattened.view.compose.stem_waveforms[index].stem_key);
    std::ranges::sort(keys);
    expect_gpu_work(keys == expected);
}
} // namespace

TEST_CASE("BMSON production projection excludes one thousand packed channels and selected packed slices",
          "[sys][waveform-projection][bmson][stems][hybrid-lanes]") {
    channel_projection_fixture_t fixture;
    const auto packed_analysis = configure_mixed_projection(fixture);
    check_mixed_production_projection(fixture, packed_analysis, false);
    check_mixed_production_projection(fixture, packed_analysis, true);
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

TEST_CASE("Shared stem row table is prepared once for song and selected channel and reuses scroll rows",
          "[sys][waveform-projection][bmson][stem][row-table]") {
    channel_projection_fixture_t fixture;
    fixture.project();
    const auto generation = fixture.cache.waveform.waveform_row_table.viewport.value().generation;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().generation == generation);
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().built_rows == 202);
    fixture.interaction.viewport_scroll_pixels += 1;
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().built_rows == 1);
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().reused_rows == 201);
    const auto scrolled_generation = fixture.cache.waveform.waveform_row_table.viewport.value().generation;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().generation == scrolled_generation);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 1);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().reused_rows == 201);
    fixture.flattened.view.compose.pixels_per_standard_measure *= 2;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().built_rows == 202);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 202);
}

TEST_CASE("Stem row table retires display rings and native cut derivation when timing identity changes",
          "[sys][waveform-projection][bmson][stem][row-table]") {
    channel_projection_fixture_t fixture;
    fixture.project_channel();
    const auto generation = fixture.cache.waveform.waveform_row_table.clock.value().generation;
    auto& chart = fixture.history.document.charts.front();
    chart.bpms.front().beats_per_minute = {240, 1};
    fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
    REQUIRE(fixture.cache.timing.marker_timing_index);
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.clock.value().generation == generation + 1);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 202);
    expect_gpu_work(fixture.cache.waveform.channel_waveform_slices.value().has_value());
    const auto unchanged = fixture.cache.waveform.waveform_row_table.viewport.value().generation;
    const auto projected = fixture.cache.waveform.channel_projection.ring.value().projected_rows;
    const auto calls = fixture.cache.waveform.channel_projection.ring.value().projection_calls;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().generation == unchanged);
    // A cache hit leaves the last projection counters intact.
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == projected);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projection_calls == calls);
}

namespace {
struct packed_projection_fixture_t : channel_projection_fixture_t {
    packed_projection_fixture_t() {
        auto& chart = history.document.charts.front();
        chart.measures.resize(2);
        chart.bpms = {{.id = object_id_t{99000}, .position = {measure_id_t{1}, 0},
                       .beats_per_minute = {2400, 1}}};
        chart.bgm_source_lanes = {{.id = object_id_t{3}}, {.id = object_id_t{4}}};
        chart.sound_channels[0].source_clip.begin_frame =
            history.document.audio_assets.back().stream.frame_count.value() / 4;
        chart.sound_channels[1].source_clip.end_frame = 1;
        chart.measures[0].bgm_events.push_back({.id = object_id_t{74002},
            .position = {measure_id_t{1}, 480}, .source_lane = object_id_t{3}});
        flattened.view.compose.selected_bmson_bgm_event_id.value = 0;
        interaction.chart = chart.id;
        interaction.pixels_per_standard_measure = interaction.viewport_height_pixels = 200;
        cache.timing.marker_timing_index = timing_index_t::from(chart).value;
        expect_gpu_work(cache.timing.marker_timing_index.has_value());
    }
    void project_packed(bool ready = true) {
        const std::optional<asset_id_t> absent;
        const std::shared_ptr<const audio::waveform_analysis_result_t> missing;
        const bmson_waveform_projection_context_t context{flattened, history, workspace,
            &interaction, cache, retained_sources_for(history.document, project_sources), {absent, missing}, {absent, missing}, 1,
            [&](asset_id_t) { return ready ? channel_analysis : nullptr; }};
        populate_bmson_waveform_projection(context);
    }
    auto piece() const -> const wormedit::ui::compose_stem_waveform_view_t& {
        const auto views = std::span(flattened.view.compose.stem_waveforms);
        const auto found = std::ranges::find(views, std::uint64_t{74000},
                                             &wormedit::ui::compose_stem_waveform_view_t::stem_key);
        expect_gpu_work(found != views.end());
        return *found;
    }
};

void check_packed_clip_rows(const packed_projection_fixture_t& fixture) {
    const auto& piece = fixture.piece();
    expect_gpu_work(piece.piece_kind == wormedit::ui::compose_waveform_piece_kind_t::packed_event);
    expect_gpu_work(piece.source_lane_id == object_id_t{3} && !piece.pending && piece.rows.size() > 0);
    const auto downscroll = compose_field_downscroll(fixture.history.document.charts.front());
    for (const auto& row : piece.rows) {
        expect_gpu_work(downscroll ? row.y_pixels >= 99 : row.y_pixels <= 101);
        expect_gpu_work(row.absolute_peak[0] >= .20f);
    }
}
} // namespace

TEST_CASE("BMSON packed waveform uses its native clip and next column event",
          "[sys][frame-timing][stem][bmson][packed-waveform]") {
    packed_projection_fixture_t fixture;
    fixture.project_packed();
    check_packed_clip_rows(fixture);
    const auto key = fixture.piece().stem_key;
    const auto builds = fixture.cache.stems.packed_waveform_index.build_count();
    fixture.interaction.viewport_scroll_pixels = 1;
    fixture.project_packed();
    expect_gpu_work(fixture.piece().stem_key == key);
    expect_gpu_work(fixture.cache.stems.packed_waveform_index.build_count() == builds);
    INFO("Scrolled packed rows: " << fixture.cache.stems.packed_waveforms.front().projection.ring.value().projected_rows);
    expect_gpu_work(fixture.cache.stems.packed_waveforms.front().projection.ring.value().projected_rows == 1);
    const auto projected = fixture.cache.stems.packed_waveforms.front().projection.ring.value().projected_rows;
    const auto calls = fixture.cache.stems.packed_waveforms.front().projection.ring.value().projection_calls;
    const auto rows = fixture.cache.stems.packed_waveforms.front().projection.view.value().rows.data();
    const auto source = std::get<0>(*fixture.cache.stems.packed_waveforms.front().slices.key());
    fixture.project_packed();
    expect_gpu_work(fixture.cache.stems.packed_waveforms.front().projection.ring.value().projected_rows == projected);
    expect_gpu_work(fixture.cache.stems.packed_waveforms.front().projection.ring.value().projection_calls == calls);
    expect_gpu_work(fixture.cache.stems.packed_waveforms.front().projection.view.value().rows.data() == rows);
    expect_gpu_work(std::get<0>(*fixture.cache.stems.packed_waveforms.front().slices.key()) == source);
    expect_gpu_work(fixture.cache.stems.packed_waveform_index.build_count() == builds);
}

TEST_CASE("BMSON packed waveform pending band stays inside its event interval",
          "[sys][frame-timing][stem][bmson][packed-waveform]") {
    packed_projection_fixture_t fixture;
    fixture.project_packed(false);
    const auto piece = fixture.piece();
    expect_gpu_work(piece.pending && piece.rows.size() == 0 && piece.bounded);
    expect_gpu_work(piece.end_y_pixels - piece.begin_y_pixels <= 101);
    expect_gpu_work(piece.begin_y_pixels >= 0 && piece.end_y_pixels <= 200);
    fixture.project_packed();
    check_packed_clip_rows(fixture);
    fixture.history.document.audio_assets.back().stream.sample_rate = 0;
    fixture.history.document.audio_assets.back().stream.frame_count.reset();
    fixture.sources.clear();
    ++fixture.history.document.revision;
    fixture.project_packed(false);
    expect_gpu_work(fixture.piece().pending && fixture.piece().bounded);
}

TEST_CASE("BMSON packed sample tail draws beyond authored timing without changing stems",
          "[sys][frame-timing][stem][bmson][packed-waveform]") {
    packed_projection_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.measures.resize(1);
    chart.measures[0].bgm_events.resize(1);
    ++fixture.history.document.revision;
    fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
    const auto authored_rows = fixture.cache.timing.marker_timing_index->total_rows;
    fixture.interaction.viewport_scroll_pixels = 220;
    fixture.project_packed();
    const auto& piece = fixture.piece();
    expect_gpu_work(!piece.pending && piece.rows.size() > 0);
    expect_gpu_work(fixture.cache.stems.packed_waveform_row_table.clock.value().total_rows > authored_rows);
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.clock.value().total_rows == authored_rows);
    expect_gpu_work(fixture.cache.timing.marker_timing_index->total_rows == authored_rows);
    expect_gpu_work(chart.measures.size() == 1);
    const auto previous_peak = piece.rows[0].absolute_peak[0];
    chart.bpms.front().beats_per_minute = {4800, 1};
    ++fixture.history.document.revision;
    fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
    fixture.project_packed();
    expect_gpu_work(fixture.piece().rows[0].absolute_peak[0] < previous_peak);
}
#endif

namespace {
auto resizable_stem_song() -> song_document_t {
    auto song = compose_test_song();
    song.audio_assets = {{.id = asset_id_t{900}, .source_path = "音楽-long-stem.ogg"}};
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.sound_channels = {{.id = object_id_t{901},
        .source_clip = {.id = clip_id_t{902}, .source = asset_id_t{900}}},
        {.id = object_id_t{906}, .source_clip = {.id = clip_id_t{907}, .source = asset_id_t{900}}}};
    chart.bgm_source_lanes = {{.id = object_id_t{903}, .stem_channel = object_id_t{901}},
        {.id = object_id_t{904}}, {.id = object_id_t{905}, .stem_channel = object_id_t{906}}};
    return song;
}

void scroll_resized_bgm(compose_test_fixture_t& fixture, double delta) {
    auto action = cursor_placement_action(fixture);
    action.input.shortcuts.window_focused = true;
    action.input.compose_viewport_inputs.resize(1);
    action.input.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_x = delta,
        .cursor_x_pixels = fixture.flattened.view.compose.playable_field_right_pixels + 20,
        .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}
} // namespace

TEST_CASE("Compose stem width changes reproject same revision and preserve packed width",
          "[sys][compose][stem-lane-width]") {
    compose_test_fixture_t fixture(resizable_stem_song());
    double width = 36;
    std::size_t calls{};
    fixture.application.services.compose_bgm_lane_width = [&](auto, auto, auto) {
        ++calls;
        return width;
    };
    fixture.project();
    const auto revision = fixture.history.document.revision;
    const auto generation = fixture.flattened.view.compose.static_scene_source_generation;
    expect_compose_test(calls == 2);
    width = 300;
    fixture.project();
    expect_compose_test(calls == 4);
    expect_compose_test(fixture.cache.scene.bgm_lane_widths_pixels == std::vector<double>{300, 36, 300});
    expect_compose_test(fixture.flattened.view.compose.static_scene_source_generation != generation);
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[0].width_pixels == 300);
    expect_compose_test(std::string(fixture.flattened.view.compose.bgm_lanes[0].filename) == "音楽-long-stem.ogg");
    expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
}

TEST_CASE("Compose BGM dispatcher reaches last widened lane and clamps resized content",
          "[sys][compose][stem-lane-width]") {
    compose_test_fixture_t fixture(resizable_stem_song());
    fixture.application.services.compose_bgm_lane_width = [](auto, auto, auto) { return 480; };
    fixture.project();
    scroll_resized_bgm(fixture, 10000);
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto maximum = 996.0 - fixture.cache.frame.preview_scene_request.bgm_pane_width_pixels;
    expect_compose_test(std::abs(fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[1] - maximum) < 1e-6);
    expect_compose_test(view.bgm_lanes[view.bgm_lanes.size() - 1].source_lane_id == object_id_t{905});
    scroll_resized_bgm(fixture, -10000);
    expect_compose_test(fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[1] == 0);
    expect_compose_test(fixture.history.document.revision == 1 && fixture.history.undo.empty());
}

namespace {
void t463_check_projected_rows(const compose_test_fixture_t& fixture) {
    const auto& view = fixture.flattened.view.compose;
    for (uint64_t index = 0; index < 3; ++index) {
        const auto rows = std::span(view.keysounds);
        const auto row = std::ranges::find(rows, wormedit::resource_id_t{100 + index}, &wormedit::ui::compose_keysound_row_view_t::resource_id);
        t475_require(row != rows.end());
        t475_check(row->stem_source_lane_id.value == (index < 2 ? 600 + index : 0));
    }
}
} // namespace

TEST_CASE("BMSON Draw projection keeps duplicate asset stem and packed resource ownership distinct",
          "[sys][compose][bmson][stem][draw-source]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 101;
    t475_project(fixture);
    t463_check_projected_rows(fixture);
    t475_check(fixture.flattened.view.compose.draw_source_stem_lane_id.value == 601);
    const auto builds = fixture.cache.stems.draw_stem_sources.build_count();
    fixture.application.project.compose_gesture.draw_source_id = 102;
    t475_project(fixture);
    t463_check_projected_rows(fixture);
    t475_check(fixture.flattened.view.compose.draw_source_stem_lane_id.value == 0);
    t475_check(fixture.cache.stems.draw_stem_sources.build_count() == builds);
}

TEST_CASE("BMSON Draw resource ownership cache invalidates revision and replacement identity",
          "[sys][compose][bmson][stem][draw-source]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    t475_project(fixture);
    const auto builds = fixture.cache.stems.draw_stem_sources.build_count();
    auto proposed = fixture.history.document;
    proposed.charts.front().bgm_source_lanes[1].stem_channel.reset();
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Unlink stem"));
    t475_project(fixture);
    t475_check(fixture.cache.stems.draw_stem_sources.build_count() == builds + 1);
    t475_check(!fixture.cache.stems.draw_stem_sources.value().resource_lanes.contains(101));
    compose_test_fixture_t replacement(t463_shared_asset_song());
    replacement.history.document.revision = fixture.history.document.revision;
    replacement.cache = std::move(fixture.cache);
    t475_project(replacement);
    t463_check_projected_rows(replacement);
    t475_check(replacement.cache.stems.draw_stem_sources.build_count() == builds + 2);
}

TEST_CASE("BMSON Draw source names remain frame owned after document edits and frame moves",
          "[sys][compose][bmson][stem][draw-source][frame-arena]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 101;
    t475_project(fixture);
    const std::string original{fixture.flattened.view.compose.draw_source_stem_lane_name};
    auto retained = std::move(fixture.flattened);
    auto proposed = fixture.history.document;
    proposed.charts.front().bgm_source_lanes[1].name = std::string(256, 'v');
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Rename stem"));
    t475_check(std::string(retained.view.compose.draw_source_stem_lane_name) == original);
    const auto rows = std::span(retained.view.compose.keysounds);
    const auto row = std::ranges::find(rows, wormedit::resource_id_t{uint64_t{101}}, &wormedit::ui::compose_keysound_row_view_t::resource_id);
    t475_require(row != rows.end());
    t475_check(std::string(row->stem_lane_name.c_str()) == original);
    t475_project(fixture);
    t475_check(std::string(fixture.flattened.view.compose.draw_source_stem_lane_name) == std::string(256, 'v'));
}
