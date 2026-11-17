#pragma once

#include "frame_timing_support.h"
#include "compose_scene_cache_fixture.h"
#include "../support/workspace_result_assertions.h"
#include "../sys/application_dispatch_fixture.h"
#include "project_audio_sources_fixture.h"
#include "sys/audio/bmson_waveform_view.h"
#include "sys/compose/input/compose_input_internal.h"
#include "sys/compose/views/compose_view_internal.h"
#include "../support/temporary_directory.h"
#include "sys/audio/waveform_analysis_runtime.h"
#include "sys/audio/waveform_view.h"
#include <wormedit/visual/job_result.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
inline auto dense_song(std::uint64_t measure_count = 1000,
                       std::uint64_t notes_per_measure = 50) -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 1;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.lane_layouts = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "frame-timing", .display_name = "Timing workload",
                   .lanes = {{.id = lane_id_t{1}, .name = "Key", .role = lane_role_t::playable}}}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    for (std::uint64_t measure = 0; measure < measure_count; ++measure) {
        measure_chunk_t value;
        value.id = measure_id_t{measure + 1};
        for (std::uint64_t note = 0; note < notes_per_measure; ++note)
            value.notes.push_back({.id = object_id_t{3 + measure * notes_per_measure + note},
                                   .position = {value.id, note * 18}, .lane = lane_id_t{1}});
        chart.measures.push_back(std::move(value));
    }
    song.charts.push_back(std::move(chart));
    return song;
}

inline auto compose_test_song() -> song_document_t {
    return dense_song(1, 8);
}

struct compose_test_fixture_t {
    document_history_t history;
    decltype(workspace_controller_t::from(history)) binding;
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    frontend_workspace_view_t flattened;
    explicit compose_test_fixture_t(song_document_t song = compose_test_song())
        : history(document_history_t::from(std::move(song))), binding(workspace_controller_t::from(history)) {
        expect_compose_test(binding.value.has_value());
        binding.value->active_tab = workspace_tab_t::compose;
        binding.value->selected_chart = history.document.read_charts().front()->id;
    }
    void project() {
        application.project.bms_program.synchronize(history, binding.value->selected_chart);
        flattened = {};
        flattened.view.compose.visible = true;
        populate_compose_view(flattened, history, *binding.value, cache, 1280, 980,
                              interaction, application, nullptr);
        CAPTURE(flattened.projection_feedback.size(), cache.frame.projection_error,
                flattened.view.active_tab, history.read_chart(*binding.value->selected_chart)->measures.size(),
                interaction.has_value());
        for (const auto& feedback : flattened.projection_feedback)
            UNSCOPED_INFO(feedback.message);
        expect_compose_test(flattened.view.compose.scene_available);
    }
};

inline void dispatch_t437_quantize_settings(compose_test_fixture_t& fixture,
           quantization_tie_t tie, quantization_hold_t holds, std::uint64_t divisions = 4,
           quantization_space_t space = quantization_space_t::standard_measure,
           bool apply = false) {
    notification_center_t center;
    notification_sink_t sink{center};
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t resource_copy{scheduler, sink};
    runtime::output_group_export_t export_runtime{sink};
    wormedit::ui::workspace_action_t action{};
    action.compose_action = apply ? WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE
                                  : WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS;
    action.stamp.project_id.value = fixture.history.document.id.value;
    action.stamp.chart_id.value = fixture.history.document.charts.front().id.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.quantize.divisions = divisions;
    action.quantize.space = space;
    action.quantize.tie = tie;
    action.quantize.holds = holds;
    (void)dispatch_compose_view_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, fixture.flattened.view.compose.viewport_height_pixels,
        fixture.application, {}, scheduler, resource_copy, export_runtime, false, sink);
    fixture.application.project.quantize.quantize_editor->preview_enabled = true;
}

inline auto bga_virtual_preview_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bms;
    chart.bga_events = {{.id = object_id_t{500},
        .position = {measure_id_t{1}, 120}, .resource = resource_id_t{501}}};
    song.resources = {{.id = resource_id_t{501}, .name = "preview image",
        .asset = asset_id_t{601}, .roles = {resource_role_t::bga}}};
    song.visual_assets = {{.id = asset_id_t{601}, .kind = asset_kind_t::image}};
    return song;
}
} // namespace

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

