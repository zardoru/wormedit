#include <wormedit/core/storyboard.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace {

using namespace wormedit;

auto fade(std::int64_t start, std::int64_t end, double from, double to, std::uint64_t order = 1)
    -> storyboard_node_t {
    storyboard_command_t command;
    command.opcode = storyboard_opcode_t::fade;
    command.easing = 0;
    command.start_time_ms = start;
    command.end_time_ms = end;
    command.from[0] = from;
    command.to[0] = to;
    command.value_count = 1;
    command.source_order = order;
    return {
        .kind = storyboard_node_kind_t::command,
        .command = command,
        .source_order = order,
    };
}

} // namespace

TEST_CASE("storyboard frame arena grows in bounded chunks and reuses storage",
          "[unit][storyboard][arena]") {
    storyboard_frame_arena_t arena{{.chunk_bytes = 8, .maximum_bytes = 64, .maximum_objects = 2}};
    const auto first = arena.allocate(12, 8);
    const auto second = arena.allocate(12, 8);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first != *second);
    CHECK(arena.object_count() == 2);
    CHECK(arena.committed_bytes() >= 32);

    const auto before_failure = arena.used_bytes();
    CHECK(arena.allocate(1, 3).error() == failure_code_t::invalid_request);
    CHECK(arena.allocate(1, 8).error() == failure_code_t::capacity_exceeded);
    CHECK(arena.object_count() == 2);
    CHECK(arena.used_bytes() == before_failure);

    const auto committed = arena.committed_bytes();
    arena.reset();
    const auto reused = arena.allocate(12, 8);
    REQUIRE(reused);
    CHECK(*reused == *first);
    CHECK(arena.committed_bytes() == committed);
}

TEST_CASE("storyboard arena rejects byte exhaustion without partial allocation",
          "[unit][storyboard][arena][capacity]") {
    storyboard_frame_arena_t arena{{.chunk_bytes = 8, .maximum_bytes = 24, .maximum_objects = 10}};
    REQUIRE(arena.allocate(12, 8));
    const auto object_count = arena.object_count();
    const auto used_bytes = arena.used_bytes();
    const auto committed_bytes = arena.committed_bytes();

    const auto rejected = arena.allocate(20, 8);
    CHECK_FALSE(rejected);
    CHECK(rejected.error() == failure_code_t::capacity_exceeded);
    CHECK(arena.object_count() == object_count);
    CHECK(arena.used_bytes() == used_bytes);
    CHECK(arena.committed_bytes() == committed_bytes);
}

TEST_CASE("OSB parser and emitter preserve nested loop semantics deterministically",
          "[unit][storyboard][osb][loops]") {
    constexpr std::string_view source = "Sprite,Foreground,Centre,\"images/a,b.png\",320,240\n"
                                        " L,100,2\n"
                                        "  F,5,0,100,0,1\n"
                                        "  L,25,3\n"
                                        "   M,0,0,50,0,0,10,20\n"
                                        "   T,HitSound,0,100\n"
                                        " L,500,0\n"
                                        "  F,0,0,10,1,0\n";
    const auto parsed = parse_osb(source);
    REQUIRE(parsed.value);
    REQUIRE(parsed.value->sprites.size() == 1);
    const auto& sprite = parsed.value->sprites.front();
    REQUIRE(sprite.nodes.size() == 2);
    REQUIRE(sprite.nodes[0].kind == storyboard_node_kind_t::loop);
    REQUIRE(sprite.nodes[0].children.size() == 2);
    REQUIRE(sprite.nodes[0].children[1].kind == storyboard_node_kind_t::loop);
    CHECK(sprite.nodes[0].children[1].loop_iteration_count == 3);
    CHECK(sprite.nodes[1].loop_iteration_count == 0);

    const auto emitted = emit_osb(*parsed.value);
    REQUIRE(emitted.value);
    const auto emitted_again = emit_osb(*parsed.value);
    REQUIRE(emitted_again.value);
    CHECK(*emitted.value == *emitted_again.value);
    CHECK(emitted.value->find("   T,HitSound,0,100\n") != std::string::npos);

    const auto reparsed = parse_osb(*emitted.value);
    REQUIRE(reparsed.value);
    CHECK(storyboard_semantically_equal(*parsed.value, *reparsed.value));
}

