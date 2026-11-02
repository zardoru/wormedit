#include "sys/storyboard/storyboard_transport_view.h"
#include "sys/storyboard/storyboard_canvas_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include "sys/storyboard/storyboard_timeline_view.h"
#include "sys/project/workspace_frame_view.h"
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#endif
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>

using namespace wormedit;
namespace {
auto selected_timeline() -> ui::storyboard_timeline_view_t {
    return {.rows = {
        {.track = {.node = "first", .kind = osbx_track_kind_t::opacity}, .editable = true,
         .keys = {{.id = "ignored-before", .time_ms = -900},
                  {.id = "left", .time_ms = 100, .selected = true}}},
        {.track = {.node = "second", .kind = osbx_track_kind_t::visibility},
         .keys = {{.id = "right", .time_ms = 400, .selected = true},
                  {.id = "ignored-after", .time_ms = 900}}}}};
}
void check_shared_scene_time(const ui::storyboard_canvas_view_t& canvas,
    const ui::storyboard_timeline_view_t& timeline, storyboard_time_t time) {
    CHECK(canvas.available);
    CHECK(canvas.time_ms == Catch::Approx(static_cast<double>(time.milliseconds) + time.fraction()));
    CHECK(timeline.time == time);
}
} // namespace

TEST_CASE("Storyboard transport loops the extrema of selected keys across readonly and editable rows",
          "[sys][storyboard-transport]") {
    const auto timeline = selected_timeline();
    const auto before = timeline;
    ui::storyboard_transport_view_t view;
    const sys::frame_timing_t timer;
    sys::build_storyboard_transport_view(view, timeline, true, false, timer);
    REQUIRE(view.selection);
    CHECK(*view.selection == ui::storyboard_loop_region_t{.begin_ms = 100, .end_ms = 400});
    CHECK(view.can_toggle_loop());
    CHECK(timeline == before);
}

TEST_CASE("Storyboard transport clears a vanished selection without losing shared loop disable",
          "[sys][storyboard-transport]") {
    auto timeline = selected_timeline();
    ui::storyboard_transport_view_t view;
    const sys::frame_timing_t timer;
    sys::build_storyboard_transport_view(view, timeline, true, false, timer);
    timeline.rows.clear();
    sys::build_storyboard_transport_view(view, timeline, true, true, timer);
    CHECK_FALSE(view.selection);
    CHECK(view.loop_enabled);
    CHECK(view.can_toggle_loop());
    sys::build_storyboard_transport_view(view, timeline, false, true, timer);
    CHECK_FALSE(view.can_toggle_loop());
}

TEST_CASE("Storyboard transport refuses zero-span selection without inventing an extra millisecond",
          "[sys][storyboard-transport]") {
    const bool single = GENERATE(false, true);
    auto timeline = selected_timeline();
    timeline.rows[1].keys[0].selected = !single;
    timeline.rows[1].keys[0].time_ms = 100;
    ui::storyboard_transport_view_t view;
    const sys::frame_timing_t timer;
    sys::build_storyboard_transport_view(view, timeline, true, false, timer);
    REQUIRE(view.selection);
    CHECK(view.selection->begin_ms == view.selection->end_ms);
    CHECK_FALSE(view.can_toggle_loop());
}

TEST_CASE("Storyboard transport preserves signed endpoints and refuses a negative audio loop",
          "[sys][storyboard-transport]") {
    auto timeline = selected_timeline();
    timeline.rows[0].keys[1].time_ms = std::numeric_limits<std::int64_t>::min();
    timeline.rows[1].keys[0].time_ms = std::numeric_limits<std::int64_t>::max();
    ui::storyboard_transport_view_t view;
    const sys::frame_timing_t timer;
    sys::build_storyboard_transport_view(view, timeline, true, false, timer);
    REQUIRE(view.selection);
    CHECK(view.selection->begin_ms == std::numeric_limits<std::int64_t>::min());
    CHECK(view.selection->end_ms == std::numeric_limits<std::int64_t>::max());
    CHECK_FALSE(view.can_toggle_loop());
}

