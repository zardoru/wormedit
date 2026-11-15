// Warmed Compose view refills without heap allocations.
#include "frame_timing_fixtures.h"
#include "sys/compose/views/generator_view.h"
#include "sys/compose/generator_runtime.h"
#include "sys/resources/bga_dock_runtime.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void refill_owned_scene_geometry(compose_test_fixture_t& fixture, bool bgm_only = false) {
    const auto& chart = fixture.history.document.charts.front();
    const auto& index = *fixture.cache.scene.index.value();
    const auto& interaction = *fixture.cache.frame.interaction_view;
    const timing_lane_pseudonote_outcome_t* stage{};
    const std::span<const local_position_t> comparison{};
    const compose_bga_move_preview_input_t* bga{};
    const compose_frame_inputs_t frame{fixture.flattened, fixture.history, fixture.cache,
        fixture.interaction, fixture.application, stage, comparison, bga};
    const auto scope = compose_chart_scope_t<>::from({chart, index.measures}, interaction,
        false, compose_pixels_per_row(chart, interaction));
    const auto inputs = compose_projection_context_t<>::from(frame, scope);
    const auto& view = fixture.flattened.view.compose;
    const compose_projection_detail::compose_scene_projection_context_t projection{
        .inputs = inputs, .index = index, .scene = *fixture.cache.scene.scene_window.scene,
        .field_left_pixels = view.playable_field_left_pixels,
        .playable_field_width = view.playable_field_right_pixels - view.playable_field_left_pixels};
    compose_projection_detail::populate_compose_sound_labels(projection);
    compose_projection_detail::build_compose_bgm_view(projection, fixture.cache);
    if (bgm_only) return;
    compose_projection_detail::build_compose_layout_and_notes_view(projection);
    compose_projection_detail::build_compose_bga_view(projection);
    populate_lane_layout_segments(fixture.flattened, chart,
        index.measures, interaction, false, fixture.flattened.view.compose);
}

void refill_owned_timing_geometry(compose_test_fixture_t& fixture) {
    const auto& chart = fixture.history.document.charts.front();
    const auto& interaction = *fixture.cache.frame.interaction_view;
    auto& view = fixture.flattened.view.compose;
    const auto pixels = compose_pixels_per_row(chart, interaction);
    const auto rows = fixture.cache.scene.index.value()->measures.total_rows;
    compose_projection_detail::populate_compose_timing_markers(fixture.flattened,
        fixture.history.document, chart, fixture.cache, interaction, view, false, pixels, rows, nullptr);
    compose_projection_detail::populate_compose_dead_spans(fixture.flattened,
        fixture.cache, interaction, view, false, pixels);
    compose_projection_detail::populate_compose_kiai_and_sv(fixture.flattened,
        fixture.cache, interaction, view, false, pixels, rows, 0);
    compose_projection_detail::populate_compose_speed_spans(fixture.flattened,
        fixture.cache, interaction, view, false, pixels);
    compose_projection_detail::populate_compose_recipe_areas(fixture.flattened,
        chart, fixture.cache, interaction, view, false, pixels, rows, 0, nullptr);
}
} // namespace