TEST_CASE("OSB shorthand canonicalizes timestamps and sequential endpoint groups",
          "[unit][storyboard][osb][shorthand]") {
    constexpr std::string_view source = "Sprite,Foreground,Centre,\"short.png\",320,240\n"
                                        " F,0,5,,0.5\n"
                                        " MX,0,10,20,12\n"
                                        " M,0,30,40,0,0,10,10,20,20\n";
    const auto parsed = parse_osb(source);
    REQUIRE(parsed.value);
    const auto& nodes = parsed.value->sprites.front().nodes;
    REQUIRE(nodes.size() == 4);
    CHECK(nodes[0].command.start_time_ms == 5);
    CHECK(nodes[0].command.end_time_ms == 5);
    CHECK(nodes[0].command.from[0] == 0.5);
    CHECK(nodes[0].command.to[0] == 0.5);
    CHECK(nodes[1].command.from[0] == 12.0);
    CHECK(nodes[1].command.to[0] == 12.0);
    CHECK(nodes[2].command.start_time_ms == 30);
    CHECK(nodes[2].command.end_time_ms == 40);
    CHECK(nodes[2].command.from[0] == 0.0);
    CHECK(nodes[2].command.to[0] == 10.0);
    CHECK(nodes[3].command.start_time_ms == 40);
    CHECK(nodes[3].command.end_time_ms == 50);
    CHECK(nodes[3].command.from[0] == 10.0);
    CHECK(nodes[3].command.to[0] == 20.0);

    const auto emitted = emit_osb(*parsed.value);
    REQUIRE(emitted.value);
    const auto reparsed = parse_osb(*emitted.value);
    REQUIRE(reparsed.value);
    CHECK(storyboard_semantically_equal(*parsed.value, *reparsed.value));

    const auto invalid = parse_osb("Sprite,Foreground,Centre,\"bad.png\",0,0\n F,35,0,1,0,1\n");
    CHECK_FALSE(invalid.value);
    REQUIRE_FALSE(invalid.diagnostics.empty());
    CHECK(invalid.diagnostics.front().code == storyboard_diagnostic_code_t::invalid_command);
}

TEST_CASE("OSB easing 15 through 34 evaluates and invalid model easing is rejected",
          "[unit][storyboard][easing]") {
    const auto sample = [](std::int32_t easing, std::int64_t time) {
        storyboard_sprite_t sprite;
        sprite.nodes.push_back(fade(0, 100, 0.0, 1.0));
        sprite.nodes.front().command.easing = easing;
        return evaluate_storyboard_property(sprite, storyboard_opcode_t::fade, time);
    };

    for (std::int32_t easing = 15; easing <= 34; ++easing) {
        const auto value = sample(easing, 50);
        INFO("easing=" << easing);
        REQUIRE(value.succeeded);
        REQUIRE(value.value.present);
        CHECK(std::isfinite(value.value.values[0]));
        CHECK(sample(easing, 0).value.values[0] == 0.0);
        CHECK(sample(easing, 100).value.values[0] == 1.0);
    }
    CHECK(std::abs(sample(15, 50).value.values[0] - (1.0 - std::sqrt(0.5))) < 1e-12);
    CHECK(std::abs(sample(16, 50).value.values[0] - std::sqrt(0.5)) < 1e-12);
    CHECK(std::abs(sample(26, 50).value.values[0] - 0.984130859375) < 1e-12);
    CHECK(std::abs(sample(27, 50).value.values[0] - 1.027307435) < 1e-8);

    storyboard_document_model_t invalid_document;
    storyboard_sprite_t sprite;
    sprite.nodes.push_back(fade(0, 100, 0.0, 1.0));
    sprite.nodes.front().command.easing = 35;
    invalid_document.sprites.push_back(sprite);
    CHECK_FALSE(compile_storyboard_schedule(invalid_document).value);
    CHECK_FALSE(emit_osb(invalid_document).value);
    CHECK_FALSE(sample(35, 50).succeeded);
}

