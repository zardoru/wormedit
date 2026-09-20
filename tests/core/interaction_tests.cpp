#include <wormedit/core/interaction.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <ranges>

using namespace wormedit;

namespace {

auto make_song() -> song_document_t {
    return {
        .id = song_id_t{1},
        .charts =
            {
                {
                    .id = chart_id_t{1},
                    .divisions_per_standard_measure = 960,
                    .measures =
                        {
                            {.id = measure_id_t{1}},
                            {.id = measure_id_t{2}},
                        },
                    .lane_layouts =
                        {
                            {
                                .id = object_id_t{100},
                                .position = {measure_id_t{1}, 0},
                                .layout =
                                    {
                                        .profile_id = "interaction-test",
                                        .lanes =
                                            {
                                                {.id = lane_id_t{1}, .name = "Key 1"},
                                                {.id = lane_id_t{2},
                                                 .name = "Scratch",
                                                 .role = lane_role_t::scratch},
                                            },
                                    },
                            },
                        },
                },
            },
    };
}

auto make_state(document_history_t& history) -> editor_interaction_state_t {
    constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}, std::uint64_t{6}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                         {measure_id_t{1}, 840}, snaps, 4);
    REQUIRE(setup);
    return std::move(*setup.state);
}

auto note_count(const document_history_t& history) -> std::size_t {
    std::size_t count{};
    for (const auto& measure : history.document.charts.front().measures) {
        count += measure.notes.size();
    }
    return count;
}

auto find_note(const document_history_t& history, object_id_t id) -> const note_t* {
    for (const auto& measure : history.document.charts.front().measures) {
        const auto found = std::ranges::find(measure.notes, id, &note_t::id);
        if (found != measure.notes.end())
            return &*found;
    }
    return nullptr;
}

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE(
    "headless editor actions cycle exact snaps, step across measures, and place undoable taps",
    "[unit][core][interaction]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);
    constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}, std::uint64_t{6}};
    const auto initial = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                                 {measure_id_t{1}, 840}, snaps, 4);
    REQUIRE(initial);
    const auto before_anchor = editor_position_y_pixels(history.document.charts.front(),
                                                        *initial.view, {measure_id_t{1}, 840});
    const auto before_next_measure = editor_position_y_pixels(history.document.charts.front(),
                                                              *initial.view, {measure_id_t{2}, 0});
    REQUIRE(before_anchor.has_value());
    REQUIRE(before_next_measure.has_value());
    CHECK(*before_next_measure - *before_anchor == 24.0);

    const auto zoomed = dispatch_editor_action(history, state, adjust_editor_zoom_action_t{2.0});
    REQUIRE(zoomed);
    REQUIRE(zoomed.view.has_value());
    CHECK(zoomed.view->pixels_per_standard_measure == 384.0);
    const auto cursor_screen_y = 840.0 * zoomed.view->pixels_per_standard_measure / 960.0 -
                                 zoomed.view->viewport_scroll_pixels;
    CHECK(cursor_screen_y == 168.0);
    CHECK(zoomed.view->cursor == local_position_t{measure_id_t{1}, 840});
    CHECK(history.document.revision == 1);
    const auto zoomed_anchor = editor_position_y_pixels(history.document.charts.front(),
                                                        *zoomed.view, {measure_id_t{1}, 840});
    const auto zoomed_next_measure = editor_position_y_pixels(history.document.charts.front(),
                                                              *zoomed.view, {measure_id_t{2}, 0});
    REQUIRE(zoomed_anchor.has_value());
    REQUIRE(zoomed_next_measure.has_value());
    CHECK(*zoomed_anchor == *before_anchor);
    CHECK(*zoomed_next_measure - *zoomed_anchor == 48.0);

    const auto moved = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    REQUIRE(moved);
    REQUIRE(moved.view.has_value());
    CHECK(moved.view->cursor == local_position_t{measure_id_t{2}, 0});
    CHECK(moved.view->active_lane_name == "Key 1");
    CHECK(moved.view->can_move_backward);
    CHECK(moved.view->can_move_forward);
    CHECK(history.document.revision == 1);

    const auto max_zoom = dispatch_editor_action(
        history, state, adjust_editor_zoom_action_t{maximum_pixels_per_standard_measure});
    REQUIRE(max_zoom);
    REQUIRE(max_zoom.view.has_value());
    CHECK(max_zoom.view->pixels_per_standard_measure == maximum_pixels_per_standard_measure);
    CHECK(max_zoom.view->viewport_scroll_pixels != zoomed.view->viewport_scroll_pixels);

    const auto cycled = dispatch_editor_action(
        history, state, cycle_edit_snap_action_t{interaction_direction_t::forward});
    REQUIRE(cycled);
    REQUIRE(cycled.view.has_value());
    CHECK(cycled.view->active_snap_denominator == 8);
    CHECK(cycled.view->supported_snap_denominators == std::vector<std::uint64_t>{4, 8, 6});

    const auto placed =
        dispatch_editor_action(history, state, place_tap_action_t{{measure_id_t{2}, 121}});
    REQUIRE(placed);
    CHECK(placed.document_changed);
    REQUIRE(placed.view.has_value());
    CHECK(placed.view->cursor == local_position_t{measure_id_t{2}, 120});
    CHECK(placed.view->document_revision == 2);
    REQUIRE(note_count(history) == 1);
    const auto& note = history.document.charts.front().measures.back().notes.front();
    CHECK(note.position == local_position_t{measure_id_t{2}, 120});
    CHECK(note.lane == lane_id_t{1});
    CHECK(note.kind == note_kind_t::tap);

    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 0);
    CHECK(history.document.revision == 1);
    REQUIRE(redo_document_command(history));
    CHECK(note_count(history) == 1);
    CHECK(history.document.revision == 2);

    const auto removed =
        dispatch_editor_action(history, state, place_tap_action_t{{measure_id_t{2}, 120}});
    REQUIRE(removed);
    CHECK(removed.document_changed);
    CHECK(note_count(history) == 0);
    CHECK(history.document.revision == 3);
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 1);
    CHECK(history.document.revision == 2);
}

TEST_CASE("cursor paging preserves fractional measure rows and keeps the cursor visible",
          "[unit][core][interaction][navigation]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.measures = {
        {.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
        {.id = measure_id_t{2}, .definition = {{3, 2}, meter_t{3, 2}}},
        {.id = measure_id_t{3}, .definition = {{1, 2}, meter_t{1, 2}}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                         {measure_id_t{1}, 320}, snaps, 4, 192.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);

    const auto first_page = dispatch_editor_action(
        history, state, page_edit_cursor_action_t{interaction_direction_t::forward, 1});
    REQUIRE(first_page);
    REQUIRE(first_page.view.has_value());
    CHECK(first_page.view->cursor == local_position_t{measure_id_t{2}, 480});
    CHECK(first_page.view->viewport_scroll_pixels > 0.0);
    const auto second_page = dispatch_editor_action(
        history, state, page_edit_cursor_action_t{interaction_direction_t::forward, 1});
    REQUIRE(second_page);
    CHECK(second_page.view->cursor == local_position_t{measure_id_t{3}, 160});
    const auto back = dispatch_editor_action(
        history, state, page_edit_cursor_action_t{interaction_direction_t::backward, 1});
    REQUIRE(back);
    CHECK(back.view->cursor == local_position_t{measure_id_t{2}, 480});

    REQUIRE(dispatch_editor_action(
        history, state,
        jump_edit_cursor_action_t{{measure_id_t{1}, 1}, history.document.revision}));
    const auto tie_earlier = dispatch_editor_action(
        history, state, page_edit_cursor_action_t{interaction_direction_t::forward, 2});
    REQUIRE(tie_earlier);
    CHECK(tie_earlier.view->cursor == local_position_t{measure_id_t{3}, 0});
    const auto clamped = dispatch_editor_action(
        history, state, page_edit_cursor_action_t{interaction_direction_t::forward, 99});
    REQUIRE(clamped);
    CHECK(clamped.view->cursor == local_position_t{measure_id_t{3}, 0});
    const auto invalid = dispatch_editor_action(
        history, state, page_edit_cursor_action_t{interaction_direction_t::forward, 0});
    CHECK_FALSE(invalid);
    CHECK(has_code(invalid.diagnostics, diagnostic_code_t::invalid_resolution));
}

TEST_CASE("editor y mapping preserves exact rows through zoom and scroll",
          "[unit][core][interaction][mapping]") {
    auto history = make_document_history(make_song());
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 4, 192.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);

    const auto scrolled = dispatch_editor_action(history, state, scroll_editor_view_action_t{48.0});
    REQUIRE(scrolled);
    REQUIRE(scrolled.view.has_value());
    const auto before_zoom = editor_position_y_pixels(history.document.charts.front(),
                                                      *scrolled.view, {measure_id_t{1}, 480});
    REQUIRE(before_zoom.has_value());
    CHECK(*before_zoom == 48.0);
    CHECK(editor_position_at_y_pixels(history.document.charts.front(), *scrolled.view,
                                      *before_zoom) == local_position_t{measure_id_t{1}, 480});

    const auto zoomed =
        dispatch_editor_action(history, state, adjust_editor_zoom_action_t{2.0, 48.0});
    REQUIRE(zoomed);
    REQUIRE(zoomed.view.has_value());
    const auto after_zoom = editor_position_y_pixels(history.document.charts.front(), *zoomed.view,
                                                     {measure_id_t{1}, 480});
    REQUIRE(after_zoom.has_value());
    CHECK(*after_zoom == 48.0);
    CHECK(editor_position_at_y_pixels(history.document.charts.front(), *zoomed.view, *after_zoom) ==
          local_position_t{measure_id_t{1}, 480});
}

TEST_CASE("hit testing returns lane, note, hold head, and cross-measure tail targets",
          "[unit][core][interaction][hit-test]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::tap},
        {.id = object_id_t{102},
         .position = {measure_id_t{1}, 480},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{2}, 120}},
        {.id = object_id_t{103},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{2},
         .kind = note_kind_t::tap},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    const auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                               {measure_id_t{1}, 0}, snaps, 4, 384.0);
    REQUIRE(setup);
    const auto built =
        build_editor_hit_test_index(history.document.charts.front(), history.document.revision);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(built));
    const auto& index = std::get<editor_hit_test_index_t>(built);
    const auto hit = [&](double x, local_position_t position) {
        const auto y =
            editor_position_y_pixels(history.document.charts.front(), *setup.view, position);
        REQUIRE(y.has_value());
        return hit_test_editor(history.document.charts.front(), index, *setup.view,
                               {.x_pixels = x,
                                .y_pixels = *y,
                                .field_left_pixels = 100.0,
                                .field_width_pixels = 200.0,
                                .note_hit_radius_pixels = 3.0});
    };

    const auto tap = hit(150.0, {measure_id_t{1}, 240});
    REQUIRE(tap.has_value());
    CHECK(tap->kind == editor_hit_target_kind_t::note);
    CHECK(tap->note == object_id_t{101});
    const auto hold_head = hit(150.0, {measure_id_t{1}, 480});
    REQUIRE(hold_head.has_value());
    CHECK(hold_head->kind == editor_hit_target_kind_t::hold_head);
    CHECK(hold_head->note == object_id_t{102});
    const auto hold_tail = hit(150.0, {measure_id_t{2}, 120});
    REQUIRE(hold_tail.has_value());
    CHECK(hold_tail->kind == editor_hit_target_kind_t::hold_tail);
    CHECK(hold_tail->note == object_id_t{102});
    const auto hold_body = hit(150.0, {measure_id_t{1}, 720});
    REQUIRE(hold_body.has_value());
    CHECK(hold_body->kind == editor_hit_target_kind_t::note);
    CHECK(hold_body->note == object_id_t{102});
    CHECK(hold_body->note_kind == note_kind_t::hold);
    const auto empty_lane = hit(250.0, {measure_id_t{2}, 240});
    REQUIRE(empty_lane.has_value());
    CHECK(empty_lane->kind == editor_hit_target_kind_t::lane);
    CHECK(empty_lane->lane == lane_id_t{2});

    auto stale_view = *setup.view;
    ++stale_view.document_revision;
    CHECK_FALSE(hit_test_editor(history.document.charts.front(), index, stale_view,
                                {.x_pixels = 150.0,
                                 .y_pixels = 48.0,
                                 .field_left_pixels = 100.0,
                                 .field_width_pixels = 200.0}));
}

TEST_CASE("overlapping hit records use stable hold-head priority",
          "[unit][core][interaction][hit-test][priority]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::tap},
        {.id = object_id_t{102},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 480}},
    };
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    const auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                               {measure_id_t{1}, 0}, snaps, 4, 192.0);
    REQUIRE(setup);
    const auto built = build_editor_hit_test_index(history.document.charts.front(), 1);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(built));
    const auto y = editor_position_y_pixels(history.document.charts.front(), *setup.view,
                                            {measure_id_t{1}, 240});
    REQUIRE(y.has_value());
    const editor_hit_test_request_t request{
        .x_pixels = 150.0,
        .y_pixels = *y,
        .field_left_pixels = 100.0,
        .field_width_pixels = 200.0,
    };
    const auto first =
        hit_test_editor(history.document.charts.front(), std::get<editor_hit_test_index_t>(built),
                        *setup.view, request);
    const auto second =
        hit_test_editor(history.document.charts.front(), std::get<editor_hit_test_index_t>(built),
                        *setup.view, request);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->kind == editor_hit_target_kind_t::hold_head);
    CHECK(first->note == object_id_t{102});
    CHECK(first == second);
}

