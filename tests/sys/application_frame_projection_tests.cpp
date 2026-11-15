// Application frame projection benchmarks, source-clock cuts and projection caches.
#include "frame_timing_fixtures.h"
#include "sys/app/application_frame_view.h"
#include "sys/metadata/metadata_view.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
constexpr std::array<const char*, 13> t275_counter_names{
    "resource_rows", "keysound_rows", "branch_rows", "recipe_rows", "metadata_rows",
    "scene_index", "static_source", "timing_payload", "resource_catalog", "keysound_source",
    "metadata_inheritance", "metadata_lane_targets", "metadata_model_rows"};

auto t275_workload_song() -> song_document_t {
    auto song = dense_song(100, 50);
    song.metadata.title = "Revision-stable projection workload";
    for (std::uint64_t index = 0; index < 1000; ++index) {
        const asset_id_t asset{index + 1};
        song.audio_assets.push_back({.id = asset, .source_path = "sound-" + std::to_string(index) + ".wav"});
        song.resources.push_back({.id = resource_id_t{index + 1}, .name = "Sound " + std::to_string(index),
            .asset = asset, .roles = {resource_role_t::sample}});
    }
    auto& chart = song.charts.front();
    chart.metadata.name = "Projection workload";
    chart.sv_recipes = recipe_test_song().charts.front().sv_recipes;
    for (auto& measure : chart.measures) for (auto& note : measure.notes)
        note.sound = resource_id_t{note.id.value % 1000 + 1};
    return song;
}

struct t275_projection_fixture_t {
    compose_test_fixture_t compose{t275_workload_song()};
    std::optional<workspace_view_t> model;
    metadata_drop_state_t metadata_drop;
    runtime::metadata_asset_t metadata_runtime;
    std::string status;
    ui::keysound_dock_request_t request{.row_limit = 64, .include_ungrouped = true,
        .automatic_row_limit = 64, .filter_layer_row_limit = 64};
    std::array<double, 3> costs{};
    int width{1280}, height{980};

    explicit t275_projection_fixture_t(workspace_tab_t tab) {
        compose.binding.value->active_tab = tab;
        compose.application.project.timing_recipe.recipe_selected = 7001;
        refill();
    }
    void refill() {
        const auto source = workspace_view_t::from(*compose.binding.value, compose.history.document);
        expect_compose_test(source.value.has_value());
        model = std::move(*source.value);
        auto begin = std::chrono::steady_clock::now();
        compose.flattened.refill(*model, compose.history.document, status, {}, false,
            dock_sort_t::natural_name, request, {}, {}, &compose.cache.keysound_list_cache,
            {}, &compose.cache.resource_catalog_cache);
        auto end = std::chrono::steady_clock::now();
        costs[0] = std::chrono::duration<double, std::milli>(end - begin).count();
        begin = end;
        populate_compose_view(compose.flattened, compose.history, *compose.binding.value, compose.cache,
            width, height, compose.interaction, compose.application, nullptr);
        end = std::chrono::steady_clock::now();
        costs[1] = std::chrono::duration<double, std::milli>(end - begin).count();
        begin = end;
        populate_metadata_view_projection(compose.flattened, compose.history, *compose.binding.value,
            {}, metadata_drop, metadata_runtime);
        costs[2] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    }
    auto counts() const -> std::array<std::size_t, 13> {
        const auto& rows = compose.flattened.compose;
        const auto& metadata = compose.flattened.metadata_projection_cache;
        return {rows.resource_rows_refills, rows.keysound_rows_refills, rows.branch_outline_refills,
            rows.recipe_auxiliary_refills, metadata.static_rows_refills, compose.cache.scene.index.build_count(),
            compose.cache.scene.static_source.build_count(), compose.cache.timing_payload_cache.build_count(),
            compose.cache.resource_catalog_cache.catalog.build_count(), compose.cache.keysound_list_cache.projection_build_count(),
            metadata.inheritance.build_count(), metadata.lane_targets.build_count(), metadata.model_rows_refills};
    }
    void transition(unsigned scenario, unsigned frame) {
        auto& history = compose.history;
        auto& interaction = compose.interaction;
        switch (scenario) {
        case 0: break;
        case 1:
            compose.binding.value->transport.position_frame += 256;
            compose.binding.value->transport.playing = true;
            break;
        case 2:
            if (interaction) interaction->viewport_scroll_pixels += 1.0;
            break;
        case 3:
            request.selected_resource_ids = {resource_id_t{frame % 2 + 1}};
            request.primary_resource_id = request.selected_resource_ids.front();
            if (interaction) (void)dispatch_editor_action(history, *interaction,
                select_editor_notes_action_t{{object_id_t{frame % 2 + 3}}});
            break;
        case 4:
            history.document.charts.front().metadata.name = "Edit " + std::to_string(frame);
            ++history.document.revision;
            wormedit::test::require_workspace_result(rebind_workspace(*compose.binding.value, history));
            break;
        case 5: width = frame % 2 ? 1280 : 1296; height = frame % 2 ? 980 : 996; break;
        }
    }
};