namespace {
inline auto overview_analysis(float peak) -> audio::waveform_analysis_result_t {
    audio::waveform_t waveform;
    waveform.key = {{"fake:overview", "content:v1"}, 256, 0, 0};
    waveform.total_frames = 256 * 4096;
    waveform.bins.resize(4096);
    for (auto& bin : waveform.bins) {
        bin.minimum = {-peak, -peak};
        bin.maximum = {peak, peak};
        bin.absolute_peak = {peak, peak};
        bin.frame_count = 256;
    }
    return {.waveform = std::move(waveform)};
}

struct envelope_projection_fixture_t {
    document_history_t history{document_history_t::from(dense_song())};
    compose_scene_cache_t cache;
    frontend_workspace_view_t flattened;
    editor_interaction_state_t interaction;
    audio::waveform_analysis_result_t analysis{overview_analysis(.25f)};
    audio::decoded_block_cache_t blocks;
    audio::aligned_waveform_style_t style;
    envelope_projection_fixture_t() {
        static_cast<void>(cache.scene.index.get_or_build(compose_scene_source_key_t{
            .stamp = {.document = {.revision = history.document.revision},
                      .chart = history.document.charts.front().id}}, retained_scene_fixture_t{}));
        cache.timing.marker_timing_index = timing_index_t::from(history.document.charts.front()).value;
        expect_gpu_work(cache.timing.marker_timing_index.has_value());
        auto& view = flattened.view.compose;
        view.visible = view.scene_available = true;
        view.project_id.value = history.document.id.value;
        view.viewport_height_pixels = view.pixels_per_standard_measure = 200;
        interaction.viewport_scroll_pixels = 10;
        for (std::size_t index = 0; index < analysis.waveform->bins.size(); ++index) {
            auto& bin = analysis.waveform->bins[index];
            const auto value = static_cast<float>(index + 1) / analysis.waveform->bins.size();
            bin.absolute_peak = {value, value};
            bin.rms = {value * .5f, value * .5f};
        }
    }
    void project() {
        expect_gpu_work(populate_compose_waveform_projection(flattened, cache, &analysis, false,
            true, &interaction, nullptr, &blocks, &style));
    }
};

inline void expect_published_waveform_rows(std::span<const wormedit::ui::compose_waveform_row_view_t> rows, double height) {
    for (const auto& row : rows)
        expect_gpu_work(std::isfinite(row.y_pixels) && row.y_pixels >= 0 && row.y_pixels <= height);
}

inline void compare_cached_envelope_source(envelope_projection_fixture_t& fixture) {
    const auto height = fixture.flattened.view.compose.viewport_height_pixels;
    const auto scroll = std::floor(fixture.interaction.viewport_scroll_pixels);
    const auto reference = audio::aligned_waveform_view_result_t::from(*fixture.analysis.waveform,
        nullptr, *fixture.cache.timing.marker_timing_index,
        {.viewport_scroll_pixels = scroll - 1, .viewport_height_pixels = height + 2,
         .pixels_per_standard_measure = fixture.flattened.view.compose.pixels_per_standard_measure},
        &fixture.blocks, fixture.style);
    expect_gpu_work(static_cast<bool>((!reference.status.has_failure())));
    std::size_t index = 0;
    for (const auto& row : reference.rows) {
        const auto y = row.y_pixels - 1;
        if (y < 0 || y > height) continue;
        expect_gpu_work(index < fixture.flattened.view.compose.waveform_rows.size());
        const auto& cached = fixture.flattened.view.compose.waveform_rows[index++];
        expect_gpu_work(std::abs(cached.y_pixels - y) < 1e-8);
        expect_gpu_work(std::abs(cached.absolute_peak[0] - row.absolute_peak[0]) < 1e-6f);
        expect_gpu_work(std::abs(cached.rms[0] - row.rms[0]) < 1e-6f);
    }
    expect_gpu_work(index == fixture.flattened.view.compose.waveform_rows.size());
}

inline void prepare_waveform_sample_points(envelope_projection_fixture_t& fixture) {
    fixture.flattened.view.compose.pixels_per_standard_measure = 200'000;
    fixture.style = {.sample_points = true, .detail_level = true};
    const audio::audio_block_key_t key{fixture.analysis.waveform->key.source, 0};
    const std::vector<float> pcm(32768, .8f);
    expect_gpu_work(fixture.blocks.store(key, 16384, pcm).has_value());
}

struct channel_projection_fixture_t : envelope_projection_fixture_t {
    workspace_controller_t workspace;
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    std::optional<asset_id_t> asset{asset_id_t{7}};
    std::shared_ptr<const audio::waveform_analysis_result_t> channel_analysis;
    channel_projection_fixture_t() {
        const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                          "fixtures" / "audio" / "keysound-c4.ogg";
        const auto added = sources.create_source({asset_id_t{7}, path, {"waveform:test", "immutable:v1"}});
        expect_gpu_work(static_cast<bool>(added));
        history.document.audio_assets.push_back({.id = asset_id_t{7}, .source_path = path,
            .stream = {added->source_sample_rate, 2, added->source_frame_count, true}});
        auto& waveform = *analysis.waveform;
        waveform.key.source = added->source;
        waveform.sample_rate = added->output_sample_rate;
        waveform.total_frames = added->output_frame_count;
        waveform.bins.resize((waveform.total_frames + 255) / 256);
        for (std::size_t index = 0; index < waveform.bins.size(); ++index) {
            auto& bin = waveform.bins[index];
            const auto peak = static_cast<float>(index + 1) / waveform.bins.size();
            bin.absolute_peak = {peak, peak};
            bin.rms = {peak * .5f, peak * .5f};
            bin.frame_count = std::min<std::uint64_t>(256, waveform.total_frames - index * 256);
        }
        channel_analysis = std::make_shared<audio::waveform_analysis_result_t>(analysis);
        auto& chart = history.document.charts.front();
        chart.source_family = format_family_t::bmson;
        chart.lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::arrow_left;
        chart.sound_channels = {
            {.id = object_id_t{71000}, .source_clip = {clip_id_t{72000}, asset_id_t{7}},
             .events = {{.id = object_id_t{73000}, .position = {measure_id_t{1}, 0},
                         .lane = lane_id_t{3}, .playable = false}}},
            {.id = object_id_t{71001}, .source_clip = {clip_id_t{72001}, asset_id_t{7}},
             .events = {{.id = object_id_t{73001}, .position = {measure_id_t{1}, 240},
                         .lane = lane_id_t{3}, .playable = false}}}};
        chart.bgm_source_lanes = {{.id = object_id_t{3}, .stem_channel = object_id_t{71000}},
                                  {.id = object_id_t{4}, .stem_channel = object_id_t{71001}}};
        chart.measures.front().bgm_events = {
            {.id = object_id_t{74000}, .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{3},
             .sound_channel_event = object_id_t{73000}},
            {.id = object_id_t{74001}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{4},
             .sound_channel_event = object_id_t{73001}}};
        workspace.selected_chart = chart.id;
        flattened.view.compose.selected_bmson_bgm_event_id.value = 74000;
        interaction.viewport_scroll_pixels = 0;
    }
    void project_channel() {
        cache.waveform.channel_projection.view.value().rows.clear();
        const bmson_waveform_projection_context_t context{flattened, history, workspace, &interaction,
            cache, retained_sources_for(history.document, project_sources), {asset, channel_analysis}, {asset, channel_analysis}, 1};
        populate_bmson_waveform_projection(context);
        expect_published_waveform_rows(cache.waveform.channel_projection.view.value().rows, flattened.view.compose.viewport_height_pixels);
    }
};
} // namespace
#endif