TEST_CASE("typed object hover is deterministic and supplies a same-object property request",
          "[unit][core][interaction][hover][double-click]") {
    auto song = make_song();
    song.audio_assets = {{.id = asset_id_t{1}}};
    song.resources = {{.id = resource_id_t{7},
                       .name = "Verse sample",
                       .asset = asset_id_t{1},
                       .roles = {resource_role_t::sample}}};
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::tap,
         .sound = resource_id_t{7},
         .label = "Verse"},
    };
    song.charts.front().bpms = {
        {.id = object_id_t{201}, .position = {measure_id_t{1}, 480}, .beats_per_minute = {180, 1}},
        {.id = object_id_t{202}, .position = {measure_id_t{1}, 720}, .beats_per_minute = {240, 1}},
    };
    song.charts.front().pauses = {
        {.id = object_id_t{203},
         .position = {measure_id_t{1}, 720},
         .kind = pause_kind_t::stop,
         .duration = {1, 2},
         .source_unit = pause_unit_t::seconds},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    const auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                               {measure_id_t{1}, 0}, snaps, 4, 384.0);
    REQUIRE(setup);
    const auto built = build_editor_hit_test_index(history.document.charts.front(), 1);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(built));
    const auto& index = std::get<editor_hit_test_index_t>(built);

    const auto note_y = editor_position_y_pixels(history.document.charts.front(), *setup.view,
                                                 {measure_id_t{1}, 240});
    REQUIRE(note_y.has_value());
    const auto note_hover = hover_editor_object(history.document.charts.front(), index, *setup.view,
                                                {.x_pixels = 150.0,
                                                 .y_pixels = *note_y,
                                                 .field_left_pixels = 100.0,
                                                 .field_width_pixels = 200.0,
                                                 .hit_radius_pixels = 0.0,
                                                 .scope = {.notes = true}});
    REQUIRE(note_hover.has_value());
    CHECK(note_hover->object ==
          editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{101}});
    CHECK(note_hover->editor == editor_inline_editor_kind_t::note_properties);
    CHECK(note_hover->tooltip.find("Tap") != std::string::npos);
    CHECK(note_hover->tooltip.find("Verse") != std::string::npos);
    CHECK(note_hover->tooltip.find("sample 7") != std::string::npos);
    auto stale_view = *setup.view;
    ++stale_view.document_revision;
    CHECK_FALSE(hover_editor_object(history.document.charts.front(), index, stale_view,
                                    {.x_pixels = 150.0,
                                     .y_pixels = *note_y,
                                     .field_left_pixels = 100.0,
                                     .field_width_pixels = 200.0,
                                     .hit_radius_pixels = 0.0,
                                     .scope = {.notes = true}}));
    CHECK_FALSE(hover_editor_object(history.document.charts.front(), index, *setup.view,
                                    {.x_pixels = 400.0,
                                     .y_pixels = *note_y,
                                     .field_left_pixels = 100.0,
                                     .field_width_pixels = 200.0,
                                     .hit_radius_pixels = 0.0,
                                     .scope = {.notes = true}}));

    editor_double_click_state_t click_state;
    CHECK_FALSE(register_editor_object_click(click_state, index, *setup.view, note_hover, 150.0,
                                             *note_y, 100));
    const auto note_edit = register_editor_object_click(click_state, index, *setup.view, note_hover,
                                                        151.0, *note_y, 200);
    REQUIRE(note_edit.has_value());
    CHECK(note_edit->chart == chart_id_t{1});
    CHECK(note_edit->document_revision == 1);
    CHECK(note_edit->object == note_hover->object);
    CHECK(note_edit->editor == editor_inline_editor_kind_t::note_properties);
    CHECK_FALSE(click_state.previous_click.has_value());
    clear_editor_double_click_state(click_state);
    CHECK_FALSE(click_state.previous_click.has_value());

    const auto bpm_y = editor_position_y_pixels(history.document.charts.front(), *setup.view,
                                                {measure_id_t{1}, 480});
    REQUIRE(bpm_y.has_value());
    const auto bpm_request = editor_object_hover_request_t{
        .x_pixels = 150.0,
        .y_pixels = *bpm_y,
        .field_left_pixels = 100.0,
        .field_width_pixels = 200.0,
        .hit_radius_pixels = 0.0,
        .scope = {.notes = false, .timing = true},
    };
    const auto bpm_hover =
        hover_editor_object(history.document.charts.front(), index, *setup.view, bpm_request);
    REQUIRE(bpm_hover.has_value());
    CHECK(bpm_hover->object ==
          editor_selection_object_t{editor_selection_object_kind_t::bpm_event, object_id_t{201}});
    CHECK(bpm_hover->editor == editor_inline_editor_kind_t::bpm_event);
    CHECK(bpm_hover->tooltip.find("BPM 180") != std::string::npos);
    CHECK(hover_editor_object(history.document.charts.front(), index, *setup.view, bpm_request) ==
          bpm_hover);

    const auto overlap_y = editor_position_y_pixels(history.document.charts.front(), *setup.view,
                                                    {measure_id_t{1}, 720});
    REQUIRE(overlap_y.has_value());
    auto overlap_request = bpm_request;
    overlap_request.y_pixels = *overlap_y;
    const auto overlap =
        hover_editor_object(history.document.charts.front(), index, *setup.view, overlap_request);
    REQUIRE(overlap.has_value());
    CHECK(overlap->object ==
          editor_selection_object_t{editor_selection_object_kind_t::bpm_event, object_id_t{202}});
    CHECK(hover_editor_object(history.document.charts.front(), index, *setup.view,
                              overlap_request) == overlap);
}

TEST_CASE("typed double-click requests clear across stale revisions, focus, and capture loss",
          "[unit][core][interaction][double-click][safety]") {
    auto song = make_song();
    song.charts.front().bpms = {
        {.id = object_id_t{201}, .position = {measure_id_t{1}, 240}, .beats_per_minute = {180, 1}},
    };
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    const auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                               {measure_id_t{1}, 0}, snaps, 4, 384.0);
    REQUIRE(setup);
    const auto first_index = build_editor_hit_test_index(history.document.charts.front(), 1);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(first_index));
    const auto y = editor_position_y_pixels(history.document.charts.front(), *setup.view,
                                            {measure_id_t{1}, 240});
    REQUIRE(y.has_value());
    const auto request = editor_object_hover_request_t{
        .x_pixels = 150.0,
        .y_pixels = *y,
        .field_left_pixels = 100.0,
        .field_width_pixels = 200.0,
        .hit_radius_pixels = 0.0,
        .scope = {.notes = false, .timing = true},
    };
    const auto first_hover =
        hover_editor_object(history.document.charts.front(),
                            std::get<editor_hit_test_index_t>(first_index), *setup.view, request);
    REQUIRE(first_hover.has_value());

    editor_double_click_state_t state;
    CHECK_FALSE(register_editor_object_click(state, std::get<editor_hit_test_index_t>(first_index),
                                             *setup.view, first_hover, 150.0, *y, 100));
    auto revised_view = *setup.view;
    ++revised_view.document_revision;
    const auto revised_index = build_editor_hit_test_index(history.document.charts.front(), 2);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(revised_index));
    const auto revised_hover = hover_editor_object(history.document.charts.front(),
                                                   std::get<editor_hit_test_index_t>(revised_index),
                                                   revised_view, request);
    REQUIRE(revised_hover.has_value());
    CHECK_FALSE(register_editor_object_click(state,
                                             std::get<editor_hit_test_index_t>(revised_index),
                                             revised_view, revised_hover, 150.0, *y, 200));
    REQUIRE(state.previous_click.has_value());
    CHECK(state.previous_click->document_revision == 2);

    CHECK_FALSE(
        register_editor_object_click(state, std::get<editor_hit_test_index_t>(revised_index),
                                     revised_view, revised_hover, 150.0, *y, 250, {}, false, true));
    CHECK_FALSE(state.previous_click.has_value());
    CHECK_FALSE(
        register_editor_object_click(state, std::get<editor_hit_test_index_t>(revised_index),
                                     revised_view, revised_hover, 150.0, *y, 300, {}, true, false));
    CHECK_FALSE(state.previous_click.has_value());

    CHECK_FALSE(register_editor_object_click(state,
                                             std::get<editor_hit_test_index_t>(revised_index),
                                             revised_view, revised_hover, 150.0, *y, 400));
    const auto request_after_cancel =
        register_editor_object_click(state, std::get<editor_hit_test_index_t>(revised_index),
                                     revised_view, revised_hover, 150.0, *y, 450);
    REQUIRE(request_after_cancel.has_value());
    CHECK(request_after_cancel->document_revision == 2);
    CHECK(request_after_cancel->editor == editor_inline_editor_kind_t::bpm_event);
}

TEST_CASE("hover lookup stays row-indexed for fifty thousand typed events",
          "[unit][core][interaction][hover][scale]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    constexpr std::size_t event_count = 50'000;
    constexpr std::size_t events_per_measure = 960;
    chart.measures.clear();
    chart.bpms.clear();
    const auto measure_count = (event_count + events_per_measure - 1) / events_per_measure;
    chart.measures.reserve(measure_count);
    chart.bpms.reserve(event_count);
    for (std::size_t measure_number = 0; measure_number < measure_count; ++measure_number) {
        const auto measure_id = measure_id_t{measure_number + 1};
        chart.measures.push_back({.id = measure_id});
        const auto count =
            std::min(events_per_measure, event_count - measure_number * events_per_measure);
        for (std::size_t row = 0; row < count; ++row) {
            const auto event_number = measure_number * events_per_measure + row;
            chart.bpms.push_back({
                .id = object_id_t{event_number + 1001},
                .position = {measure_id, row},
                .beats_per_minute = {120, 1},
            });
        }
    }
    const auto diagnostics = validate_document(song);
    INFO("validation diagnostics: "
         << diagnostics.size() << (diagnostics.empty() ? "" : ": " + diagnostics.front().message));
    REQUIRE(diagnostics.empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    const auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                               {measure_id_t{1}, 0}, snaps, 4, 384.0);
    REQUIRE(setup);
    const auto built = build_editor_hit_test_index(history.document.charts.front(), 1);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(built));
    const auto& index = std::get<editor_hit_test_index_t>(built);
    REQUIRE(index.hover_objects_by_id.size() == event_count);
    REQUIRE(std::ranges::is_sorted(index.selection_spans, [](const auto& left, const auto& right) {
        if (left.begin_row != right.begin_row)
            return left.begin_row < right.begin_row;
        if (left.end_row != right.end_row)
            return left.end_row < right.end_row;
        return left.object < right.object;
    }));

    constexpr std::size_t target_event = event_count - 37;
    const auto target_measure = target_event / events_per_measure;
    const auto target_row = target_event % events_per_measure;
    const auto target_position = local_position_t{measure_id_t{target_measure + 1}, target_row};
    auto view = *setup.view;
    view.viewport_scroll_pixels =
        static_cast<double>(measure_count) * view.pixels_per_standard_measure -
        view.viewport_height_pixels;
    const auto y = editor_position_y_pixels(history.document.charts.front(), view, target_position);
    REQUIRE(y.has_value());
    const auto hover = hover_editor_object(history.document.charts.front(), index, view,
                                           {.x_pixels = 150.0,
                                            .y_pixels = *y,
                                            .field_left_pixels = 100.0,
                                            .field_width_pixels = 200.0,
                                            .hit_radius_pixels = 0.0,
                                            .scope = {.notes = false, .timing = true}});
    REQUIRE(hover.has_value());
    CHECK(hover->object.id == object_id_t{target_event + 1001});
}

TEST_CASE("area selection normalizes all drag directions and applies replace, add, and toggle",
          "[unit][core][interaction][selection][area]") {
    auto make_selection_history = [] {
        auto song = make_song();
        song.charts.front().measures.front().notes = {
            {.id = object_id_t{101}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
            {.id = object_id_t{102},
             .position = {measure_id_t{1}, 480},
             .lane = lane_id_t{2},
             .kind = note_kind_t::hold,
             .end = local_position_t{measure_id_t{1}, 720}},
        };
        REQUIRE(validate_document(song).empty());
        return make_document_history(std::move(song));
    };
    constexpr std::array snaps{std::uint64_t{4}};
    const std::array drag_corners{
        std::array{110.0, 45.0, 290.0, 100.0},
        std::array{290.0, 45.0, 110.0, 100.0},
        std::array{110.0, 100.0, 290.0, 45.0},
        std::array{290.0, 100.0, 110.0, 45.0},
    };
    for (const auto& drag : drag_corners) {
        auto history = make_selection_history();
        auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                             {measure_id_t{1}, 0}, snaps, 4, 192.0);
        REQUIRE(setup);
        auto state = std::move(*setup.state);
        const auto begun = dispatch_editor_action(
            history, state,
            begin_editor_area_selection_action_t{drag[0], drag[1], 100.0, 200.0,
                                                 editor_area_selection_mode_t::replace, true});
        REQUIRE(begun);
        REQUIRE(begun.view->pending_editor_area_selection.has_value());
        CHECK(state.selected_notes.empty());
        const auto committed = dispatch_editor_action(
            history, state, commit_editor_area_selection_action_t{drag[2], drag[3], true});
        REQUIRE(committed);
        CHECK(committed.view->selected_notes ==
              std::vector<object_id_t>{object_id_t{101}, object_id_t{102}});
        CHECK(committed.view->retained_selection_range ==
              position_span_t{{measure_id_t{1}, 225}, {measure_id_t{1}, 500}});
        CHECK_FALSE(committed.view->pending_editor_area_selection.has_value());
        CHECK_FALSE(committed.document_changed);
        CHECK(history.document.revision == 1);
    }

    auto history = make_selection_history();
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 4, 192.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(
        dispatch_editor_action(history, state, select_editor_notes_action_t{{object_id_t{102}}}));
    const auto select_first = [&](editor_area_selection_mode_t mode) {
        REQUIRE(dispatch_editor_action(
            history, state,
            begin_editor_area_selection_action_t{110.0, 46.0, 100.0, 200.0, mode, true}));
        return dispatch_editor_action(history, state,
                                      commit_editor_area_selection_action_t{190.0, 50.0, true});
    };
    const auto added = select_first(editor_area_selection_mode_t::add);
    REQUIRE(added);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{102}, object_id_t{101}});
    const auto toggled = select_first(editor_area_selection_mode_t::toggle);
    REQUIRE(toggled);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{102}});
    const auto replaced = select_first(editor_area_selection_mode_t::replace);
    REQUIRE(replaced);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{110.0, 70.0, 100.0, 200.0,
                                             editor_area_selection_mode_t::replace, true}));
    const auto cleared = dispatch_editor_action(
        history, state, commit_editor_area_selection_action_t{190.0, 75.0, true});
    REQUIRE(cleared);
    CHECK(state.selected_notes.empty());
}