TEST_CASE("storyboard semantic equality compares retained directives and ignores blank lines",
          "[unit][storyboard][osb][semantic]") {
    const auto with_blanks =
        parse_osb("[Events]\n\nSprite,Foreground,Centre,\"a.png\",0,0\n  _P,0,1,2,H\n   \n");
    const auto no_blanks =
        parse_osb("[Events]\nSprite,Foreground,Centre,\"a.png\",0,0\n  _P,0,1,2,H\n");
    REQUIRE(with_blanks.value);
    REQUIRE(no_blanks.value);
    CHECK(storyboard_semantically_equal(*with_blanks.value, *no_blanks.value));

    auto changed_top_level = *no_blanks.value;
    REQUIRE_FALSE(changed_top_level.retained_top_level_lines.empty());
    changed_top_level.retained_top_level_lines.front().text = "[OtherEvents]";
    CHECK_FALSE(storyboard_semantically_equal(*no_blanks.value, changed_top_level));

    auto changed_sprite_directive = *no_blanks.value;
    REQUIRE_FALSE(changed_sprite_directive.sprites.front().nodes.empty());
    changed_sprite_directive.sprites.front().nodes.front().retained_line = "  _P,0,1,2,V";
    CHECK_FALSE(storyboard_semantically_equal(*no_blanks.value, changed_sprite_directive));
}

TEST_CASE("storyboard schedule seeks by time and advances one contiguous head",
          "[unit][storyboard][schedule][seek]") {
    storyboard_document_model_t document;
    storyboard_sprite_t sprite;
    sprite.stable_id = 12;
    sprite.source_order = 4;
    sprite.layer = "Foreground";
    sprite.origin = "Centre";
    sprite.path = "sprite.png";
    sprite.nodes = {fade(10, 20, 0.0, 1.0, 8), fade(10, 30, 1.0, 0.0, 3)};
    document.sprites.push_back(sprite);

    const auto built = compile_storyboard_schedule(document);
    REQUIRE(built.value);
    REQUIRE(built.value->events.size() == 4);
    CHECK(built.value->events[0].time_ms == 10);
    CHECK(built.value->events[0].source_order == 3);
    CHECK(built.value->events[1].time_ms == 10);
    CHECK(built.value->events[1].source_order == 8);

    const auto bounded = compile_storyboard_schedule(
        document, {.maximum_events = 1, .maximum_nodes = 10, .maximum_nesting = 4});
    CHECK_FALSE(bounded.value);
    CHECK(bounded.diagnostics.front().code == storyboard_diagnostic_code_t::capacity_exceeded);

    storyboard_event_cursor_t cursor;
    CHECK(cursor.start(*built.value, 10).has_value());
    const auto first_window = cursor.advance_events(21);
    REQUIRE(first_window);
    CHECK(first_window->size() == 3);
    CHECK((*first_window)[0].time_ms == 10);
    CHECK((*first_window)[2].time_ms == 20);
    CHECK(cursor.advance_events(19).error() == failure_code_t::invalid_state);

    CHECK(cursor.seek(25).has_value());
    const auto after_seek = cursor.advance_events(31);
    REQUIRE(after_seek);
    REQUIRE(after_seek->size() == 1);
    CHECK(after_seek->front().time_ms == 30);
}