TEST_CASE("Owned populated Compose geometry refills without heap allocations",
          "[sys][compose-geometry][allocation]") {
    auto song = recipe_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes[1].kind = note_kind_t::hold;
    chart.measures.front().notes[1].end = local_position_t{measure_id_t{1}, 360};
    chart.measures.front().notes.front().sound = resource_id_t{900};
    chart.measures.front().notes.front().osu_hitsample = osu_hitsample_t{
        .whistle = true, .filename = "retained-custom-note-sample.wav"};
    song.audio_assets.push_back({.id = asset_id_t{900}, .source_path = "sample.wav"});
    song.resources.push_back({.id = resource_id_t{900},
        .name = "Long retained sample label for geometry allocation coverage.wav",
        .asset = asset_id_t{900}, .roles = {resource_role_t::sample}});
    chart.pauses.push_back({.id = object_id_t{901}, .position = {measure_id_t{1}, 240},
        .duration = {1, 2}});
    chart.scrolls.push_back({.id = object_id_t{902}, .position = {measure_id_t{1}, 120},
        .multiplier = {2, 1}});
    chart.kiai.push_back({.id = object_id_t{903}, .position = {measure_id_t{1}, 120}, .enabled = true});
    chart.speeds.push_back({.id = object_id_t{904}, .position = {measure_id_t{1}, 120},
        .multiplier = {2, 1}, .transition_beats = {1, 1}});
    compose_test_fixture_t fixture(std::move(song));
    fixture.project();
    refill_owned_scene_geometry(fixture);
    refill_owned_timing_geometry(fixture);
    auto& view = fixture.flattened.view.compose;
    expect_compose_test(!view.note_endpoints.empty() && !view.hold_bodies.empty());
    expect_compose_test(!view.grid_lines.empty() && !view.lane_layout_segments.empty());
    expect_compose_test(!view.dead_spans.empty() && !view.kiai_spans.empty());
    expect_compose_test(!view.recipe_areas.empty() && !view.recipe_graph_points.empty());
    expect_compose_test(!view.sv_graph_points.empty() && !view.speed_graph_points.empty());
    const auto* notes = view.note_endpoints.data();
    const auto* grid = view.grid_lines.data();
    const auto before = frame_heap_allocation_snapshot();
    refill_owned_scene_geometry(fixture);
    refill_owned_timing_geometry(fixture);
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(view.note_endpoints.data() == notes && view.grid_lines.data() == grid);
    expect_compose_test(view.note_endpoints.front().hitsound != nullptr);
    expect_compose_test(view.note_endpoints.front().hitsound->filename == "retained-custom-note-sample.wav");
    const auto previous_label = fixture.flattened.compose.sound_labels.at(900);
    fixture.history.document.resources.back().name = "Reopened project sample.wav";
    ++fixture.flattened.query_session_generation;
    refill_owned_scene_geometry(fixture);
    expect_compose_test(fixture.flattened.compose.sound_labels.at(900) != previous_label);
    const auto note_capacity = view.note_endpoints.capacity();
    fixture.flattened.reset();
    expect_compose_test(view.note_endpoints.empty() && view.note_endpoints.capacity() == note_capacity);
}

TEST_CASE("Compose layout segment lists exceed the retired storage cutoff", "[sys][compose-geometry]") {
    constexpr std::size_t segment_count = 2049;
    auto song = dense_song(segment_count, 0);
    auto& chart = song.charts.front();
    const auto layout = chart.lane_layouts.front().layout;
    chart.lane_layouts.clear();
    for (std::size_t index = 0; index < segment_count; ++index)
        chart.lane_layouts.push_back({.id = object_id_t{10000 + index},
            .position = {chart.measures[index].id, 0}, .layout = layout});
    const auto measures = measure_index_t::from(chart);
    REQUIRE(measures);
    frontend_workspace_view_t flattened;
    auto& view = flattened.view.compose;
    view.viewport_height_pixels = 600;
    editor_interaction_view_model_t interaction;
    interaction.pixels_per_standard_measure = 240;
    populate_lane_layout_segments(flattened, chart, *measures, interaction, false, view);
    expect_compose_test(view.lane_layout_segments.size() == segment_count && !view.truncated);
    expect_compose_test(view.lane_layout_segments.back().segment_id == object_id_t{12048});
}

