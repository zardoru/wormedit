// Compose geometry and paired grid ownership across frame arenas.
#include "frame_timing_fixtures.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void check_static_source_has_no_frame_overlays(const wormedit::ui::compose_view_t& source) {
    expect_compose_test(source.status_text.empty() && source.selected_note_label.empty() && source.hover_tooltip.empty());
    expect_compose_test(source.paste_ghosts.empty() && source.paste_diagnostic.empty());
    expect_compose_test(source.resources.empty());
    expect_compose_test(source.output_groups.empty());
    expect_compose_test(source.branch_rows.empty());
    expect_compose_test(source.branch_choices.empty());
    expect_compose_test(source.branch_diagnostic_count() == 0);
    for (const auto& diagnostic : source.branch_diagnostics) expect_compose_test(diagnostic.empty());
}

void check_static_source_has_no_frame_markers(const wormedit::ui::compose_view_t& source) {
    expect_compose_test(source.beat_bookmarks.empty());
    expect_compose_test(source.beat_sections.empty());
    expect_compose_test(source.audio_bookmarks.empty());
    expect_compose_test(source.audio_sections.empty());
    expect_compose_test(source.semantic_layers.empty() && source.semantic_layers.size() == 0);
    expect_compose_test(source.staged_bmson_marker_playable_lanes.empty() && source.staged_bmson_marker_restarts.empty() &&
        source.staged_bmson_marker_restarts.size() == 0);
    expect_compose_test(source.lane_key_bindings.empty() && source.lane_key_bindings.size() == 0);
}

void check_static_source_has_no_frame_previews(const wormedit::ui::compose_view_t& source) {
    expect_compose_test(source.proposed_grid_lines.empty());
    expect_compose_test(source.timing_preview_rects.empty());
    expect_compose_test(source.timing_preview_markers.empty());
    expect_compose_test(source.waveform_rows.empty());
    expect_compose_test(source.channel_waveform_rows.empty());
    expect_compose_test(source.waveform_overview_rows.empty());
    expect_compose_test(source.bmson_marker_drafts.empty());
    expect_compose_test(!source.bmson_marker_move_preview && !source.cursor && !source.static_scene);
}

void project_static_source_arena_frame(compose_test_fixture_t& fixture) {
    workspace_view_t model;
    model.active_tab = workspace_tab_t::compose;
    model.tab_view = workspace_compose_view_t{.chart = chart_id_t{1}};
    fixture.flattened =
        frontend_workspace_view_t::from(model, fixture.history.document, "ready", {}, false);
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    expect_compose_test(fixture.flattened.view.compose.scene_available);
    expect_compose_test(fixture.flattened.view.compose.resources.size() == 1);
    expect_compose_test(fixture.flattened.view.compose.output_groups.size() == 1);
    expect_compose_test(fixture.flattened.view.compose.static_scene != nullptr);
}

void check_static_source_owned_geometry(const frontend_workspace_view_t& storage) {
    const auto& source = storage.view.compose;
    check_static_source_has_no_frame_overlays(source);
    check_static_source_has_no_frame_markers(source);
    check_static_source_has_no_frame_previews(source);
}

void check_owned_hitsound_note(const wormedit::ui::compose_note_view_t& note) {
    expect_compose_test(note.hitsound != nullptr);
    expect_compose_test(note.hitsound->resolved_normal_set == osu_sample_set_t::soft);
    expect_compose_test(note.hitsound->resolved_addition_set == osu_sample_set_t::drum);
    expect_compose_test(note.hitsound->additions == 10);
    expect_compose_test(note.hitsound->resolved_custom_index == 3);
    expect_compose_test(note.hitsound->resolved_volume == 75);
    expect_compose_test(note.hitsound->filename == "custom-retained-hit.wav");
}
} // namespace

