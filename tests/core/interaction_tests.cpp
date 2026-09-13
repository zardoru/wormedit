#include <wormedit/core/interaction.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
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

TEST_CASE("editor y mapping preserves exact rows through zoom and scroll",
          "[unit][core][interaction][mapping]") {
    auto history = make_document_history(make_song());
    constexpr std::array snaps{4ULL};
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
    constexpr std::array snaps{4ULL};
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
    constexpr std::array snaps{4ULL};
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
    constexpr std::array snaps{4ULL};
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
    constexpr std::array snaps{4ULL};
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
    constexpr std::array snaps{4ULL};
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
    constexpr std::array snaps{4ULL};
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
    constexpr std::array snaps{100'001ULL};
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
    constexpr std::array snaps{100'000ULL};
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
    constexpr std::array snaps{100'000ULL};
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
