// Workspace timing cache and dense inspector location reuse.
#include "frame_timing_fixtures.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct workspace_timing_snapshot_t {
    std::uint64_t id{}, source{}, storage{}, row{};
    double y{}, left{}, right{};
    wormedit::timing_lane_kind_t lane{};
    std::string label, detail;
    bool fixed{}, selected{};
    auto operator==(const workspace_timing_snapshot_t&) const -> bool = default;
};

auto workspace_timing_snapshot(const compose_test_fixture_t& fixture)
    -> std::vector<workspace_timing_snapshot_t> {
    std::vector<workspace_timing_snapshot_t> result;
    for (const auto& marker : fixture.flattened.view.compose.timing_markers)
        result.push_back({marker.object_id.value, marker.source_order, marker.storage_order,
            marker.chart_row, marker.marker_y_pixels, marker.marker_left_fraction,
            marker.marker_right_fraction, marker.lane, std::string{marker.short_label}, std::string{marker.detail},
            marker.fixed_initial, marker.selected});
    return result;
}

auto workspace_timing_cache_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.bpms.clear();
    for (std::uint64_t index = 0; index < 70; ++index)
        chart.bpms.push_back({.id = object_id_t{10000 + index},
            .position = {measure_id_t{1}, index * 12},
            .beats_per_minute = {120 + static_cast<std::int64_t>(index), 1}});
    song.audio_assets = {{.id = asset_id_t{1}}};
    for (std::uint64_t id = 1; id <= 600; ++id)
        song.resources.push_back({.id = resource_id_t{id}, .name = "sound" + std::to_string(id),
            .asset = asset_id_t{1}, .roles = {resource_role_t::sample}});
    return song;
}

auto workspace_timing_cache_tracks_frame_state() -> bool {
    const auto song = workspace_timing_cache_song();
    compose_test_fixture_t cached(song), reference(song);
    reference.cache.timing_payload_cache_enabled = false;
    cached.project(); reference.project();
    const auto original = workspace_timing_snapshot(cached);
    for (double scroll : {0.0, 150.0, 0.0}) {
        for (auto* fixture : {&cached, &reference}) {
            fixture->interaction->viewport_scroll_pixels = scroll;
            fixture->interaction->selected_objects = {
                {editor_selection_object_kind_t::bpm_event, object_id_t{10005}}};
            fixture->project();
        }
        if (workspace_timing_snapshot(cached) != workspace_timing_snapshot(reference))
            return false;
    }
    const auto selected = std::ranges::find_if(cached.flattened.view.compose.timing_markers,
        [](const auto& marker) { return marker.object_id == object_id_t{10005}; });
    if (selected == cached.flattened.view.compose.timing_markers.end() || !selected->selected ||
        original == workspace_timing_snapshot(cached)) return false;
    for (auto* fixture : {&cached, &reference}) {
        fixture->interaction->selected_objects.clear();
        fixture->project();
    }
    return workspace_timing_snapshot(cached) == workspace_timing_snapshot(reference) &&
        std::ranges::none_of(cached.flattened.view.compose.timing_markers,
                            [](const auto& marker) { return marker.selected; }) &&
        cached.cache.timing_payload_cache.build_count() == 1 &&
        reference.cache.timing_payload_cache.build_count() == 0;
}

auto workspace_timing_cache_invalidates_revision() -> bool {
    compose_test_fixture_t cached(workspace_timing_cache_song());
    cached.project();
    auto& document = cached.history.document;
    document.charts.front().bpms[5].beats_per_minute = {999, 1};
    ++document.revision;
    cached.binding.value->document_revision = document.revision;
    cached.project();
    compose_test_fixture_t reference(document);
    reference.cache.timing_payload_cache_enabled = false;
    reference.project();
    return cached.cache.timing_payload_cache.build_count() == 2 &&
        workspace_timing_snapshot(cached) == workspace_timing_snapshot(reference);
}