TEST_CASE("Persistent Compose static source owns geometry across frame arena reset",
          "[sys][frame-arena][static-source-lifetime]") {
    auto song = compose_test_song();
    song.audio_assets.push_back({.id = asset_id_t{9}, .source_path = "sample.wav"});
    song.resources.push_back({.id = resource_id_t{9}, .name = "Long independently owned sound label",
        .asset = asset_id_t{9}, .roles = {resource_role_t::sample}});
    song.output_groups.push_back({.name = "Disposable frame output group",
        .charts = {chart_id_t{1}}, .relative_output_path = "exports/chart.bms"});
    song.charts.front().measures.front().notes.front().sound = resource_id_t{9};
    song.charts.front().measures.front().notes.front().osu_hitsample = osu_hitsample_t{
        .whistle = true, .clap = true, .normal_set = osu_sample_set_t::soft,
        .addition_set = osu_sample_set_t::drum, .custom_index = 3, .volume = 75,
        .filename = "custom-retained-hit.wav"};
    compose_test_fixture_t fixture(std::move(song));
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_static_source_arena_frame(fixture);
    const auto stored = fixture.cache.scene.static_source.value().storage;
    const auto generation = fixture.cache.scene.static_source.build_count();
    check_static_source_owned_geometry(*stored);
    expect_compose_test(!stored->view.compose.note_endpoints.empty());
    const auto before = stored->view.compose.note_endpoints[0];
    check_owned_hitsound_note(before);
    const std::string label{before.sound_label};
    const std::string detail{before.sound_detail};
    expect_compose_test(!stored->view.compose.timing_markers.empty());
    const auto timing = stored->view.compose.timing_markers.front();
    const std::string timing_label{timing.short_label};
    const std::string timing_detail{timing.detail};
    fixture.flattened = {};
    arena.reset();
    const auto overwritten = arena.allocate(4096, alignof(char));
    expect_compose_test(static_cast<bool>(overwritten));
    std::memset(*overwritten, 0xA5, 4096);
    expect_compose_test(stored->view.compose.note_endpoints[0].note_id.value == before.note_id.value);
    expect_compose_test(stored->view.compose.note_endpoints[0].bounds.top == before.bounds.top);
    expect_compose_test(stored->view.compose.note_endpoints[0].sound_label == label);
    expect_compose_test(stored->view.compose.note_endpoints[0].sound_detail == detail);
    check_owned_hitsound_note(stored->view.compose.note_endpoints[0]);
    expect_compose_test(stored->view.compose.timing_markers.front().short_label == timing_label);
    expect_compose_test(stored->view.compose.timing_markers.front().detail == timing_detail);
    fixture.interaction->viewport_scroll_pixels = 0.5;
    project_static_source_arena_frame(fixture);
    expect_compose_test(fixture.cache.scene.static_source.value().storage == stored);
    expect_compose_test(fixture.cache.scene.static_source.build_count() == generation);
    check_static_source_owned_geometry(*fixture.cache.scene.static_source.value().storage);
    ++fixture.history.document.revision;
    project_static_source_arena_frame(fixture);
    expect_compose_test(fixture.cache.scene.static_source.value().storage != stored);
    expect_compose_test(fixture.cache.scene.static_source.build_count() > generation);
    check_static_source_owned_geometry(*fixture.cache.scene.static_source.value().storage);
}

