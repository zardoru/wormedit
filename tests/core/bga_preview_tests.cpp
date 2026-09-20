#include <wormedit/core/bga_preview.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using namespace wormedit;

auto visual(std::uint64_t frame, std::uint64_t object, std::uint64_t resource,
            visual_event_kind_t kind, std::size_t draw_order, std::size_t source_order)
    -> preview_visual_event_t {
    return {
        .sample_frame = frame,
        .object = object_id_t{object},
        .resource = resource_id_t{resource},
        .kind = kind,
        .draw_order = draw_order,
        .source_order = source_order,
    };
}

auto schedule() -> preview_schedule_t {
    return {.sample_rate = 44'100};
}

} // namespace

TEST_CASE("BGA preview queue rejects schedules beyond its configured capacity",
          "[unit][preview][bga][capacity]") {
    auto chart = schedule();
    chart.bga = {
        visual(0, 1, 1, visual_event_kind_t::background, 0, 0),
        visual(1, 2, 2, visual_event_kind_t::layer, 2, 1),
        visual(2, 3, 3, visual_event_kind_t::poor, 1, 2),
    };

    bga_preview_queue_t queue{2};
    CHECK(queue.start(chart).status == bga_preview_status_t::capacity_exceeded);
    CHECK_FALSE(queue.current_layers({1}).has_value());
}

TEST_CASE("BGA preview queue preserves compiled simultaneous draw/source order",
          "[unit][preview][bga][ordering]") {
    auto chart = schedule();
    chart.bga = {
        visual(7, 1, 11, visual_event_kind_t::background, 0, 1),
        visual(7, 2, 12, visual_event_kind_t::background, 0, 8),
        visual(7, 3, 20, visual_event_kind_t::poor, 1, 2),
        visual(7, 4, 30, visual_event_kind_t::layer, 2, 3),
        visual(7, 5, 40, visual_event_kind_t::video, 3, 4),
        visual(7, 6, 50, visual_event_kind_t::storyboard, 4, 5),
    };

    bga_preview_queue_t queue;
    const auto started = queue.start(chart);
    REQUIRE(started);

    const auto before = queue.advance(*started.token, 7);
    REQUIRE(before.status == bga_preview_status_t::ready);
    CHECK(before.events.empty());
    CHECK_FALSE(before.layers.background.has_value());

    const auto at = queue.advance(*started.token, 8);
    REQUIRE(at.status == bga_preview_status_t::ready);
    REQUIRE(at.events.size() == chart.bga.size());
    CHECK(at.events.data() == chart.bga.data());
    for (std::size_t index = 0; index < chart.bga.size(); ++index) {
        CHECK(at.events[index].sample_frame == chart.bga[index].sample_frame);
        CHECK(at.events[index].object == chart.bga[index].object);
        CHECK(at.events[index].resource == chart.bga[index].resource);
        CHECK(at.events[index].kind == chart.bga[index].kind);
        CHECK(at.events[index].draw_order == chart.bga[index].draw_order);
        CHECK(at.events[index].source_order == chart.bga[index].source_order);
    }
    REQUIRE(at.layers.background.has_value());
    CHECK(at.layers.background->resource == resource_id_t{12});
    REQUIRE(at.layers.poor.has_value());
    CHECK(at.layers.poor->resource == resource_id_t{20});
    REQUIRE(at.layers.layer.has_value());
    CHECK(at.layers.layer->resource == resource_id_t{30});
    REQUIRE(at.layers.video.has_value());
    CHECK(at.layers.video->resource == resource_id_t{40});
    REQUIRE(at.layers.storyboard.has_value());
    CHECK(at.layers.storyboard->resource == resource_id_t{50});

    const auto drained = queue.advance(*started.token, 9);
    CHECK(drained.events.empty());
    REQUIRE(drained.layers.background.has_value());
    CHECK(drained.layers.background->resource == at.layers.background->resource);
    REQUIRE(drained.layers.poor.has_value());
    CHECK(drained.layers.poor->resource == at.layers.poor->resource);
    REQUIRE(drained.layers.layer.has_value());
    CHECK(drained.layers.layer->resource == at.layers.layer->resource);
    REQUIRE(drained.layers.video.has_value());
    CHECK(drained.layers.video->resource == at.layers.video->resource);
    REQUIRE(drained.layers.storyboard.has_value());
    CHECK(drained.layers.storyboard->resource == at.layers.storyboard->resource);
}