namespace {
inline void populate_measure_cache_frame(document_history_t& history, workspace_controller_t& workspace,
                                         compose_scene_cache_t& cache,
                                         std::optional<editor_interaction_state_t>& interaction) {
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    application_command_state_t application;
    populate_compose_view(flattened, history, workspace, cache, 1280, 980, interaction,
                          application, nullptr);
    INFO(flattened.projection_feedback.size());
    REQUIRE(flattened.view.compose.scene_available);
}

inline auto static_style_song() -> song_document_t {
    auto song=dense_song();
    auto& chart=song.charts.front();
    chart.measures.resize(1);
    chart.measures.front().notes.resize(8);
    chart.semantic_layers={
        {.id=layer_id_t{101},.name="First",.color={99,120,140,255}},
        {.id=layer_id_t{102},.name="Second",.color={150,100,130,255}}};
    for (std::size_t index=0; index<chart.measures.front().notes.size(); ++index)
        chart.measures.front().notes[index].semantic_layers={layer_id_t{101+index%2}};
    return song;
}

inline auto scalar_reuse_song() -> song_document_t {
    auto song = dense_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.profile_id = "stepmania:dance-single";
    auto& hold = chart.measures.front().notes.front();
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{measure_id_t{8}, 240};
    chart.bgm_source_lanes = {{.id = object_id_t{70000}, .name = "Retained BGM"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{70001},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{70000}}};
    return song;
}

inline void compare_scalar_bounds(wormedit::ui::compose_rect_t actual, wormedit::ui::compose_rect_t expected) {
    expect_static_style(std::abs(actual.left - expected.left) < 1e-8);
    expect_static_style(std::abs(actual.top - expected.top) < 1e-8);
    expect_static_style(std::abs(actual.right - expected.right) < 1e-8);
    expect_static_style(std::abs(actual.bottom - expected.bottom) < 1e-8);
}

inline void compare_scalar_notes(const wormedit::ui::compose_view_t& actual, const wormedit::ui::compose_view_t& expected) {
    expect_static_style(actual.note_endpoints.size() == expected.note_endpoints.size());
    for (std::size_t index = 0; index < actual.note_endpoints.size(); ++index) {
        const auto& note = actual.note_endpoints[index];
        const auto& fresh = expected.note_endpoints[index];
        compare_scalar_bounds(note.bounds, fresh.bounds);
        expect_static_style(std::tie(note.note_id.value, note.lane_id.value, note.display_lane_number,
                                     note.glyph, note.snap_class, note.kind, note.tail,
                                     note.selected, note.semantic_color_visible) ==
                            std::tie(fresh.note_id.value, fresh.lane_id.value, fresh.display_lane_number,
                                     fresh.glyph, fresh.snap_class, fresh.kind, fresh.tail,
                                     fresh.selected, fresh.semantic_color_visible));
        expect_static_style(std::string_view(note.sound_label) ==
                            std::string_view(fresh.sound_label));
        expect_static_style(std::string_view(note.sound_detail) ==
                            std::string_view(fresh.sound_detail));
    }
}

inline void compare_scalar_holds_bgm(const wormedit::ui::compose_view_t& actual, const wormedit::ui::compose_view_t& expected) {
    expect_static_style(actual.hold_bodies.size() == expected.hold_bodies.size());
    for (std::size_t index = 0; index < actual.hold_bodies.size(); ++index) {
        compare_scalar_bounds(actual.hold_bodies[index].bounds, expected.hold_bodies[index].bounds);
        expect_static_style(actual.hold_bodies[index].note_id.value ==
                            expected.hold_bodies[index].note_id.value);
        expect_static_style(actual.hold_bodies[index].selected ==
                            expected.hold_bodies[index].selected);
    }
    expect_static_style(actual.bgm_events.size() == expected.bgm_events.size());
    for (std::size_t index = 0; index < actual.bgm_events.size(); ++index) {
        compare_scalar_bounds(actual.bgm_events[index].bounds, expected.bgm_events[index].bounds);
        expect_static_style(actual.bgm_events[index].event_id ==
                            expected.bgm_events[index].event_id);
    }
}

inline void compare_scalar_grid(const wormedit::ui::compose_view_t& actual, const wormedit::ui::compose_view_t& expected) {
    expect_static_style(actual.grid_lines.size() == expected.grid_lines.size());
    for (std::size_t index = 0; index < actual.grid_lines.size(); ++index) {
        const auto& line = actual.grid_lines[index];
        const auto& fresh = expected.grid_lines[index];
        expect_static_style(std::abs(line.y_pixels - fresh.y_pixels) < 1e-8);
        expect_static_style(
            std::tie(line.kind, line.emphasis, line.measure_number, line.chart_row) ==
            std::tie(fresh.kind, fresh.emphasis, fresh.measure_number, fresh.chart_row));
    }
}

inline auto paste_preview_song() -> song_document_t {
    auto song = compose_test_song();
    auto& notes = song.charts.front().measures.front().notes;
    notes.resize(1);
    notes.front().position.row = 0;
    return song;
}
} // namespace

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