namespace {
auto geometry_arena_song() -> song_document_t {
    auto song = compose_test_song();
    auto& hold = song.charts.front().measures.front().notes[1];
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{measure_id_t{1}, 360};
    return song;
}

template<class Rows>
void check_geometry_between(const Rows& rows, std::uintptr_t begin, std::uintptr_t end) {
    if (rows.empty()) return;
    const auto address = reinterpret_cast<std::uintptr_t>(rows.data());
    expect_compose_test(address >= begin && address + rows.size() * sizeof(rows[0]) <= end);
}

template<class Rows>
void check_owned_geometry_storage(const Rows& rows, std::uintptr_t begin, std::uintptr_t end) {
    if (rows.empty()) return;
    const auto address = reinterpret_cast<std::uintptr_t>(rows.data());
    expect_compose_test(address < begin || address >= end);
}

void compare_geometry_note(const wormedit::ui::compose_note_view_t& a, const wormedit::ui::compose_note_view_t& b) {
    compare_geometry_rect(a.bounds, b.bounds);
    expect_compose_test(a.note_id.value == b.note_id.value && a.lane_id.value == b.lane_id.value && a.display_lane_number == b.display_lane_number);
    expect_compose_test(a.glyph == b.glyph && a.kind == b.kind && a.snap_class == b.snap_class && a.tail == b.tail);
    expect_compose_test(a.selected == b.selected && a.proposal_origin == b.proposal_origin && a.proposal_ghost == b.proposal_ghost);
    expect_compose_test(a.semantic_color_visible == b.semantic_color_visible && a.semantic_red == b.semantic_red &&
        a.semantic_green == b.semantic_green && a.semantic_blue == b.semantic_blue && a.semantic_alpha == b.semantic_alpha);
    expect_compose_test(a.paint_preview == b.paint_preview && a.paint_erase_preview == b.paint_erase_preview && a.paint_invalid_preview == b.paint_invalid_preview);
    expect_compose_test(a.sound_label == b.sound_label);
    expect_compose_test(a.sound_detail == b.sound_detail);
}

void compare_geometry_hold(const wormedit::ui::compose_hold_view_t& a, const wormedit::ui::compose_hold_view_t& b) {
    compare_geometry_rect(a.bounds, b.bounds);
    expect_compose_test(a.note_id.value == b.note_id.value && a.lane_id.value == b.lane_id.value && a.display_lane_number == b.display_lane_number);
    expect_compose_test(a.selected == b.selected && a.proposal_origin == b.proposal_origin && a.proposal_ghost == b.proposal_ghost);
    expect_compose_test(a.semantic_color_visible == b.semantic_color_visible && a.semantic_red == b.semantic_red &&
        a.semantic_green == b.semantic_green && a.semantic_blue == b.semantic_blue && a.semantic_alpha == b.semantic_alpha);
}

void compare_geometry_frames(const wormedit::ui::compose_view_t& a, const wormedit::ui::compose_view_t& b) {
    expect_compose_test(a.bands.size() == b.bands.size() && a.lanes.size() == b.lanes.size() && a.differences.size() == b.differences.size());
    expect_compose_test(a.note_endpoints.size() == b.note_endpoints.size() && a.hold_bodies.size() == b.hold_bodies.size());
    for (std::size_t i = 0; i < a.bands.size(); ++i) {
        compare_geometry_rect(a.bands[i].bounds, b.bands[i].bounds);
        expect_compose_test(a.bands[i].split_stage == b.bands[i].split_stage);
    }
    for (std::size_t i = 0; i < a.lanes.size(); ++i) {
        compare_geometry_rect(a.lanes[i].bounds, b.lanes[i].bounds);
        expect_compose_test(a.lanes[i].lane_id.value == b.lanes[i].lane_id.value && a.lanes[i].display_order == b.lanes[i].display_order);
        expect_compose_test(a.lanes[i].stage == b.lanes[i].stage && a.lanes[i].role == b.lanes[i].role && a.lanes[i].glyph == b.lanes[i].glyph);
    }
    for (std::size_t i = 0; i < a.note_endpoints.size(); ++i) compare_geometry_note(a.note_endpoints[i], b.note_endpoints[i]);
    for (std::size_t i = 0; i < a.hold_bodies.size(); ++i) compare_geometry_hold(a.hold_bodies[i], b.hold_bodies[i]);
}

void check_fresh_reused_geometry(std::uint32_t direction, format_family_t family) {
    auto song = geometry_arena_song();
    song.charts.front().source_family = family;
    compose_test_fixture_t reused(song), fresh(song);
    reused.application.preferences.editor_preferences.scroll_directions.fill(direction);
    fresh.application.preferences.editor_preferences.scroll_directions.fill(direction);
    wormedit::frame_arena_t reused_arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    wormedit::frame_arena_t fresh_arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(reused);
    project_geometry_arena(fresh);
    const auto source = reused.cache.scene.static_source.value().storage;
    expect_compose_test(source != nullptr);
    for (const bool legend : {false, true}) {
        reused.application.preferences.editor_preferences.lane_key_legend_visible = legend;
        fresh.application.preferences.editor_preferences.lane_key_legend_visible = legend;
        reused.interaction->viewport_scroll_pixels = 0.5;
        fresh.interaction->viewport_scroll_pixels = 0.5;
        reused_arena.reset();
        fresh_arena.reset();
        fresh.cache = {};
        project_geometry_arena(reused);
        project_geometry_arena(fresh);
        expect_compose_test(reused.flattened.view.compose.scene_available && fresh.flattened.view.compose.scene_available);
        compare_geometry_frames(reused.flattened.view.compose, fresh.flattened.view.compose);
        reused_arena.reset();
        project_geometry_arena(reused);
        expect_compose_test(reused.cache.scene.static_projection_reused_frames > 0);
        compare_geometry_frames(reused.flattened.view.compose, fresh.flattened.view.compose);
    }
}
} // namespace

