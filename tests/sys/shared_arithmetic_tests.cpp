// Shared text diagnostics and checked arithmetic in options and speed spans.
#include "frame_timing_fixtures.h"
#include "sys/preferences/options_runtime.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void check_options_text_diagnostic(const options_runtime_t& runtime, WormUiOptionsField field) {
    CHECK(std::ranges::find(runtime.diagnostics, field, &options_diagnostic_t::field) != runtime.diagnostics.end());
}
} // namespace

TEST_CASE("Options identifies malformed UTF8 in each display text field", "[sys][shared-text]") {
    options_runtime_t runtime;
    open_options(runtime, program_preferences_t::with_defaults());
    runtime.category = WORM_UI_OPTIONS_DISPLAY;
    runtime.draft.display.field_scheme = "\x80";
    runtime.draft.display.theme = "\x80";
    (void)wormedit::ui::options_view_t::from(runtime);
    CHECK(runtime.diagnostics.size() == 2);
    check_options_text_diagnostic(runtime, WORM_UI_OPTIONS_FIELD_FIELD_SCHEME);
    check_options_text_diagnostic(runtime, WORM_UI_OPTIONS_FIELD_THEME);
}

TEST_CASE("Options identifies malformed UTF8 in the shortcut profile path", "[sys][shared-text]") {
    options_runtime_t runtime;
    open_options(runtime, program_preferences_t::with_defaults());
    runtime.category = WORM_UI_OPTIONS_INPUT;
    runtime.draft.general.shortcut_profile_path = "\x80";
    (void)wormedit::ui::options_view_t::from(runtime);
    check_options_text_diagnostic(runtime, WORM_UI_OPTIONS_FIELD_SHORTCUT_PATH);
}

TEST_CASE("Options discards malformed UTF8 layout identities and display names", "[sys][shared-text]") {
    options_runtime_t runtime;
    set_options_field_layout(runtime, "\x80", "\x80");
    CHECK(runtime.current_field_layout_key.empty());
    CHECK(runtime.current_field_layout_name.empty());
}

TEST_CASE("Timing stage converts transition fractions through the shared careful rounding", "[sys][shared-arithmetic]") {
    auto history = document_history_t::from(dense_song());
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    binding.value->selected_chart = chart_id_t{1};
    frontend_workspace_view_t flattened;
    timing_scene_cache_t cache;
    timing_lane_pseudonote_outcome_t stage{timing_lane_pseudonote_proposal_t::from(
        chart_stamp_t::from(history.document, chart_id_t{1}),
        timing_lane_pseudonote_preview_t{.report = {
            .kind = timing_lane_kind_t::speed, .position = {measure_id_t{1}, 0},
            .value = timing_speed_value_t{.transition_beats = {9'007'199'254'740'993ULL, 3}}}})};
    populate_timing_view_for_compose(flattened, history, *binding.value, cache, 1280, 980, {}, &stage);
    CHECK(flattened.view.timing.stage_secondary_value == 3'002'399'751'580'331.0);
}

namespace {
auto speed_span_transition_at_boundaries(signed_rational_t earlier, signed_rational_t later)
    -> std::optional<rational_t> {
    auto chart = dense_song().charts.front();
    timing_index_t timing{.divisions_per_standard_measure = chart.divisions_per_standard_measure,
                          .total_rows = 960};
    timing.rows = {
        {.absolute_row = 0, .before_delays_seconds = earlier, .event_seconds = earlier,
         .after_stops_seconds = earlier, .effective_bpm = {120, 1}},
        {.absolute_row = 960, .before_delays_seconds = later, .event_seconds = later,
         .after_stops_seconds = later, .effective_bpm = {120, 1}}};
    const speed_event_t speed{.transition_is_seconds = true};
    return compose_input_detail::speed_transition_between(chart, &timing, speed, 0, 960);
}

void check_speed_span_transition(signed_rational_t earlier, signed_rational_t later, rational_t expected) {
    const auto transition = speed_span_transition_at_boundaries(earlier, later);
    REQUIRE(transition);
    CHECK(*transition == expected);
}
} // namespace

TEST_CASE("Speed spans reduce denominator intermediates before rejecting durations", "[sys][shared-arithmetic]") {
    check_speed_span_transition({1, 10'000'000'000ULL}, {2, 10'000'000'000ULL}, {1, 10'000'000'000ULL});
}

TEST_CASE("Speed spans reduce numerator intermediates before rejecting durations", "[sys][shared-arithmetic]") {
    check_speed_span_transition({1, 1'000'000}, {30'000'000'000'000LL, 1'000'000}, {29'999'999'999'999ULL, 1'000'000});
}

TEST_CASE("Speed spans subtract opposite signed boundaries without wide overflow", "[sys][shared-arithmetic]") {
    const auto denominator = std::numeric_limits<std::uint64_t>::max();
    check_speed_span_transition({std::numeric_limits<std::int64_t>::min(), denominator},
                               {std::numeric_limits<std::int64_t>::max(), denominator}, {1, 1});
}

TEST_CASE("Speed spans retain the full unsigned duration range", "[sys][shared-arithmetic]") {
    check_speed_span_transition({std::numeric_limits<std::int64_t>::min(), 1},
                               {std::numeric_limits<std::int64_t>::max(), 1},
                               {std::numeric_limits<std::uint64_t>::max(), 1});
}

TEST_CASE("Dead span labels use the guarded shared conversion for malformed fractions", "[sys][shared-arithmetic]") {
    const dead_span_band_t band{.kind = dead_span_kind_t::stop, .seconds = {1, 0}};
    std::string label;
    compose_projection_detail::assign_dead_span_label(label, band);
    CHECK(label == "STOP 0.000s");
}

TEST_CASE("Timing stage borrowed owner survives rejected and repaired revisions",
          "[sys][timing-stage][proposal-shape]") {
    auto history = document_history_t::from(dense_song());
    runtime::timing_edit_t editor;
    editor.stage = timing_lane_pseudonote_preview_t::propose(
        history, chart_id_t{1}, timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{2}}, false);
    const auto* borrowed = editor.stage_view();
    *editor.stage = timing_lane_pseudonote_preview_t::revise(
        std::move(*editor.stage), history, timing_bpm_value_t{{0, 1}}, false);
    CHECK(editor.stage_view() == borrowed);
    CHECK_FALSE(borrowed->value);
    CHECK(std::get<timing_bpm_value_t>(borrowed->report()->value)
              .beats_per_minute == signed_rational_t{0, 1});
    *editor.stage = timing_lane_pseudonote_preview_t::revise(
        std::move(*editor.stage), history, timing_bpm_value_t{{150, 1}}, false);
    CHECK(borrowed->value.has_value());
    CHECK(history.undo.empty());
}
