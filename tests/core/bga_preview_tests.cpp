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

void check_layer2_state(bool value) { CHECK(value); }

void check_rejected_window(const bga_preview_advance_outcome_t& rejected,
                           const bga_layer_state_t& previous,
                           const bga_layer_state_t& current) {
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == failure_code_t::invalid_state);
    CHECK(current == previous);
}

void check_resumed_window(const bga_preview_advance_outcome_t& resumed,
                          const bga_layer_state_t& current) {
    REQUIRE(resumed);
    REQUIRE(resumed->size() == 1);
    REQUIRE(current.background);
    CHECK(resumed->front().object == object_id_t{2});
    CHECK(current.background->resource == resource_id_t{12});
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
    CHECK(queue.begin(chart) == std::unexpected(failure_code_t::capacity_exceeded));
    CHECK_FALSE(queue.current_layers({1}).has_value());
}

TEST_CASE("BGA preview queue accepts Layer 2 without mapping it onto the Layer slot",
          "[unit][preview][bga][layer2]") {
    auto chart = schedule();
    chart.bga = {visual(0, 1, 1, visual_event_kind_t::layer2, 3, 0)};

    bga_preview_queue_t queue;
    const auto started = queue.begin(chart);
    check_layer2_state(started.has_value());
    REQUIRE(started);
    const auto started_layers = queue.current_layers(*started).value();
    const auto advanced = queue.advance_events(*started, 1);
    const auto advanced_layers = queue.current_layers(*started).value();
    REQUIRE(advanced_layers.layer2.has_value());
    check_layer2_state(advanced_layers.layer2->resource == resource_id_t{1});
    check_layer2_state(!started_layers.layer.has_value());
    const auto sought = queue.reposition(*started, 1);
    REQUIRE(sought);
    check_layer2_state(queue.current_layers(*sought).value().layer2 == advanced_layers.layer2);
    const auto stale_restart = queue.reposition(*started, 0);
    check_layer2_state(stale_restart == std::unexpected(failure_code_t::stale_generation));
    check_layer2_state(!queue.current_layers(*started).has_value());
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
    const auto started = queue.begin(chart);
    REQUIRE(started);

    const auto before = queue.advance_events(*started, 7);
    REQUIRE(before.has_value());
    const auto before_layers = queue.current_layers(*started).value();
    CHECK(before.value().empty());
    CHECK_FALSE(before_layers.background.has_value());

    const auto at = queue.advance_events(*started, 8);
    REQUIRE(at.has_value());
    const auto at_layers = queue.current_layers(*started).value();
    REQUIRE(at.value().size() == chart.bga.size());
    CHECK(at.value().data() == chart.bga.data());
    for (std::size_t index = 0; index < chart.bga.size(); ++index) {
        CHECK(at.value()[index].sample_frame == chart.bga[index].sample_frame);
        CHECK(at.value()[index].object == chart.bga[index].object);
        CHECK(at.value()[index].resource == chart.bga[index].resource);
        CHECK(at.value()[index].kind == chart.bga[index].kind);
        CHECK(at.value()[index].draw_order == chart.bga[index].draw_order);
        CHECK(at.value()[index].source_order == chart.bga[index].source_order);
    }
    REQUIRE(at_layers.background.has_value());
    CHECK(at_layers.background->resource == resource_id_t{12});
    REQUIRE(at_layers.poor.has_value());
    CHECK(at_layers.poor->resource == resource_id_t{20});
    REQUIRE(at_layers.layer.has_value());
    CHECK(at_layers.layer->resource == resource_id_t{30});
    REQUIRE(at_layers.video.has_value());
    CHECK(at_layers.video->resource == resource_id_t{40});
    REQUIRE(at_layers.storyboard.has_value());
    CHECK(at_layers.storyboard->resource == resource_id_t{50});

    const auto drained = queue.advance_events(*started, 9);
    const auto drained_layers = queue.current_layers(*started).value();
    CHECK(drained.value().empty());
    REQUIRE(drained_layers.background.has_value());
    CHECK(drained_layers.background->resource == at_layers.background->resource);
    REQUIRE(drained_layers.poor.has_value());
    CHECK(drained_layers.poor->resource == at_layers.poor->resource);
    REQUIRE(drained_layers.layer.has_value());
    CHECK(drained_layers.layer->resource == at_layers.layer->resource);
    REQUIRE(drained_layers.video.has_value());
    CHECK(drained_layers.video->resource == at_layers.video->resource);
    REQUIRE(drained_layers.storyboard.has_value());
    CHECK(drained_layers.storyboard->resource == at_layers.storyboard->resource);
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
    const auto started = queue.begin(chart, 6);
    REQUIRE(started);
    const auto started_layers = queue.current_layers(*started).value();
    REQUIRE(started_layers.background.has_value());
    CHECK(started_layers.background->resource == resource_id_t{11});
    REQUIRE(started_layers.poor.has_value());
    CHECK(started_layers.poor->resource == resource_id_t{20});
    REQUIRE(started_layers.layer.has_value());
    CHECK(started_layers.layer->resource == resource_id_t{30});
    REQUIRE(started_layers.video.has_value());
    CHECK(started_layers.video->resource == resource_id_t{40});
    REQUIRE(started_layers.storyboard.has_value());
    CHECK(started_layers.storyboard->resource == resource_id_t{50});
    CHECK(queue.advance_events(*started, 7).value().empty());

    const auto sought = queue.reposition(*started, 7);
    REQUIRE(sought);
    const auto sought_layers = queue.current_layers(*sought).value();
    CHECK(sought != started);
    REQUIRE(sought_layers.background.has_value());
    CHECK(sought_layers.background->resource == resource_id_t{11});
    const auto at_seven = queue.advance_events(*sought, 8);
    const auto at_seven_layers = queue.current_layers(*sought).value();
    REQUIRE(at_seven.value().size() == 1);
    CHECK(at_seven.value().front().resource == resource_id_t{12});
    CHECK(at_seven_layers.background->resource == resource_id_t{12});
    CHECK(queue.advance_events(*started, 9) == std::unexpected(failure_code_t::stale_generation));

    const auto restarted = queue.reposition(*sought, 0);
    REQUIRE(restarted);
    const auto restarted_layers = queue.current_layers(*restarted).value();
    CHECK(restarted != sought);
    CHECK_FALSE(restarted_layers.background.has_value());
    CHECK_FALSE(restarted_layers.poor.has_value());
    CHECK_FALSE(restarted_layers.layer.has_value());
    CHECK_FALSE(restarted_layers.video.has_value());
    CHECK_FALSE(restarted_layers.storyboard.has_value());
    const auto from_zero = queue.advance_events(*restarted, 3);
    REQUIRE(from_zero.value().size() == 1);
    CHECK(from_zero.value().front().resource == resource_id_t{11});

    queue.stop();
    CHECK(queue.advance_events(*restarted, 10) == std::unexpected(failure_code_t::not_started));
    CHECK_FALSE(queue.current_layers(*restarted).has_value());

    const auto new_session = queue.begin(chart);
    REQUIRE(new_session);
    const auto new_session_layers = queue.current_layers(*new_session).value();
    CHECK(new_session != restarted);
    CHECK_FALSE(new_session_layers.background.has_value());
}

