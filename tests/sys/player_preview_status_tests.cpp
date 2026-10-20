#include "sys/player_preview_presenter.h"

#include <wormedit/core/command.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {

using namespace wormedit;

auto document_with_revision(std::uint64_t revision, bool with_bpm = true) -> song_document_t {
    chart_document_t chart{
        .id = chart_id_t{7},
        .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}}},
    };
    if (with_bpm)
        chart.bpms = {{.id = object_id_t{3},
                       .position = {measure_id_t{1}, 0},
                       .beats_per_minute = {120, 1}}};
    return {.id = song_id_t{1}, .revision = revision, .charts = {std::move(chart)}};
}

// A visible view without a scene must always carry a status the frontend can show.
void check_unavailable_view_has_status(const wormedit::ui::player_preview_view_t& view) {
    CHECK(view.visible);
    CHECK_FALSE(view.scene_available);
    CHECK_FALSE(view.status_text.empty());
    CHECK(view.viewport_width_pixels > 0.0);
    CHECK(view.viewport_height_pixels > 0.0);
}

void populate_visible(sys::player_preview_presenter_t& presenter, wormedit::ui::player_preview_view_t& view,
                      const document_history_t& history, std::optional<chart_id_t> chart) {
    presenter.populate(view, history, chart, true, player_preview_skin_t::mania, 0, 48'000, {});
}

} // namespace

TEST_CASE("player preview reports a status when the index cannot be built",
          "[unit][sys][player-preview]") {
    // A chart without a BPM has no timing, so the index cannot be compiled.
    const auto history = document_history_t::from(document_with_revision(9, false));
    sys::player_preview_presenter_t presenter;
    wormedit::ui::player_preview_view_t view{};

    populate_visible(presenter, view, history, chart_id_t{7});
    check_unavailable_view_has_status(view);

    // The failed index is cached; the next frame must report the same status.
    populate_visible(presenter, view, history, chart_id_t{7});
    check_unavailable_view_has_status(view);
}

TEST_CASE("player preview reports a status when no chart is usable",
          "[unit][sys][player-preview]") {
    const auto history = document_history_t::from(document_with_revision(9));
    sys::player_preview_presenter_t presenter;
    wormedit::ui::player_preview_view_t view{};

    populate_visible(presenter, view, history, std::nullopt);
    check_unavailable_view_has_status(view);
    populate_visible(presenter, view, history, chart_id_t{99});
    check_unavailable_view_has_status(view);
}

TEST_CASE("player preview publishes a scene for a valid chart", "[unit][sys][player-preview]") {
    const auto history = document_history_t::from(document_with_revision(9));
    sys::player_preview_presenter_t presenter;
    wormedit::ui::player_preview_view_t view{};

    populate_visible(presenter, view, history, chart_id_t{7});
    CHECK(view.visible);
    CHECK(view.project_id == song_id_t{1});
    CHECK(view.document_revision == 9);
}