auto workspace_timing_cache_rejects_reused_ids() -> bool {
    auto original = workspace_timing_cache_song();
    original.charts.front().measures.push_back({.id = measure_id_t{2}});
    original.charts.front().bpms.back().position = {measure_id_t{2}, 0};
    compose_test_fixture_t previous(original);
    previous.project();
    auto replacement = original;
    auto& chart = replacement.charts.front();
    chart.measures.front().id = measure_id_t{10};
    chart.measures.back().id = measure_id_t{20};
    chart.measures.front().definition.length = {2, 1};
    chart.measures.front().definition.meter = meter_t{8, 4};
    for (auto& note : chart.measures.front().notes) note.position.measure = measure_id_t{10};
    chart.lane_layouts.front().position.measure = measure_id_t{10};
    for (auto& bpm : chart.bpms) bpm.position.measure = measure_id_t{10};
    chart.bpms.back().position.measure = measure_id_t{20};
    chart.bpms.back().beats_per_minute = {777, 1};
    compose_test_fixture_t cached(replacement), reference(replacement);
    cached.cache = std::move(previous.cache);
    reference.cache.timing_payload_cache_enabled = false;
    cached.project(); reference.project();
    const auto& payloads = cached.cache.timing_payload_cache.value();
    const auto changed = std::ranges::find(payloads, std::uint64_t{10069},
                                          &compose_timing_marker_payload_t::id);
    return changed != payloads.end() && changed->row == 1920 &&
        cached.cache.timing_payload_cache.build_count() == 2 &&
        workspace_timing_snapshot(cached) == workspace_timing_snapshot(reference);
}
} // namespace

TEST_CASE("Workspace timing cache matches reference with current scrolling and selection",
          "[sys][compose][workspace-cache]") {
    expect_compose_test(workspace_timing_cache_tracks_frame_state());
}

TEST_CASE("Workspace timing payload cache invalidates document revisions",
          "[sys][compose][workspace-cache]") {
    expect_compose_test(workspace_timing_cache_invalidates_revision());
}

TEST_CASE("Workspace timing cache rebuilds measure mapping for reused document IDs",
          "[sys][compose][workspace-cache]") {
    expect_compose_test(workspace_timing_cache_rejects_reused_ids());
}

namespace {
auto t475_inspector_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes = {{.id = object_id_t{500},
        .position = {measure_id_t{1}, 0}, .lane = lane_id_t{1}, .label = "first"}};
    chart.measures.push_back({.id = measure_id_t{2},
        .notes = {{.id = object_id_t{600}, .position = {measure_id_t{2}, 240},
                   .lane = lane_id_t{1}, .label = "late"}},
        .bgm_events = {{.id = object_id_t{700}, .position = {measure_id_t{2}, 480},
                       .source_lane = object_id_t{800}}}});
    chart.bgm_source_lanes = {{.id = object_id_t{800}, .name = "BGM"}};
    song.audio_assets = {{.id = asset_id_t{900}, .source_path = "retained.wav"}};
    song.resources = {{.id = resource_id_t{901}, .name = "Retained sound",
        .asset = asset_id_t{900}, .roles = {resource_role_t::sample, resource_role_t::bgm}}};
    return song;
}

void t475_compare_fresh(compose_test_fixture_t& retained) {
    compose_test_fixture_t fresh{retained.history.document};
    fresh.binding.value->selected_chart = retained.binding.value->selected_chart;
    fresh.interaction = retained.interaction;
    fresh.application = retained.application;
    t475_project(fresh); // Establish the source before copying transient pointer state.
    fresh.interaction = retained.interaction;
    fresh.cache.frame.paint_pointer = retained.cache.frame.paint_pointer;
    t475_project(fresh);
    const auto& a = retained.flattened.view.compose;
    const auto& b = fresh.flattened.view.compose;
    t475_check(a.selected_note_label_note_id.value == b.selected_note_label_note_id.value);
    t475_check(std::string_view(a.selected_note_label) ==
          std::string_view(b.selected_note_label));
    t475_check(a.selected_bgm_event_id.value == b.selected_bgm_event_id.value);
    t475_check(a.selected_assigned_resource_id.value == b.selected_assigned_resource_id.value);
    t475_check(a.selected_bmson_note_event_id.value == b.selected_bmson_note_event_id.value);
    t475_check(a.selected_bmson_source_lane_id.value == b.selected_bmson_source_lane_id.value);
    std::string labels;
    for (std::size_t i = 0; i < std::min(a.note_endpoints.size(), b.note_endpoints.size()); ++i) {
        labels += std::string{"note "} + std::to_string(a.note_endpoints[i].note_id.value) +
            " actual=" + std::string{a.note_endpoints[i].sound_label} + " reference=" + std::string{b.note_endpoints[i].sound_label} +
            " detail actual=" + std::string{a.note_endpoints[i].sound_detail} +
            " reference=" + std::string{b.note_endpoints[i].sound_detail} + "; ";
    }
    INFO(labels);
    std::string sources = "doc revision=" + std::to_string(retained.history.document.revision) +
        " scene revision=" + std::to_string(retained.cache.scene.index.value()->document_revision);
    for (const auto& note : retained.cache.scene.index.value()->notes)
        sources += " id=" + std::to_string(note.note.value) + " sound=" +
            std::to_string(note.sound.value_or(resource_id_t{}).value);
    if (retained.cache.scene.static_source.value().storage) {
        for (const auto& [id, label] : retained.cache.scene.static_source.value().storage->compose.sound_labels)
            sources += " static label " + std::to_string(id) + "=" + label.first;
    }
    INFO(sources);
    compare_scalar_notes(a, b);
    compare_scalar_holds_bgm(a, b);
    compare_scalar_grid(a, b);
    t475_check(a.paint_ghost_visible == b.paint_ghost_visible);
    if (a.paint_ghost_visible) {
        compare_scalar_bounds(a.paint_ghost.bounds, b.paint_ghost.bounds);
        t475_check(a.paint_ghost.paint_erase_preview == b.paint_ghost.paint_erase_preview);
    }
}