TEST_CASE("Space range selection previews inclusive cross-measure heads, tails, and bodies",
          "[unit][core][interaction][selection][space]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{3}, .name = "Key 2", .display_order = 2});
    chart.measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
        {.id = object_id_t{102},
         .position = {measure_id_t{1}, 480},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{2}, 240}},
        {.id = object_id_t{103},
         .position = {measure_id_t{1}, 720},
         .lane = lane_id_t{2},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{2}, 0}},
        {.id = object_id_t{105},
         .position = {measure_id_t{1}, 120},
         .lane = lane_id_t{3},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{2}, 400}},
    };
    chart.measures.back().notes = {
        {.id = object_id_t{104}, .position = {measure_id_t{2}, 240}, .lane = lane_id_t{2}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                         {measure_id_t{1}, 480}, snaps, 4, 300.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(
        dispatch_editor_action(history, state, select_editor_notes_action_t{{object_id_t{101}}}));

    const auto armed = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    REQUIRE(armed);
    REQUIRE(armed.view->pending_space_range_selection.has_value());
    CHECK(armed.view->pending_space_range_selection->anchor ==
          local_position_t{measure_id_t{1}, 480});
    CHECK(*armed.view->pending_space_range_selection->notes ==
          std::vector<object_id_t>{object_id_t{102}, object_id_t{105}});
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    CHECK(history.document.revision == 1);

    const auto extended = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    REQUIRE(extended);
    REQUIRE(extended.view->pending_space_range_selection.has_value());
    CHECK(extended.view->cursor == local_position_t{measure_id_t{1}, 720});
    CHECK(extended.view->pending_space_range_selection->first_row == 480);
    CHECK(extended.view->pending_space_range_selection->last_row == 720);
    CHECK(*extended.view->pending_space_range_selection->notes ==
          std::vector<object_id_t>{object_id_t{102}, object_id_t{103}, object_id_t{105}});
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});

    const auto committed = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    REQUIRE(committed);
    CHECK(committed.view->selected_notes ==
          std::vector<object_id_t>{object_id_t{102}, object_id_t{103}, object_id_t{105}});
    CHECK(committed.view->retained_selection_range ==
          position_span_t{{measure_id_t{1}, 480}, {measure_id_t{1}, 720}});
    CHECK_FALSE(committed.view->pending_space_range_selection.has_value());
    CHECK_FALSE(committed.document_changed);
    CHECK(history.document.revision == 1);

    // Start later in the chart and move backward. The inclusive reversed range
    // touches both short hold tails, the long hold body, and a tap head.
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::forward}));
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::forward}));
    CHECK(state.cursor == local_position_t{measure_id_t{2}, 240});
    const auto reverse_armed = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    REQUIRE(reverse_armed);
    CHECK(*reverse_armed.view->pending_space_range_selection->notes ==
          std::vector<object_id_t>{object_id_t{102}, object_id_t{104}, object_id_t{105}});
    const auto reversed = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
    REQUIRE(reversed);
    CHECK(reversed.view->cursor == local_position_t{measure_id_t{2}, 0});
    CHECK(reversed.view->pending_space_range_selection->first_row == 960);
    CHECK(reversed.view->pending_space_range_selection->last_row == 1200);
    CHECK(*reversed.view->pending_space_range_selection->notes ==
          std::vector<object_id_t>{object_id_t{102}, object_id_t{103}, object_id_t{104},
                                   object_id_t{105}});
    const auto reverse_committed = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    REQUIRE(reverse_committed);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{102}, object_id_t{103},
                                                           object_id_t{104}, object_id_t{105}});
    CHECK(state.retained_selection_range ==
          position_span_t{{measure_id_t{2}, 0}, {measure_id_t{2}, 240}});
}

TEST_CASE("area and Space selection address scoped BGM, timing, and BGA objects by typed identity",
          "[unit][core][interaction][selection][tracks]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes.push_back({.id = object_id_t{301}, .name = "BGM"});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{201},
                                                 .position = {measure_id_t{1}, 240},
                                                 .source_lane = object_id_t{301}});
    chart.bpms.push_back({.id = object_id_t{202},
                          .position = {measure_id_t{1}, 240},
                          .beats_per_minute = signed_rational_t{120, 1}});
    chart.pauses.push_back({.id = object_id_t{203},
                            .position = {measure_id_t{1}, 240},
                            .kind = pause_kind_t::stop,
                            .duration = signed_rational_t{1, 2}});
    chart.pauses.push_back({.id = object_id_t{212},
                            .position = {measure_id_t{1}, 241},
                            .kind = pause_kind_t::delay,
                            .duration = signed_rational_t{1, 4}});
    chart.warps.push_back(
        {.id = object_id_t{204}, .span = {{measure_id_t{1}, 480}, {measure_id_t{1}, 720}}});
    chart.scrolls.push_back({.id = object_id_t{205},
                             .position = {measure_id_t{1}, 240},
                             .multiplier = signed_rational_t{2, 1}});
    chart.speeds.push_back({.id = object_id_t{206},
                            .position = {measure_id_t{1}, 240},
                            .multiplier = signed_rational_t{3, 2}});
    chart.kiai.push_back({.id = object_id_t{207}, .position = {measure_id_t{1}, 240}});
    chart.timing_anchors.push_back({.id = object_id_t{208}, .position = {measure_id_t{1}, 240}});
    chart.bookmarks.push_back({.id = object_id_t{210},
                               .label = "Beat marker",
                               .anchor = marker_anchor_t::beat,
                               .position = local_position_t{measure_id_t{1}, 240}});
    chart.sections.push_back(
        {.id = object_id_t{211},
         .label = "Section",
         .anchor = marker_anchor_t::beat,
         .beat_span = position_span_t{{measure_id_t{1}, 240}, {measure_id_t{1}, 300}}});
    chart.bookmarks.push_back({.id = object_id_t{213},
                               .label = "Audio marker",
                               .anchor = marker_anchor_t::audio_time,
                               .audio_milliseconds = 1'200});
    chart.sections.push_back(
        {.id = object_id_t{214},
         .label = "Audio section",
         .anchor = marker_anchor_t::audio_time,
         .audio_millisecond_span = std::pair<std::int64_t, std::int64_t>{1'000, 2'000}});
    song.visual_assets.push_back({.id = asset_id_t{302},
                                  .kind = asset_kind_t::image,
                                  .source_path = "background.png",
                                  .width = 1,
                                  .height = 1});
    song.resources.push_back({.id = resource_id_t{303},
                              .name = "background",
                              .asset = asset_id_t{302},
                              .roles = {resource_role_t::bga}});
    chart.bga_events.push_back({.id = object_id_t{209},
                                .position = {measure_id_t{1}, 240},
                                .resource = resource_id_t{303}});
    REQUIRE(validate_document(song).empty());

    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                         {measure_id_t{1}, 240}, snaps, 4, 300.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    const editor_selection_scope_t scope{.notes = false, .bgm = true, .timing = true, .bga = true};
    const auto indexed =
        build_editor_hit_test_index(history.document.charts.front(), history.document.revision);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(indexed));
    const auto& chart_ref = history.document.charts.front();
    const auto row_query = editor_objects_in_row_range(
        chart_ref, std::get<editor_hit_test_index_t>(indexed), 240, 240, scope);
    REQUIRE(std::holds_alternative<std::vector<editor_selection_object_t>>(row_query));
    const auto& row_objects = std::get<std::vector<editor_selection_object_t>>(row_query);
    CHECK(row_objects.size() == 10);
    CHECK(std::ranges::find(row_objects,
                            editor_selection_object_t{editor_selection_object_kind_t::bgm_event,
                                                      object_id_t{201}}) != row_objects.end());
    CHECK(std::ranges::find(row_objects,
                            editor_selection_object_t{editor_selection_object_kind_t::bga_event,
                                                      object_id_t{209}}) != row_objects.end());
    CHECK(editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{55}} !=
          editor_selection_object_t{editor_selection_object_kind_t::bga_event, object_id_t{55}});

    const auto fractional_area = [&](double first_y, double last_y) {
        return editor_objects_in_area_selection(
            chart_ref, std::get<editor_hit_test_index_t>(indexed), *setup.view,
            editor_area_selection_request_t{.start_x_pixels = 120.0,
                                            .start_y_pixels = first_y,
                                            .end_x_pixels = 180.0,
                                            .end_y_pixels = last_y,
                                            .field_left_pixels = 100.0,
                                            .field_width_pixels = 200.0,
                                            .scope = scope});
    };
    const auto just_after_row_240 = fractional_area(48.05, 48.10); // rows 240.25–240.5
    REQUIRE(std::holds_alternative<std::vector<editor_selection_object_t>>(just_after_row_240));
    const auto& fractional_objects =
        std::get<std::vector<editor_selection_object_t>>(just_after_row_240);
    CHECK(std::ranges::none_of(fractional_objects, [](const auto& object) {
        return object.id == object_id_t{201} || object.id == object_id_t{202} ||
               object.id == object_id_t{203} || object.id == object_id_t{205} ||
               object.id == object_id_t{206} || object.id == object_id_t{207} ||
               object.id == object_id_t{208} || object.id == object_id_t{209} ||
               object.id == object_id_t{210};
    }));
    CHECK(std::ranges::any_of(fractional_objects, [](const auto& object) {
        return object.kind == editor_selection_object_kind_t::section &&
               object.id == object_id_t{211};
    }));
    const auto across_row_241 = fractional_area(48.19, 48.21); // rows 240.95–241.05
    REQUIRE(std::holds_alternative<std::vector<editor_selection_object_t>>(across_row_241));
    CHECK(std::ranges::any_of(
        std::get<std::vector<editor_selection_object_t>>(across_row_241), [](const auto& object) {
            return object.kind == editor_selection_object_kind_t::delay_event &&
                   object.id == object_id_t{212};
        }));
    CHECK(std::ranges::none_of(
        std::get<editor_hit_test_index_t>(indexed).available_objects, [](const auto& object) {
            return object.id == object_id_t{213} || object.id == object_id_t{214};
        }));

    const auto area_begin = dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{120.0, 47.0, 100.0, 200.0,
                                             editor_area_selection_mode_t::replace, true, scope});
    REQUIRE(area_begin);
    REQUIRE(area_begin.view->pending_editor_area_selection.has_value());
    REQUIRE(area_begin.view->pending_editor_area_selection->objects != nullptr);
    const auto area_commit = dispatch_editor_action(
        history, state, commit_editor_area_selection_action_t{180.0, 49.0, true});
    REQUIRE(area_commit);
    CHECK(state.selected_notes.empty());
    CHECK(state.selected_objects.size() == 11);
    CHECK_FALSE(area_commit.view->pending_editor_area_selection.has_value());

    const editor_selection_scope_t bgm_only{.notes = false, .bgm = true};
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{
            120.0, 47.0, 100.0, 200.0, editor_area_selection_mode_t::toggle, true, bgm_only}));
    REQUIRE(dispatch_editor_action(history, state,
                                   commit_editor_area_selection_action_t{180.0, 49.0, true}));
    CHECK(state.selected_objects.size() == 10);
    CHECK(std::ranges::none_of(state.selected_objects, [](const auto& object) {
        return object.kind == editor_selection_object_kind_t::bgm_event;
    }));
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{120.0, 47.0, 100.0, 200.0,
                                             editor_area_selection_mode_t::add, true, bgm_only}));
    REQUIRE(dispatch_editor_action(history, state,
                                   commit_editor_area_selection_action_t{180.0, 49.0, true}));
    CHECK(state.selected_objects.size() == 11);

    const auto before_cancel = state.selected_objects;
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{120.0, 120.0, 100.0, 200.0,
                                             editor_area_selection_mode_t::toggle, true, scope}));
    REQUIRE(dispatch_editor_action(history, state, cancel_editor_area_selection_action_t{}));
    CHECK(state.selected_objects == before_cancel);

    REQUIRE(dispatch_editor_action(
        history, state,
        space_range_selection_action_t{editor_area_selection_mode_t::replace, scope}));
    const auto moved = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    REQUIRE(moved);
    REQUIRE(moved.view->pending_space_range_selection.has_value());
    const auto preview = moved.view->pending_space_range_selection->objects;
    REQUIRE(preview != nullptr);
    CHECK(std::ranges::any_of(*preview, [](const auto& object) {
        return object.kind == editor_selection_object_kind_t::warp_event &&
               object.id == object_id_t{204};
    }));
    const auto committed = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    REQUIRE(committed);
    CHECK(std::ranges::any_of(state.selected_objects, [](const auto& object) {
        return object.kind == editor_selection_object_kind_t::bgm_event &&
               object.id == object_id_t{201};
    }));
    CHECK_FALSE(committed.view->pending_space_range_selection.has_value());
}