TEST_CASE("Compose geometry owns storage outside the supplied arena and accounts static chunks",
          "[sys][frame-arena][compose-geometry]") {
    frontend_workspace_view_t untouched;
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto begin = arena.allocate(1, alignof(char));
    project_geometry_arena(fixture);
    const auto end = arena.allocate(1, alignof(char));
    expect_compose_test(static_cast<bool>(begin) && static_cast<bool>(end));
    expect_compose_test(fixture.flattened.view.compose.scene_available && !fixture.flattened.view.compose.truncated);
    const auto first = reinterpret_cast<std::uintptr_t>(*begin);
    const auto last = reinterpret_cast<std::uintptr_t>(*end);
    check_owned_geometry_storage(fixture.flattened.view.compose.bands, first, last);
    check_owned_geometry_storage(fixture.flattened.view.compose.lanes, first, last);
    check_owned_geometry_storage(fixture.flattened.view.compose.hold_bodies, first, last);
    check_owned_geometry_storage(fixture.flattened.view.compose.note_endpoints, first, last);
    expect_compose_test(!fixture.flattened.view.compose.hold_bodies.empty() && !fixture.flattened.view.compose.note_endpoints.empty());
    const auto source = fixture.cache.scene.static_source.value().storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(source->view.compose.note_endpoints.data() != fixture.flattened.view.compose.note_endpoints.data());
    expect_compose_test(fixture.cache.scene.static_source.value().accounted_bytes >= sizeof(*source));
    expect_compose_test(fixture.cache.scene.static_source.value().accounted_bytes <= 16 * 1024 * 1024);
}

TEST_CASE("Compose geometry arena fresh and reused primitives agree in both directions and label modes",
          "[sys][frame-arena][compose-geometry]") {
    for (const auto direction : {1U, 2U})
        for (const auto family : {format_family_t::native_project, format_family_t::bms})
            check_fresh_reused_geometry(direction, family);
}

TEST_CASE("Compose geometry owns complete lists when the frame arena is exhausted",
          "[sys][frame-arena][compose-geometry]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    const auto revision = fixture.history.document.revision;
    for (const auto bytes : {8U, 128U}) {
        wormedit::frame_arena_t arena({.chunk_bytes = bytes, .maximum_bytes = bytes});
        fixture.cache = {};
        project_geometry_arena(fixture);
        const auto& view = fixture.flattened.view.compose;
        expect_compose_test(!view.bands.empty() && !view.lanes.empty());
        expect_compose_test(!view.note_endpoints.empty() && !view.hold_bodies.empty());
        expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
    }
}

TEST_CASE("Warmed Compose geometry arena stays bounded through 1000 actual cached projections",
          "[sys][frame-arena][compose-geometry]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(fixture);
    arena.reset();
    project_geometry_arena(fixture);
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    const auto source = fixture.cache.scene.static_source.value().storage;
    arena.reset();
    allocations = 0;
    count_allocations = true;
    project_geometry_arena(fixture);
    count_allocations = false;
    const auto remaining_allocations = allocations;
    bool stable = true;
    bool consistent = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        allocations = 0;
        count_allocations = true;
        project_geometry_arena(fixture);
        count_allocations = false;
        consistent = consistent && allocations == remaining_allocations;
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed &&
            fixture.cache.scene.static_source.value().storage == source &&
            !fixture.flattened.view.compose.truncated;
    }
    INFO("Remaining actual Compose projection allocations: " << remaining_allocations);
    expect_compose_test(stable);
    expect_compose_test(consistent);
}

TEST_CASE("Compose geometry scene budget rejects dense excess without exceeding published caps",
          "[sys][frame-arena][compose-geometry]") {
    compose_test_fixture_t fixture(dense_song());
    const auto revision = fixture.history.document.revision;
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024});
    project_geometry_arena(fixture, 2000000);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_compose_test(!fixture.flattened.projection_feedback.empty());
    expect_compose_test(!fixture.flattened.view.compose.static_scene);
    expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
}

