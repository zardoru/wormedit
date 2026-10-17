#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

namespace {
auto bga_history() -> wormedit::document_history_t {
    using namespace wormedit;
    chart_document_t chart{.id = chart_id_t{7},
                           .divisions_per_standard_measure = 960,
                           .measures = {{.id = measure_id_t{1},
                                         .definition = {{1, 1}, meter_t{4, 4}}},
                                        {.id = measure_id_t{2},
                                         .definition = {{1, 1}, meter_t{4, 4}}}}};
    chart.bga_events = {
        {.id = object_id_t{11},
         .position = {measure_id_t{1}, 120},
         .resource = resource_id_t{31},
         .kind = visual_event_kind_t::background,
         .provenance = {.family = format_family_t::bms, .source_field = "04 slot 1",
                        .source_spelling = "#00104:01", .source_order = 4}},
        {.id = object_id_t{12},
         .position = {measure_id_t{1}, 240},
         .resource = resource_id_t{32},
         .kind = visual_event_kind_t::video},
        {.id = object_id_t{13},
         .position = {measure_id_t{1}, 360},
         .resource = resource_id_t{33},
         .kind = visual_event_kind_t::layer},
    };
    const auto profile = make_bms_5k_profile();
    chart.lane_layouts = {{.id = object_id_t{5},
        .position = {measure_id_t{1}, 0},
        .layout = profile.layout}};
    chart.bgm_source_lanes = {{.id = object_id_t{40}, .name = "BGM"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{41},
                                          .position = {measure_id_t{1}, 480},
                                          .source_lane = object_id_t{40}}};
    chart.measures.front().notes = {{.id = object_id_t{51},
                                     .position = {measure_id_t{1}, 600},
                                     .lane = profile.layout.lanes.front().id}};
    song_document_t song{.id = song_id_t{1}, .revision = 7, .charts = {chart}};
    song.resources = {
        {.id = resource_id_t{31}, .name = "background", .asset = asset_id_t{61},
         .roles = {resource_role_t::bga}},
        {.id = resource_id_t{32}, .name = "video", .asset = asset_id_t{62},
         .roles = {resource_role_t::bga}},
        {.id = resource_id_t{33}, .name = "layer", .asset = asset_id_t{63},
         .roles = {resource_role_t::bga}},
        {.id = resource_id_t{34}, .name = "replacement", .asset = asset_id_t{64},
         .roles = {resource_role_t::bga}},
    };
    song.visual_assets = {{.id = asset_id_t{61}, .kind = asset_kind_t::image},
                          {.id = asset_id_t{62}, .kind = asset_kind_t::video},
                          {.id = asset_id_t{63}, .kind = asset_kind_t::image},
                          {.id = asset_id_t{64}, .kind = asset_kind_t::image}};
    return document_history_t::from(std::move(song));
}

void check_event_provenance(const wormedit::bga_event_t& event,
                            const wormedit::bga_event_t& before) {
    CHECK(event.provenance.source_spelling == before.provenance.source_spelling);
    CHECK(event.provenance.source_order == before.provenance.source_order);
}

void check_moved_background(const wormedit::chart_document_t& chart,
                            const wormedit::chart_document_t& before) {
    const auto event = std::ranges::find(chart.bga_events, wormedit::object_id_t{11},
                                         &wormedit::bga_event_t::id);
    REQUIRE(event != chart.bga_events.end());
    CHECK(event->position == wormedit::local_position_t{wormedit::measure_id_t{1}, 480});
    CHECK(event->resource == wormedit::resource_id_t{31});
    CHECK(event->kind == wormedit::visual_event_kind_t::background);
    CHECK(event->provenance.source_field == before.bga_events.front().provenance.source_field);
    check_event_provenance(*event, before.bga_events.front());
}

void check_unrelated_bga_events(const wormedit::chart_document_t& chart,
                                const wormedit::chart_document_t& before) {
    CHECK(chart.bga_events.size() == before.bga_events.size());
    for (std::size_t index = 1; index < 3; ++index) {
        CHECK(chart.bga_events[index].id == before.bga_events[index].id);
        CHECK(chart.bga_events[index].position == before.bga_events[index].position);
        CHECK(chart.bga_events[index].kind == before.bga_events[index].kind);
    }
}

void check_rejections_leave_state(const wormedit::document_history_t& history,
                                  const std::vector<wormedit::bga_event_t>& before) {
    CHECK(history.document.charts.front().bga_events.size() == before.size());
    CHECK(history.document.charts.front().bga_events.front().position == before.front().position);
    CHECK(history.document.charts.front().bga_events.front().resource == before.front().resource);
    CHECK(history.undo.empty());
}

void check_other_object_kinds(const wormedit::chart_document_t& chart,
                              const wormedit::chart_document_t& before) {
    CHECK(chart.measures.front().bgm_events.front().id == before.measures.front().bgm_events.front().id);
    CHECK(chart.measures.front().bgm_events.front().position == before.measures.front().bgm_events.front().position);
    CHECK(chart.measures.front().notes.front().id == before.measures.front().notes.front().id);
    CHECK(chart.measures.front().notes.front().position == before.measures.front().notes.front().position);
}

void check_replaced_event(const wormedit::bga_event_t& event,
                          const wormedit::bga_event_t& original) {
    CHECK(event.id == original.id);
    CHECK(event.resource == wormedit::resource_id_t{34});
    CHECK(event.position == original.position);
    CHECK(event.kind == original.kind);
    check_event_provenance(event, original);
}

void check_virtual_move_result(const wormedit::chart_document_t& chart,
                               const wormedit::bga_event_t& original) {
    REQUIRE(chart.measures.size() == 3);
    const auto event = std::ranges::find(chart.bga_events, original.id,
                                         &wormedit::bga_event_t::id);
    REQUIRE(event != chart.bga_events.end());
    CHECK(event->position == wormedit::local_position_t{wormedit::measure_id_t{3}, 0});
    CHECK(event->resource == original.resource);
    CHECK(event->kind == original.kind);
    check_event_provenance(*event, original);
}

void check_bga_identity_at(const wormedit::chart_document_t& chart,
                           const wormedit::bga_event_t& original,
                           wormedit::local_position_t position) {
    const auto event = std::ranges::find(chart.bga_events, original.id,
                                         &wormedit::bga_event_t::id);
    REQUIRE(event != chart.bga_events.end());
    CHECK(event->position == position);
    CHECK(event->resource == original.resource);
    CHECK(event->kind == original.kind);
    check_event_provenance(*event, original);
}

void check_move_undo_redo(wormedit::document_history_t& history,
                          const wormedit::bga_event_t& original,
                          wormedit::local_position_t destination) {
    REQUIRE(wormedit::undo_document_command(history));
    check_bga_identity_at(history.document.charts.front(), original, original.position);
    REQUIRE(wormedit::redo_document_command(history));
    check_bga_identity_at(history.document.charts.front(), original, destination);
}

void check_deleted_objects_unchanged(wormedit::document_history_t& history,
                                     const wormedit::bga_event_t& deleted,
                                     const wormedit::chart_document_t& before) {
    const auto& chart = history.document.charts.front();
    CHECK(std::ranges::find(chart.bga_events, deleted.id, &wormedit::bga_event_t::id) ==
          chart.bga_events.end());
    CHECK(chart.bga_events.size() + 1 == before.bga_events.size());
    CHECK(chart.measures.front().bgm_events.front().id ==
          before.measures.front().bgm_events.front().id);
    CHECK(chart.measures.front().notes.front().id == before.measures.front().notes.front().id);
}

void apply_typed_bga_delete(wormedit::document_history_t& history,
                            wormedit::editor_interaction_state_t& state) {
    const auto result = wormedit::dispatch_editor_action(
        history, state, wormedit::delete_editor_selection_action_t{});
    for (const auto& diagnostic : result.diagnostics) WARN(diagnostic.message);
    REQUIRE(result);
    CHECK(result.document_changed);
    REQUIRE(history.undo.size() == 1);
}
}

