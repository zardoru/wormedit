// Paste preview and pointer paste, plus offset-lock capture handling.
#include "frame_timing_fixtures.h"
#include "../support/stale_action_assertions.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void copy_preview_note(compose_test_fixture_t& fixture) {
    fixture.project();
    fixture.interaction->selected_notes = {fixture.history.document.charts.front().measures.front().notes.front().id};
    auto copied = chart_clipboard_t::from(fixture.history.document, *fixture.interaction,
                                      fixture.history.document.revision);
    REQUIRE(copied);
    fixture.application.project.clipboard.chart_clipboard = std::move(*copied);
}
} // namespace

TEST_CASE("Paste preview owns geometry across frames and publishes one undoable command",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 240;
    std::string status;
    expect_compose_test(static_cast<bool>(begin_compose_paste(fixture.application, fixture.history,
                               *fixture.interaction, status)));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
    expect_compose_test(static_cast<bool>(fixture.history.document.charts.front().measures.front().notes.size() == 1));
    fixture.project();
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_active));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_valid));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghosts.size() == 1));
    const auto first = fixture.flattened.view.compose.paste_ghosts[0].bounds;
    fixture.flattened = {};
    fixture.project();
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghosts[0].bounds.top == first.top));
    expect_compose_test(static_cast<bool>(fixture.application.project.clipboard.apply(fixture.history,
                                 *fixture.interaction, status)));
    expect_compose_test(!static_cast<bool>(fixture.application.project.clipboard.paste_session));
    expect_compose_test(static_cast<bool>(fixture.application.project.clipboard.chart_clipboard.has_value()));
    expect_compose_test(static_cast<bool>(fixture.history.undo.size() == 1));
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    expect_compose_test(static_cast<bool>(notes.size() == 2));
    expect_compose_test(static_cast<bool>(notes.back().position.row == 240));
    expect_compose_test(static_cast<bool>(notes.back().id != notes.front().id));
    expect_compose_test(static_cast<bool>(undo_document_command(fixture.history)));
    expect_compose_test(static_cast<bool>(fixture.history.document.charts.front().measures.front().notes.size() == 1));
}

TEST_CASE("Paste collision remains visible and stale previews cannot publish",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 0;
    std::string status;
    expect_compose_test(static_cast<bool>(begin_compose_paste(fixture.application, fixture.history,
                               *fixture.interaction, status)));
    fixture.project();
    expect_compose_test(!static_cast<bool>(fixture.flattened.view.compose.paste_valid));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghosts.size() == 1));
    expect_compose_test(!static_cast<bool>(fixture.application.project.clipboard.apply(fixture.history,
                                     *fixture.interaction, status)));
    expect_compose_test(!static_cast<bool>(status.empty()));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
    fixture.interaction->cursor.row = 240;
    expect_compose_test(static_cast<bool>(fixture.application.project.clipboard.revise(fixture.history,
                                fixture.interaction->chart, fixture.interaction->cursor)));
    expect_compose_test(static_cast<bool>(fixture.application.project.clipboard.paste_session->proposal.value.has_value()));
    ++fixture.history.document.revision;
    expect_compose_test(!static_cast<bool>(fixture.application.project.clipboard.apply(fixture.history,
                                     *fixture.interaction, status)));
    wormedit::test::check_stale_action_message(status, "Paste preview");
    expect_compose_test(!static_cast<bool>(fixture.application.project.clipboard.paste_session));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

namespace {
auto paste_pointer_action(compose_test_fixture_t& fixture, bool commit, std::uint64_t row = 240) -> wormedit::ui::workspace_action_t {
    const auto& chart = fixture.history.document.charts.front();
    const auto inspected = inspect_editor_interaction(fixture.history, *fixture.interaction);
    const auto* view = std::get_if<editor_interaction_view_model_t>(&inspected);
    expect_compose_test(view != nullptr);
    const auto y = editor_position_y_pixels(chart, *view, {chart.measures.front().id, row});
    expect_compose_test(y.has_value());
    // Use an interior point in the first snap cell, avoiding a chart-boundary
    // cancellation error when the downscroll screen transform is reversed.
    const bool downscroll = compose_field_downscroll(chart, fixture.application.preferences.editor_preferences);
    const auto screen_y = compose_source_y(*y + (row == 0 ? 0.5 : 0.0),
                                           view->viewport_height_pixels, downscroll);
    const auto raw = editor_position_at_y_pixels(chart, *view,
        compose_source_y(screen_y, view->viewport_height_pixels, downscroll));
    const auto snapped = raw ? resolve_resource_placement_position(chart, *raw,
        active_snap_grid(*fixture.interaction)) : std::nullopt;
    expect_compose_test(snapped.has_value());
    expect_compose_test(snapped->row == row);
    wormedit::ui::workspace_action_t action{};
    action.stamp.chart_id.value = chart.id.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{1}));
    action.input.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_PASTE_UPDATE,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = screen_y};
    action.input.compose_inputs.resize(1);
    if (commit) {
        action.input.compose_inputs.resize(std::max(action.input.compose_inputs.size(), std::size_t{2}));
        action.input.compose_inputs[1] = {.kind = WORM_UI_COMPOSE_INPUT_PASTE_COMMIT};
        action.input.compose_inputs.resize(2);
    }
    return action;
}