namespace {
inline auto dispatch_paste_action(compose_test_fixture_t& fixture, wormedit::ui::workspace_action_t& action) -> std::string {
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    return dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}

inline auto kiai_screen_y(compose_test_fixture_t& fixture, std::uint64_t row) -> double {
    const auto& chart = fixture.history.document.charts.front();
    const auto inspected = inspect_editor_interaction(fixture.history, *fixture.interaction);
    const auto* view = std::get_if<editor_interaction_view_model_t>(&inspected);
    expect_compose_test(view != nullptr);
    const auto y = editor_position_y_pixels(chart, *view, {chart.measures.front().id, row});
    expect_compose_test(y.has_value());
    const bool downscroll = compose_field_downscroll(chart, fixture.application.preferences.editor_preferences);
    return compose_source_y(*y, view->viewport_height_pixels, downscroll);
}

inline auto invoke_compose_registered_command(compose_test_fixture_t& fixture,
           std::string_view command_id = "edit.delete") -> command_dispatch_outcome_t {
    notification_center_t center;
    notification_sink_t sink{center};
    std::optional<timing_lane_pseudonote_outcome_t> timing;
    const auto registry = command_registry_t::from([&](const command_invocation_context_t& context) {
        const auto command = parse_application_command(context.command.value);
        if (!command) return dispatch_outcome_t{};
        return invoke_application_command(*command, fixture.history, *fixture.binding.value,
            fixture.interaction, timing, fixture.application, sink);
    });
    return invoke_command(registry, {std::string(command_id)}, {.workspace_active = true,
        .active_tab = workspace_tab_t::compose, .visible_tabs = {workspace_tab_t::compose}});
}

inline auto kiai_input(compose_test_fixture_t& fixture, WormUiComposeInputKind kind, std::uint64_t head,
                       std::uint8_t edge = WORM_UI_SPAN_HANDLE_BEGIN, double y = 0.0,
                       double anchor_y = 0.0) -> wormedit::ui::workspace_action_t {
    const auto& chart = fixture.history.document.charts.front();
    wormedit::ui::workspace_action_t action{};
    action.stamp.chart_id.value = chart.id.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{1}));
    action.input.compose_inputs[0] = {.kind = kind, .object_id = {head}, .timing_chart_id = {chart.id.value},
                                .timing_document_revision = fixture.history.document.revision,
                                .y_pixels = y, .span_edge = edge,
                                .span_anchor_y_pixels = anchor_y};
    action.input.compose_inputs.resize(1);
    return action;
}

