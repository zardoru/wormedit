#include "sys/storyboard/storyboard_timeline_view.h"
#include "sys/app/frame_timing.h"
#include "../support/storyboard_timeline_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using namespace wormedit;
using test::timeline_song;
using test::timeline_root;

TEST_CASE("Warmed storyboard timeline projection keeps row key storage without frame allocations",
          "[sys][storyboard-timeline][allocation]") {
    const auto song = timeline_song();
    auto root = timeline_root();
    ui::storyboard_timeline_view_t view;
    sys::storyboard_timeline_projection_t projection;
    projection.refill(view, song, root, {});
    auto* rows = view.rows.data(); auto* keys = view.rows[1].keys.data();
    root.state.selection.keys = {"key-b"};
    const auto before = sys::frame_heap_allocation_snapshot();
    for (std::int64_t i = 0; i < 100; ++i) projection.refill(view, song, root, {.milliseconds = i});
    const auto after = sys::frame_heap_allocation_snapshot();
    CHECK(after.count == before.count);
    CHECK(view.rows.data() == rows);
    CHECK(view.rows[1].keys.data() == keys);
    CHECK(view.rows[1].keys[1].selected);
    CHECK(view.time.milliseconds == 99);
}