TEST_CASE("large OSB loop counts remain structural and interpolate in each iteration",
          "[unit][storyboard][loops][bounded]") {
    storyboard_document_model_t document;
    storyboard_sprite_t sprite;
    sprite.stable_id = 1;
    sprite.layer = "Foreground";
    sprite.origin = "Centre";
    sprite.path = "sprite.png";
    storyboard_node_t loop;
    loop.kind = storyboard_node_kind_t::loop;
    loop.loop_start_time_ms = 100;
    loop.loop_iteration_count = 4'000'000'000ULL;
    loop.source_order = 1;
    auto repeated_fade = fade(0, 100, 0.0, 1.0, 2);
    repeated_fade.command.easing = 5;
    loop.children.push_back(repeated_fade);
    sprite.nodes.push_back(loop);
    document.sprites.push_back(sprite);

    const auto schedule = compile_storyboard_schedule(document);
    REQUIRE(schedule.value);
    CHECK(schedule.value->events.size() == 2);
    CHECK(schedule.value->events[0].time_ms == 100);
    CHECK(schedule.value->events[1].time_ms == 400'000'000'100LL);

    const auto first_iteration =
        evaluate_storyboard_property(document.sprites.front(), storyboard_opcode_t::fade, 150);
    REQUIRE(first_iteration.succeeded);
    REQUIRE(first_iteration.value.present);
    CHECK(first_iteration.value.values[0] == 0.5);

    const auto next_iteration =
        evaluate_storyboard_property(document.sprites.front(), storyboard_opcode_t::fade, 250);
    REQUIRE(next_iteration.succeeded);
    REQUIRE(next_iteration.value.present);
    CHECK(next_iteration.value.values[0] == 0.5);

    const auto easing =
        evaluate_storyboard_property(document.sprites.front(), storyboard_opcode_t::fade, 125);
    REQUIRE(easing.succeeded);
    REQUIRE(easing.value.present);
    CHECK(easing.value.values[0] == 0.125);
}

TEST_CASE("storyboard event schedule rejects loop time overflow atomically",
          "[unit][storyboard][schedule][capacity]") {
    storyboard_document_model_t document;
    storyboard_sprite_t sprite;
    storyboard_node_t loop;
    loop.kind = storyboard_node_kind_t::loop;
    loop.loop_start_time_ms = 0;
    loop.loop_iteration_count = std::numeric_limits<std::uint64_t>::max();
    loop.children.push_back(fade(0, 2, 0.0, 1.0));
    sprite.nodes.push_back(loop);
    document.sprites.push_back(sprite);

    const auto result = compile_storyboard_schedule(document);
    CHECK_FALSE(result.value);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == storyboard_diagnostic_code_t::time_overflow);
}

TEST_CASE("OSB parsing rejects configured bounds before publishing a document",
          "[unit][storyboard][osb][limits]") {
    const auto input_limited =
        parse_osb("Sprite,Foreground,Centre,\"a.png\",0,0\n", {.maximum_input_bytes = 4});
    CHECK_FALSE(input_limited.value);
    CHECK(input_limited.diagnostics.front().code == storyboard_diagnostic_code_t::input_too_large);

    const auto node_limited = parse_osb(
        "Sprite,Foreground,Centre,\"a.png\",0,0\n L,0,1\n  F,0,0,1,0,1\n", {.maximum_nodes = 1});
    CHECK_FALSE(node_limited.value);
    CHECK(node_limited.diagnostics.front().code == storyboard_diagnostic_code_t::node_limit);
}

namespace {
auto has_error_annotated_storyboard_values() -> bool {
    const storyboard_diagnostic_t diagnostic{storyboard_diagnostic_code_t::invalid_model, 7, "error"};
    storyboard_parse_outcome_t parsed{.value = storyboard_document_model_t{}, .diagnostics = {diagnostic}};
    storyboard_emit_outcome_t emitted{.value = std::string{"text"}, .diagnostics = {diagnostic}};
    storyboard_schedule_outcome_t scheduled{.value = storyboard_schedule_t{}, .diagnostics = {diagnostic}};
    if (parsed.ok() || emitted.ok() || scheduled.ok()) return false;
    return parsed.value && emitted.value && scheduled.value && parsed.diagnostics.size() == 1 &&
        emitted.diagnostics == parsed.diagnostics && scheduled.diagnostics == parsed.diagnostics;
}
} // namespace
TEST_CASE("storyboard value-presence acceptance is independent of diagnostics", "[core][storyboard][results]") {
    CHECK(has_error_annotated_storyboard_values());
}

TEST_CASE("storyboard cursor errors and missing properties remain distinct outcomes", "[core][storyboard][results]") {
    storyboard_event_cursor_t cursor;
    const auto advance = cursor.advance_events(1);
    REQUIRE_FALSE(advance);
    CHECK(advance.error() == failure_code_t::not_started);
    const storyboard_sprite_t sprite;
    auto evaluated = evaluate_storyboard_property(sprite, storyboard_opcode_t::fade, 0);
    REQUIRE(evaluated.succeeded);
    CHECK_FALSE(evaluated.value.present);
}