inline void project_with_eighth_snap(compose_test_fixture_t& fixture) {
    fixture.application.project.compose_view.requested_snap_denominators.emplace(
        fixture.history.document.charts.front().id.value, 8);
    fixture.project();
}

inline auto recipe_test_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    sv_recipe_t recipe{.id = recipe_id_t{7001},
                       .name = "rise",
                       .range = {{chart.measures.front().id, 240}, {chart.measures.front().id, 720}},
                       .output = sv_recipe_output_t::sv,
                       .curve = sv_recipe_curve_t::linear,
                       .parameters = {.value_from = {1, 1}, .value_to = {3, 1}}};
    recipe.sampling.fixed_point_count = 3;
    chart.sv_recipes.push_back(recipe);
    return song;
}
} // namespace
#endif

namespace {
inline void project_geometry_arena(compose_test_fixture_t& fixture,
                                   int height = 980) {
    fixture.flattened = frontend_workspace_view_t{};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, height, fixture.interaction, fixture.application, nullptr);
}

inline void compare_geometry_rect(const wormedit::ui::compose_rect_t& a, const wormedit::ui::compose_rect_t& b) {
    expect_compose_test(a.left == Catch::Approx(b.left));
    expect_compose_test(a.top == Catch::Approx(b.top));
    expect_compose_test(a.right == Catch::Approx(b.right));
    expect_compose_test(a.bottom == Catch::Approx(b.bottom));
}