TEST_CASE("Owned populated Compose BGM waveform and stem views refill without heap allocations",
          "[sys][compose-bgm][waveform-projection][stem][allocation]") {
    compose_test_fixture_t bgm(bgm_arena_song(format_family_t::bms));
    bgm.project();
    refill_owned_scene_geometry(bgm, true);
    envelope_projection_fixture_t waveform;
    waveform.project();
    channel_projection_fixture_t stems;
    const bmson_waveform_projection_context_t context{stems.flattened, stems.history,
        stems.workspace, &stems.interaction, stems.cache,
        retained_sources_for(stems.history.document, stems.project_sources),
        {stems.asset, stems.channel_analysis}, {stems.asset, stems.channel_analysis}, 1,
        [&](asset_id_t) { return stems.channel_analysis; }};
    populate_bmson_waveform_projection(context);
    expect_compose_test(!bgm.flattened.view.compose.bgm_lanes.empty());
    expect_compose_test(!bgm.flattened.view.compose.bgm_events.empty());
    expect_compose_test(!waveform.flattened.view.compose.waveform_rows.empty());
    expect_compose_test(!waveform.flattened.view.compose.waveform_overview_rows.empty());
    expect_compose_test(!stems.flattened.view.compose.stem_waveforms.empty());
    expect_compose_test(!stems.flattened.view.compose.stem_waveforms.front().rows.empty());
    const auto* events = bgm.flattened.view.compose.bgm_events.data();
    const auto* rows = waveform.flattened.view.compose.waveform_rows.data();
    const auto* pieces = stems.flattened.view.compose.stem_waveforms.data();
    const auto before = frame_heap_allocation_snapshot();
    refill_owned_scene_geometry(bgm, true);
    const auto available = project_waveform_frame(waveform);
    populate_bmson_waveform_projection(context);
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(available);
    expect_compose_test(bgm.flattened.view.compose.bgm_events.data() == events);
    expect_compose_test(waveform.flattened.view.compose.waveform_rows.data() == rows);
    expect_compose_test(stems.flattened.view.compose.stem_waveforms.data() == pieces);
    const auto event_capacity = bgm.flattened.view.compose.bgm_events.capacity();
    const auto row_capacity = waveform.flattened.view.compose.waveform_rows.capacity();
    const auto piece_capacity = stems.flattened.view.compose.stem_waveforms.capacity();
    bgm.flattened.reset();
    waveform.flattened.reset();
    stems.flattened.reset();
    expect_compose_test(bgm.flattened.view.compose.bgm_events.empty() &&
        bgm.flattened.view.compose.bgm_events.capacity() == event_capacity);
    expect_compose_test(waveform.flattened.view.compose.waveform_rows.empty() &&
        waveform.flattened.view.compose.waveform_rows.capacity() == row_capacity);
    // BMSON rows survive frame reset; hidden Compose retirement clears them.
    expect_compose_test(!stems.flattened.view.compose.stem_waveforms.empty() &&
        stems.flattened.view.compose.stem_waveforms.capacity() == piece_capacity);
}

TEST_CASE("Compose BGM event view has no retired storage cutoff", "[sys][compose-bgm]") {
    constexpr std::size_t event_count = 32769;
    frontend_workspace_view_t flattened;
    std::vector<editor_field_bgm_event_geometry_t> events(event_count);
    for (std::size_t index = 0; index < events.size(); ++index) {
        events[index].event = object_id_t{index + 1};
        events[index].source_lane = object_id_t{1};
        events[index].bounds = {.left = 0, .top = 0, .right = 20, .bottom = 10};
    }
    // Exercise the view storage owner directly: scene admission retains its separate
    // primitive work budget, while projecting an admitted list must not truncate it.
    compose_projection_detail::build_compose_bgm_events_view(flattened, events, flattened.view.compose);
    expect_compose_test(flattened.view.compose.bgm_events.size() == event_count);
    expect_compose_test(flattened.view.compose.bgm_events.back().event_id == object_id_t{event_count});
    expect_compose_test(!flattened.view.compose.truncated);
}