void t275_measure_scenario(workspace_tab_t tab, unsigned scenario) {
    t275_projection_fixture_t fixture{tab};
    for (unsigned warm = 0; warm < 3; ++warm) fixture.refill();
    const auto before = fixture.counts();
    constexpr unsigned samples = 30;
    std::array<std::array<double, samples>, 3> times{};
    for (unsigned frame = 0; frame < samples; ++frame) {
        fixture.transition(scenario, frame);
        fixture.refill();
        for (unsigned pass = 0; pass < times.size(); ++pass) times[pass][frame] = fixture.costs[pass];
    }
    const auto after = fixture.counts();
    std::printf("T275 pane=%s scenario=%u samples=%u", tab == workspace_tab_t::compose ? "compose" : "metadata", scenario, samples);
    for (unsigned index = 0; index < before.size(); ++index)
        std::printf(" %s=%zu", t275_counter_names[index], after[index] - before[index]);
    for (unsigned pass = 0; pass < times.size(); ++pass) {
        const auto total = std::accumulate(times[pass].begin(), times[pass].end(), 0.0);
        std::ranges::sort(times[pass]);
        std::printf(" phase%u_avg_ms=%.6f phase%u_p99_ms=%.6f", pass, total / samples, pass, times[pass].back());
    }
    std::printf("\n");
    expect_compose_test(fixture.compose.history.document.charts.front().measures.size() == 100);
}
} // namespace