inline auto bgm_arena_song(format_family_t family) -> song_document_t {
    auto song = scalar_reuse_song();
    auto& chart = song.charts.front();
    chart.source_family = family;
    song.resources.push_back({.id = resource_id_t{81000}, .name = "Independent BGM resource label",
        .roles = {resource_role_t::bgm}, .target_identities = {
            {.family = format_family_t::bms, .identifier = "01", .namespace_name = "WAV"}}});
    chart.measures.front().bgm_events.front().sound = resource_id_t{81000};
    if (family == format_family_t::bmson) {
        chart.sound_channels = {{.id = object_id_t{82000}, .name = "BGM slices",
            .source_clip = {.id = clip_id_t{82001}, .source = asset_id_t{82002}},
            .events = {{.id = object_id_t{82003}, .position = {measure_id_t{1},240},
                .end = local_position_t{measure_id_t{1},480}, .lane = lane_id_t{0},
                .continuation = false, .playable = false}}}};
        chart.measures.front().bgm_events.front().sound_channel_event = object_id_t{82003};
    }
    return song;
}

inline auto semantic_arena_song(std::size_t count=2) -> song_document_t {
    auto song=compose_test_song();
    auto& chart=song.charts.front();
    for(std::size_t i=0;i<count;++i)
        chart.semantic_layers.push_back({.id=layer_id_t{101+i},.name=std::string(80,'L')+std::to_string(i),
            .color={99,120,140,255},.visible=true,.locked=i==0});
    if(count) chart.measures.front().notes.front().semantic_layers={layer_id_t{101}};
    return song;
}

inline auto project_waveform_frame(envelope_projection_fixture_t& fixture) -> bool {
    return populate_compose_waveform_projection(fixture.flattened, fixture.cache,
        &fixture.analysis, false, true, &fixture.interaction, nullptr,
        &fixture.blocks, &fixture.style);
}

inline auto marker_arena_song(std::size_t count = 2) -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    for (std::size_t index = 0; index < count; ++index) {
        chart.bookmarks.push_back({.id = object_id_t{10000 + index * 2},
            .label = std::string(100, 'b') + std::to_string(index), .anchor = marker_anchor_t::beat,
            .position = local_position_t{measure_id_t{1}, 96 + index % 100}});
        chart.bookmarks.push_back({.id = object_id_t{10001 + index * 2},
            .label = std::string(100, 'a') + std::to_string(index), .anchor = marker_anchor_t::audio_time,
            .audio_milliseconds = static_cast<std::int64_t>(500 + index)});
        chart.sections.push_back({.id = object_id_t{20000 + index * 2},
            .label = std::string(100, 's') + std::to_string(index), .anchor = marker_anchor_t::beat,
            .beat_span = position_span_t{{measure_id_t{1}, 96}, {measure_id_t{1}, 480}}});
        chart.sections.push_back({.id = object_id_t{20001 + index * 2},
            .label = std::string(100, 't') + std::to_string(index), .anchor = marker_anchor_t::audio_time,
            .audio_millisecond_span = std::pair<std::int64_t, std::int64_t>{200, 1600}});
    }
    return song;
}

inline auto cursor_placement_action(compose_test_fixture_t& fixture) -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
    action.input.shortcuts.window_focused = true;
    action.stamp.chart_id.value = fixture.interaction->chart.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    return action;
}

inline auto paint_stroke_inputs(compose_test_fixture_t& fixture, std::uint64_t sound) -> wormedit::ui::workspace_action_t {
    auto& state = *fixture.interaction;
    const local_position_t position{(*fixture.history.read_chart(*fixture.binding.value->selected_chart)).measures.front().id, 240};
    const auto& chart = (*fixture.history.read_chart(*fixture.binding.value->selected_chart));
    const auto view = inspect_editor_interaction(fixture.history, state);
    const auto y = editor_position_y_pixels(chart,
        std::get<editor_interaction_view_model_t>(view), position);
    expect_compose_test(y.has_value());
    auto action = cursor_placement_action(fixture);
    action.input.compose_inputs.resize(2);
    action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{1}));
    action.input.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, state.viewport_height_pixels,
            compose_field_downscroll(chart, fixture.application.preferences.editor_preferences)),
        .resource_id = {sound}};
    action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{2}));
    action.input.compose_inputs[1] = action.input.compose_inputs[0];
    action.input.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE;
    return action;
}