TEST_CASE("Compose direct sample waveform cache refreshes its source style and viewport",
          "[sys][waveform-projection][allocation]") {
    envelope_projection_fixture_t fixture;
    fixture.flattened.view.compose.viewport_height_pixels = 32;
    prepare_waveform_sample_points(fixture);
    fixture.project();
    const auto& rows = fixture.flattened.view.compose.waveform_rows;
    expect_compose_test(!rows.empty() && rows.front().sample_point);
    expect_compose_test(std::abs(rows.front().samples[0] - .8f) < .00001f);
    const auto builds = fixture.cache.waveform.direct_projection.build_count();
    const auto before = frame_heap_allocation_snapshot();
    const auto available = project_waveform_frame(fixture);
    const auto after = frame_heap_allocation_snapshot();
    expect_compose_test(available);
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == builds);

    const auto previous_size = rows.size();
    fixture.flattened.view.compose.viewport_height_pixels = 40;
    fixture.project();
    expect_compose_test(rows.size() > previous_size);
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == builds + 1);
    fixture.style.detail_level = false;
    fixture.project();
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == builds + 2);
    fixture.style.detail_level = true;
    fixture.style.sample_points = false;
    fixture.project();
    expect_compose_test(std::ranges::none_of(rows, &wormedit::ui::compose_waveform_row_view_t::sample_point));
    fixture.style.sample_points = true;
    fixture.project();
    expect_compose_test(std::ranges::all_of(rows, &wormedit::ui::compose_waveform_row_view_t::sample_point));

    auto replacement = fixture.analysis;
    replacement.waveform->key.source = {"replacement:direct-samples", "content:v2"};
    const audio::audio_block_key_t key{replacement.waveform->key.source, 0};
    const std::vector<float> pcm(32768, .2f);
    expect_compose_test(fixture.blocks.store(key, 16384, pcm).has_value());
    fixture.analysis = std::move(replacement);
    fixture.project();
    expect_compose_test(!rows.empty() && rows.front().sample_point);
    expect_compose_test(std::abs(rows.front().samples[0] - .2f) < .00001f);

    const auto original_stats = fixture.blocks.stats();
    const auto original_builds = fixture.cache.waveform.direct_projection.build_count();
    const std::vector<float> replacement_pcm(32768, .4f);
    expect_compose_test(fixture.blocks.store(key, 16384, replacement_pcm).has_value());
    auto current_stats = fixture.blocks.stats();
    expect_compose_test(current_stats.entries == original_stats.entries &&
        current_stats.cached_frames == original_stats.cached_frames &&
        current_stats.evictions == original_stats.evictions);
    fixture.project();
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == original_builds + 1);
    expect_compose_test(!rows.empty() && rows.front().sample_point);
    expect_compose_test(std::abs(rows.front().samples[0] - .4f) < .00001f);

    fixture.blocks.clear_source(key.source);
    const audio::audio_block_key_t unrelated{{"unrelated:direct-samples", "content:v1"}, 0};
    expect_compose_test(fixture.blocks.store(unrelated, 16384, replacement_pcm).has_value());
    current_stats = fixture.blocks.stats();
    expect_compose_test(current_stats.entries == original_stats.entries &&
        current_stats.cached_frames == original_stats.cached_frames &&
        current_stats.evictions == original_stats.evictions);
    fixture.project();
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == original_builds + 2);
    expect_compose_test(!rows.empty());
    expect_compose_test(std::abs(rows.front().samples[0] - .4f) > .1f);
}

TEST_CASE("Compose BGA dock owns complete rows and reuses warmed projection storage",
          "[sys][compose-bga][allocation]") {
    song_document_t song;
    song.id = song_id_t{7};
    song.revision = 4;
    song.visual_assets.push_back({.id = asset_id_t{1}, .kind = asset_kind_t::image,
        .source_path = "long-retained-image-source-path.png"});
    song.resources.push_back({.id = resource_id_t{2}, .name = std::string(80, 'b'),
        .asset = asset_id_t{1}, .roles = {resource_role_t::bga}, .missing = true});
    bga_dock_runtime_t runtime;
    wormedit::ui::compose_view_t view{};
    const ui::bga_dock_request_t request{.visible = true, .ids_available = true, .row_limit = 64};
    runtime.project(song, {}, request, view, 1);
    REQUIRE(view.bga_rows.size() == 1);
    const auto* rows = view.bga_rows.data();
    const auto before = frame_heap_allocation_snapshot();
    runtime.project(song, {}, request, view, 1);
    const auto after = frame_heap_allocation_snapshot();
    expect_compose_test(after.count == before.count);
    expect_compose_test(after.bytes == before.bytes);
    expect_compose_test(view.bga_rows.data() == rows);
    expect_compose_test(view.bga_rows.front().resource_id == resource_id_t{2});
    expect_compose_test(view.bga_matching_ids == std::vector<std::uint64_t>{2});
    song.resources.front().name = "Reopened BGA";
    runtime.project(song, {}, request, view, 2);
    song.resources.clear();
    expect_compose_test(view.bga_rows.front().name == "Reopened BGA");
}

