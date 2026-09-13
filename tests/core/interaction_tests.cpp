#include <wormedit/core/interaction.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
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
    constexpr std::array snaps{4ULL, 8ULL, 6ULL};
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
    constexpr std::array snaps{4ULL, 8ULL, 6ULL};
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
    constexpr std::array snaps{7ULL, 6ULL, 8ULL};

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

TEST_CASE("pointer zoom anchors chart rows and viewport scrolling clamps to content",
          "[unit][core][interaction][viewport]") {
    auto history = make_document_history(make_song());
    constexpr std::array snaps{4ULL, 8ULL, 6ULL};
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
    const auto collision =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{2}, 0}});
    CHECK_FALSE(collision);
    CHECK(has_code(collision.diagnostics, diagnostic_code_t::collision));
    CHECK(state.pending_hold.has_value());
    CHECK(history.document.revision == 2);
    CHECK(note_count(history) == 1);

    const auto cancelled = dispatch_editor_action(history, state, cancel_hold_action_t{});
    REQUIRE(cancelled);
    CHECK_FALSE(state.pending_hold.has_value());
    CHECK_FALSE(cancelled.document_changed);
    CHECK(history.document.revision == 2);

    const auto no_start =
        dispatch_editor_action(history, state, commit_hold_action_t{{measure_id_t{2}, 480}});
    CHECK_FALSE(no_start);
    CHECK(has_code(no_start.diagnostics, diagnostic_code_t::invalid_hold));
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