auto dispatch_paste_pointer(compose_test_fixture_t& fixture, bool commit) -> std::string {
    auto action = paste_pointer_action(fixture, commit);
    return dispatch_paste_action(fixture, action);
}
} // namespace

TEST_CASE("Pointer paste retains cursor and viewport and FIFO release commits that anchor",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 0;
    std::string status;
    expect_compose_test(begin_compose_paste(fixture.application, fixture.history,
                                   *fixture.interaction, status));
    const auto cursor = fixture.interaction->cursor;
    const auto scroll = fixture.interaction->viewport_scroll_pixels;
    (void)dispatch_paste_pointer(fixture, false);
    expect_compose_test(fixture.interaction->cursor == cursor);
    expect_compose_test(fixture.interaction->viewport_scroll_pixels == scroll);
    expect_compose_test(fixture.history.undo.empty());
    fixture.project();
    expect_compose_test(fixture.application.project.clipboard.paste_session->anchor.row == 240);
    expect_compose_test(fixture.interaction->cursor == cursor);
    expect_compose_test(fixture.interaction->viewport_scroll_pixels == scroll);
    status = dispatch_paste_pointer(fixture, true);
    expect_compose_test(status == "Selection pasted.");
    expect_compose_test(fixture.history.undo.size() == 1);
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    expect_compose_test(notes.size() == 2);
    expect_compose_test(notes.back().position.row == 240);
    expect_compose_test(fixture.interaction->cursor == cursor);
}

TEST_CASE("Paste cached anchor keeps collision diagnostics across an outside pointer",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 0;
    std::string status;
    expect_compose_test(begin_compose_paste(fixture.application, fixture.history,
                                   *fixture.interaction, status));
    const std::string collision{fixture.application.project.clipboard.paste_session->proposal.diagnostics.front().message};
    expect_compose_test(!collision.empty());
    auto outside = paste_pointer_action(fixture, false);
    outside.input.compose_inputs[0].y_pixels = 10000;
    (void)dispatch_paste_action(fixture, outside);
    fixture.project();
    expect_compose_test(std::string{fixture.flattened.view.compose.paste_diagnostic}.find("outside") != std::string::npos);
    auto original = paste_pointer_action(fixture, false, 0);
    (void)dispatch_paste_action(fixture, original);
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.paste_valid);
    expect_compose_test(std::string{fixture.flattened.view.compose.paste_diagnostic} == collision);
    (void)dispatch_paste_pointer(fixture, false);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paste_valid);
    (void)dispatch_paste_action(fixture, outside);
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.paste_valid);
    (void)dispatch_paste_pointer(fixture, false);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paste_valid);
    expect_compose_test(std::string{fixture.flattened.view.compose.paste_diagnostic}.empty());
    expect_compose_test(fixture.history.undo.empty());
}
#endif