TEST_CASE("typed object selection validates BGM events and synchronizes note selection",
          "[unit][core][interaction][selection][tracks]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes.push_back({.id = object_id_t{301}, .name = "BGM"});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{201},
                                                 .position = {measure_id_t{1}, 240},
                                                 .source_lane = object_id_t{301}});
    chart.measures.front().notes.push_back(
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{1}});
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);

    const auto bgm_selected =
        dispatch_editor_action(history, state,
                               select_editor_objects_action_t{
                                   {{editor_selection_object_kind_t::bgm_event, object_id_t{201}}},
                                   editor_area_selection_mode_t::replace,
                                   true});
    REQUIRE(bgm_selected);
    REQUIRE(bgm_selected.audition.has_value());
    CHECK(*bgm_selected.audition ==
          editor_object_audition_request_t{
              history.document.revision,
              chart_id_t{1},
              {editor_selection_object_kind_t::bgm_event, object_id_t{201}}});
    CHECK(state.selected_objects ==
          std::vector<editor_selection_object_t>{
              {editor_selection_object_kind_t::bgm_event, object_id_t{201}}});
    CHECK(state.selected_notes.empty());

    const auto note_selected = dispatch_editor_action(
        history, state,
        select_editor_objects_action_t{{{editor_selection_object_kind_t::note, object_id_t{101}}}});
    REQUIRE(note_selected);
    CHECK_FALSE(note_selected.audition.has_value());
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});

    const auto bgm_added =
        dispatch_editor_action(history, state,
                               select_editor_objects_action_t{
                                   {{editor_selection_object_kind_t::bgm_event, object_id_t{201}}},
                                   editor_area_selection_mode_t::add,
                                   true});
    REQUIRE(bgm_added);
    REQUIRE(bgm_added.audition.has_value());
    CHECK(state.selected_objects ==
          std::vector<editor_selection_object_t>{
              {editor_selection_object_kind_t::note, object_id_t{101}},
              {editor_selection_object_kind_t::bgm_event, object_id_t{201}}});
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});

    const auto bgm_toggled =
        dispatch_editor_action(history, state,
                               select_editor_objects_action_t{
                                   {{editor_selection_object_kind_t::bgm_event, object_id_t{201}}},
                                   editor_area_selection_mode_t::toggle,
                                   true});
    REQUIRE(bgm_toggled);
    REQUIRE(bgm_toggled.audition.has_value());
    CHECK(bgm_toggled.audition->object ==
          editor_selection_object_t{editor_selection_object_kind_t::bgm_event, object_id_t{201}});
    CHECK(state.selected_objects == std::vector<editor_selection_object_t>{
                                        {editor_selection_object_kind_t::note, object_id_t{101}}});
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});

    const auto unknown = dispatch_editor_action(
        history, state,
        select_editor_objects_action_t{
            {{editor_selection_object_kind_t::bgm_event, object_id_t{999}}}});
    CHECK_FALSE(unknown);
    CHECK_FALSE(unknown.audition.has_value());
    CHECK(has_code(unknown.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});

    const auto multiple_audition =
        dispatch_editor_action(history, state,
                               select_editor_objects_action_t{
                                   {{editor_selection_object_kind_t::note, object_id_t{101}},
                                    {editor_selection_object_kind_t::bgm_event, object_id_t{201}}},
                                   editor_area_selection_mode_t::replace,
                                   true});
    CHECK_FALSE(multiple_audition);
    CHECK_FALSE(multiple_audition.audition.has_value());
    CHECK(has_code(multiple_audition.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
}

TEST_CASE("selection index handles a sparse 50k BGM track and rejects malformed row queries",
          "[unit][core][interaction][selection][tracks][stress]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 100'000;
    chart.measures.front().bgm_events.reserve(50'000);
    for (std::uint64_t event = 0; event < 50'000; ++event) {
        chart.measures.front().bgm_events.push_back({.id = object_id_t{10'000 + event},
                                                     .position = {measure_id_t{1}, event * 2},
                                                     .source_lane = object_id_t{900}});
    }
    const auto built = build_editor_hit_test_index(chart, 1);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(built));
    const auto& index = std::get<editor_hit_test_index_t>(built);
    const auto result = editor_objects_in_row_range(
        chart, index, 98'000, 98'000, editor_selection_scope_t{.notes = false, .bgm = true});
    REQUIRE(std::holds_alternative<std::vector<editor_selection_object_t>>(result));
    CHECK(std::get<std::vector<editor_selection_object_t>>(result) ==
          std::vector<editor_selection_object_t>{
              {editor_selection_object_kind_t::bgm_event, object_id_t{59'000}}});
    CHECK(std::holds_alternative<diagnostic_list_t>(editor_objects_in_row_range(
        chart, index, 200'001, 200'001, editor_selection_scope_t{.notes = false, .bgm = true})));

    auto malformed = make_song().charts.front();
    malformed.measures.front().bgm_events = {
        {.id = object_id_t{700}, .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{900}},
        {.id = object_id_t{700},
         .position = {measure_id_t{1}, 240},
         .source_lane = object_id_t{900}},
    };
    const auto rejected = build_editor_hit_test_index(malformed, 1);
    REQUIRE(std::holds_alternative<diagnostic_list_t>(rejected));
    CHECK(has_code(std::get<diagnostic_list_t>(rejected), diagnostic_code_t::duplicate_id));
}

TEST_CASE("Space range selection supports add and toggle and cancels safely",
          "[unit][core][interaction][selection][space][cancel]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{2}},
    };
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                         {measure_id_t{1}, 240}, snaps, 4, 192.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(
        dispatch_editor_action(history, state, select_editor_notes_action_t{{object_id_t{101}}}));

    REQUIRE(dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::add}));
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    const auto finer_snap = dispatch_editor_action(
        history, state, cycle_edit_snap_action_t{interaction_direction_t::forward});
    REQUIRE(finer_snap);
    CHECK(finer_snap.view->active_snap_denominator == 8);
    CHECK(finer_snap.view->pending_space_range_selection->anchor ==
          local_position_t{measure_id_t{1}, 240});
    CHECK(finer_snap.view->pending_space_range_selection->first_row == 240);
    CHECK(finer_snap.view->pending_space_range_selection->last_row == 240);
    const auto first_step = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    REQUIRE(first_step);
    CHECK(first_step.view->cursor == local_position_t{measure_id_t{1}, 360});
    CHECK(*first_step.view->pending_space_range_selection->notes ==
          std::vector<object_id_t>{object_id_t{101}});
    const auto second_step = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    REQUIRE(second_step);
    CHECK(second_step.view->cursor == local_position_t{measure_id_t{1}, 480});
    CHECK(*second_step.view->pending_space_range_selection->notes ==
          std::vector<object_id_t>{object_id_t{101}, object_id_t{102}});
    const auto added = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    REQUIRE(added);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}, object_id_t{102}});

    REQUIRE(dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::toggle}));
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::backward}));
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::backward}));
    const auto toggled = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    REQUIRE(toggled);
    CHECK(state.selected_notes.empty());

    REQUIRE(
        dispatch_editor_action(history, state, select_editor_notes_action_t{{object_id_t{101}}}));
    REQUIRE(dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace}));
    const auto cancelled = dispatch_editor_action(
        history, state, cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::focus_lost});
    REQUIRE(cancelled);
    CHECK_FALSE(state.pending_space_range_selection.has_value());
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});

    REQUIRE(dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace}));
    auto external_edit = history.document;
    external_edit.charts.front().lane_layouts.front().layout.display_name = "Changed layout";
    REQUIRE(commit_document_command(history, std::move(external_edit), "Change lane layout"));
    const auto stale = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    CHECK(state.pending_space_range_selection.has_value());
    REQUIRE(dispatch_editor_action(
        history, state, cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::escape}));
    CHECK_FALSE(state.pending_space_range_selection.has_value());
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
}

TEST_CASE("Space range selection uses span indexes for a sparse 50k-note chart",
          "[unit][core][interaction][selection][space][stress]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 100'000;
    chart.measures.front().notes.reserve(50'000);
    for (std::uint64_t note = 0; note < 50'000; ++note) {
        chart.measures.front().notes.push_back({
            .id = object_id_t{1'000 + note},
            .position = {measure_id_t{1}, note * 2},
            .lane = lane_id_t{1},
        });
    }
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                         {measure_id_t{1}, 24'000}, snaps, 4, 1200.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    const auto selected = dispatch_editor_action(
        history, state, space_range_selection_action_t{editor_area_selection_mode_t::replace});
    REQUIRE(selected);
    REQUIRE(selected.view->pending_space_range_selection.has_value());
    CHECK(*selected.view->pending_space_range_selection->notes ==
          std::vector<object_id_t>{object_id_t{13'000}});
    CHECK(state.selected_notes.empty());
}

TEST_CASE("area selection maps through zoom and scroll and intersects cross-measure hold bodies",
          "[unit][core][interaction][selection][area][mapping]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 480},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{2}, 480}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 4, 300.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    const auto scrolled_before_zoom =
        dispatch_editor_action(history, state, scroll_editor_view_action_t{48.0});
    REQUIRE(scrolled_before_zoom);
    CHECK(scrolled_before_zoom.view->viewport_scroll_pixels == 48.0);
    const auto zoomed =
        dispatch_editor_action(history, state, adjust_editor_zoom_action_t{2.0, 100.0});
    REQUIRE(zoomed);
    const auto& chart = history.document.charts.front();
    const auto body_start = editor_position_y_pixels(chart, *zoomed.view, {measure_id_t{1}, 900});
    const auto body_end = editor_position_y_pixels(chart, *zoomed.view, {measure_id_t{2}, 40});
    REQUIRE(body_start.has_value());
    REQUIRE(body_end.has_value());
    CHECK(*body_start < *body_end);
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{110.0, *body_start, 100.0, 200.0,
                                             editor_area_selection_mode_t::replace, true}));
    const auto selected_body = dispatch_editor_action(
        history, state, commit_editor_area_selection_action_t{190.0, *body_end, true});
    REQUIRE(selected_body);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    CHECK(history.document.revision == 1);

    const auto scrolled =
        dispatch_editor_action(history, state, scroll_editor_view_action_t{150.0});
    REQUIRE(scrolled);
    const auto tail_y = editor_position_y_pixels(chart, *scrolled.view, {measure_id_t{2}, 480});
    REQUIRE(tail_y.has_value());
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{110.0, *tail_y, 100.0, 200.0,
                                             editor_area_selection_mode_t::replace, true}));
    const auto selected_tail = dispatch_editor_action(
        history, state, commit_editor_area_selection_action_t{190.0, *tail_y, true});
    REQUIRE(selected_tail);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
}

TEST_CASE("area selection capture loss cancels and stale revisions preserve committed selection",
          "[unit][core][interaction][selection][area][cancel]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{2}},
    };
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 4, 192.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(
        dispatch_editor_action(history, state, select_editor_notes_action_t{{object_id_t{101}}}));
    const auto failed_begin = dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{210.0, 95.0, 100.0, 200.0,
                                             editor_area_selection_mode_t::replace, false});
    CHECK_FALSE(failed_begin);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    CHECK_FALSE(state.pending_editor_area_selection.has_value());

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{210.0, 95.0, 100.0, 200.0,
                                             editor_area_selection_mode_t::replace, true}));
    const auto capture_lost = dispatch_editor_action(
        history, state, update_editor_area_selection_action_t{290.0, 100.0, false});
    REQUIRE(capture_lost);
    CHECK_FALSE(state.pending_editor_area_selection.has_value());
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{210.0, 95.0, 100.0, 200.0,
                                             editor_area_selection_mode_t::replace, true}));
    const auto capture_lost_on_release = dispatch_editor_action(
        history, state, commit_editor_area_selection_action_t{290.0, 100.0, false});
    REQUIRE(capture_lost_on_release);
    CHECK_FALSE(state.pending_editor_area_selection.has_value());
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_editor_area_selection_action_t{210.0, 95.0, 100.0, 200.0,
                                             editor_area_selection_mode_t::replace, true}));
    auto external_edit = history.document;
    external_edit.metadata.title = "revision change";
    REQUIRE(commit_document_command(history, std::move(external_edit), "External edit"));
    const auto stale = dispatch_editor_action(
        history, state, commit_editor_area_selection_action_t{290.0, 100.0, true});
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    CHECK(state.pending_editor_area_selection.has_value());
    REQUIRE(dispatch_editor_action(history, state, cancel_editor_area_selection_action_t{}));
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}});
    CHECK_FALSE(state.pending_editor_area_selection.has_value());
}

TEST_CASE("area selection narrows a 50k-note query to a sparse row interval",
          "[unit][core][interaction][selection][area][stress]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 100'000;
    chart.measures.front().notes.reserve(50'000);
    for (std::uint64_t note = 0; note < 50'000; ++note) {
        chart.measures.front().notes.push_back({
            .id = object_id_t{1'000 + note},
            .position = {measure_id_t{1}, note * 2},
            .lane = lane_id_t{1},
        });
    }
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 4, 1200.0);
    REQUIRE(setup);
    const auto built =
        build_editor_hit_test_index(history.document.charts.front(), history.document.revision);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(built));
    const auto y = editor_position_y_pixels(history.document.charts.front(), *setup.view,
                                            {measure_id_t{1}, 24'000});
    REQUIRE(y.has_value());
    const editor_area_selection_request_t request{
        .start_x_pixels = 110.0,
        .start_y_pixels = *y - 1.0e-9,
        .end_x_pixels = 190.0,
        .end_y_pixels = *y + 1.0e-9,
        .field_left_pixels = 100.0,
        .field_width_pixels = 200.0,
    };
    const auto selected = editor_notes_in_area_selection(history.document.charts.front(),
                                                         std::get<editor_hit_test_index_t>(built),
                                                         *setup.view, request);
    REQUIRE(std::holds_alternative<std::vector<object_id_t>>(selected));
    CHECK(std::get<std::vector<object_id_t>>(selected) ==
          std::vector<object_id_t>{object_id_t{13'000}});
}

TEST_CASE("note paint interpolates snap rows, suppresses duplicates, and commits atomically",
          "[unit][core][interaction][paint]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, true}));
    const auto preview = dispatch_editor_action(
        history, state,
        update_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 480}, true});
    REQUIRE(preview);
    REQUIRE(preview.view->pending_note_paint_gesture.has_value());
    REQUIRE(preview.view->pending_note_paint_gesture->targets != nullptr);
    CHECK(preview.view->pending_note_paint_gesture->targets->size() == 3);
    REQUIRE(dispatch_editor_action(
        history, state,
        update_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 480}, true}));
    CHECK(state.pending_note_paint_gesture->targets->size() == 3);
    CHECK(note_count(history) == 0);
    CHECK(history.document.revision == 1);

    const auto committed = dispatch_editor_action(
        history, state,
        end_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 480}, true});
    REQUIRE(committed);
    CHECK(committed.document_changed);
    CHECK_FALSE(state.pending_note_paint_gesture.has_value());
    CHECK(note_count(history) == 3);
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().measures.front().notes[0].position.row == 0);
    CHECK(history.document.charts.front().measures.front().notes[1].position.row == 240);
    CHECK(history.document.charts.front().measures.front().notes[2].position.row == 480);
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 0);
}