TEST_CASE("BGA lane move keeps event identity and leaves other event kinds untouched",
          "[unit][sys][bga-lane]") {
    using namespace wormedit;
    auto history = bga_history();
    const auto before = history.document.charts.front();
    const auto moved = apply_bga_event_move(
        history,
        {.chart = chart_id_t{7}, .event = object_id_t{11},
         .position = {measure_id_t{1}, 480}, .snap_denominator = 4});
    if (!moved) for (const auto& diagnostic : moved.diagnostics) WARN(diagnostic.message);
    REQUIRE(moved);
    REQUIRE(history.undo.size() == 1);
    const auto& chart = history.document.charts.front();
    check_unrelated_bga_events(chart, before);
    check_moved_background(chart, before);
    check_other_object_kinds(chart, before);
    check_move_undo_redo(history, before.bga_events.front(),
                         {measure_id_t{1}, 480});
}

TEST_CASE("BGA lane replacement preserves event kind, ID, provenance, and undo boundary",
          "[unit][sys][bga-lane]") {
    using namespace wormedit;
    auto history = bga_history();
    const auto original = history.document.charts.front().bga_events.front();
    const auto replaced = apply_resource_assignment(
        history,
        {.chart = chart_id_t{7}, .resource = resource_id_t{34},
         .objects = {{editor_selection_object_kind_t::bga_event, object_id_t{11}}}});
    if (!replaced) for (const auto& diagnostic : replaced.diagnostics) WARN(diagnostic.message);
    REQUIRE(replaced);
    REQUIRE(history.undo.size() == 1);
    const auto& event = history.document.charts.front().bga_events.front();
    check_replaced_event(event, original);
}