void t475_select_note(compose_test_fixture_t& fixture, object_id_t id) {
    fixture.interaction->selected_notes = {id};
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::note, id}};
    t475_project(fixture);
}

void t475_project_at_row(compose_test_fixture_t& fixture, std::uint64_t row, bool downscroll) {
    fixture.application.preferences.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    t475_project(fixture);
    fixture.interaction->viewport_scroll_pixels = -20; // Keep row zero inside both hit-test directions.
    t475_project(fixture);
    const auto& view = fixture.flattened.view.compose;
    t475_check(view.downscroll == downscroll);
    const auto lane = view.lanes[0];
    const auto source_y = row * view.pixels_per_standard_measure / 960.0 -
                          fixture.interaction->viewport_scroll_pixels;
    fixture.cache.frame.paint_pointer = std::pair{(lane.bounds.left + lane.bounds.right) / 2,
        downscroll ? view.viewport_height_pixels - source_y : source_y};
    t475_project(fixture);
    INFO("paint target row=" << row << " downscroll=" << downscroll
        << " x=" << fixture.cache.frame.paint_pointer->first << " y=" << fixture.cache.frame.paint_pointer->second
        << " lane bounds=" << lane.bounds.top << "," << lane.bounds.bottom);
    t475_require(fixture.flattened.view.compose.paint_ghost_visible);
}
} // namespace

TEST_CASE("Dense inspector locations reuse exact source values through selection and viewport changes",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t fixture{t475_inspector_song()};
    t475_project(fixture);
    for (const auto id : {object_id_t{500}, object_id_t{600}}) {
        t475_select_note(fixture, id);
        const auto scanned = fixture.cache.scene.selected_source_scanned;
        fixture.interaction->pixels_per_standard_measure = 250;
        fixture.interaction->viewport_scroll_pixels = 15;
        fixture.interaction->cursor = {measure_id_t{2}, 120};
        t475_project(fixture);
        t475_check(fixture.cache.scene.selected_source_scanned == scanned);
        t475_check(fixture.cache.scene.selected_source_reused > 0);
        t475_compare_fresh(fixture);
    }
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{700}}};
    t475_project(fixture);
    const auto scanned = fixture.cache.scene.selected_source_scanned;
    t475_project(fixture);
    t475_check(fixture.cache.scene.selected_source_scanned == scanned);
    t475_check(fixture.flattened.view.compose.selected_bgm_event_id.value == 700);
    t475_compare_fresh(fixture);
}

TEST_CASE("Dense inspector locations invalidate after label edit delete and undo",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t fixture{t475_inspector_song()};
    t475_project(fixture);
    t475_select_note(fixture, object_id_t{600});
    t475_require(apply_note_label_edit(
        fixture.history, {.chart = chart_id_t{1}, .notes = {object_id_t{600}}, .label = "edited"}));
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "edited");
    t475_compare_fresh(fixture);
    t475_require(dispatch_editor_action(fixture.history, *fixture.interaction,
        delete_editor_selection_action_t{}));
    t475_select_note(fixture, object_id_t{600});
    t475_check(fixture.flattened.view.compose.selected_note_label_note_id.value == 0);
    t475_require(undo_document_command(fixture.history));
    t475_select_note(fixture, object_id_t{600});
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "edited");
    t475_compare_fresh(fixture);
    t475_require(undo_document_command(fixture.history));
    t475_select_note(fixture, object_id_t{600});
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "late");
    t475_compare_fresh(fixture);
}