TEST_CASE("Offset lock action rejects stale binding without clearing captured edits",
          "[sys][offset-stamp]") {
    compose_test_fixture_t fixture;
    fixture.project();
    wormedit::ui::workspace_action_t action{};
    action.stamp.chart_id.value = fixture.interaction->chart.value;
    action.stamp.document_revision = fixture.history.document.revision + 1;
    std::optional<proposal_t<audio_offset_drag_t>> drag{proposal_t<audio_offset_drag_t>{}};
    std::optional<workspace_tab_t> tab{workspace_tab_t::compose};
    const auto message = toggle_compose_audio_offset_lock(action.stamp, fixture.history, *fixture.binding.value,
        fixture.application, fixture.cache, drag, tab);
    test::check_stale_action_message(message, "Audio offset lock");
    expect_compose_test(!fixture.application.project.timing_recipe.audio_offset_locked);
    expect_compose_test(drag.has_value() && tab.has_value());
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Offset fit capture expiry remains separate from an absent drag",
          "[sys][offset-stamp]") {
    const auto missing = GENERATE(false, true);
    compose_test_fixture_t fixture;
    fixture.project();
    fixture.binding.value->transport.audio_asset = asset_id_t{42};
    if (!missing) {
        fixture.cache.timing.offset_fit_drag = compose_scene_cache_t::offset_fit_drag_t{};
        fixture.cache.timing.offset_fit_drag->asset = asset_id_t{42};
        fixture.cache.timing.offset_fit_drag->beat.stamp.document.revision = fixture.history.document.revision + 1;
    }
    auto action = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE, 2);
    const auto message = dispatch_paste_action(fixture, action);
    if (missing) CHECK(message == "Offset fit expired after its chart or waveform changed.");
    else test::check_stale_action_message(message, "Offset fit");
    expect_compose_test(!fixture.cache.timing.offset_fit_drag.has_value());
    expect_compose_test(action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Offset padlock disables exact offset controls independently of BPM timing",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_editable);
    const auto revision = fixture.history.document.revision;
    const auto bpm = fixture.history.document.charts.front().bpms.front().beats_per_minute;
    fixture.application.project.timing_recipe.audio_offset_locked = true;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_locked);
    expect_compose_test(!fixture.flattened.view.compose.audio_offset_editable);
    expect_compose_test(!fixture.flattened.view.compose.waveform_timing_locked);
    expect_compose_test(fixture.history.document.revision == revision);
    expect_compose_test(fixture.history.document.charts.front().bpms.front().beats_per_minute == bpm);
    expect_compose_test(fixture.history.undo.empty());
    fixture.application.project.timing_recipe.audio_offset_locked = false;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_editable);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Locked offset fit rejects captured input without clearing a BPM preview",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    fixture.application.project.timing_recipe.audio_offset_locked = true;
    fixture.cache.timing.beat_fit_drag = proposal_t<beat_interval_drag_t>{};
    wormedit::ui::workspace_action_t action{};
    action.stamp.chart_id.value = fixture.interaction->chart.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.input.compose_inputs.resize(1);
    action.input.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN, .object_id = {2}};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history,
        *fixture.binding.value, *fixture.interaction, viewport, navigation,
        fixture.application, fixture.cache, 0);
    INFO(status);
    expect_compose_test(status.find("unlocked waveform") != std::string::npos);
    expect_compose_test(fixture.cache.timing.beat_fit_drag.has_value());
    expect_compose_test(!fixture.cache.timing.offset_fit_drag.has_value());
    expect_compose_test(action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Offset locking takes precedence over a captured release and never replays it",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    auto begun = audio_offset_drag_t::propose(fixture.history.document, chart_id_t{1}, 48000);
    REQUIRE(begun.value);
    auto updated = begun.value->revise(fixture.history.document, {}, 4800);
    REQUIRE(updated.value);
    std::optional<proposal_t<audio_offset_drag_t>> drag{updated.value};
    std::optional<workspace_tab_t> tab = workspace_tab_t::compose;
    fixture.cache.timing.offset_fit_drag = compose_scene_cache_t::offset_fit_drag_t{};
    fixture.cache.timing.beat_fit_drag = proposal_t<beat_interval_drag_t>{};
    static_cast<void>(fixture.cache.timing.proposed_offset_timing_index.get_or_build(signed_rational_t{1, 10},
        [] { return std::optional<timing_index_t>{}; }));
    wormedit::ui::workspace_action_t action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK;
    action.stamp.chart_id.value = 1;
    action.stamp.project_id = fixture.history.document.id;
    action.stamp.document_revision = fixture.history.document.revision;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_COMMIT;
    (void)toggle_compose_audio_offset_lock(action.stamp, fixture.history, *fixture.binding.value,
        fixture.application, fixture.cache, drag, tab);
    expect_compose_test(fixture.application.project.timing_recipe.audio_offset_locked);
    expect_compose_test(!drag && !tab && !fixture.cache.timing.offset_fit_drag);
    expect_compose_test(!fixture.cache.timing.proposed_offset_timing_index.key());
    expect_compose_test(fixture.cache.timing.beat_fit_drag.has_value());
    expect_compose_test(fixture.history.undo.empty());
    (void)toggle_compose_audio_offset_lock(action.stamp, fixture.history, *fixture.binding.value,
        fixture.application, fixture.cache, drag, tab);
    expect_compose_test(!fixture.application.project.timing_recipe.audio_offset_locked);
    expect_compose_test(!drag && !tab && !fixture.cache.timing.offset_fit_drag);
    expect_compose_test(fixture.history.undo.empty());
}