TEST_CASE("note painting rejects active area and Space range selection gestures",
          "[unit][core][interaction][paint][selection]") {
    for (const auto selection_kind : {0, 1}) {
        auto history = make_document_history(make_song());
        auto state = make_state(history);
        state.viewport_height_pixels = 200.0;
        if (selection_kind == 0) {
            REQUIRE(dispatch_editor_action(
                history, state,
                begin_editor_area_selection_action_t{110.0, 95.0, 100.0, 200.0,
                                                     editor_area_selection_mode_t::replace, true}));
            REQUIRE(state.pending_editor_area_selection.has_value());
        } else {
            REQUIRE(dispatch_editor_action(
                history, state,
                space_range_selection_action_t{editor_area_selection_mode_t::replace}));
            REQUIRE(state.pending_space_range_selection.has_value());
        }

        const auto rejected = dispatch_editor_action(
            history, state,
            begin_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 240}, true});
        CHECK_FALSE(rejected);
        CHECK(has_code(rejected.diagnostics, diagnostic_code_t::invalid_hold));
        CHECK_FALSE(state.pending_note_paint_gesture.has_value());
        CHECK(state.pending_editor_area_selection.has_value() == (selection_kind == 0));
        CHECK(state.pending_space_range_selection.has_value() == (selection_kind == 1));
        CHECK(note_count(history) == 0);
    }
}

TEST_CASE("semantic-layer paint visits notes once and commits one undoable toggle",
          "[unit][core][interaction][semantic-layer][paint]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.semantic_layers = {{.id = layer_id_t{7}, .name = "Phrase", .color = {255, 96, 48, 255}}};
    chart.measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{1},
         .kind = note_kind_t::tap},
        {.id = object_id_t{102},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::tap},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_semantic_layer_paint_gesture_action_t{layer_id_t{7}, object_id_t{101}, true}));
    REQUIRE(state.pending_semantic_layer_paint_gesture.has_value());
    REQUIRE(dispatch_editor_action(
        history, state, update_semantic_layer_paint_gesture_action_t{object_id_t{102}, true}));
    REQUIRE(dispatch_editor_action(
        history, state, update_semantic_layer_paint_gesture_action_t{object_id_t{102}, true}));
    CHECK(state.pending_semantic_layer_paint_gesture->notes->size() == 2);
    CHECK(history.document.revision == 1);

    const auto committed = dispatch_editor_action(
        history, state, end_semantic_layer_paint_gesture_action_t{object_id_t{}, true});
    REQUIRE(committed);
    CHECK(committed.document_changed);
    CHECK_FALSE(state.pending_semantic_layer_paint_gesture.has_value());
    CHECK(history.undo.size() == 1);
    CHECK(find_note(history, object_id_t{101})->semantic_layers == std::vector{layer_id_t{7}});
    CHECK(find_note(history, object_id_t{102})->semantic_layers == std::vector{layer_id_t{7}});

    REQUIRE(undo_document_command(history));
    CHECK(find_note(history, object_id_t{101})->semantic_layers.empty());
    CHECK(find_note(history, object_id_t{102})->semantic_layers.empty());
}

TEST_CASE("semantic-layer paint requires an editable visible layer and cancels on capture loss",
          "[unit][core][interaction][semantic-layer][paint][cancel]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.semantic_layers = {
        {.id = layer_id_t{7}, .name = "Hidden", .color = {255, 96, 48, 255}, .visible = false},
        {.id = layer_id_t{8}, .name = "Locked", .color = {48, 96, 255, 255}, .locked = true},
        {.id = layer_id_t{9}, .name = "Paint", .color = {96, 255, 48, 255}},
    };
    chart.measures.front().notes = {{.id = object_id_t{101},
                                     .position = {measure_id_t{1}, 0},
                                     .lane = lane_id_t{1},
                                     .kind = note_kind_t::tap}};
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);

    const auto hidden = dispatch_editor_action(
        history, state,
        begin_semantic_layer_paint_gesture_action_t{layer_id_t{7}, object_id_t{101}, true});
    CHECK_FALSE(hidden);
    CHECK(has_code(hidden.diagnostics, diagnostic_code_t::invalid_id));
    const auto locked = dispatch_editor_action(
        history, state,
        begin_semantic_layer_paint_gesture_action_t{layer_id_t{8}, object_id_t{101}, true});
    CHECK_FALSE(locked);
    CHECK(has_code(locked.diagnostics, diagnostic_code_t::invalid_id));

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_semantic_layer_paint_gesture_action_t{layer_id_t{9}, object_id_t{101}, true}));
    const auto capture_lost = dispatch_editor_action(
        history, state, update_semantic_layer_paint_gesture_action_t{object_id_t{101}, false});
    REQUIRE(capture_lost);
    CHECK_FALSE(state.pending_semantic_layer_paint_gesture.has_value());
    CHECK(history.undo.empty());
    CHECK(find_note(history, object_id_t{101})->semantic_layers.empty());
}

TEST_CASE("single-cell paint toggles taps and rejects hold collisions atomically",
          "[unit][core][interaction][paint]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);
    const auto click = [&] {
        REQUIRE(dispatch_editor_action(
            history, state,
            begin_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 240}, true}));
        return dispatch_editor_action(
            history, state,
            end_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 240}, true});
    };
    const auto created = click();
    REQUIRE(created);
    CHECK(note_count(history) == 1);
    const auto removed = click();
    REQUIRE(removed);
    CHECK(removed.document_changed);
    CHECK(note_count(history) == 0);
    CHECK(history.undo.size() == 2);
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 1);

    auto colliding_song = make_song();
    colliding_song.charts.front().measures.front().notes.push_back({
        .id = object_id_t{101},
        .position = {measure_id_t{1}, 240},
        .lane = lane_id_t{1},
        .kind = note_kind_t::hold,
        .end = local_position_t{measure_id_t{1}, 720},
    });
    REQUIRE(validate_document(colliding_song).empty());
    auto colliding_history = make_document_history(std::move(colliding_song));
    auto colliding_state = make_state(colliding_history);
    REQUIRE(dispatch_editor_action(
        colliding_history, colliding_state,
        begin_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, true}));
    REQUIRE(dispatch_editor_action(
        colliding_history, colliding_state,
        update_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 480}, true}));
    const auto collision = dispatch_editor_action(
        colliding_history, colliding_state,
        end_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 480}, true});
    CHECK_FALSE(collision);
    CHECK(has_code(collision.diagnostics, diagnostic_code_t::collision));
    CHECK(colliding_history.document.revision == 1);
    CHECK(colliding_history.undo.empty());
    CHECK(note_count(colliding_history) == 1);
    CHECK(colliding_state.pending_note_paint_gesture.has_value());
}

TEST_CASE("paint gestures cancel on capture loss and leave previews intact on cap rejection",
          "[unit][core][interaction][paint][cancel]") {
    auto song = make_song();
    song.charts.front().divisions_per_standard_measure = 100'001;
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{100001}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 100'001);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, true}));
    const auto capped = dispatch_editor_action(
        history, state,
        update_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 50'000}, true});
    CHECK_FALSE(capped);
    CHECK(has_code(capped.diagnostics, diagnostic_code_t::invalid_resolution));
    REQUIRE(state.pending_note_paint_gesture.has_value());
    CHECK(state.pending_note_paint_gesture->targets->size() == 1);
    CHECK(note_count(history) == 0);
    CHECK(history.document.revision == 1);

    const auto capture_lost = dispatch_editor_action(
        history, state,
        update_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, false});
    REQUIRE(capture_lost);
    CHECK_FALSE(state.pending_note_paint_gesture.has_value());
    REQUIRE(dispatch_editor_action(
        history, state,
        begin_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, true}));
    REQUIRE(dispatch_editor_action(
        history, state,
        cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::focus_lost}));
    CHECK_FALSE(state.pending_note_paint_gesture.has_value());

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, true}));
    auto external_edit = history.document;
    external_edit.metadata.title = "external change";
    REQUIRE(commit_document_command(history, std::move(external_edit), "External edit"));
    const auto stale = dispatch_editor_action(
        history, state,
        end_note_paint_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 240}, true});
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(history.document.revision == 2);
    CHECK(note_count(history) == 0);
    CHECK(history.undo.size() == 1);
    CHECK(state.pending_note_paint_gesture.has_value());
    REQUIRE(dispatch_editor_action(
        history, state, cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::escape}));
    CHECK_FALSE(state.pending_note_paint_gesture.has_value());
}