TEST_CASE("Compose projection failure posts once until repaired or revision changes", "[sys][notification][compose-geometry]") {
    notification_center_t center;
    notification_sink_t sink{center};
    compose_test_fixture_t fixture(dense_song());
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024});
    projection_notification_feedback_t feedback;
    auto identity = projection_identity_t{fixture.history.document.id, fixture.history.document.revision, chart_id_t{1}, 1};
    project_geometry_arena(fixture, 2000000);
    expect_compose_test(!fixture.flattened.projection_feedback.empty());
    feedback.publish(identity, fixture.flattened.projection_feedback, sink);
    center.drain_inbox(notification_time_t{0});
    const auto first = center.snapshot(notification_capacity);
    expect_compose_test(first.size() == 1);
    expect_compose_test(first[0].severity == notification_severity_t::error);
    feedback.publish(identity, fixture.flattened.projection_feedback, sink);
    center.drain_inbox(notification_time_t{3000});
    expect_compose_test(center.snapshot(notification_capacity)[0].count == 1);
    feedback.publish(identity, {}, sink);
    feedback.publish(identity, fixture.flattened.projection_feedback, sink);
    center.drain_inbox(notification_time_t{6000});
    expect_compose_test(center.snapshot(notification_capacity)[0].count == 2);
    ++identity.revision;
    feedback.publish(identity, fixture.flattened.projection_feedback, sink);
    center.drain_inbox(notification_time_t{9000});
    expect_compose_test(center.snapshot(notification_capacity)[0].count == 3);
    expect_compose_test(fixture.history.undo.empty());
}

namespace {
void check_grid_pairs(const frontend_workspace_view_t& frame) {
    const auto& grid = frame.compose.grid;
    const auto& view = frame.view.compose;
    expect_compose_test(view.grid_lines.size() == grid.row.size());
    expect_compose_test(view.grid_lines.size() <= compose_scene_grid_work_limit);
    for (std::size_t index = 0; index < view.grid_lines.size(); ++index) {
        const auto& row = grid.row[index];
        expect_compose_test(view.grid_lines[index].chart_row ==
            (row.fractional_rows.numerator == 0 ? row.whole_rows : 0));
    }
}

void compare_grid_frames(const frontend_workspace_view_t& actual, const frontend_workspace_view_t& expected) {
    compare_scalar_grid(actual.view.compose, expected.view.compose);
    expect_compose_test(actual.compose.grid.row.size() == expected.compose.grid.row.size());
    for (std::size_t index = 0; index < actual.compose.grid.row.size(); ++index)
        expect_compose_test(actual.compose.grid.row[index] == expected.compose.grid.row[index]);
    check_grid_pairs(actual);
}

void check_grid_cache_parity(std::uint32_t direction, format_family_t family) {
    auto song = geometry_arena_song();
    song.charts.front().source_family = family;
    compose_test_fixture_t cached(song), fresh(song);
    cached.application.preferences.editor_preferences.scroll_directions.fill(direction);
    fresh.application.preferences.editor_preferences.scroll_directions.fill(direction);
    wormedit::frame_arena_t arena, reference;
    project_geometry_arena(cached);
    project_geometry_arena(fresh);
    const auto source = cached.cache.scene.static_source.value().storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(cached.flattened.view.compose.bms_measure_labels == (family == format_family_t::bms));
    if (family == format_family_t::bms)
        expect_compose_test(std::any_of(cached.flattened.view.compose.grid_lines.begin(),
            cached.flattened.view.compose.grid_lines.end(), [](const auto& line) { return line.measure_number > 1; }));
    for (const auto scroll : {0.5, 20.0, 0.5}) {
        arena.reset(); reference.reset();
        cached.interaction->viewport_scroll_pixels = scroll;
        fresh.interaction->viewport_scroll_pixels = scroll;
        fresh.cache = {};
        project_geometry_arena(cached);
        project_geometry_arena(fresh);
        compare_grid_frames(cached.flattened, fresh.flattened);
        expect_compose_test(cached.cache.scene.static_source.value().storage == source);
    }
    expect_compose_test(cached.cache.scene.static_projection_reused_frames > 0);
    ++cached.history.document.revision;
    ++fresh.history.document.revision;
    arena.reset(); reference.reset(); fresh.cache = {};
    project_geometry_arena(cached);
    project_geometry_arena(fresh);
    expect_compose_test(cached.cache.scene.static_source.value().storage != source);
    compare_grid_frames(cached.flattened, fresh.flattened);
}
} // namespace