TEST_CASE("Compose fallback paths retain every candidate without warmed allocations",
          "[sys][compose-keysound][audio-fallback][allocation]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto stem = std::string(80, 's');
    std::ofstream(directory->path() / (stem + ".ogg")) << "x";
    std::ofstream(directory->path() / (stem + ".flac")) << "x";
    document_history_t history;
    history.document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = stem + ".wav"});
    history.document.resources.push_back({.id = resource_id_t{2}, .asset = asset_id_t{1}, .missing = true});
    frontend_workspace_view_t frame;
    populate_compose_keysound_fallbacks(frame, history, directory->path());
    REQUIRE(frame.view.compose.keysound_fallbacks.size() == 1);
    REQUIRE(frame.view.compose.keysound_fallbacks.front().candidates.size() == 2);
    const auto before = frame_heap_allocation_snapshot();
    populate_compose_keysound_fallbacks(frame, history, directory->path());
    const auto after = frame_heap_allocation_snapshot();
    expect_compose_test(after.count == before.count);
    expect_compose_test(after.bytes == before.bytes);
    expect_compose_test(frame.view.compose.keysound_fallbacks.front().outcome == audio_fallback_outcome_t::ambiguous);
    history.document.resources.front().missing = false;
    populate_compose_keysound_fallbacks(frame, history, directory->path());
    expect_compose_test(frame.view.compose.keysound_fallbacks.empty());
}

TEST_CASE("Remaining Compose leaf families refill retained text without warmed allocations",
          "[sys][compose-remaining][allocation]") {
    auto song = marker_arena_song();
    auto& chart = song.charts.front();
    chart.semantic_layers = semantic_arena_song().charts.front().semantic_layers;
    chart.sv_recipes = recipe_test_song().charts.front().sv_recipes;
    song.quantization_presets.push_back({.name = std::string(64, 'p'), .divisions = 8});
    compose_test_fixture_t fixture(std::move(song));
    fixture.application.project.timing_recipe.recipe_selected = 7001;
    fixture.project();
    auto& app = fixture.application.project.semantic_layer;
    app.note_layer_menu_chart = chart_id_t{1};
    app.note_layer_menu_revision = fixture.history.document.revision;
    app.note_layer_menu_row_count = 18;
    const auto& stored = fixture.history.document.charts.front();
    const auto& measures = *fixture.cache.scene.cached_measure_index;
    const auto refill = [&] {
        fixture.flattened.reset();
        auto& view = fixture.flattened.view.compose;
        view.visible = true;
        const auto bookmarks = compose_projection_detail::populate_compose_bookmark_storage(
            fixture.flattened, stored, measures, view);
        const auto sections = compose_projection_detail::populate_compose_section_storage(
            fixture.flattened, stored, measures, view);
        const auto layers = compose_projection_detail::populate_semantic_layer_palette(
            fixture.flattened, stored, fixture.cache, fixture.history, fixture.application);
        compose_projection_detail::populate_note_layer_menu(fixture.flattened, stored,
            fixture.history.document.revision, fixture.application);
        compose_projection_detail::populate_quantize_preview(fixture.cache, fixture.history.document,
            stored, fixture.history.document.revision, *fixture.interaction, fixture.application, view);
        compose_projection_detail::populate_compose_recipe_form(fixture.flattened, stored,
            fixture.application, fixture.interaction->supported_snap_denominators, view);

        return bookmarks && sections && layers;
    };
    expect_compose_test(refill());
    const auto before = frame_heap_allocation_snapshot();
    const auto valid = refill();
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(valid);
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.beat_bookmarks.size() == 2 && view.audio_sections.size() == 2);
    expect_compose_test(view.semantic_layers.size() == 2 && view.note_menu_layers.size() == 2);
    expect_compose_test(view.quantize_presets.size() == 1 && view.recipe_form.visible);
    expect_compose_test(view.measure_rows.front().measure_id == stored.measures.front().id);
}