TEST_CASE("Dense inspector geometry belongs to the actual document and chart source",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t prior{t475_inspector_song()};
    t475_project(prior);
    t475_select_note(prior, object_id_t{600});
    auto replacement_song = t475_inspector_song();
    replacement_song.charts.front().measures.back().notes.front().label = "replacement";
    replacement_song.charts.front().measures.front().definition = {{2, 1}, meter_t{8, 4}};
    compose_test_fixture_t replacement{std::move(replacement_song)};
    replacement.cache = std::move(prior.cache);
    replacement.interaction = prior.interaction;
    t475_project(replacement);
    t475_check(std::string_view(replacement.flattened.view.compose.selected_note_label) == "replacement");
    t475_check(replacement.cache.scene.index.key()->document_identity == &replacement.history.document);
    t475_compare_fresh(replacement);
    auto& charts = replacement.history.document.charts;
    auto chart = charts.front();
    chart.id = chart_id_t{2};
    chart.measures.back().notes.front().label = "other chart";
    charts.push_back(std::move(chart));
    replacement.binding.value->selected_chart = chart_id_t{2};
    replacement.interaction.reset();
    t475_project(replacement);
    t475_select_note(replacement, object_id_t{600});
    t475_check(std::string_view(replacement.flattened.view.compose.selected_note_label) == "other chart");
}

TEST_CASE("Indexed paint occupancy uses only matching heads including across hold tails and lanes",
          "[sys][frame-timing][dense-projection]") {
    for (const auto downscroll : {false, true}) {
        auto song = compose_test_song();
        auto& chart = song.charts.front();
        chart.lane_layouts.front().layout.lanes.push_back(
            {.id = lane_id_t{2}, .name = "Other", .display_order = 1});
        chart.measures.front().notes = {
            {.id = object_id_t{500}, .position = {measure_id_t{1}, 0},
             .lane = lane_id_t{1}, .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{1}, 480}},
            {.id = object_id_t{600}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{2}}};
        compose_test_fixture_t fixture{std::move(song)};
        fixture.application.project.compose_gesture.note_paint_mode_active = true;
        for (const auto row : {0U, 240U, 480U, 720U}) {
            const auto scanned = fixture.cache.scene.paint_occupancy_scanned;
            t475_project_at_row(fixture, row, downscroll);
            t475_check(fixture.flattened.view.compose.paint_ghost.paint_erase_preview == (row == 0));
            t475_check(fixture.cache.scene.paint_occupancy_scanned - scanned <= 3);
            t475_compare_fresh(fixture);
        }
    }
}

TEST_CASE("Dense inspector missing locations recover on insertion and read current resource values",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t fixture{t475_inspector_song()};
    t475_project(fixture);
    t475_select_note(fixture, object_id_t{999});
    const auto scanned = fixture.cache.scene.selected_source_scanned;
    t475_project(fixture);
    t475_check(fixture.cache.scene.selected_source_scanned == scanned);
    t475_check(fixture.flattened.view.compose.selected_note_label_note_id.value == 0);
    auto inserted = fixture.history.document;
    auto& notes = inserted.charts.front().measures.back().notes;
    notes.insert(notes.begin(), {.id = object_id_t{999}, .position = {measure_id_t{2}, 0},
        .lane = lane_id_t{1}, .label = "inserted"});
    t475_require(commit_document_command(fixture.history, std::move(inserted), "Insert memo target"));
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "inserted");
    t475_require(apply_resource_assignment(fixture.history,
        {.chart = chart_id_t{1}, .resource = resource_id_t{901},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{999}}}}));
    t475_project(fixture);
    t475_check(fixture.flattened.view.compose.selected_assigned_resource_id.value == 901);
    t475_compare_fresh(fixture);
    auto reordered = fixture.history.document;
    std::ranges::reverse(reordered.charts.front().measures.back().notes);
    t475_require(commit_document_command(fixture.history, std::move(reordered), "Reorder memo targets"));
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "inserted");
    t475_compare_fresh(fixture);
}