TEST_CASE("hit-test buckets handle dense measures and 50k notes within a discrete safety ceiling",
          "[unit][core][interaction][hit-test][stress]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 100'000;
    chart.measures.front().notes.reserve(50'000);
    for (std::uint64_t index = 0; index < 50'000; ++index) {
        chart.measures.front().notes.push_back({
            .id = object_id_t{1'000 + index},
            .position = {measure_id_t{1}, index * 2},
            .lane = lane_id_t{1},
            .kind = note_kind_t::tap,
        });
    }
    chart.measures.back().notes.push_back({
        .id = object_id_t{60'000},
        .position = {measure_id_t{2}, 600},
        .lane = lane_id_t{1},
        .kind = note_kind_t::tap,
    });
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{100000}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 100'000, 200.0);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    const auto scrolled =
        dispatch_editor_action(history, state, scroll_editor_view_action_t{10'000.0});
    REQUIRE(scrolled);

    // This measures one-time index construction plus one pointer query against
    // the 125 ms discrete-interaction guard. It does not claim a 16.7 ms frame
    // budget for repeated rendering/drag updates.
    const auto started = std::chrono::steady_clock::now();
    const auto built =
        build_editor_hit_test_index(history.document.charts.front(), history.document.revision);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(built));
    const auto& index = std::get<editor_hit_test_index_t>(built);
    REQUIRE(index.measure_buckets.size() == 2);
    CHECK(index.measure_buckets.front().size() == 50'000);
    CHECK(index.measure_buckets.back().size() == 1);
    const auto y = editor_position_y_pixels(history.document.charts.front(), *scrolled.view,
                                            {measure_id_t{2}, 600});
    REQUIRE(y.has_value());
    const auto hit = hit_test_editor(history.document.charts.front(), index, *scrolled.view,
                                     {.x_pixels = 150.0,
                                      .y_pixels = *y,
                                      .field_left_pixels = 100.0,
                                      .field_width_pixels = 200.0,
                                      .note_hit_radius_pixels = 2.0});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(hit.has_value());
    CHECK(hit->note == object_id_t{60'000});
    CHECK(elapsed < std::chrono::milliseconds{125});
}

TEST_CASE("interaction rejects invalid targets and occupied non-tap notes atomically",
          "[unit][core][interaction][negative]") {
    auto song = make_song();
    song.charts.front().measures.front().notes.push_back({
        .id = object_id_t{101},
        .position = {measure_id_t{1}, 240},
        .lane = lane_id_t{1},
        .kind = note_kind_t::hold,
        .end = local_position_t{measure_id_t{1}, 720},
    });
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);

    const auto invalid =
        dispatch_editor_action(history, state, place_tap_action_t{{measure_id_t{99}, 0}});
    CHECK_FALSE(invalid);
    CHECK(has_code(invalid.diagnostics, diagnostic_code_t::row_out_of_range));
    CHECK(history.document.revision == 1);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
    CHECK(state.cursor == local_position_t{measure_id_t{1}, 840});

    const auto invalid_zoom =
        dispatch_editor_action(history, state, adjust_editor_zoom_action_t{0.0});
    CHECK_FALSE(invalid_zoom);
    CHECK(state.pixels_per_standard_measure == default_pixels_per_standard_measure);
    CHECK(history.document.revision == 1);

    const auto occupied =
        dispatch_editor_action(history, state, place_tap_action_t{{measure_id_t{1}, 480}});
    CHECK_FALSE(occupied);
    CHECK(has_code(occupied.diagnostics, diagnostic_code_t::collision));
    CHECK(history.document.revision == 1);
    CHECK(history.undo.empty());
    CHECK(history.document.charts.front().measures.front().notes.front().kind == note_kind_t::hold);

    history.document.charts.front().measures.front().notes.front().end =
        local_position_t{measure_id_t{2}, 240};
    const auto cross_measure_body =
        dispatch_editor_action(history, state, place_tap_action_t{{measure_id_t{2}, 0}});
    CHECK_FALSE(cross_measure_body);
    CHECK(has_code(cross_measure_body.diagnostics, diagnostic_code_t::collision));
    CHECK(history.document.revision == 1);
}

TEST_CASE("interaction setup rejects unavailable lanes and falls back to supported exact snaps",
          "[unit][core][interaction]") {
    auto history = make_document_history(make_song());
    constexpr std::array snaps{std::uint64_t{7}, std::uint64_t{6}, std::uint64_t{8}};

    const auto fallback = make_editor_interaction(history, chart_id_t{1}, lane_id_t{2},
                                                  {measure_id_t{1}, 0}, snaps, 7);
    REQUIRE(fallback);
    CHECK(fallback.state->active_snap_denominator == 6);
    CHECK(fallback.state->active_snap_was_fallback);
    CHECK(fallback.view->active_lane_name == "Scratch");

    const auto missing_lane = make_editor_interaction(history, chart_id_t{1}, lane_id_t{99},
                                                      {measure_id_t{1}, 0}, snaps, 8);
    CHECK_FALSE(missing_lane);
    CHECK(has_code(missing_lane.diagnostics, diagnostic_code_t::unknown_lane));

    const auto invalid_cursor = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                                        {measure_id_t{2}, 960}, snaps, 8);
    CHECK_FALSE(invalid_cursor);
    CHECK(has_code(invalid_cursor.diagnostics, diagnostic_code_t::row_out_of_range));
}

TEST_CASE("interaction crosses lane-layout transitions and exposes explicit lane selection",
          "[unit][core][interaction][layout]") {
    auto song = make_song();
    song.charts.front().lane_layouts.push_back({
        .id = object_id_t{101},
        .position = {measure_id_t{2}, 0},
        .layout =
            {
                .profile_id = "interaction-test-second",
                .lanes = {{.id = lane_id_t{3}, .name = "New Key 1", .display_order = 0}},
            },
    });
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);

    const auto crossed = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    REQUIRE(crossed);
    REQUIRE(crossed.view.has_value());
    CHECK(crossed.view->cursor == local_position_t{measure_id_t{2}, 0});
    CHECK(crossed.view->active_lane == lane_id_t{3});
    CHECK(crossed.view->active_lane_name == "New Key 1");

    const auto unavailable =
        dispatch_editor_action(history, state, select_editor_lane_action_t{lane_id_t{1}});
    CHECK_FALSE(unavailable);
    CHECK(state.active_lane == lane_id_t{3});
    CHECK(history.document.revision == 1);
}

TEST_CASE("exact cursor jumps preserve authored rows and transition lanes atomically",
          "[unit][core][interaction][navigation][layout]") {
    auto song = make_song();
    song.charts.front().lane_layouts.push_back({
        .id = object_id_t{101},
        .position = {measure_id_t{2}, 0},
        .layout =
            {
                .profile_id = "jump-target-layout",
                .lanes = {{.id = lane_id_t{3}, .name = "New Key 1", .display_order = 0}},
            },
    });
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);

    const auto unsnapped = dispatch_editor_action(
        history, state,
        jump_edit_cursor_action_t{{measure_id_t{1}, 137}, history.document.revision});
    REQUIRE(unsnapped);
    REQUIRE(unsnapped.view.has_value());
    CHECK(unsnapped.view->cursor == local_position_t{measure_id_t{1}, 137});
    CHECK(unsnapped.view->active_lane == lane_id_t{1});
    CHECK(history.document.revision == 1);

    const auto crossed = dispatch_editor_action(
        history, state,
        jump_edit_cursor_action_t{{measure_id_t{2}, 301}, history.document.revision});
    REQUIRE(crossed);
    REQUIRE(crossed.view.has_value());
    CHECK(crossed.view->cursor == local_position_t{measure_id_t{2}, 301});
    CHECK(crossed.view->active_lane == lane_id_t{3});
    CHECK(crossed.view->active_lane_name == "New Key 1");
    CHECK(history.document.revision == 1);
}

TEST_CASE("exact cursor jumps reject stale and out-of-chart targets without changing state",
          "[unit][core][interaction][navigation][negative]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);
    const auto original = state;
    const auto queried_revision = history.document.revision;
    auto externally_edited = history.document;
    externally_edited.metadata.title = "Changed after marker query";
    REQUIRE(commit_document_command(history, std::move(externally_edited), "Rename song"));

    const auto stale = dispatch_editor_action(
        history, state, jump_edit_cursor_action_t{{measure_id_t{1}, 120}, queried_revision});
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(state.cursor == original.cursor);
    CHECK(state.active_lane == original.active_lane);

    const auto missing_measure = dispatch_editor_action(
        history, state,
        jump_edit_cursor_action_t{{measure_id_t{99}, 0}, history.document.revision});
    CHECK_FALSE(missing_measure);
    CHECK(has_code(missing_measure.diagnostics, diagnostic_code_t::row_out_of_range));
    CHECK(state.cursor == original.cursor);
    CHECK(state.active_lane == original.active_lane);

    const auto out_of_range = dispatch_editor_action(
        history, state,
        jump_edit_cursor_action_t{{measure_id_t{2}, 960}, history.document.revision});
    CHECK_FALSE(out_of_range);
    CHECK(has_code(out_of_range.diagnostics, diagnostic_code_t::row_out_of_range));
    CHECK(state.cursor == original.cursor);
    CHECK(state.active_lane == original.active_lane);
    CHECK(history.document.revision == queried_revision + 1);
}

TEST_CASE("pointer zoom anchors chart rows and viewport scrolling clamps to content",
          "[unit][core][interaction][viewport]") {
    auto history = make_document_history(make_song());
    constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}, std::uint64_t{6}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 4, 400.0);
    REQUIRE(setup);
    auto state = *setup.state;

    const auto zoomed =
        dispatch_editor_action(history, state, adjust_editor_zoom_action_t{2.0, 100.0});
    REQUIRE(zoomed);
    REQUIRE(zoomed.view.has_value());
    CHECK(zoomed.view->viewport_scroll_pixels == 100.0);
    const auto anchored = editor_position_y_pixels(history.document.charts.front(), *zoomed.view,
                                                   {measure_id_t{1}, 500});
    REQUIRE(anchored.has_value());
    CHECK(*anchored == 100.0);

    const auto bottom =
        dispatch_editor_action(history, state, scroll_editor_view_action_t{10'000.0});
    REQUIRE(bottom);
    CHECK(bottom.view->viewport_scroll_pixels == 368.0);
    const auto top = dispatch_editor_action(history, state, scroll_editor_view_action_t{-10'000.0});
    REQUIRE(top);
    CHECK(top.view->viewport_scroll_pixels == 0.0);

    state.pixels_per_standard_measure = std::numeric_limits<double>::quiet_NaN();
    const auto invalid = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    CHECK_FALSE(invalid);
    CHECK(has_code(invalid.diagnostics, diagnostic_code_t::invalid_resolution));
}

TEST_CASE("two-step hold placement previews, commits across measures, and cancels atomically",
          "[unit][core][interaction][hold]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);

    const auto begun =
        dispatch_editor_action(history, state, begin_hold_action_t{{measure_id_t{1}, 721}});
    REQUIRE(begun);
    CHECK_FALSE(begun.document_changed);
    REQUIRE(begun.view.has_value());
    REQUIRE(begun.view->pending_hold.has_value());
    CHECK(begun.view->pending_hold->position == local_position_t{measure_id_t{1}, 720});
    CHECK(begun.view->pending_hold->lane == lane_id_t{1});
    CHECK(history.document.revision == 1);
    CHECK(note_count(history) == 0);

    const auto previewed = dispatch_editor_action(
        history, state, update_hold_preview_action_t{{measure_id_t{2}, 241}});
    REQUIRE(previewed);
    CHECK_FALSE(previewed.document_changed);
    REQUIRE(previewed.view->pending_hold->preview_end.has_value());
    CHECK(*previewed.view->pending_hold->preview_end == local_position_t{measure_id_t{2}, 240});
    CHECK(note_count(history) == 0);

    const auto committed =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{2}, 241}});
    REQUIRE(committed);
    CHECK(committed.document_changed);
    REQUIRE(committed.view.has_value());
    CHECK_FALSE(committed.view->pending_hold.has_value());
    CHECK(committed.view->cursor == local_position_t{measure_id_t{2}, 240});
    REQUIRE(note_count(history) == 1);
    const auto& hold = history.document.charts.front().measures.front().notes.front();
    CHECK(hold.kind == note_kind_t::hold);
    CHECK(hold.position == local_position_t{measure_id_t{1}, 720});
    REQUIRE(hold.end.has_value());
    CHECK(*hold.end == local_position_t{measure_id_t{2}, 240});
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 0);
    REQUIRE(redo_document_command(history));
    CHECK(note_count(history) == 1);

    const auto second_begin =
        dispatch_editor_action(history, state, begin_hold_action_t{{measure_id_t{1}, 0}});
    REQUIRE(second_begin);
    const auto merged =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{2}, 0}});
    REQUIRE(merged);
    CHECK_FALSE(state.pending_hold.has_value());
    CHECK(history.document.revision == 3);
    CHECK(note_count(history) == 1);
    const auto* merged_note = find_note(history, object_id_t{102});
    REQUIRE(merged_note != nullptr);
    CHECK(merged_note->position == local_position_t{measure_id_t{1}, 0});
    CHECK(merged_note->end == local_position_t{measure_id_t{2}, 240});

    const auto cancelled = dispatch_editor_action(history, state, cancel_hold_action_t{});
    REQUIRE(cancelled);
    CHECK_FALSE(state.pending_hold.has_value());
    CHECK_FALSE(cancelled.document_changed);
    CHECK(history.document.revision == 3);

    const auto no_start =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{2}, 480}});
    CHECK_FALSE(no_start);
    CHECK(has_code(no_start.diagnostics, diagnostic_code_t::invalid_hold));
}

TEST_CASE("hold placement moves its head before the fixed anchor and normalizes collisions",
          "[unit][core][interaction][hold][union]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);

    REQUIRE(dispatch_editor_action(history, state, begin_hold_action_t{{measure_id_t{1}, 720}}));
    const auto reverse_preview = dispatch_editor_action(
        history, state, update_hold_preview_action_t{{measure_id_t{1}, 480}});
    REQUIRE(reverse_preview);
    REQUIRE(reverse_preview.view->pending_hold->preview_end.has_value());
    CHECK(*reverse_preview.view->pending_hold->preview_end ==
          local_position_t{measure_id_t{1}, 480});
    REQUIRE(reverse_preview.view->pending_hold->placement_preview.has_value());
    CHECK(reverse_preview.view->pending_hold->placement_preview->head ==
          local_position_t{measure_id_t{1}, 480});
    CHECK(reverse_preview.view->pending_hold->placement_preview->tail ==
          local_position_t{measure_id_t{1}, 720});
    const auto reverse_commit =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{1}, 480}});
    REQUIRE(reverse_commit);
    const auto* reverse_hold = find_note(history, object_id_t{101});
    REQUIRE(reverse_hold != nullptr);
    CHECK(reverse_hold->position == local_position_t{measure_id_t{1}, 480});
    CHECK(reverse_hold->end == local_position_t{measure_id_t{1}, 720});

    auto union_song = make_song();
    union_song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 240}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{1}},
        {.id = object_id_t{103},
         .position = {measure_id_t{1}, 720},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{2}, 0}},
    };
    auto union_history = make_document_history(std::move(union_song));
    auto union_state = make_state(union_history);
    REQUIRE(dispatch_editor_action(union_history, union_state,
                                   begin_hold_action_t{{measure_id_t{1}, 240}}));
    const auto union_preview = dispatch_editor_action(
        union_history, union_state, update_hold_preview_action_t{{measure_id_t{1}, 720}});
    REQUIRE(union_preview);
    REQUIRE(union_preview.view->pending_hold->placement_preview.has_value());
    const auto& placement = *union_preview.view->pending_hold->placement_preview;
    CHECK(placement.adds_note);
    CHECK(placement.head == local_position_t{measure_id_t{1}, 0});
    CHECK(placement.tail == local_position_t{measure_id_t{2}, 0});
    REQUIRE(placement.removals);
    CHECK(*placement.removals ==
          std::vector<object_id_t>{object_id_t{101}, object_id_t{102}, object_id_t{103}});
    const auto union_commit = dispatch_editor_action(union_history, union_state,
                                                     commit_hold_action_t{{measure_id_t{1}, 720}});
    REQUIRE(union_commit);
    REQUIRE(note_count(union_history) == 1);
    const auto* union_hold = find_note(union_history, object_id_t{104});
    REQUIRE(union_hold != nullptr);
    CHECK(union_hold->position == local_position_t{measure_id_t{1}, 0});
    CHECK(union_hold->end == local_position_t{measure_id_t{2}, 0});
    REQUIRE(undo_document_command(union_history));
    CHECK(note_count(union_history) == 3);
    CHECK(find_note(union_history, object_id_t{101}) != nullptr);
    CHECK(find_note(union_history, object_id_t{102}) != nullptr);
    CHECK(find_note(union_history, object_id_t{103}) != nullptr);

    auto conflicting_song = make_song();
    conflicting_song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 480},
         .lane = lane_id_t{1},
         .kind = note_kind_t::mine},
    };
    auto conflicting_history = make_document_history(std::move(conflicting_song));
    auto conflicting_state = make_state(conflicting_history);
    REQUIRE(dispatch_editor_action(conflicting_history, conflicting_state,
                                   begin_hold_action_t{{measure_id_t{1}, 0}}));
    const auto conflict = dispatch_editor_action(conflicting_history, conflicting_state,
                                                 commit_hold_action_t{{measure_id_t{1}, 480}});
    CHECK_FALSE(conflict);
    CHECK(has_code(conflict.diagnostics, diagnostic_code_t::collision));
    CHECK(conflicting_state.pending_hold.has_value());
    CHECK(conflicting_history.document.revision == 1);
    CHECK(note_count(conflicting_history) == 1);

    auto metadata_song = make_song();
    metadata_song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 480},
         .lane = lane_id_t{1},
         .label = std::string{"keep this label"}},
    };
    auto metadata_history = make_document_history(std::move(metadata_song));
    auto metadata_state = make_state(metadata_history);
    const auto metadata_conflict = dispatch_editor_action(
        metadata_history, metadata_state, begin_hold_action_t{{measure_id_t{1}, 480}});
    CHECK_FALSE(metadata_conflict);
    CHECK(has_code(metadata_conflict.diagnostics, diagnostic_code_t::collision));
    CHECK_FALSE(metadata_state.pending_hold.has_value());
    CHECK(metadata_history.document.revision == 1);
}

TEST_CASE("releasing a pending hold at its head places a tap", "[unit][core][interaction][hold]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);

    REQUIRE(dispatch_editor_action(history, state, begin_hold_action_t{{measure_id_t{1}, 241}}));
    const auto committed =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{1}, 239}});

    REQUIRE(committed);
    CHECK(committed.document_changed);
    CHECK_FALSE(state.pending_hold.has_value());
    REQUIRE(note_count(history) == 1);
    const auto& note = history.document.charts.front().measures.front().notes.front();
    CHECK(note.kind == note_kind_t::tap);
    CHECK_FALSE(note.end.has_value());
    CHECK(note.position == local_position_t{measure_id_t{1}, 240});
}

TEST_CASE("lane-key hold gestures snap, extend, contract to a tap, and undo atomically",
          "[unit][core][interaction][gesture][hold]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);

    const auto pressed =
        dispatch_editor_action(history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{1}});
    REQUIRE(pressed);
    REQUIRE(pressed.view->pending_hold.has_value());
    CHECK(pressed.view->pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane);
    CHECK(pressed.view->pending_hold->position == local_position_t{measure_id_t{1}, 720});
    CHECK(history.document.revision == 1);
    CHECK(note_count(history) == 0);

    const auto extended = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    REQUIRE(extended);
    const auto extended_again = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    REQUIRE(extended_again);
    REQUIRE(extended_again.view->pending_hold->preview_end.has_value());
    CHECK(*extended_again.view->pending_hold->preview_end ==
          local_position_t{measure_id_t{2}, 240});

    const auto reversed = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
    REQUIRE(reversed);
    const auto reversed_again = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
    REQUIRE(reversed_again);
    CHECK_FALSE(reversed_again.view->pending_hold->preview_end.has_value());
    const auto tap =
        dispatch_editor_action(history, state, release_lane_key_hold_gesture_action_t{});
    REQUIRE(tap);
    CHECK(tap.document_changed);
    REQUIRE(note_count(history) == 1);
    CHECK(find_note(history, object_id_t{101})->kind == note_kind_t::tap);
    CHECK_FALSE(state.pending_hold.has_value());
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 0);

    REQUIRE(
        dispatch_editor_action(history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{1}}));
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::forward}));
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::forward}));
    const auto hold =
        dispatch_editor_action(history, state, release_lane_key_hold_gesture_action_t{});
    REQUIRE(hold);
    CHECK(find_note(history, object_id_t{101})->kind == note_kind_t::hold);
    CHECK(find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{2}, 240});
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 0);

    REQUIRE(
        dispatch_editor_action(history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{1}}));
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::backward}));
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::backward}));
    REQUIRE(dispatch_editor_action(history, state,
                                   move_edit_cursor_action_t{interaction_direction_t::backward}));
    const auto backward_hold =
        dispatch_editor_action(history, state, release_lane_key_hold_gesture_action_t{});
    REQUIRE(backward_hold);
    const auto* moved_head = find_note(history, object_id_t{101});
    REQUIRE(moved_head != nullptr);
    CHECK(moved_head->position == local_position_t{measure_id_t{1}, 480});
    CHECK(moved_head->end == local_position_t{measure_id_t{2}, 240});
}