TEST_CASE("T275 projection owner benchmark across stable transport scroll selection edit and resize frames",
    "[.][projection-work-benchmark]") {
    for (const auto tab : {workspace_tab_t::compose, workspace_tab_t::metadata})
        for (unsigned scenario = 0; scenario < 6; ++scenario) t275_measure_scenario(tab, scenario);
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

namespace {
struct t275_application_frame_fixture_t {
    channel_projection_fixture_t channel;
    application_command_state_t commands;
    native_project_recovery_controller_t recovery;
    timing_grid_state_t grid;
    timing_scene_cache_t timing;
    runtime::timing_edit_t timing_edit;
    player_preview_presenter_t player;
    std::optional<editor_interaction_state_t> interaction;
    std::optional<proposal_t<audio_offset_drag_t>> offset_drag;
    std::optional<workspace_tab_t> offset_tab;
    metadata_drop_state_t drop;
    runtime::metadata_asset_t metadata;
    WormWindow window{.width = 1280, .height = 980};
    audio::decoded_block_cache_t transport_blocks;
    audio::transport_t transport{transport_blocks};
    audio::preview_transport_bridge_t preview_bridge{transport};
    audio::audition_transport_bridge_t audition_bridge{transport};
    audio::preview_session_t preview{channel.project_sources.registry(), transport, preview_bridge};
    audio::audition_session_t audition{channel.project_sources.registry(), transport, audition_bridge};
    runtime::chart_preview_t preview_state;
    runtime::waveform_analysis_t waveforms{channel.project_sources.registry()};
    std::optional<workspace_view_t> model;
    std::filesystem::path asset_base;

    t275_application_frame_fixture_t() {
        auto bound = workspace_controller_t::from(channel.history);
        expect_compose_test(bound.value.has_value());
        channel.workspace = std::move(*bound.value);
        channel.workspace.selected_chart = channel.history.document.charts.front().id;
        channel.workspace.active_tab = workspace_tab_t::compose;
        (void)retained_sources_for(channel.history.document, channel.project_sources);
        prepare();
        refill();
        for (unsigned warm = 0; warm < 30; ++warm) {
            waveforms.service(channel.history.document, channel.workspace, channel.cache,
                interaction ? &*interaction : nullptr);
            refill();
            if (waveforms.stem_waveform_analysis(asset_id_t{7})) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }
    void prepare() {
        const auto built = workspace_view_t::from(channel.workspace, channel.history.document);
        expect_compose_test(built.value.has_value());
        model = *built.value;
    }
    void refill() {
        channel.flattened.refill({.model = *model, .history = channel.history, .workspace = channel.workspace,
            .recovery_controller = recovery, .project_session_generation = 1, .asset_base = asset_base,
            .timing_grid = grid, .timing_scene_cache = timing, .timing_edit = timing_edit,
            .compose_scene_cache = channel.cache, .player_preview_presenter = player,
            .application_command_state = commands, .compose_interaction_state = interaction,
            .audio_offset_drag = offset_drag, .audio_offset_drag_tab = offset_tab,
            .metadata_drop = drop, .metadata_asset_runtime = metadata, .window = window,
            .waveform_runtime = waveforms, .audition_sources = channel.project_sources,
            .audition_source_generation = channel.project_sources.generation,
            .audition_session = audition, .preview_session = preview, .preview_state = preview_state,
            .preview_output_available = true});
    }
    void transition(unsigned scenario, unsigned frame) {
        switch (scenario) {
        case 0: break;
        case 1: preview_state.paused = true; preview_state.resume_frame += 256; break;
        case 2: if (interaction) interaction->viewport_scroll_pixels += 1; break;
        case 3:
            if (interaction) (void)dispatch_editor_action(channel.history, *interaction,
                select_editor_objects_action_t{.objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{74000 + frame % 2}}}});
            break;
        case 4:
            channel.history.document.charts.front().metadata.name = "BMSON edit " + std::to_string(frame);
            ++channel.history.document.revision;
            wormedit::test::require_workspace_result(rebind_workspace(channel.workspace, channel.history));
            break;
        case 5: window.width = frame % 2 ? 1280 : 1296; window.height = frame % 2 ? 980 : 996; break;
        }
        prepare();
        waveforms.service(channel.history.document, channel.workspace, channel.cache, interaction ? &*interaction : nullptr);
    }
};

void t275_measure_application_frame(unsigned scenario) {
    t275_application_frame_fixture_t fixture;
    for (unsigned warm = 0; warm < 3; ++warm) fixture.refill();
    const auto before_index = fixture.channel.cache.stems.stem_index.build_count();
    const auto before_selection = fixture.channel.cache.stems.stem_index.value().selection.build_count();
    const auto before_queries = fixture.channel.cache.stems.visible_channel_queries;
    const auto before_projection = fixture.channel.cache.stems.waveform_projection_calls;
#if defined(WORMEDIT_T275_BEFORE)
    const auto before_rows = before_projection;
#else
    const auto before_rows = fixture.channel.cache.stems.row_refills;
#endif
    std::size_t frame_allocations{};
    double milliseconds{}, bmson_ms{}, controls_ms{};
    constexpr unsigned frames = 30;
    for (unsigned frame = 0; frame < frames; ++frame) {
        fixture.transition(scenario, frame);
        allocations = 0;
        count_allocations = true;
        frame_timing().begin_frame();
        const auto begin = std::chrono::steady_clock::now();
        fixture.refill();
        milliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        frame_timing().end_frame();
        count_allocations = false;
        frame_allocations += allocations;
        const auto& sample = frame_timing().sample(frame_timing().size() - 1);
        bmson_ms += sample.phase_ms[static_cast<std::size_t>(frame_phase_t::workspace_bmson)];
        controls_ms += sample.phase_ms[static_cast<std::size_t>(frame_phase_t::workspace_compose_controls)];
    }
    const auto& cache = fixture.channel.cache;
#if defined(WORMEDIT_T275_BEFORE)
    const auto row_refills = cache.stems.waveform_projection_calls - before_rows;
    const char* row_counter = "derived_from_unconditional_dispatch";
#else
    const auto row_refills = cache.stems.row_refills - before_rows;
    const char* row_counter = "actual_owner_counter";
#endif
    std::printf("T275 application_bmson scenario=%u samples=%u index_builds=%zu selection_builds=%zu channel_queries=%zu projection_calls=%zu row_refills=%zu row_counter=%s frame_allocations=%zu avg_ms=%.6f bmson_avg_ms=%.6f controls_avg_ms=%.6f stems=%zu\n",
        scenario, frames, cache.stems.stem_index.build_count() - before_index,
        cache.stems.stem_index.value().selection.build_count() - before_selection,
        cache.stems.visible_channel_queries - before_queries, cache.stems.waveform_projection_calls - before_projection, row_refills, row_counter,
        frame_allocations, milliseconds / frames, bmson_ms / frames, controls_ms / frames,
        fixture.channel.flattened.view.compose.stem_waveforms.size());
    expect_compose_test(cache.stems.stem_index.has_value());
}
} // namespace