namespace {
void refill_lua_recipe_views(compose_test_fixture_t& fixture) {
    const auto& chart = fixture.history.document.charts.front();
    const auto& interaction = *fixture.cache.frame.interaction_view;
    auto& view = fixture.flattened.view.compose;
    const auto& state = fixture.application.project.timing_recipe;
    compose_projection_detail::populate_compose_recipe_form(fixture.flattened, chart,
        fixture.application, interaction.supported_snap_denominators, view);
    compose_projection_detail::populate_compose_recipe_areas(fixture.flattened,
        chart, fixture.cache, interaction, view, false, compose_pixels_per_row(chart, interaction),
        fixture.cache.scene.index.value()->measures.total_rows, state.recipe_selected,
        state.recipe_preview ? &*state.recipe_preview : nullptr);
}

void prepare_lua_recipe_view(compose_test_fixture_t& fixture) {
    const auto& chart = fixture.history.document.charts.front();
    const auto& recipe = chart.sv_recipes.front();
    auto preview = recipe_form_preview_t::propose(fixture.history.document, chart, recipe,
        ui::timing_recipe_form_t::from(recipe));
    const std::vector emissions{sv_recipe_emission_t{240, 1}, sv_recipe_emission_t{480, 2}, sv_recipe_emission_t{720, 3}};
    auto prepared = sv_recipe_prepared_t::from_emissions(chart, recipe, emissions);
    expect_compose_test(prepared.diagnostics.empty());
    preview.report()->composed_points = prepared.value.samples().points;
    preview.report()->prepared.push_back(std::move(prepared.value));
    fixture.application.project.timing_recipe.recipe_selected = recipe.id.value;
    fixture.application.project.timing_recipe.recipe_preview = std::move(preview);
}
} // namespace

TEST_CASE("Lua inspector and prepared recipe graph reuse their warmed buffers", "[sys][recipe-form][recipe-areas][allocation]") {
    auto song = recipe_test_song();
    auto& recipe = song.charts.front().sv_recipes.front();
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "function(p,ctx) return 1+p end";
    recipe.lua = sv_recipe_lua_t{.source_reference = "scripts/a-long-retained-lua-module-reference.lua",
        .parameters = {{"gain", {3, 2}}},
        .mode = sv_recipe_lua_mode_t::ordered, .cycles = {{3}}};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    prepare_lua_recipe_view(fixture);
    refill_lua_recipe_views(fixture);
    auto& form = fixture.flattened.view.compose.recipe_form.form;
    const auto* parameters = form.lua_parameters.data();
    const auto* cycles = form.lua_cycles.data();
    const auto* points = fixture.flattened.compose.timing_geometry.recipe_preview.points.data();
    const auto before = frame_heap_allocation_snapshot();
    refill_lua_recipe_views(fixture);
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(form.lua_parameters.data() == parameters && form.lua_cycles.data() == cycles);
    expect_compose_test(fixture.flattened.compose.timing_geometry.recipe_preview.points.data() == points);
    expect_compose_test(!fixture.flattened.view.compose.recipe_form.sampled_points.empty());
    fixture.history.document.charts.front().sv_recipes.front().lua->parameters.front().value = {7, 2};
    ++fixture.history.document.revision;
    fixture.project();
    expect_compose_test(std::strcmp(form.lua_parameters.front().value.text, "3.5") == 0);
}

#if defined(WORMEDIT_HAS_LUA)

namespace {
auto generator_geometry_song() -> song_document_t {
    auto song = dense_song(2, 0);
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes = {
        {.id = lane_id_t{72}, .name = "Sparse left", .display_order = 50, .glyph = lane_glyph_t::bar},
        {.id = lane_id_t{4}, .name = "Sparse right", .display_order = 100, .glyph = lane_glyph_t::arrow_left}};
    auto later = chart.lane_layouts.front();
    later.id = object_id_t{900001};
    later.position.row = 240;
    later.layout.lanes[0].display_order = 100;
    later.layout.lanes[0].glyph = lane_glyph_t::square;
    later.layout.lanes[1].display_order = 50;
    later.layout.lanes[1].glyph = lane_glyph_t::arrow_up;
    chart.lane_layouts.push_back(std::move(later));
    const std::string source = R"(return {layers={{key="generated",name="Generated"}},notes={
        {key="hold",measure=1,row=120,lane=72,kind="hold",end_measure=1,end_row=360,layers={"generated"}},
        {key="tap",measure=1,row=480,lane=4}}})";
    song.generator_recipes.push_back({.id = recipe_id_t{900010}, .api_version = "1",
        .source_hash = native_project_content_checksum(source), .source = source, .chart = chart.id});
    return song;
}