TEST_CASE("lane-key press removes an occupied ordinary tap or hold",
          "[unit][core][interaction][hold]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 480},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 720}},
    };
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    state.cursor = {measure_id_t{1}, 480};

    const auto pressed =
        dispatch_editor_action(history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{1}});
    REQUIRE(pressed);
    CHECK(pressed.document_changed);
    CHECK_FALSE(state.pending_hold.has_value());
    CHECK(note_count(history) == 0);
    const auto released =
        dispatch_editor_action(history, state, release_lane_key_hold_gesture_action_t{});
    REQUIRE(released);
    CHECK_FALSE(released.document_changed);
    REQUIRE(undo_document_command(history));
    CHECK(find_note(history, object_id_t{101}) != nullptr);
}

TEST_CASE("Alt pointer hold gestures cancel on modifier release or capture loss",
          "[unit][core][interaction][gesture][hold]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);

    const auto no_modifier = dispatch_editor_action(
        history, state,
        begin_alt_pointer_hold_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, false});
    CHECK_FALSE(no_modifier);
    CHECK_FALSE(state.pending_hold.has_value());

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_alt_pointer_hold_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, true}));
    REQUIRE(dispatch_editor_action(
        history, state,
        update_alt_pointer_hold_gesture_action_t{{measure_id_t{1}, 240}, true, true}));
    const auto modifier_lost = dispatch_editor_action(
        history, state,
        update_alt_pointer_hold_gesture_action_t{{measure_id_t{1}, 480}, false, true});
    REQUIRE(modifier_lost);
    CHECK_FALSE(state.pending_hold.has_value());
    CHECK(note_count(history) == 0);

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_alt_pointer_hold_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, true}));
    const auto capture_lost = dispatch_editor_action(
        history, state,
        release_alt_pointer_hold_gesture_action_t{{measure_id_t{1}, 480}, true, false});
    REQUIRE(capture_lost);
    CHECK_FALSE(state.pending_hold.has_value());
    CHECK(note_count(history) == 0);

    REQUIRE(dispatch_editor_action(
        history, state,
        begin_alt_pointer_hold_gesture_action_t{lane_id_t{1}, {measure_id_t{1}, 0}, true}));
    const auto committed = dispatch_editor_action(
        history, state,
        release_alt_pointer_hold_gesture_action_t{{measure_id_t{2}, 240}, true, true});
    REQUIRE(committed);
    CHECK(committed.document_changed);
    CHECK(find_note(history, object_id_t{101})->kind == note_kind_t::hold);
    CHECK(find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{2}, 240});
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 0);
}

TEST_CASE("hold input gestures retain stale previews and can cancel after focus loss",
          "[unit][core][interaction][gesture][hold][revision]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);
    REQUIRE(
        dispatch_editor_action(history, state, begin_lane_key_hold_gesture_action_t{lane_id_t{1}}));

    auto proposed = history.document;
    proposed.metadata.title = "external edit";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename song"));
    const auto stale = dispatch_editor_action(
        history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(state.pending_hold.has_value());
    CHECK(note_count(history) == 0);

    const auto cancelled = dispatch_editor_action(
        history, state, cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::focus_lost});
    REQUIRE(cancelled);
    CHECK_FALSE(state.pending_hold.has_value());
    CHECK(note_count(history) == 0);
}

TEST_CASE("tail drag previews and commits the same exact duration delta for a selection",
          "[unit][core][interaction][gesture][selection][hold]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 240}},
        {.id = object_id_t{102},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{2},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 480}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{101}, object_id_t{102}}}));

    REQUIRE(dispatch_editor_action(history, state,
                                   begin_note_duration_gesture_action_t{object_id_t{101}}));
    const auto expanded = dispatch_editor_action(
        history, state, update_note_duration_gesture_action_t{{measure_id_t{1}, 480}});
    REQUIRE(expanded);
    REQUIRE(expanded.view->pending_note_duration_gesture.has_value());
    REQUIRE(expanded.view->pending_note_duration_gesture->preview.size() == 2);
    CHECK(expanded.view->pending_note_duration_gesture->preview[0].end ==
          local_position_t{measure_id_t{1}, 480});
    CHECK(expanded.view->pending_note_duration_gesture->preview[1].end ==
          local_position_t{measure_id_t{1}, 720});
    CHECK(history.document.revision == 1);

    const auto committed = dispatch_editor_action(
        history, state, release_note_duration_gesture_action_t{{measure_id_t{1}, 480}});
    REQUIRE(committed);
    CHECK(committed.document_changed);
    CHECK_FALSE(state.pending_note_duration_gesture.has_value());
    CHECK(find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{1}, 480});
    CHECK(find_note(history, object_id_t{102})->end == local_position_t{measure_id_t{1}, 720});
    REQUIRE(undo_document_command(history));
    CHECK(find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{1}, 240});
    CHECK(find_note(history, object_id_t{102})->end == local_position_t{measure_id_t{1}, 480});

    REQUIRE(dispatch_editor_action(history, state,
                                   begin_note_duration_gesture_action_t{object_id_t{101}}));
    const auto contracted = dispatch_editor_action(
        history, state, update_note_duration_gesture_action_t{{measure_id_t{1}, 0}});
    REQUIRE(contracted);
    REQUIRE(contracted.view->pending_note_duration_gesture.has_value());
    CHECK(contracted.view->pending_note_duration_gesture->preview[0].kind == note_kind_t::tap);
    CHECK_FALSE(contracted.view->pending_note_duration_gesture->preview[0].end.has_value());
    CHECK(contracted.view->pending_note_duration_gesture->preview[1].end ==
          local_position_t{measure_id_t{1}, 240});
    const auto collapsed = dispatch_editor_action(
        history, state, release_note_duration_gesture_action_t{{measure_id_t{1}, 0}});
    REQUIRE(collapsed);
    CHECK(find_note(history, object_id_t{101})->kind == note_kind_t::tap);
    CHECK(find_note(history, object_id_t{102})->end == local_position_t{measure_id_t{1}, 240});
}

TEST_CASE("duration gestures reject collisions and stale revisions without partial edits",
          "[unit][core][interaction][gesture][selection][collision]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 240}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 720}, .lane = lane_id_t{1}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(history, state,
                                   begin_note_duration_gesture_action_t{object_id_t{101}}));
    const auto collision = dispatch_editor_action(
        history, state, update_note_duration_gesture_action_t{{measure_id_t{1}, 720}});
    CHECK_FALSE(collision);
    CHECK(has_code(collision.diagnostics, diagnostic_code_t::collision));
    CHECK(history.document.revision == 1);
    CHECK(find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{1}, 240});
    CHECK(state.pending_note_duration_gesture.has_value());

    const auto cancel = dispatch_editor_action(
        history, state, cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::escape});
    REQUIRE(cancel);
    CHECK_FALSE(state.pending_note_duration_gesture.has_value());

    REQUIRE(dispatch_editor_action(history, state,
                                   begin_note_duration_gesture_action_t{object_id_t{101}}));
    auto proposed = history.document;
    proposed.metadata.title = "revision changed during drag";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename song"));
    const auto stale = dispatch_editor_action(
        history, state, update_note_duration_gesture_action_t{{measure_id_t{1}, 480}});
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{1}, 240});
    CHECK(state.pending_note_duration_gesture.has_value());
    REQUIRE(dispatch_editor_action(
        history, state,
        cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::focus_lost}));
    CHECK_FALSE(state.pending_note_duration_gesture.has_value());
}

TEST_CASE("selected note moves preview and commit one snapped translation",
          "[unit][core][interaction][gesture][selection][move]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 480}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{2}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{101}, object_id_t{102}}}));
    REQUIRE(
        dispatch_editor_action(history, state, begin_note_move_gesture_action_t{object_id_t{101}}));

    const auto preview = dispatch_editor_action(
        history, state, update_note_move_gesture_action_t{{measure_id_t{2}, 0}});
    REQUIRE(preview);
    REQUIRE(preview.view->pending_note_move_gesture.has_value());
    const auto& moved = preview.view->pending_note_move_gesture->preview;
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].position == local_position_t{measure_id_t{2}, 0});
    CHECK(moved[0].end == local_position_t{measure_id_t{2}, 240});
    CHECK(moved[1].position == local_position_t{measure_id_t{2}, 240});
    CHECK_FALSE(moved[1].end.has_value());
    CHECK(history.document.revision == 1);

    const auto committed = dispatch_editor_action(
        history, state, release_note_move_gesture_action_t{{measure_id_t{2}, 0}});
    REQUIRE(committed);
    CHECK(committed.document_changed);
    CHECK_FALSE(state.pending_note_move_gesture.has_value());
    CHECK(find_note(history, object_id_t{101})->position == local_position_t{measure_id_t{2}, 0});
    CHECK(find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{2}, 240});
    CHECK(find_note(history, object_id_t{102})->position == local_position_t{measure_id_t{2}, 240});
    REQUIRE(undo_document_command(history));
    CHECK(find_note(history, object_id_t{101})->position == local_position_t{measure_id_t{1}, 240});
    CHECK(find_note(history, object_id_t{102})->position == local_position_t{measure_id_t{1}, 480});
}

TEST_CASE("note move gestures reject collisions and stale revisions without partial edits",
          "[unit][core][interaction][gesture][move][collision]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 720}, .lane = lane_id_t{1}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(
        dispatch_editor_action(history, state, begin_note_move_gesture_action_t{object_id_t{101}}));
    const auto collision = dispatch_editor_action(
        history, state, update_note_move_gesture_action_t{{measure_id_t{1}, 720}});
    CHECK_FALSE(collision);
    CHECK(has_code(collision.diagnostics, diagnostic_code_t::collision));
    CHECK(state.pending_note_move_gesture.has_value());
    CHECK(find_note(history, object_id_t{101})->position == local_position_t{measure_id_t{1}, 240});

    auto proposed = history.document;
    proposed.metadata.title = "revision changed during move";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename song"));
    const auto stale = dispatch_editor_action(
        history, state, update_note_move_gesture_action_t{{measure_id_t{1}, 480}});
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(state.pending_note_move_gesture.has_value());
    REQUIRE(dispatch_editor_action(
        history, state,
        cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::focus_lost}));
    CHECK_FALSE(state.pending_note_move_gesture.has_value());
}

TEST_CASE("selected note moves translate playable lane columns atomically",
          "[unit][core][interaction][gesture][move][lane]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes[1].display_order = 1;
    chart.lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{3}, .name = "Key 3", .display_order = 2});
    chart.measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 480}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{2}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{101}, object_id_t{102}}}));
    REQUIRE(
        dispatch_editor_action(history, state, begin_note_move_gesture_action_t{object_id_t{101}}));

    const auto preview = dispatch_editor_action(
        history, state, update_note_move_gesture_action_t{{measure_id_t{2}, 0}, lane_id_t{2}});
    REQUIRE(preview);
    REQUIRE(preview.view->pending_note_move_gesture.has_value());
    const auto& moved = preview.view->pending_note_move_gesture->preview;
    REQUIRE(moved.size() == 2);
    CHECK(moved[0].lane == lane_id_t{2});
    CHECK(moved[1].lane == lane_id_t{3});

    const auto committed = dispatch_editor_action(
        history, state, release_note_move_gesture_action_t{{measure_id_t{2}, 0}, lane_id_t{2}});
    REQUIRE(committed);
    CHECK(find_note(history, object_id_t{101})->lane == lane_id_t{2});
    CHECK(find_note(history, object_id_t{102})->lane == lane_id_t{3});
    REQUIRE(undo_document_command(history));
    CHECK(find_note(history, object_id_t{101})->lane == lane_id_t{1});
    CHECK(find_note(history, object_id_t{102})->lane == lane_id_t{2});
}

TEST_CASE("selected note lane moves reject unavailable columns and destination collisions",
          "[unit][core][interaction][gesture][move][lane][collision]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes[1].display_order = 1;
    chart.measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
    };
    chart.measures[1].notes = {
        {.id = object_id_t{102}, .position = {measure_id_t{2}, 0}, .lane = lane_id_t{2}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(
        dispatch_editor_action(history, state, begin_note_move_gesture_action_t{object_id_t{101}}));
    const auto collision = dispatch_editor_action(
        history, state, update_note_move_gesture_action_t{{measure_id_t{2}, 0}, lane_id_t{2}});
    CHECK_FALSE(collision);
    CHECK(has_code(collision.diagnostics, diagnostic_code_t::collision));
    CHECK(find_note(history, object_id_t{101})->lane == lane_id_t{1});

    const auto unavailable = dispatch_editor_action(
        history, state, update_note_move_gesture_action_t{{measure_id_t{2}, 240}, lane_id_t{3}});
    CHECK_FALSE(unavailable);
    CHECK(has_code(unavailable.diagnostics, diagnostic_code_t::unknown_lane));
    CHECK(state.pending_note_move_gesture.has_value());
}

TEST_CASE("selected note lane moves validate every target lane across layout boundaries",
          "[unit][core][interaction][gesture][move][lane][layout]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes[1].display_order = 1;
    chart.lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{3}, .name = "Key 3", .display_order = 2});
    auto reduced_layout = chart.lane_layouts.front().layout;
    reduced_layout.lanes.pop_back();
    chart.lane_layouts.push_back({.id = object_id_t{103},
                                  .position = {measure_id_t{1}, 600},
                                  .layout = std::move(reduced_layout)});
    chart.measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{2}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{101}, object_id_t{102}}}));
    REQUIRE(
        dispatch_editor_action(history, state, begin_note_move_gesture_action_t{object_id_t{101}}));

    const auto unavailable = dispatch_editor_action(
        history, state, update_note_move_gesture_action_t{{measure_id_t{1}, 480}, lane_id_t{2}});
    CHECK_FALSE(unavailable);
    CHECK(has_code(unavailable.diagnostics, diagnostic_code_t::unknown_lane));
    CHECK(find_note(history, object_id_t{101})->lane == lane_id_t{1});
    CHECK(find_note(history, object_id_t{102})->lane == lane_id_t{2});
    CHECK(state.pending_note_move_gesture.has_value());
}