TEST_CASE("T275 real application frame BMSON projection benchmark", "[.][application-projection-benchmark]") {
    for (unsigned scenario = 0; scenario < 6; ++scenario) t275_measure_application_frame(scenario);
}

TEST_CASE("Compose zoom requests waveform detail for the visible source after export cuts",
          "[sys][waveform-projection][export-cut]") {
    t275_application_frame_fixture_t fixture;
    auto& document = fixture.channel.history.document;
    // The short fixture still separates the visible window from the removed intro.
    document.charts.front().bpms.front().beats_per_minute = {1200, 1};
    document.export_cuts = {{.id = object_id_t{90000},
        .begin_milliseconds = 0, .end_milliseconds = 100}};
    ++document.revision;
    retained_sources_for(document, fixture.channel.project_sources);
    wormedit::test::require_workspace_result(rebind_workspace(fixture.channel.workspace,
        fixture.channel.history));
    fixture.channel.workspace.transport.audio_asset = asset_id_t{7};
    expect_compose_test(fixture.interaction.has_value());
    fixture.interaction->pixels_per_standard_measure = GENERATE(1024.0, 4096.0);
    fixture.interaction->viewport_scroll_pixels = 0;
    fixture.prepare();
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        fixture.waveforms.service(document, fixture.channel.workspace, fixture.channel.cache,
            &*fixture.interaction);
        fixture.refill();
        if (fixture.waveforms.detail_analysis()) break;
        SDL_Delay(5);
    }
    const auto detail = fixture.waveforms.detail_analysis();
    expect_compose_test(static_cast<bool>(detail));
    expect_compose_test(detail->waveform.has_value());
    expect_compose_test(static_cast<bool>(fixture.interaction->cut_axis));
    const auto& view = fixture.channel.flattened.view.compose;
    const auto expected = audio::aligned_waveform_window(*fixture.waveforms.waveform_analysis()->waveform,
        *fixture.channel.cache.timing.marker_timing_index,
        {.viewport_scroll_pixels = fixture.interaction->viewport_scroll_pixels,
         .viewport_height_pixels = view.viewport_height_pixels,
         .pixels_per_standard_measure = view.pixels_per_standard_measure,
         .cut_axis = fixture.interaction->cut_axis});
    expect_compose_test(expected.has_value());
    expect_compose_test(detail->waveform->key.begin_frame <= expected->begin_frame);
    expect_compose_test(detail->waveform->key.end_frame >= expected->end_frame);
    expect_compose_test(detail->waveform->key.frames_per_bin == 32);
    expect_compose_test(expected->begin_frame >= audio::output_sample_rate / 10);
    expect_compose_test(!view.waveform_rows.empty());
}