TEST_CASE("BGA preview queue rejects invalid and non-monotonic advancement",
          "[unit][preview][bga][negative]") {
    auto invalid_rate = schedule();
    invalid_rate.sample_rate = 0;
    bga_preview_queue_t queue;
    CHECK(queue.begin(invalid_rate) == std::unexpected(failure_code_t::invalid_data));

    auto unsorted = schedule();
    unsorted.bga = {
        visual(2, 1, 1, visual_event_kind_t::background, 0, 0),
        visual(1, 2, 2, visual_event_kind_t::layer, 2, 1),
    };
    CHECK(queue.begin(unsorted) == std::unexpected(failure_code_t::invalid_data));

    auto chart = schedule();
    chart.bga = {visual(1, 1, 1, visual_event_kind_t::background, 0, 0)};
    const auto started = queue.begin(chart);
    REQUIRE(started);
    REQUIRE(queue.advance_events(*started, 3).has_value());
    CHECK(queue.advance_events(*started, 2) == std::unexpected(failure_code_t::invalid_state));
}

TEST_CASE("BGA seek rejects stale generations without moving the active cursor",
          "[unit][preview][bga][negative][generation]") {
    auto chart = schedule();
    chart.bga = {
        visual(1, 1, 1, visual_event_kind_t::background, 0, 0),
        visual(3, 2, 2, visual_event_kind_t::layer, 2, 1),
    };
    bga_preview_queue_t queue;
    const auto started = queue.begin(chart);
    REQUIRE(started);
    const auto sought = queue.reposition(*started, 2);
    REQUIRE(sought);

    CHECK(queue.reposition(*started, 3) == std::unexpected(failure_code_t::stale_generation));
    const auto pending = queue.advance_events(*sought, 4);
    REQUIRE(pending.value().size() == 1);
    CHECK(pending.value().front().resource == resource_id_t{2});
}

TEST_CASE("BGA preview rejected windows retain layers without consuming events",
          "[unit][preview][bga][window]") {
    auto chart = schedule();
    chart.bga = {
        visual(0, 1, 11, visual_event_kind_t::background, 0, 0),
        visual(2, 2, 12, visual_event_kind_t::background, 0, 1),
    };
    bga_preview_queue_t queue;
    const auto started = queue.begin(chart);
    REQUIRE(started);
    const auto first = queue.advance_events(*started, 1);
    const auto first_layers = queue.current_layers(*started).value();
    REQUIRE(first_layers.background);
    const auto rejected = queue.advance_events(*started, 0);
    check_rejected_window(rejected, first_layers, queue.current_layers(*started).value());
    const auto resumed = queue.advance_events(*started, 3);
    check_resumed_window(resumed, queue.current_layers(*started).value());
}

TEST_CASE("BGA preview stale windows return an empty layer snapshot",
          "[unit][preview][bga][window]") {
    auto chart = schedule();
    chart.bga = {visual(0, 1, 11, visual_event_kind_t::background, 0, 0)};
    bga_preview_queue_t queue;
    const auto started = queue.begin(chart);
    REQUIRE(started);
    (void)queue.advance_events(*started, 1);
    const auto stale = queue.advance_events({started->generation + 1}, 2);
    const auto stale_layers = queue.current_layers({started->generation + 1}).value_or(bga_layer_state_t{});
    CHECK(stale == std::unexpected(failure_code_t::stale_generation));
    CHECK_FALSE(stale.has_value());
    CHECK(stale_layers == bga_layer_state_t{});
}