TEST_CASE("duration gesture previews scale to fifty thousand selected notes",
          "[unit][core][interaction][gesture][stress]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 100'000;
    chart.measures.front().notes.reserve(50'000);
    std::vector<object_id_t> selected;
    selected.reserve(50'000);
    for (std::uint64_t index = 0; index < 50'000; ++index) {
        const auto id = object_id_t{1'000 + index};
        chart.measures.front().notes.push_back({
            .id = id,
            .position = {measure_id_t{1}, index * 2},
            .lane = lane_id_t{1},
            .kind = note_kind_t::tap,
        });
        selected.push_back(id);
    }
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{100000}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 100'000);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(dispatch_editor_action(history, state, select_editor_notes_action_t{selected}));
    REQUIRE(dispatch_editor_action(history, state,
                                   begin_note_duration_gesture_action_t{object_id_t{1'000}}));

    const auto preview = dispatch_editor_action(
        history, state, update_note_duration_gesture_action_t{{measure_id_t{1}, 1}});
    REQUIRE(preview);
    REQUIRE(preview.view->pending_note_duration_gesture.has_value());
    REQUIRE(preview.view->pending_note_duration_gesture->preview.size() == 50'000);
    CHECK(preview.view->pending_note_duration_gesture->preview.front().end ==
          local_position_t{measure_id_t{1}, 1});
    CHECK(preview.view->pending_note_duration_gesture->preview.back().end ==
          local_position_t{measure_id_t{1}, 99'999});
    CHECK(history.document.revision == 1);
}

TEST_CASE("hold completion restores its lane across layout transitions",
          "[unit][core][interaction][hold][layout]") {
    auto song = make_song();
    song.charts.front().lane_layouts.push_back({
        .id = object_id_t{101},
        .position = {measure_id_t{2}, 0},
        .layout =
            {
                .profile_id = "hold-end-layout",
                .lanes = {{.id = lane_id_t{1}, .name = "Key 1"}},
            },
    });
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);

    REQUIRE(dispatch_editor_action(history, state, begin_hold_action_t{{measure_id_t{1}, 0}}));
    REQUIRE(dispatch_editor_action(history, state, select_editor_lane_action_t{lane_id_t{2}}));
    const auto committed =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{2}, 240}});
    REQUIRE(committed);
    REQUIRE(committed.view.has_value());
    CHECK(committed.view->active_lane == lane_id_t{1});
    CHECK(committed.view->cursor == local_position_t{measure_id_t{2}, 240});
    CHECK(note_count(history) == 1);
}

TEST_CASE("hold previews reject stale document revisions and remain cancellable",
          "[unit][core][interaction][hold][revision]") {
    auto history = make_document_history(make_song());
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(history, state, begin_hold_action_t{{measure_id_t{1}, 240}}));

    auto proposed = history.document;
    proposed.metadata.title = "external edit";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename song"));
    const auto stale =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{1}, 720}});
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(state.pending_hold.has_value());
    CHECK(note_count(history) == 0);

    history.document.charts.front().lane_layouts.front().layout.lanes.clear();
    const auto cancelled = dispatch_editor_action(history, state, cancel_hold_action_t{});
    REQUIRE(cancelled);
    CHECK_FALSE(state.pending_hold.has_value());
    CHECK_FALSE(cancelled.view.has_value());
    CHECK(has_code(cancelled.diagnostics, diagnostic_code_t::unknown_lane));
}

TEST_CASE("editor deletion removes a cursor note or selected notes atomically",
          "[unit][core][interaction][selection][delete]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 840}, .lane = lane_id_t{1}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 0}, .lane = lane_id_t{2}},
    };
    song.charts.front().measures.back().notes = {
        {.id = object_id_t{103}, .position = {measure_id_t{2}, 0}, .lane = lane_id_t{1}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);

    const auto cursor_delete =
        dispatch_editor_action(history, state, delete_editor_notes_action_t{});
    REQUIRE(cursor_delete);
    CHECK(cursor_delete.document_changed);
    CHECK(note_count(history) == 2);
    CHECK_FALSE(
        std::ranges::any_of(history.document.charts.front().measures, [](const auto& measure) {
            return std::ranges::find(measure.notes, object_id_t{101}, &note_t::id) !=
                   measure.notes.end();
        }));
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 3);

    const auto selected = dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{102}, object_id_t{103}}});
    REQUIRE(selected);
    CHECK(selected.view->selected_notes ==
          std::vector<object_id_t>{object_id_t{102}, object_id_t{103}});
    const auto selection_delete =
        dispatch_editor_action(history, state, delete_editor_notes_action_t{});
    REQUIRE(selection_delete);
    CHECK(selection_delete.document_changed);
    CHECK(selection_delete.view->selected_notes.empty());
    CHECK(note_count(history) == 1);
    CHECK(history.document.charts.front().measures.front().notes.front().id == object_id_t{101});
    REQUIRE(undo_document_command(history));
    CHECK(note_count(history) == 3);

    const auto unknown =
        dispatch_editor_action(history, state, select_editor_notes_action_t{{object_id_t{999}}});
    CHECK_FALSE(unknown);
    CHECK(has_code(unknown.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(state.selected_notes.empty());
    CHECK(note_count(history) == 3);
}

TEST_CASE("bare lane assignment moves every selected note and retains selection",
          "[unit][core][interaction][selection][lane]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 0}, .lane = lane_id_t{1}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{2}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{102}, object_id_t{101}}}));

    const auto assigned = dispatch_editor_action(
        history, state, assign_selected_notes_to_lane_action_t{lane_id_t{2}, false});
    REQUIRE(assigned);
    CHECK(assigned.document_changed);
    CHECK_FALSE(assigned.audition.has_value());
    CHECK(find_note(history, object_id_t{101})->lane == lane_id_t{2});
    CHECK(find_note(history, object_id_t{102})->lane == lane_id_t{2});
    CHECK(assigned.view->selected_notes ==
          std::vector<object_id_t>{object_id_t{102}, object_id_t{101}});
    CHECK(assigned.view->active_lane == lane_id_t{2});
    REQUIRE(undo_document_command(history));
    CHECK(find_note(history, object_id_t{101})->lane == lane_id_t{1});
}

TEST_CASE("Shift lane assignment consumes deterministic selected notes and requests audition",
          "[unit][core][interaction][selection][lane]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{1}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 0}, .lane = lane_id_t{1}},
        {.id = object_id_t{103}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state,
        select_editor_notes_action_t{{object_id_t{101}, object_id_t{103}, object_id_t{102}}}));

    const auto first = dispatch_editor_action(
        history, state, assign_selected_notes_to_lane_action_t{lane_id_t{2}, true});
    REQUIRE(first);
    CHECK(first.document_changed);
    REQUIRE(first.audition.has_value());
    CHECK(first.audition->object ==
          editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{102}});
    CHECK(first.audition->document_revision == history.document.revision);
    CHECK(first.view->selected_notes ==
          std::vector<object_id_t>{object_id_t{101}, object_id_t{103}});
    CHECK(find_note(history, object_id_t{102})->lane == lane_id_t{2});

    const auto second = dispatch_editor_action(
        history, state, assign_selected_notes_to_lane_action_t{lane_id_t{2}, true});
    REQUIRE(second);
    REQUIRE(second.audition.has_value());
    CHECK(second.audition->object ==
          editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{103}});
    CHECK(second.view->selected_notes == std::vector<object_id_t>{object_id_t{101}});
    CHECK(find_note(history, object_id_t{103})->lane == lane_id_t{2});
}

TEST_CASE("lane assignment rejects selected and existing collisions atomically",
          "[unit][core][interaction][selection][lane]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101}, .position = {measure_id_t{1}, 0}, .lane = lane_id_t{1}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 0}, .lane = lane_id_t{2}},
        {.id = object_id_t{103}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{2}},
        {.id = object_id_t{104}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{1}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{101}, object_id_t{102}}}));
    const auto revision = history.document.revision;

    const auto selected_collision = dispatch_editor_action(
        history, state, assign_selected_notes_to_lane_action_t{lane_id_t{2}, false});
    CHECK_FALSE(selected_collision);
    CHECK(has_code(selected_collision.diagnostics, diagnostic_code_t::collision));
    CHECK(history.document.revision == revision);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{101}, object_id_t{102}});

    REQUIRE(
        dispatch_editor_action(history, state, select_editor_notes_action_t{{object_id_t{104}}}));
    const auto existing_collision = dispatch_editor_action(
        history, state, assign_selected_notes_to_lane_action_t{lane_id_t{2}, false});
    CHECK_FALSE(existing_collision);
    CHECK(has_code(existing_collision.diagnostics, diagnostic_code_t::collision));
    CHECK(find_note(history, object_id_t{104})->lane == lane_id_t{1});
}

TEST_CASE("lane assignment remains bounded for fifty thousand selected notes",
          "[unit][core][interaction][selection][lane][stress]") {
    auto song = make_song();
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 100'000;
    chart.measures.resize(1);
    chart.measures.front().notes.clear();
    chart.measures.front().notes.reserve(50'000);
    std::vector<object_id_t> selected;
    selected.reserve(50'000);
    for (std::uint64_t index = 0; index < 50'000; ++index) {
        const auto id = object_id_t{1'000 + index};
        chart.measures.front().notes.push_back(
            {.id = id, .position = {measure_id_t{1}, index * 2}, .lane = lane_id_t{1}});
        selected.push_back(id);
    }
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    constexpr std::array snaps{std::uint64_t{100000}};
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{1}, 0},
                                         snaps, 100'000);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(dispatch_editor_action(history, state, select_editor_notes_action_t{selected}));

    const auto assigned = dispatch_editor_action(
        history, state, assign_selected_notes_to_lane_action_t{lane_id_t{2}, false});
    REQUIRE(assigned);
    CHECK(assigned.document_changed);
    CHECK(assigned.view->selected_notes.size() == 50'000);
    CHECK(history.document.charts.front().measures.front().notes.front().lane == lane_id_t{2});
    CHECK(history.document.charts.front().measures.front().notes.back().lane == lane_id_t{2});
}

TEST_CASE("duration resizing applies the reference delta to every selected note",
          "[unit][core][interaction][selection][hold]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 240}},
        {.id = object_id_t{102},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{2},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 480}},
    };
    song.charts.front().measures.back().notes = {
        {.id = object_id_t{103}, .position = {measure_id_t{2}, 0}, .lane = lane_id_t{2}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{101}, object_id_t{102}}}));

    const auto expanded = dispatch_editor_action(
        history, state, resize_note_durations_action_t{object_id_t{101}, {measure_id_t{1}, 480}});
    REQUIRE(expanded);
    CHECK(expanded.document_changed);
    REQUIRE(find_note(history, object_id_t{101})->end.has_value());
    CHECK(*find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{1}, 480});
    REQUIRE(find_note(history, object_id_t{102})->end.has_value());
    CHECK(*find_note(history, object_id_t{102})->end == local_position_t{measure_id_t{1}, 720});

    const auto collision = dispatch_editor_action(
        history, state, resize_note_durations_action_t{object_id_t{101}, {measure_id_t{1}, 720}});
    CHECK_FALSE(collision);
    CHECK(has_code(collision.diagnostics, diagnostic_code_t::collision));
    CHECK(history.document.revision == 2);
    CHECK(*find_note(history, object_id_t{102})->end == local_position_t{measure_id_t{1}, 720});

    const auto collapsed = dispatch_editor_action(
        history, state, resize_note_durations_action_t{object_id_t{101}, {measure_id_t{1}, 0}});
    REQUIRE(collapsed);
    CHECK(collapsed.document_changed);
    CHECK(find_note(history, object_id_t{101})->kind == note_kind_t::tap);
    CHECK_FALSE(find_note(history, object_id_t{101})->end.has_value());
    CHECK(find_note(history, object_id_t{102})->kind == note_kind_t::hold);
    REQUIRE(find_note(history, object_id_t{102})->end.has_value());
    CHECK(*find_note(history, object_id_t{102})->end == local_position_t{measure_id_t{1}, 240});
    REQUIRE(undo_document_command(history));
    CHECK(*find_note(history, object_id_t{101})->end == local_position_t{measure_id_t{1}, 480});
    CHECK(*find_note(history, object_id_t{102})->end == local_position_t{measure_id_t{1}, 720});
}

TEST_CASE("duration resizing rejects an atomic selection that would cross a note head",
          "[unit][core][interaction][selection][hold][negative]") {
    auto song = make_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 0},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 240}},
        {.id = object_id_t{102}, .position = {measure_id_t{1}, 480}, .lane = lane_id_t{2}},
    };
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));
    auto state = make_state(history);
    REQUIRE(dispatch_editor_action(
        history, state, select_editor_notes_action_t{{object_id_t{101}, object_id_t{102}}}));

    const auto rejected = dispatch_editor_action(
        history, state, resize_note_durations_action_t{object_id_t{101}, {measure_id_t{1}, 0}});
    CHECK_FALSE(rejected);
    CHECK(has_code(rejected.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(history.document.revision == 1);
    CHECK(find_note(history, object_id_t{101})->kind == note_kind_t::hold);
    CHECK(find_note(history, object_id_t{102})->kind == note_kind_t::tap);
}