TEST_CASE("Storyboard transport retains distinct adjacent selected times near the signed clock limit",
          "[sys][storyboard-transport]") {
    auto timeline = selected_timeline();
    timeline.rows[0].keys[1].time_ms = std::numeric_limits<std::int64_t>::max() - 1;
    timeline.rows[1].keys[0].time_ms = std::numeric_limits<std::int64_t>::max();
    ui::storyboard_transport_view_t view;
    const sys::frame_timing_t timer;
    sys::build_storyboard_transport_view(view, timeline, true, false, timer);
    REQUIRE(view.selection);
    CHECK(view.selection->is_playable());
    CHECK(view.selection->begin_ms == std::numeric_limits<std::int64_t>::max() - 1);
    CHECK(view.selection->end_ms == std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("Storyboard transport frame-cost readout borrows the last completed common timing sample",
          "[sys][storyboard-transport]") {
    sys::frame_timing_t timer;
    ui::storyboard_transport_view_t view;
    sys::build_storyboard_transport_view(view, {}, true, false, timer);
    CHECK_FALSE(view.frame_ms);
    timer.record({.frame_ms = 3.25});
    timer.record({.frame_ms = 4.75});
    sys::build_storyboard_transport_view(view, {}, true, false, timer);
    REQUIRE(view.frame_ms);
    CHECK(*view.frame_ms == 4.75);
}

TEST_CASE("Storyboard canvas and timeline consume the same fractional source transport frame",
          "[sys][storyboard-transport]") {
    const auto frame = GENERATE(1U, 22051U, 48001U);
    const sys::workspace_frame_transport_t transport{.available = true, .playing = true,
        .frame = frame, .end_frame = 96000, .sample_rate = 48000};
    const auto time = storyboard_time_t::from_frame(transport.source_frame(), transport.sample_rate);
    REQUIRE(time);
    song_document_t song{.id = song_id_t{1}, .revision = 1,
        .storyboards = {{.id = storyboard_id_t{11}, .osbx = osbx_document_t{}}}};
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_cache_t image_cache;
    task_scheduler_t image_scheduler{1, 2};
    visual::image_job_t image_job{image_cache, image_scheduler};
#endif
    sys::storyboard_scene_presenter_t scenes;
    scenes.begin(song, 7, {}
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        , image_job
#endif
    );
    ui::storyboard_view_t root;
    root.state.selection.storyboard = storyboard_id_t{11};
    sys::populate_storyboard_canvas(root.canvas, scenes, storyboard_id_t{11}, time);
    sys::storyboard_timeline_projection_t projection;
    projection.refill(root.timeline, song, root, *time);
    check_shared_scene_time(root.canvas, root.timeline, *time);
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/audio/chart_preview_state.h"
#include <wormedit/audio/decoder.h>
#include <array>

namespace {
// Seed only the existing owner's compiled query input. No replacement clock,
// source/cut mapping or session dispatcher is implemented in this fixture.
struct source_loop_fixture_t {
    song_document_t song{.id = song_id_t{1}, .revision = 1,
        .charts = {{.id = chart_id_t{7}}}};
    workspace_controller_t workspace{.song = song_id_t{1}, .document_revision = 1,
        .active_tab = workspace_tab_t::storyboard, .selected_chart = chart_id_t{7}};
    sys::runtime::chart_preview_t preview;

    void bind(std::optional<export_cut_frame_timeline_t> cuts = {}) {
        using preview_t = sys::runtime::chart_preview_t;
        const preview_t::source_key_t key{chart_id_t{7}, song.revision, std::nullopt, 0, false};
        (void)preview.compiled.get_or_build(key, [&](preview_t::compiled_t& data) {
            data.schedule = preview_schedule_t{};
            data.cut_frames = cuts;
            data.end_frame = cuts ? cuts->collapsed_frame(audio::output_sample_rate * 2)
                : audio::output_sample_rate * 2;
        });
    }
    auto region(std::int64_t begin, std::int64_t end) const -> std::optional<audio::preview_loop_region_t> {
        return preview.loop_region_at_audio_milliseconds(begin, end, song, workspace);
    }
};
} // namespace

TEST_CASE("Storyboard audio loop query uses the existing seek source-ms conversion policy",
          "[sys][storyboard-transport][audio]") {
    using preview_t = sys::runtime::chart_preview_t;
    CHECK(preview_t::source_frame_at_audio_milliseconds(-1, 44100) == 0);
    CHECK(preview_t::source_frame_at_audio_milliseconds(1, 44100) == 45);
    CHECK(preview_t::source_frame_at_audio_milliseconds(1, 48000) == 48);
    CHECK(preview_t::source_frame_at_audio_milliseconds(std::numeric_limits<std::int64_t>::max(), 44100)
        == std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("Storyboard source-audio loop query does not reapply chart offset or paused cursor state",
          "[sys][storyboard-transport][audio]") {
    source_loop_fixture_t flow; flow.bind();
    const auto region = flow.region(100, 400); REQUIRE(region);
    CHECK(*region == audio::preview_loop_region_t{4410, 17640});
    flow.song.charts[0].beat_zero_audio_offset_seconds = {-7, 4};
    flow.preview.pause_at(35000);
    CHECK(flow.region(100, 400) == region);
    CHECK(flow.preview.resume_frame == 35000);
}

TEST_CASE("Storyboard audio loop query maps source endpoints through the one cut index",
          "[sys][storyboard-transport][audio]") {
    const std::array cuts{export_cut_t{.id = object_id_t{1},
        .begin_milliseconds = 200, .end_milliseconds = 300}};
    const auto source = export_cut_timeline_t::from(cuts);
    REQUIRE(source.ok());
    const auto frames = export_cut_frame_timeline_t::from(source.value, audio::output_sample_rate);
    REQUIRE(frames.ok());
    source_loop_fixture_t flow; flow.bind(frames.value);
    const auto region = flow.region(100, 400); REQUIRE(region);
    CHECK(*region == audio::preview_loop_region_t{4410, 13230});
    CHECK_FALSE(flow.region(250, 400));
    CHECK_FALSE(flow.region(100, 250));
}

TEST_CASE("Storyboard audio loop query refuses invalid or stale ranges without changing owner state",
          "[sys][storyboard-transport][audio]") {
    const auto begin = GENERATE(std::int64_t{-1}, std::int64_t{100}, std::int64_t{3000},
        std::numeric_limits<std::int64_t>::max());
    source_loop_fixture_t flow; flow.bind(); flow.preview.pause_at(1234);
    CHECK_FALSE(flow.region(begin, 100));
    CHECK(flow.preview.resume_frame == 1234);
    CHECK(flow.preview.paused);
    ++flow.song.revision;
    CHECK_FALSE(flow.region(100, 400));
}

TEST_CASE("Storyboard audio loop query requires the current compiled selected chart",
          "[sys][storyboard-transport][audio]") {
    source_loop_fixture_t flow;
    CHECK_FALSE(flow.region(100, 400));
    flow.bind();
    flow.workspace.selected_chart.reset();
    CHECK_FALSE(flow.region(100, 400));
    flow.workspace.selected_chart = chart_id_t{8};
    CHECK_FALSE(flow.region(100, 400));
    flow.workspace.selected_chart = chart_id_t{7};
    CHECK_FALSE(flow.region(100, 3000));
}
#endif