TEST_CASE("BGA lane move rejects missing and non-lane event targets unchanged",
          "[unit][sys][bga-lane]") {
    using namespace wormedit;
    auto history = bga_history();
    const auto before = history.document.charts.front().bga_events;
    const auto missing = apply_bga_event_move(history,
        {.chart = chart_id_t{7}, .event = object_id_t{99}, .position = {measure_id_t{1}, 480}});
    const auto unsupported = apply_bga_event_move(history,
        {.chart = chart_id_t{7}, .event = object_id_t{13}, .position = {measure_id_t{1}, 480},
         .bga_kind = visual_event_kind_t::storyboard});
    CHECK_FALSE(missing);
    CHECK_FALSE(unsupported);
    check_rejections_leave_state(history, before);
}

TEST_CASE("Layer to Layer 2 changes kind at the same row with one undo", "[bga][bga-lane]") {
    using namespace wormedit;
    auto history = bga_history();
    const auto original = history.document.charts.front().bga_events.back();
    REQUIRE(apply_bga_event_move(history,
        {.chart = chart_id_t{7}, .event = original.id, .position = original.position,
         .bga_kind = visual_event_kind_t::layer2}));
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().bga_events.back().kind == visual_event_kind_t::layer2);
    CHECK(history.document.charts.front().bga_events.back().resource == original.resource);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bga_events.back().kind == visual_event_kind_t::layer);
}

namespace {
void check_stacked_bga_move(bool change_kind) {
    using namespace wormedit;
    auto history = bga_history();
    const auto original = history.document.charts.front().bga_events[change_kind ? 2 : 0];
    const auto destination = local_position_t{measure_id_t{1}, change_kind ? 360U : 240U};
    const auto kind = change_kind ? visual_event_kind_t::layer2 : original.kind;
    if (change_kind) history.document.charts.front().bga_events.push_back({.id = object_id_t{14},
        .position = destination, .resource = original.resource, .kind = kind});
    REQUIRE(apply_bga_event_move(history,
        {.chart = chart_id_t{7}, .event = original.id, .position = destination, .bga_kind = kind}));
    CHECK(history.undo.size() == 1);
    const auto moved = std::ranges::find(history.document.charts.front().bga_events, original.id, &bga_event_t::id);
    CHECK(moved->position == destination);
    CHECK(moved->kind == kind);
    REQUIRE(undo_document_command(history));
    check_bga_identity_at(history.document.charts.front(), original, original.position);
}
}

TEST_CASE("BGA same-kind and kind-changing moves preserve stacked-row support", "[bga][bga-lane]") {
    check_stacked_bga_move(false);
    check_stacked_bga_move(true);
}

TEST_CASE("BGA overlay video move and placement reject unchanged", "[bga][bga-lane]") {
    using namespace wormedit;
    auto history = bga_history();
    const auto revision = history.document.revision;
    const auto original = history.document.charts.front().bga_events;
    const auto moved = apply_bga_event_move(history,
        {.chart = chart_id_t{7}, .event = object_id_t{12}, .position = {measure_id_t{1}, 480},
         .bga_kind = visual_event_kind_t::layer2});
    CHECK_FALSE(moved);
    REQUIRE_FALSE(moved.diagnostics.empty());
    const auto placed = apply_resource_placement(history,
        {.chart = chart_id_t{7}, .resource = resource_id_t{32}, .position = {measure_id_t{1}, 480},
         .kind = resource_placement_kind_t::bga_background, .bga_kind = visual_event_kind_t::layer});
    CHECK_FALSE(placed);
    CHECK(history.document.revision == revision);
    check_rejections_leave_state(history, original);
}