TEST_CASE("Player preview and chart readout follow the source frame across audio cuts",
          "[sys][player-preview][export-cut]") {
    t275_application_frame_fixture_t fixture;
    fixture.commands.project.compose_view.player_preview_visible = true;
    const std::array cuts{export_cut_t{.id = object_id_t{90000},
        .begin_milliseconds = 1000, .end_milliseconds = 2000}};
    const auto timeline = export_cut_timeline_t::from(cuts);
    const auto frames = export_cut_frame_timeline_t::from(timeline.value, audio::output_sample_rate);
    expect_compose_test(frames.diagnostics.empty());
    const auto has_cuts = GENERATE(true, false);
    if (has_cuts) fixture.preview_state.compiled.value().cut_frames = frames.value;
    fixture.preview_state.paused = GENERATE(true, false);
    const auto collapsed = GENERATE(44'099ULL, 44'100ULL, 66'150ULL);
    fixture.preview_state.resume_frame = collapsed;
    if (!fixture.preview_state.paused) {
        const preview_schedule_t schedule{.sample_rate = audio::output_sample_rate};
        expect_compose_test(static_cast<bool>(fixture.preview.start_session(schedule, {}, {},
            collapsed, {}, 200'000)));
    }
    fixture.refill();
    const auto& view = fixture.channel.flattened.view;
    const auto source = !has_cuts || collapsed < 44'100 ? collapsed : collapsed + 44'100;
    expect_compose_test(view.transport_frame == collapsed);
    expect_compose_test(view.player_preview.scene_available);
    expect_compose_test(view.player_preview.sample_frame == source);
    expect_compose_test(view.transport_position.has_value());
    expect_compose_test(view.transport_position->source_row == static_cast<std::int64_t>(source * 480 / 44'100));
}

TEST_CASE("Stopped cursor and audition preview retain their source clock with compiled cuts",
          "[sys][player-preview][export-cut]") {
    t275_application_frame_fixture_t fixture;
    fixture.commands.project.compose_view.player_preview_visible = true;
    const std::array cuts{export_cut_t{.id = object_id_t{90000},
        .begin_milliseconds = 0, .end_milliseconds = 1000}};
    const auto timeline = export_cut_timeline_t::from(cuts);
    fixture.preview_state.compiled.value().cut_frames =
        export_cut_frame_timeline_t::from(timeline.value, audio::output_sample_rate).value;
    expect_compose_test(fixture.interaction.has_value());
    fixture.interaction->cursor = {measure_id_t{1}, 720};
    const auto auditioning = GENERATE(true, false);
    if (auditioning) {
        fixture.preview_state.paused = true;
        const auto source = fixture.channel.sources.create_generated_source({
            .source = {"cut-preview-audition", "v1"},
            .interleaved_stereo = std::vector<float>(1024, 0.25F)});
        expect_compose_test(source.has_value());
        const audio::audition_clip_t clip{.source = source->source, .frame_count = 512};
        expect_compose_test(static_cast<bool>(fixture.audition.start_session(std::span{&clip, 1})));
    }
    fixture.refill();
    const auto& view = fixture.channel.flattened.view;
    const std::uint64_t expected = auditioning ? 0 : 66'150;
    expect_compose_test(view.transport_frame == expected);
    expect_compose_test(view.player_preview.sample_frame == expected);
}
#endif

namespace {
void t275_check_retained_content(const ui::workspace_view_t& cached, const ui::workspace_view_t& cold) {
    expect_compose_test(cached.summary_lines == cold.summary_lines);
    expect_compose_test(cached.compose.resources == cold.compose.resources);
    expect_compose_test(cached.compose.keysounds == cold.compose.keysounds);
    expect_compose_test(cached.compose.keysound_selected_rows == cold.compose.keysound_selected_rows);
    expect_compose_test(cached.compose.keysound_selection_rows == cold.compose.keysound_selection_rows);
    expect_compose_test(cached.compose.keysound_display_rows == cold.compose.keysound_display_rows);
    expect_compose_test(cached.compose.recipe_form == cold.compose.recipe_form);
    expect_compose_test(cached.metadata.search_tags == cold.metadata.search_tags);
    expect_compose_test(cached.metadata.format_fields == cold.metadata.format_fields);
    expect_compose_test(cached.metadata.format_targets == cold.metadata.format_targets);
    expect_compose_test(cached.metadata.inheritance == cold.metadata.inheritance);
    expect_compose_test(cached.metadata.timing_links == cold.metadata.timing_links);
    expect_compose_test(cached.metadata.chart_choices == cold.metadata.chart_choices);
    expect_compose_test(cached.metadata.channel_map_lanes == cold.metadata.channel_map_lanes);
    expect_compose_test(cached.metadata.lane_profile_choices == cold.metadata.lane_profile_choices);
    expect_compose_test(cached.metadata.output_groups == cold.metadata.output_groups);
}

void t275_check_cold_equivalence(t275_projection_fixture_t& fixture) {
    auto cached = std::move(fixture.compose.flattened);
    fixture.compose.flattened = {};
    fixture.refill();
    t275_check_retained_content(cached.view, fixture.compose.flattened.view);
    fixture.compose.flattened = std::move(cached);
}
} // namespace

TEST_CASE("Revision-stable workspace products do no refill work on transport scroll and resize", "[sys][projection-cache]") {
    t275_projection_fixture_t fixture{GENERATE(workspace_tab_t::compose, workspace_tab_t::metadata)};
    const auto scenario = GENERATE(0U, 1U, 2U, 5U);
    fixture.refill();
    const auto before = fixture.counts();
    for (unsigned frame = 0; frame < 3; ++frame) { fixture.transition(scenario, frame); fixture.refill(); }
    const auto after = fixture.counts();
    for (const auto index : {0U, 1U, 2U, 3U, 4U, 12U}) expect_compose_test(after[index] == before[index]);
    t275_check_cold_equivalence(fixture);
}

TEST_CASE("Retained workspace content matches a cold projection for all six dependency scenarios", "[sys][projection-cache]") {
    t275_projection_fixture_t fixture{GENERATE(workspace_tab_t::compose, workspace_tab_t::metadata)};
    fixture.transition(GENERATE(0U, 1U, 2U, 3U, 4U, 5U), 1);
    fixture.refill();
    t275_check_cold_equivalence(fixture);
}

TEST_CASE("Same-sized source selection refreshes retained resource and keysound presentation", "[sys][projection-cache]") {
    t275_projection_fixture_t fixture{workspace_tab_t::compose};
    fixture.request.selected_resource_ids = {resource_id_t{1}};
    fixture.compose.binding.value->transport.audio_asset = asset_id_t{1};
    fixture.refill();
    const auto before = fixture.counts();
    fixture.request.selected_resource_ids.front() = resource_id_t{2};
    fixture.compose.binding.value->transport.audio_asset = asset_id_t{2};
    fixture.refill();
    const auto after = fixture.counts();
    expect_compose_test(after[0] == before[0] + 1 && after[1] == before[1] + 1);
    expect_compose_test(fixture.compose.flattened.view.compose.resources[1].selected_audio_asset);
    expect_compose_test(!fixture.compose.flattened.view.compose.resources.front().selected_audio_asset);
    t275_check_cold_equivalence(fixture);
}

TEST_CASE("Tab retirement clears cached rows and rebuilds them on reopening", "[sys][projection-cache]") {
    t275_projection_fixture_t fixture{workspace_tab_t::compose};
    const auto before = fixture.counts();
    fixture.compose.binding.value->active_tab = workspace_tab_t::metadata;
    fixture.refill();
    expect_compose_test(fixture.compose.flattened.view.compose.resources.empty());
    fixture.compose.binding.value->active_tab = workspace_tab_t::compose;
    fixture.refill();
    expect_compose_test(fixture.counts()[0] == before[0] + 1);
    t275_check_cold_equivalence(fixture);
}

TEST_CASE("Metadata projection detects a different same-stamp document owner", "[sys][projection-cache]") {
    t275_projection_fixture_t first{workspace_tab_t::metadata};
    t275_projection_fixture_t second{workspace_tab_t::metadata};
    second.compose.history.document.charts.front().metadata.name = "Replacement owner";
    second.compose.flattened = std::move(first.compose.flattened);
    second.refill();
    expect_compose_test(second.compose.flattened.view.metadata.chart_choices.front().name == "Replacement owner");
    t275_check_cold_equivalence(second);
}

TEST_CASE("Metadata jobs and drops refresh at the same document revision without static row work", "[sys][projection-cache]") {
    t275_projection_fixture_t fixture{workspace_tab_t::metadata};
    const auto before = fixture.counts();
    auto& drop = fixture.metadata_drop;
    drop.project_id = fixture.compose.history.document.id.value;
    drop.music_revision = fixture.compose.history.document.revision;
    drop.chart_id = fixture.compose.binding.value->selected_chart->value;
    drop.music_path = "/tmp/updated-music.wav";
    fixture.refill();
    expect_compose_test(fixture.compose.flattened.view.metadata.dropped_music_path == drop.music_path);
    expect_compose_test(fixture.counts()[4] == before[4]);
    t275_check_cold_equivalence(fixture);
}

TEST_CASE("Asynchronous recipe publication refreshes retained rows without a document edit", "[sys][projection-cache]") {
    t275_projection_fixture_t fixture{workspace_tab_t::compose};
    auto& history = fixture.compose.history;
    const auto& chart = history.document.charts.front();
    const auto& recipe = chart.sv_recipes.front();
    auto form = ui::timing_recipe_form_t::from(recipe);
    task_scheduler_t scheduler{1, 4};
    recipe_evaluation_runtime_t evaluation{scheduler};
    auto& preview = fixture.compose.application.project.timing_recipe.recipe_preview;
    (void)evaluation.request(history.document, chart, recipe, form, {});
    evaluation.wait(history.document, chart.id, recipe.id, preview);
    expect_compose_test(preview.has_value() && preview->report());
    const auto generation = preview->publication_generation();
    fixture.refill();
    const auto first_points = fixture.compose.flattened.view.compose.recipe_form.sampled_points;
    const auto before = fixture.counts()[3];
    std::snprintf(form.value_to.text, sizeof(form.value_to.text), "7");
    (void)evaluation.request(history.document, chart, recipe, form, {});
    evaluation.wait(history.document, chart.id, recipe.id, preview);
    expect_compose_test(preview->publication_generation() != generation);
    fixture.refill();
    expect_compose_test(fixture.counts()[3] == before + 1);
    expect_compose_test(fixture.compose.flattened.view.compose.recipe_form.sampled_points != first_points);
    t275_check_cold_equivalence(fixture);
    preview.reset();
    fixture.refill();
    expect_compose_test(fixture.compose.flattened.view.compose.recipe_form.sampled_points.empty());
}

TEST_CASE("Metadata source selection refreshes cached music headers without an edit", "[sys][projection-cache]") {
    t275_projection_fixture_t fixture{workspace_tab_t::metadata};
    fixture.compose.binding.value->transport.audio_asset = asset_id_t{1};
    fixture.refill();
    const auto before = fixture.counts()[4];
    fixture.compose.binding.value->transport.audio_asset = asset_id_t{2};
    fixture.refill();
    const auto& metadata = fixture.compose.flattened.view.metadata;
    expect_compose_test(metadata.selected_music_asset_id == asset_id_t{2});
    expect_compose_test(metadata.selected_music_source == "sound-1.wav");
    expect_compose_test(metadata.selected_music_resource_count == 1);
    expect_compose_test(fixture.counts()[4] == before + 1);
    t275_check_cold_equivalence(fixture);
}

TEST_CASE("Zoom refreshes viewport geometry while revision-stable rows stay retained", "[sys][projection-cache]") {
    t275_projection_fixture_t fixture{workspace_tab_t::compose};
    expect_compose_test(fixture.compose.interaction.has_value());
    const auto before = fixture.counts();
    fixture.compose.interaction->pixels_per_standard_measure *= 2;
    fixture.refill();
    const auto after = fixture.counts();
    expect_compose_test(after[0] == before[0] && after[1] == before[1]);
    t275_check_cold_equivalence(fixture);
}

TEST_CASE("An empty note selection projects the armed hitsound palette directly", "[sys][projection-cache]") {
    compose_test_fixture_t fixture;
    fixture.project();
    auto& state = *fixture.interaction;
    state.hitsound_palette_visible = true;
    state.armed_hitsample = osu_hitsample_t{.whistle = true, .clap = true,
        .normal_set = osu_sample_set_t::soft, .addition_set = osu_sample_set_t::drum};
    fixture.project();
    const std::array expected{*state.armed_hitsample};
    expect_compose_test(fixture.flattened.view.compose.hitsounds.visible);
    expect_compose_test(fixture.flattened.view.compose.hitsounds.values == hitsound_values_t::from(expected));
}

namespace {
void t275_measure_selected_transport() {
    t275_projection_fixture_t fixture{workspace_tab_t::compose};
    fixture.transition(3, 0);
    for (unsigned warm = 0; warm < 3; ++warm) fixture.refill();
    const auto builds = fixture.compose.cache.scene.hit_test_index.build_count();
    std::array<double, 30> costs{};
    for (unsigned frame = 0; frame < costs.size(); ++frame) {
        fixture.transition(1, frame);
        fixture.refill();
        costs[frame] = fixture.costs[1];
    }
    const auto total = std::accumulate(costs.begin(), costs.end(), 0.0);
    std::ranges::sort(costs);
    std::printf("T275 selected_stable_transport samples=30 note_index_builds=%zu controls_avg_ms=%.6f controls_p99_ms=%.6f\n",
        fixture.compose.cache.scene.hit_test_index.build_count() - builds, total / costs.size(), costs.back());
    expect_compose_test(fixture.compose.interaction->selected_notes.size() == 1);
}
} // namespace

TEST_CASE("T275 selected but stable transport projection benchmark", "[.][selected-projection-benchmark]") {
    t275_measure_selected_transport();
}

TEST_CASE("Stable selected hitsounds reuse the canonical source index and preserve mixed values", "[sys][projection-cache]") {
    auto song = dense_song(100, 50);
    auto& chart = song.charts.front();
    chart.measures.front().notes.front().osu_hitsample = osu_hitsample_t{.whistle = true};
    chart.measures.back().notes.back().osu_hitsample = osu_hitsample_t{.clap = true};
    const auto first = chart.measures.front().notes.front().id;
    const auto last = chart.measures.back().notes.back().id;
    const std::array expected{*chart.measures.front().notes.front().osu_hitsample,
        *chart.measures.back().notes.back().osu_hitsample};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    fixture.interaction->hitsound_palette_visible = true;
    (void)dispatch_editor_action(fixture.history, *fixture.interaction,
        select_editor_notes_action_t{{last, first}});
    fixture.project();
    const auto builds = fixture.cache.scene.hit_test_index.build_count();
    for (unsigned frame = 0; frame < 3; ++frame) {
        fixture.binding.value->transport.position_frame += 256;
        fixture.project();
    }
    expect_compose_test(builds != 0 && fixture.cache.scene.hit_test_index.build_count() == builds);
    expect_compose_test(fixture.flattened.view.compose.hitsounds.values == hitsound_values_t::from(expected));
}