TEST_CASE("Compose paired grid rows own independent retained storage", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena;
    const auto begin = arena.allocate(1, alignof(char));
    project_geometry_arena(fixture);
    const auto end = arena.allocate(1, alignof(char));
    expect_compose_test(static_cast<bool>(begin) && static_cast<bool>(end));
    expect_compose_test(!fixture.flattened.view.compose.grid_lines.empty());
    check_grid_pairs(fixture.flattened);
    check_owned_geometry_storage(fixture.flattened.view.compose.grid_lines,
        reinterpret_cast<std::uintptr_t>(*begin), reinterpret_cast<std::uintptr_t>(*end));
    check_owned_geometry_storage(fixture.flattened.compose.grid.row,
        reinterpret_cast<std::uintptr_t>(*begin), reinterpret_cast<std::uintptr_t>(*end));
    const auto source = fixture.cache.scene.static_source.value().storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(source->view.compose.grid_lines.data() != fixture.flattened.view.compose.grid_lines.data());
    const auto line = source->view.compose.grid_lines[0];
    const auto row = source->compose.grid.row[0];
    auto moved = std::move(fixture.flattened);
    check_grid_pairs(moved);
    fixture.flattened = {};
    arena.reset();
    const auto overwrite = arena.allocate(4096, alignof(char));
    expect_compose_test(static_cast<bool>(overwrite));
    std::memset(*overwrite, 0xA5, 4096);
    expect_compose_test(source->view.compose.grid_lines[0].y_pixels == line.y_pixels);
    expect_compose_test(source->compose.grid.row[0] == row);
    expect_compose_test(fixture.cache.scene.static_source.value().accounted_bytes >=
        sizeof(*source));
}

TEST_CASE("Compose paired grid cached filters preserve fresh BMS and native labels", "[sys][frame-arena][compose-grid]") {
    for (const auto direction : {1U, 2U})
        for (const auto family : {format_family_t::native_project, format_family_t::bms})
            check_grid_cache_parity(direction, family);
}

TEST_CASE("Compose grid storage is independent of frame arena capacity", "[sys][frame-arena][compose-grid]") {
    for (std::size_t limit = 128; limit <= 2048; limit += 32) {
        compose_test_fixture_t fixture(geometry_arena_song());
        wormedit::frame_arena_t arena({.chunk_bytes = limit, .maximum_bytes = limit});
        project_geometry_arena(fixture);
        check_grid_pairs(fixture.flattened);
        expect_compose_test(fixture.history.document.revision == 1 && fixture.history.undo.empty());
        expect_compose_test(!fixture.flattened.view.compose.grid_lines.empty());
        expect_compose_test(!fixture.flattened.view.compose.lanes.empty());
    }
}

TEST_CASE("Warmed paired grid rows remain stable through 1000 actual arena resets", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(fixture);
    arena.reset(); project_geometry_arena(fixture);
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    const auto source = fixture.cache.scene.static_source.value().storage;
    const auto count = fixture.flattened.view.compose.grid_lines.size();
    arena.reset(); allocations = 0; count_allocations = true;
    project_geometry_arena(fixture);
    count_allocations = false;
    const auto remaining = allocations;
    bool stable = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset(); allocations = 0; count_allocations = true;
        project_geometry_arena(fixture);
        count_allocations = false;
        stable = stable && allocations == remaining && arena.used_bytes() == used &&
            arena.committed_bytes() == committed && fixture.cache.scene.static_source.value().storage == source &&
            fixture.flattened.view.compose.grid_lines.size() == count &&
            fixture.flattened.compose.grid.row.size() == count && !fixture.flattened.view.compose.truncated;
    }
    INFO("remaining projection allocations=" << remaining << ", used=" << used << ", committed=" << committed);
    expect_compose_test(stable);
    check_grid_pairs(fixture.flattened);
}

TEST_CASE("Compose paired grid limits remain bounded when scene budget rejects excess", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(dense_song());
    wormedit::frame_arena_t arena;
    project_geometry_arena(fixture, 2000000);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_compose_test(fixture.flattened.view.compose.grid_lines.empty() && fixture.flattened.compose.grid.row.empty());
    check_grid_pairs(fixture.flattened);
}