TEST_CASE("BGA preview seek restores prior layer state and starts a fresh generation",
          "[unit][preview][bga][seek][generation]") {
    auto chart = schedule();
    chart.bga = {
        visual(2, 1, 11, visual_event_kind_t::background, 0, 0),
        visual(5, 2, 20, visual_event_kind_t::poor, 1, 1),
        visual(5, 3, 30, visual_event_kind_t::layer, 2, 2),
        visual(5, 5, 40, visual_event_kind_t::video, 3, 3),
        visual(5, 6, 50, visual_event_kind_t::storyboard, 4, 4),
        visual(7, 4, 12, visual_event_kind_t::background, 0, 5),
    };

    bga_preview_queue_t queue;
    const auto started = queue.start(chart, 6);
    REQUIRE(started);
    REQUIRE(started.layers.background.has_value());
    CHECK(started.layers.background->resource == resource_id_t{11});
    REQUIRE(started.layers.poor.has_value());
    CHECK(started.layers.poor->resource == resource_id_t{20});
    REQUIRE(started.layers.layer.has_value());
    CHECK(started.layers.layer->resource == resource_id_t{30});
    REQUIRE(started.layers.video.has_value());
    CHECK(started.layers.video->resource == resource_id_t{40});
    REQUIRE(started.layers.storyboard.has_value());
    CHECK(started.layers.storyboard->resource == resource_id_t{50});
    CHECK(queue.advance(*started.token, 7).events.empty());

    const auto sought = queue.seek(*started.token, 7);
    REQUIRE(sought);
    CHECK(sought.token != started.token);
    REQUIRE(sought.layers.background.has_value());
    CHECK(sought.layers.background->resource == resource_id_t{11});
    const auto at_seven = queue.advance(*sought.token, 8);
    REQUIRE(at_seven.events.size() == 1);
    CHECK(at_seven.events.front().resource == resource_id_t{12});
    CHECK(at_seven.layers.background->resource == resource_id_t{12});
    CHECK(queue.advance(*started.token, 9).status == bga_preview_status_t::stale_generation);

    const auto restarted = queue.restart(*sought.token);
    REQUIRE(restarted);
    CHECK(restarted.token != sought.token);
    CHECK_FALSE(restarted.layers.background.has_value());
    CHECK_FALSE(restarted.layers.poor.has_value());
    CHECK_FALSE(restarted.layers.layer.has_value());
    CHECK_FALSE(restarted.layers.video.has_value());
    CHECK_FALSE(restarted.layers.storyboard.has_value());
    const auto from_zero = queue.advance(*restarted.token, 3);
    REQUIRE(from_zero.events.size() == 1);
    CHECK(from_zero.events.front().resource == resource_id_t{11});

    queue.stop();
    CHECK(queue.advance(*restarted.token, 10).status == bga_preview_status_t::not_started);
    CHECK_FALSE(queue.current_layers(*restarted.token).has_value());

    const auto new_session = queue.start(chart);
    REQUIRE(new_session);
    CHECK(new_session.token != restarted.token);
    CHECK_FALSE(new_session.layers.background.has_value());
}

TEST_CASE("BGA preview queue rejects invalid and non-monotonic advancement",
          "[unit][preview][bga][negative]") {
    auto invalid_rate = schedule();
    invalid_rate.sample_rate = 0;
    bga_preview_queue_t queue;
    CHECK(queue.start(invalid_rate).status == bga_preview_status_t::invalid_schedule);

    auto unsorted = schedule();
    unsorted.bga = {
        visual(2, 1, 1, visual_event_kind_t::background, 0, 0),
        visual(1, 2, 2, visual_event_kind_t::layer, 2, 1),
    };
    CHECK(queue.start(unsorted).status == bga_preview_status_t::invalid_schedule);

    auto chart = schedule();
    chart.bga = {visual(1, 1, 1, visual_event_kind_t::background, 0, 0)};
    const auto started = queue.start(chart);
    REQUIRE(started);
    REQUIRE(queue.advance(*started.token, 3).status == bga_preview_status_t::ready);
    CHECK(queue.advance(*started.token, 2).status == bga_preview_status_t::non_monotonic_window);
}

TEST_CASE("BGA seek rejects stale generations without moving the active cursor",
          "[unit][preview][bga][negative][generation]") {
    auto chart = schedule();
    chart.bga = {
        visual(1, 1, 1, visual_event_kind_t::background, 0, 0),
        visual(3, 2, 2, visual_event_kind_t::layer, 2, 1),
    };
    bga_preview_queue_t queue;
    const auto started = queue.start(chart);
    REQUIRE(started);
    const auto sought = queue.seek(*started.token, 2);
    REQUIRE(sought);

    CHECK(queue.seek(*started.token, 3).status == bga_preview_status_t::stale_generation);
    const auto pending = queue.advance(*sought.token, 4);
    REQUIRE(pending.events.size() == 1);
    CHECK(pending.events.front().resource == resource_id_t{2});
}
