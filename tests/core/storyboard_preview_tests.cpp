#include <wormedit/core/storyboard_preview.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <string>
#include <utility>

namespace {

using namespace wormedit;

auto command(storyboard_opcode_t opcode, std::int64_t start, std::int64_t end,
             std::initializer_list<double> from, std::initializer_list<double> to,
             std::uint64_t order) -> storyboard_node_t {
    storyboard_command_t value;
    value.opcode = opcode;
    value.start_time_ms = start;
    value.end_time_ms = end;
    value.value_count = static_cast<std::uint8_t>(from.size());
    value.source_order = order;
    std::ranges::copy(from, value.from.begin());
    std::ranges::copy(to, value.to.begin());
    return {.kind = storyboard_node_kind_t::command, .command = value, .source_order = order};
}

auto sprite(std::uint64_t id, std::uint64_t order, std::string layer, std::string path)
    -> storyboard_sprite_t {
    return {.stable_id = id,
            .source_order = order,
            .layer = std::move(layer),
            .origin = "Centre",
            .path = std::move(path),
            .x = 320.0,
            .y = 240.0};
}

} // namespace

TEST_CASE("storyboard preview evaluates a bounded renderer-neutral frame",
          "[unit][storyboard][preview]") {
    storyboard_document_model_t document;
    auto foreground = sprite(10, 8, "Foreground", "front.png");
    foreground.nodes = {
        command(storyboard_opcode_t::move, 100, 200, {0.0, 10.0}, {100.0, 110.0}, 1),
        command(storyboard_opcode_t::move_x, 125, 175, {50.0}, {70.0}, 2),
        command(storyboard_opcode_t::fade, 100, 200, {0.0}, {1.0}, 3),
        command(storyboard_opcode_t::vector_scale, 100, 200, {1.0, 2.0}, {2.0, 4.0}, 4),
        command(storyboard_opcode_t::color, 100, 200, {255.0, 0.0, 10.0}, {5.0, 200.0, 210.0}, 5),
    };
    auto background = sprite(20, 2, "Background", "back.png");
    background.nodes = {
        command(storyboard_opcode_t::fade, 120, 180, {1.0}, {0.5}, 6),
    };
    document.sprites = {foreground, background};

    const auto built = storyboard_preview_index_result_t::from(storyboard_preview_index_t::from(document));
    REQUIRE(built);
    REQUIRE(built.index->sources.size() == 2);
    CHECK(built.index->sources[0].sprite_index == 1);
    CHECK(built.index->sources[1].sprite_index == 0);

    storyboard_frame_arena_t arena;
    const auto frame = evaluate_storyboard_frame(document, *built.index, 150, arena);
    REQUIRE(frame);
    REQUIRE(frame->instances.size() == 2);
    CHECK(frame->instances[0].path == "back.png");
    CHECK(frame->instances[0].layer_order == 0);
    CHECK(frame->instances[1].path == "front.png");
    CHECK(frame->instances[1].layer_order == 3);
    CHECK(frame->instances[1].x == 60.0);
    CHECK(frame->instances[1].y == 60.0);
    CHECK(frame->instances[1].opacity == 0.5);
    CHECK(frame->instances[1].scale_x == 1.5);
    CHECK(frame->instances[1].scale_y == 3.0);
    CHECK(frame->instances[1].red == 130.0);
    CHECK(frame->instances[1].green == 100.0);
    CHECK(frame->instances[1].blue == 110.0);

    const auto before = evaluate_storyboard_frame(document, *built.index, 99, arena);
    REQUIRE(before);
    CHECK(before->instances.empty());
    const auto after = evaluate_storyboard_frame(document, *built.index, 201, arena);
    REQUIRE(after);
    CHECK(after->instances.empty());
}

TEST_CASE("storyboard preview rejects stale, ambiguous, and over-capacity state",
          "[unit][storyboard][preview][negative]") {
    storyboard_document_model_t duplicate;
    auto first = sprite(1, 1, "Background", "a.png");
    first.nodes = {command(storyboard_opcode_t::fade, 0, 10, {0.0}, {1.0}, 1)};
    auto second = sprite(1, 2, "Foreground", "b.png");
    second.nodes = {command(storyboard_opcode_t::fade, 0, 10, {0.0}, {1.0}, 2)};
    duplicate.sprites = {first, second};
    CHECK_FALSE(build_storyboard_preview_index(duplicate));

    duplicate.sprites[1].stable_id = 2;
    const auto built = build_storyboard_preview_index(duplicate);
    REQUIRE(built);
    storyboard_frame_arena_t arena;
    const auto capped =
        evaluate_storyboard_frame(duplicate, *built.index, 5, arena, {.maximum_instances = 1});
    CHECK_FALSE(capped);
    REQUIRE(capped.error().diagnostic.has_value());
    CHECK(capped.error().diagnostic->code == storyboard_diagnostic_code_t::capacity_exceeded);

    auto stale = *built.index;
    stale.sources.front().stable_id = 99;
    const auto rejected = evaluate_storyboard_frame(duplicate, stale, 5, arena);
    CHECK_FALSE(rejected);
    REQUIRE(rejected.error().diagnostic.has_value());
    CHECK(rejected.error().diagnostic->code == storyboard_diagnostic_code_t::invalid_model);
}

TEST_CASE("storyboard frame expected preserves quiet empty success", "[core][storyboard][preview][results]") {
    const storyboard_document_model_t document;
    const storyboard_preview_index_t index;
    storyboard_frame_arena_t arena;
    auto frame = evaluate_storyboard_frame(document, index, 0, arena);
    REQUIRE(frame);
    CHECK(frame->instances.empty());
}