inline auto lane_key_callback_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.audio_assets = {{.id = asset_id_t{1}}};
    song.resources = {
        {.id = resource_id_t{7}, .asset = asset_id_t{1}, .roles = {resource_role_t::sample}},
        {.id = resource_id_t{8}, .asset = asset_id_t{1}, .roles = {resource_role_t::sample}}};
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.source_family = format_family_t::bms;
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    chart.lane_layouts = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "lane-key-callback", .lanes = {
            {.id = lane_id_t{1}, .name = "Key 1"}, {.id = lane_id_t{2}, .name = "Key 2"}}}}};
    chart.semantic_layers = {{.id = layer_id_t{3}, .name = "Open"},
                             {.id = layer_id_t{4}, .name = "Locked", .locked = true}};
    chart.sound_layer_defaults = {{.resource = resource_id_t{7},
                                   .layers = {layer_id_t{3}, layer_id_t{4}}}};
    song.charts.push_back(std::move(chart));
    return song;
}

struct lane_key_callback_fixture_t {
    compose_test_fixture_t compose{lane_key_callback_song()};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    lane_key_callback_fixture_t() {
        placed_resource_notifications.clear();
        compose.application.project.compose_gesture.note_paint_mode_active = true;
        compose.project();
        compose.interaction->active_snap_denominator = 4;
    }
    ~lane_key_callback_fixture_t() { placed_resource_notifications.clear(); }
    auto dispatch(WormUiComposeInputKind kind, std::uint64_t lane = 1,
                  std::uint64_t sound = 7, bool successive = false) -> std::string {
        auto action = cursor_placement_action(compose);
        action.stamp.project_id.value = compose.history.document.id.value;
        action.input.compose_inputs.resize(1);
        action.input.compose_inputs[0] = {.kind = kind, .lane_id = {lane},
                                    .successive = successive, .resource_id = {sound}};
        return dispatch_compose_input_action(action, compose.history, *compose.binding.value,
            *compose.interaction, viewport, navigation, compose.application, compose.cache, 0);
    }
    auto notes() const -> std::vector<note_t> {
        std::vector<note_t> result;
        for (const auto& measure : compose.history.document.charts.front().measures)
            result.insert(result.end(), measure.notes.begin(), measure.notes.end());
        return result;
    }
};

inline void t475_project(compose_test_fixture_t& fixture) {
    wormedit::test::require_workspace_result(rebind_workspace(*fixture.binding.value, fixture.history));
    const auto model = workspace_view_t::from(*fixture.binding.value, fixture.history.document);
    t475_require(model.value.has_value());
    fixture.flattened = frontend_workspace_view_t::from(*model.value, fixture.history.document, "", {}, false, dock_sort_t::natural_name,
        {}, {}, {}, &fixture.cache.keysound_list_cache);

    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    std::string diagnostics;
    for (const auto& feedback : fixture.flattened.projection_feedback) diagnostics += feedback.message;
    INFO(diagnostics);
    t475_require(fixture.flattened.view.compose.scene_available);
}

inline auto t463_shared_asset_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.measures.front().notes.clear();
    song.audio_assets = {{.id = asset_id_t{200}, .stream = {44'100, 2, 441'000, true}}};
    for (uint64_t index = 0; index < 3; ++index) {
        song.resources.push_back({.id = resource_id_t{100 + index},
            .name = "Shared source " + std::to_string(index), .asset = asset_id_t{200},
            .roles = {resource_role_t::sample, resource_role_t::bgm},
            .sound_kind = index < 2 ? sound_kind_t::stem : sound_kind_t::keysound});
        chart.sound_channels.push_back({.id = object_id_t{300 + index},
            .name = song.resources.back().name,
            .source_clip = {clip_id_t{300 + index}, asset_id_t{200}, 0, std::nullopt},
            .events = {{.id = object_id_t{400 + index}, .position = {measure_id_t{1}, index * 120},
                        .playable = false}}});
        chart.bgm_source_lanes.push_back({.id = object_id_t{600 + index},
            .name = song.resources.back().name, .source_order = static_cast<size_t>(index),
            .stem_channel = index < 2 ? std::optional{object_id_t{300 + index}} : std::nullopt});
        chart.measures.front().bgm_events.push_back({.id = object_id_t{500 + index},
            .position = {measure_id_t{1}, index * 120}, .source_lane = object_id_t{600 + index},
            .sound = resource_id_t{100 + index}, .sound_channel_event = object_id_t{400 + index}});
    }
    return song;
}
} // namespace