TEST_CASE("BGA per-kind placement shares a row and a collision rejects atomically", "[bga][bga-lane]") {
    using namespace wormedit;
    auto history = bga_history();
    const place_resource_action_t action{.chart = chart_id_t{7}, .resource = resource_id_t{31},
        .position = {measure_id_t{1}, 120}, .kind = resource_placement_kind_t::bga_background,
        .bga_kind = visual_event_kind_t::layer2};
    REQUIRE(apply_resource_placement(history, action));
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().bga_events.back().kind == visual_event_kind_t::layer2);
    const auto revision = history.document.revision;
    CHECK_FALSE(apply_resource_placement(history, action));
    CHECK(history.document.revision == revision);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("BGA move materializes one virtual BMS measure in the same undo step",
          "[unit][sys][bga-lane][bms]") {
    using namespace wormedit;
    auto history = bga_history();
    history.document.charts.front().source_family = format_family_t::bms;
    const auto original = history.document.charts.front().bga_events.front();
    const auto moved = apply_virtual_bms_bga_event_move(
        history,
        {.chart = chart_id_t{7}, .event = original.id, .canvas_row = 2 * 960 + 120,
         .snap_denominator = 4});
    if (!moved) for (const auto& diagnostic : moved.diagnostics) WARN(diagnostic.message);
    REQUIRE(moved);
    REQUIRE(history.undo.size() == 1);
    const auto& chart = history.document.charts.front();
    check_virtual_move_result(chart, original);
}

TEST_CASE("BGA move with snap disabled preserves the requested virtual BMS row",
          "[unit][sys][bga-lane][bms]") {
    using namespace wormedit;
    auto history = bga_history();
    history.document.charts.front().source_family = format_family_t::bms;
    const auto original = history.document.charts.front().bga_events.front();
    const auto moved = apply_virtual_bms_bga_event_move(
        history,
        {.chart = chart_id_t{7}, .event = original.id, .canvas_row = 2 * 960 + 120,
         .snap_denominator = 0});
    REQUIRE(moved);
    REQUIRE(history.undo.size() == 1);
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.measures.size() == 3);
    const auto event = std::ranges::find(chart.bga_events, original.id, &bga_event_t::id);
    REQUIRE(event != chart.bga_events.end());
    CHECK(event->position == local_position_t{measure_id_t{3}, 120});
}

TEST_CASE("typed BGA deletion preserves neighboring objects across undo and redo",
          "[unit][sys][bga-lane][delete]") {
    using namespace wormedit;
    auto history = bga_history();
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = editor_interaction_state_t::from(history, chart_id_t{7},
        history.document.charts.front().lane_layouts.front().layout.lanes.front().id,
        {measure_id_t{1}, 0}, snaps, 4);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    state.selected_objects = {{editor_selection_object_kind_t::bga_event, object_id_t{11}}};
    const auto before = history.document.charts.front();
    const auto deleted = before.bga_events.front();
    apply_typed_bga_delete(history, state);
    check_deleted_objects_unchanged(history, deleted, before);
    REQUIRE(undo_document_command(history));
    check_bga_identity_at(history.document.charts.front(), deleted, deleted.position);
    CHECK(history.document.charts.front().bga_events.size() == before.bga_events.size());
    REQUIRE(redo_document_command(history));
    CHECK(std::ranges::find(history.document.charts.front().bga_events, deleted.id,
          &bga_event_t::id) == history.document.charts.front().bga_events.end());
}

TEST_CASE("All discoverable BGA lane kinds survive save and reopen", "[unit][sys][bga-lane][bga-discovery]") {
    using namespace wormedit;
    auto history = bga_history();
    auto& events = history.document.charts.front().bga_events;
    constexpr std::array kinds{visual_event_kind_t::background, visual_event_kind_t::layer,
                               visual_event_kind_t::layer2, visual_event_kind_t::poor};
    events.clear();
    for (std::size_t index = 0; index < kinds.size(); ++index)
        events.push_back({.id = object_id_t{100 + index},
            .position = {measure_id_t{1}, static_cast<std::uint32_t>(120 * index)},
            .resource = resource_id_t{31}, .kind = kinds[index],
            .provenance = {.family = format_family_t::bms, .source_field = "retained BGA source"}});
    const auto serialized = write_native_project(history.document);
    REQUIRE(serialized.value);
    const auto reopened = parse_native_project(*serialized.value);
    REQUIRE(reopened.value);
    const auto& restored = reopened.value->charts.front().bga_events;
    REQUIRE(restored.size() == kinds.size());
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        CHECK(restored[index].kind == kinds[index]);
        CHECK(restored[index].id == events[index].id);
        CHECK(restored[index].position == events[index].position);
        CHECK(restored[index].resource == events[index].resource);
        CHECK(restored[index].provenance.source_field == events[index].provenance.source_field);
    }
}