struct generator_geometry_fixture_t {
    compose_test_fixture_t compose{generator_geometry_song()};
    task_scheduler_t scheduler{1, 4};
    std::shared_ptr<generator_runtime_t> runtime{std::make_shared<generator_runtime_t>(scheduler)};
    generator_geometry_fixture_t() {
        compose.project();
        const auto& recipe = compose.history.document.generator_recipes.front();
        auto form = ui::generator_form_t::from(recipe);
        form.use_saved_source = true;
        compose.application.project.generator.runtime = runtime;
        compose.application.project.generator.selected = recipe.id;
        static_cast<void>(runtime->request(compose.history, recipe.chart, form, {}, {}));
        runtime->wait(compose.history, recipe.chart);
        expect_compose_test(runtime->result() && runtime->result()->proposal.value && runtime->result()->geometry);
        compose.project();
    }
    void refill() {
        const auto& chart = compose.history.document.charts.front();
        populate_compose_generator(compose.flattened, compose.history, chart,
            *compose.cache.frame.interaction_view, compose.application, compose.flattened.view.compose,
            compose.cache.frame.preview_scene_request);
    }
};

void check_generator_scene_endpoints(const editor_field_scene_view_t& scene,
    const ui::compose_view_t& view) {
    expect_compose_test(scene.note_endpoints.size() == 3);
    for (const auto& endpoint : scene.note_endpoints) {
        const auto ghost = std::ranges::find_if(view.generator_ghosts, [&](const auto& value) {
            return value.kind == ui::compose_paste_ghost_kind_t::note_endpoint &&
                value.glyph == endpoint.glyph && value.bounds == ui::compose_rect_t::from(endpoint.bounds);
        });
        expect_compose_test(ghost != view.generator_ghosts.end());
    }
    const auto tail = std::ranges::find(scene.note_endpoints, true, &editor_field_note_endpoint_geometry_t::tail);
    expect_compose_test(tail != scene.note_endpoints.end() && tail->glyph == lane_glyph_t::square);
    const auto cursor_lane = std::ranges::find(view.lanes, lane_id_t{72}, &ui::compose_lane_view_t::lane_id);
    expect_compose_test(cursor_lane != view.lanes.end() && tail->bounds.left > cursor_lane->bounds.left);
}

void check_generator_scene_bodies(const editor_field_scene_view_t& scene, const ui::compose_view_t& view) {
    expect_compose_test(!scene.hold_bodies.empty());
    for (const auto& body : scene.hold_bodies) {
        const auto ghost = std::ranges::find_if(view.generator_ghosts, [&](const auto& value) {
            return value.kind == ui::compose_paste_ghost_kind_t::hold_body &&
                value.bounds == ui::compose_rect_t::from(body.bounds);
        });
        expect_compose_test(ghost != view.generator_ghosts.end());
    }
}
} // namespace

TEST_CASE("Generator ghost field scenes use each sparse reordered layout and both hold endpoints",
          "[sys][frame-timing][generator][compose-geometry]") {
    generator_geometry_fixture_t fixture;
    const auto* scene = fixture.runtime->preview_scene(*fixture.compose.cache.frame.interaction_view,
        fixture.compose.cache.frame.preview_scene_request);
    REQUIRE(scene);
    check_generator_scene_endpoints(*scene, fixture.compose.flattened.view.compose);
    check_generator_scene_bodies(*scene, fixture.compose.flattened.view.compose);
    const auto& geometry = *fixture.runtime->result()->geometry;
    CHECK(geometry.chart.semantic_layers.size() == 1);
    CHECK(geometry.index->notes.front().semantic_color.has_value());
    CHECK(fixture.compose.history.document.charts.front().semantic_layers.empty());
}

TEST_CASE("Warmed generator ghost field scenes reuse their owned geometry and projection without allocations",
          "[sys][frame-timing][generator][allocation]") {
    generator_geometry_fixture_t fixture;
    fixture.refill();
    const auto* geometry = fixture.runtime->result()->geometry.get();
    const auto* ghosts = fixture.compose.flattened.view.compose.generator_ghosts.data();
    allocations = 0;
    count_allocations = true;
    fixture.refill();
    fixture.refill();
    count_allocations = false;
    const auto counted = allocations;
    CHECK(counted == 0);
    CHECK(fixture.runtime->result()->geometry.get() == geometry);
    CHECK(fixture.compose.flattened.view.compose.generator_ghosts.data() == ghosts);
}
#endif
