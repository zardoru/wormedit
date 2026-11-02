#include <wormedit/core/storyboard.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>

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
    constexpr std::string_view source = "Sprite,Foreground,Centre,\"images/a b.png\",320,240\n"
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

TEST_CASE("OSB sprite paths follow osu! comma splitting and quote trimming",
          "[unit][storyboard][osb]") {
    CHECK_FALSE(parse_osb("Sprite,Foreground,Centre,\"images/a,b.png\",320,240\n").value);
    const auto parsed = parse_osb("Sprite,Foreground,Centre,\"images/a.png\",320,240\n F,0,0,1,0,1\n");
    REQUIRE(parsed.value);
    REQUIRE(parsed.value->sprites.size() == 1);
    CHECK(parsed.value->sprites.front().path == "images/a.png");
    for (const auto* path : {"images/a,b.png", "images/\"a\".png"}) {
        auto invalid = *parsed.value;
        invalid.sprites.front().path = path;
        CHECK_FALSE(emit_osb(invalid).value);
    }
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


namespace {
auto checked_animation_emission(const storyboard_document_model_t& model,
    std::optional<std::string_view> expected_bytes = {}) -> std::string {
    const auto emitted = emit_osb(model);
    REQUIRE(emitted.value);
    if (expected_bytes) CHECK(*emitted.value == *expected_bytes);
    return *emitted.value;
}
auto checked_animation_roundtrip(const storyboard_document_model_t& model,
    std::optional<std::string_view> expected_bytes = {}) -> storyboard_document_model_t {
    const auto bytes = checked_animation_emission(model, expected_bytes);
    const auto reparsed = parse_osb(bytes);
    REQUIRE(reparsed.value);
    CHECK(storyboard_semantically_equal(model, *reparsed.value));
    return *reparsed.value;
}
void check_nested_animation_shape(const storyboard_sprite_t& animation) {
    REQUIRE(animation.animation);
    CHECK(animation.animation->frame_count == 12);
    CHECK(animation.animation->frame_delay_ms == 31.25);
    CHECK(animation.animation->loop_type == storyboard_animation_loop_t::loop_once);
    REQUIRE(animation.nodes.size() == 1);
    CHECK(animation.nodes[0].children[1].kind == storyboard_node_kind_t::loop);
}
void check_retained_animation_owner(const storyboard_document_model_t& model) {
    REQUIRE(model.sprites.size() == 1);
    CHECK(model.sprites.front().nodes.size() == 1);
    REQUIRE(model.retained_top_level_lines.size() == 3);
}
}
TEST_CASE("OSB Animation shares nested command structure and roundtrips in object order", "[storyboard][osb][animation]") {
    constexpr std::string_view source = "Sprite,Background,Centre,\"still.png\",0,0\n"
        " F,0,0,100,1,1\nAnimation,Foreground,Centre,\"frames/burst.png\",320,240,12,31.25,LoopOnce\n"
        " L,100,2\n  F,0,0,50,0,1\n  L,25,3\n   MX,0,0,10,0,20\n"
        "   Unknown,keep,this\n";
    const auto parsed = parse_osb(source);
    REQUIRE(parsed.value);
    CHECK(parsed.diagnostics.empty());
    REQUIRE(parsed.value->sprites.size() == 2);
    CHECK_FALSE(parsed.value->sprites[0].animation);
    const auto& animation = parsed.value->sprites[1];
    check_nested_animation_shape(animation);
    auto changed = checked_animation_roundtrip(*parsed.value, source);
    changed.sprites[1].animation->frame_count = 13;
    CHECK_FALSE(storyboard_semantically_equal(*parsed.value, changed));
}

TEST_CASE("OSB Animation loop defaults and decimal delay use the common numeric model", "[storyboard][osb][animation]") {
    for (const auto* suffix : {"", ",LoopForever", ",LoopOnce"}) {
        const auto source = std::string("Animation,Fail,TopLeft,\"a.png\",10,20,2,16.5") + suffix + "\n F,0,0,100,1,0\n";
        const auto parsed = parse_osb(source);
        REQUIRE(parsed.value);
        REQUIRE(parsed.value->sprites.front().animation);
        const auto expected = std::string_view(suffix) == ",LoopOnce" ? storyboard_animation_loop_t::loop_once
            : storyboard_animation_loop_t::loop_forever;
        CHECK(parsed.value->sprites.front().animation->loop_type == expected);
        (void)checked_animation_roundtrip(*parsed.value);
    }
}

TEST_CASE("malformed OSB Animation headers retain their complete raw block and reset the active object", "[storyboard][osb][animation]") {
    for (const auto* tail : {"0,100,LoopForever", "2,0,LoopOnce", "2,-1,LoopOnce", "2,nan,LoopOnce",
            "2,inf,LoopOnce", "2147483648,100,LoopOnce", "2,100,Unknown", "2", "2,100,LoopOnce,extra"}) {
        const auto malformed = std::string("Animation,Foreground,Centre,\"bad.png\",0,0,") + tail + "\n L,0,2\n  F,0,0,10,0,1\n";
        const auto source = std::string("Sprite,Background,Centre,\"a.png\",0,0\n F,0,0,10,1,1\n") + malformed;
        const auto parsed = parse_osb(source);
        REQUIRE(parsed.value);
        REQUIRE_FALSE(parsed.diagnostics.empty());
        check_retained_animation_owner(*parsed.value);
        (void)checked_animation_emission(*parsed.value, source);
    }
}

TEST_CASE("OSB Animation metadata validation and command limits remain bounded", "[storyboard][osb][animation]") {
    const auto parsed = parse_osb("Animation,Foreground,Centre,a.png,0,0,2,100,LoopForever\n L,0,2\n  F,0,0,10,0,1\n");
    REQUIRE(parsed.value);
    auto model = *parsed.value;
    auto& metadata = *model.sprites.front().animation;
    SECTION("zero count") { metadata.frame_count = 0; }
    SECTION("large count") { metadata.frame_count = std::numeric_limits<std::uint32_t>::max(); }
    SECTION("negative delay") { metadata.frame_delay_ms = -1; }
    SECTION("non-finite delay") { metadata.frame_delay_ms = std::numeric_limits<double>::infinity(); }
    SECTION("unknown mode") { metadata.loop_type = static_cast<storyboard_animation_loop_t>(255); }
    CHECK_FALSE(emit_osb(model).value);
    CHECK_FALSE(parse_osb("Animation,Foreground,Centre,a.png,0,0,2,100\n L,0,2\n  F,0,0,10,0,1\n",
        {.maximum_nodes = 1}).value);
}


TEST_CASE("malformed Animation visual fields are retained rather than creating invalid model objects", "[storyboard][osb][animation]") {
    for (const auto* header : {"Animation,Foreground,Centre,\"\",0,0,2,100", "Animation,,Centre,a.png,0,0,2,100",
            "Animation,Foreground,Centre,a.png,nan,0,2,100", "Animation,Foreground,Centre,\"a,b.png\",0,0,2,100"}) {
        const auto source = std::string(header) + "\n F,0,0,10,1,0\n";
        const auto parsed = parse_osb(source);
        REQUIRE(parsed.value);
        CHECK(parsed.value->sprites.empty());
        CHECK_FALSE(parsed.diagnostics.empty());
        (void)checked_animation_emission(*parsed.value, source);
    }
}

namespace {
void check_sample(bool value) { CHECK(value); }
void require_sample(bool value) { REQUIRE(value); }
}

TEST_CASE("OSB Sample preserves authored ordering paths and optional volume", "[storyboard][sample]") {
    const std::string source = "Sample,-10,2,\"audio/a b.wav\"\nSprite,Foreground,Centre,\"image.png\",320,240\n F,0,0,10,1,0\nSample,10,0,\"hit.wav\",80\nUnknown,retained\n";
    const auto parsed = parse_osb(source);
    require_sample(static_cast<bool>(parsed.value));
    require_sample(static_cast<bool>(parsed.value->samples.size() == 2));
    check_sample(static_cast<bool>(parsed.value->samples.front().effective_volume() == 100));
    check_sample(!static_cast<bool>(parsed.value->samples.front().volume));
    const auto written = emit_osb(*parsed.value);
    require_sample(static_cast<bool>(written.value));
    check_sample(static_cast<bool>(*written.value == source));
}

TEST_CASE("malformed fractional and indented Samples stay raw and reset the visual owner", "[storyboard][sample]") {
    for (const auto tail : {"1.5,0,\"hit.wav\",80", "1,4,\"hit.wav\",80", "1,0,\"hit.wav\",0",
         "1,0,\"a,b.wav\",80", "1,0,\"hit.wav\",1000", "1,0,\"hit.wav\",80.5", "9223372036854775808,0,\"hit.wav\",80"}) {
        const auto source = "Sprite,Foreground,Centre,\"image.png\",0,0\nSample," + std::string(tail) + "\n F,0,0,10,1,0\n";
        const auto parsed = parse_osb(source);
        require_sample(static_cast<bool>(parsed.value));
        check_sample(!static_cast<bool>(parsed.diagnostics.empty()));
        check_sample(static_cast<bool>(parsed.value->samples.empty()));
        check_sample(static_cast<bool>(parsed.value->sprites.front().nodes.empty()));
        const auto written = emit_osb(*parsed.value);
        require_sample(static_cast<bool>(written.value));
        check_sample(static_cast<bool>(*written.value == source));
    }
}

TEST_CASE("Samples use one instantaneous schedule event and the existing half-open cursor", "[storyboard][sample]") {
    const auto parsed = parse_osb("Sample,10,0,\"a.wav\",100\nSample,10,2,\"b.wav\"\n");
    require_sample(static_cast<bool>(parsed.value));
    const auto schedule = compile_storyboard_schedule(*parsed.value);
    require_sample(static_cast<bool>(schedule.value));
    require_sample(static_cast<bool>(schedule.value->events.size() == 2));
    check_sample(static_cast<bool>(schedule.value->events.front().edge == storyboard_event_t::edge_t::sample));
    storyboard_event_cursor_t cursor;
    require_sample(static_cast<bool>(cursor.start(*schedule.value, 10)));
    check_sample(static_cast<bool>(cursor.advance_events(10)->empty()));
    check_sample(static_cast<bool>(cursor.advance_events(11)->size() == 2));
    require_sample(static_cast<bool>(cursor.seek(10)));
    check_sample(static_cast<bool>(cursor.advance_events(11)->size() == 2));
}

TEST_CASE("Sample identity event node path and output bounds fail without partial emission", "[storyboard][sample]") {
    const auto parsed = parse_osb("Sample,-9223372036854775808,0,\"hit.wav\"\nSample,9223372036854775807,3,\"hit.wav\",1\n");
    require_sample(parsed.value.has_value());
    check_sample(compile_storyboard_schedule(*parsed.value).value.has_value());
    check_sample(!compile_storyboard_schedule(*parsed.value, {.maximum_events = 1}).value.has_value());
    check_sample(!parse_osb("Sample,0,0,\"hit.wav\"\n", {.maximum_nodes = 0}).value.has_value());
    check_sample(!emit_osb(*parsed.value, 4).value.has_value());
    auto duplicate = *parsed.value;
    duplicate.samples.back().stable_id = duplicate.samples.front().stable_id;
    check_sample(!emit_osb(duplicate).value.has_value());
    check_sample(!compile_storyboard_schedule(duplicate).value.has_value());
}

namespace {
auto trigger_source(std::string_view commands) -> std::string {
    return "Sprite,Foreground,Centre,\"under_score.png\",320,240\n" + std::string(commands);
}
void check_raw_trigger_emission(const storyboard_document_model_t& model, std::string_view block) {
    const auto& nodes = model.sprites.front().nodes;
    REQUIRE_FALSE(nodes.empty());
    CHECK(nodes.front().kind == storyboard_node_kind_t::retained_line);
    CHECK(nodes.back().kind == storyboard_node_kind_t::command);
    const auto emitted = emit_osb(model);
    REQUIRE(emitted.value);
    CHECK(emitted.value->find(block) != std::string::npos);
}
void check_raw_trigger(std::string_view block) {
    const auto source = trigger_source(std::string(block) + " F,0,500,600,1,1\n");
    const auto parsed = parse_osb(source);
    REQUIRE(parsed.value);
    REQUIRE_FALSE(parsed.diagnostics.empty());
    check_raw_trigger_emission(*parsed.value, block);
    const auto sampled = evaluate_storyboard_property(parsed.value->sprites.front(), storyboard_opcode_t::fade, 10);
    CHECK_FALSE(sampled.value.present);
}
void check_supported_trigger_name(std::string_view name) {
    const auto value = storyboard_trigger_name_t::from(name);
    REQUIRE(value);
    CHECK(value->value == name);
    CHECK(value->is_valid());
}
void check_omitted_trigger_bounds(const storyboard_node_t& node) {
    REQUIRE(node.trigger);
    CHECK_FALSE(node.trigger->start_time_ms);
}
void check_signed_trigger_header(const storyboard_node_t& node) {
    REQUIRE(node.trigger);
    CHECK(node.trigger->start_time_ms == -20);
    CHECK(node.trigger->end_time_ms == 30);
    CHECK(node.trigger->group_number == -2);
    CHECK(node.children.front().kind == storyboard_node_kind_t::loop);
}
void check_trigger_header_roundtrip(const storyboard_document_model_t& model) {
    const auto emitted = emit_osb(model);
    REQUIRE(emitted.value);
    CHECK(emitted.value->find(" T,HitSound\n") != std::string::npos);
    const auto reparsed = parse_osb(*emitted.value);
    REQUIRE(reparsed.value);
    CHECK(storyboard_semantically_equal(model, *reparsed.value));
}
void check_invalid_ignored_trigger(const storyboard_document_model_t& model) {
    auto invalid = model;
    invalid.sprites.front().nodes.front().children.front().children.front().command.easing = 99;
    CHECK_FALSE(compile_storyboard_schedule(invalid).value);
    CHECK_FALSE(emit_osb(invalid).value);
}
}
TEST_CASE("OSB trigger vocabulary preserves supported exact names", "[storyboard][trigger]") {
    for (const auto name : {"HitSound", "Passing", "Failing", "HitSoundClap", "HitSoundDrumWhistle",
             "HitSoundAllSoft", "HitSoundDrumClap0", "HitSound6", "HitSoundNormalSoftFinish2147483647"}) {
        check_supported_trigger_name(name);
    }
    for (const auto name : {"", "passing", "Unknown", "HitSoundGarbage", "HitSound2147483648", "HitSound-1", "HitSound00000000000"})
        CHECK_FALSE(storyboard_trigger_name_t::from(name));
}
TEST_CASE("OSB typed trigger headers preserve omission signed fields and relative children", "[storyboard][trigger]") {
    const auto parsed = parse_osb(trigger_source(" T,HitSound\n  F,0,0,10,0,1\n T,Passing,-20,30,-2\n  L,0,2\n   M,0,0,10,0,0,1,1\n"));
    REQUIRE(parsed.value);
    CHECK(parsed.ok());
    const auto& nodes = parsed.value->sprites.front().nodes;
    REQUIRE(nodes.size() == 2);
    check_omitted_trigger_bounds(nodes[0]);
    check_signed_trigger_header(nodes[1]);
    check_trigger_header_roundtrip(*parsed.value);
}
TEST_CASE("OSB malformed trigger headers retain their whole raw subtree", "[storyboard][trigger]") {
    for (const auto header : {" T,Unknown\n", " T,Passing,,10\n", " T,Passing,1.5,10\n",
             " T,Passing,20,10\n", " T,Passing,0,10,2147483648\n", " T,Passing,0,1,0,extra\n", " T,Passing,9223372036854775808\n"})
        check_raw_trigger(std::string(header) + "  F,0,0,100,0,1\n  L,0,2\n   M,0,0,1,0,0,1,1\n");
}
TEST_CASE("OSB malformed trigger descendant rolls back preceding valid children", "[storyboard][trigger]") {
    check_raw_trigger(" T,Passing,0,100\n  F,0,0,100,0,1\n  L,0,2\n   F,not-an-easing,0,10,0,1\n  M,0,0,10,0,0,1,1\n\n");
}
TEST_CASE("OSB underscores form local compound indentation and EOF finalizes triggers", "[storyboard][trigger]") {
    const auto parsed = parse_osb(trigger_source("_T,Failing,-10\n_ _L,0,2\n_ __F,0,0,10,0,1"));
    REQUIRE(parsed.value);
    REQUIRE(parsed.value->sprites.front().nodes.front().trigger);
    CHECK(parsed.value->sprites.front().path == "under_score.png");
    CHECK(parsed.value->sprites.front().nodes.front().children.front().children.size() == 1);
    CHECK(compile_storyboard_schedule(*parsed.value).value->events.empty());
}
TEST_CASE("OSB ignored trigger subtrees still consume structural limits", "[storyboard][trigger][limits]") {
    const auto source = trigger_source(" T,Passing\n  L,0,2\n   F,0,0,10,0,1\n");
    CHECK_FALSE(parse_osb(source, {.maximum_nodes = 2}).value);
    CHECK_FALSE(parse_osb(source, {.maximum_nesting = 1}).value);
    const auto parsed = parse_osb(source);
    REQUIRE(parsed.value);
    CHECK_FALSE(compile_storyboard_schedule(*parsed.value, {.maximum_nodes = 2}).value);
    CHECK_FALSE(emit_osb(*parsed.value, 20).value);
    check_invalid_ignored_trigger(*parsed.value);
}
TEST_CASE("OSB trigger typed edits and full signed bounds are authoritative", "[storyboard][trigger]") {
    auto parsed = parse_osb(trigger_source(" T,Passing,-9223372036854775808,9223372036854775807,-2147483648\n"));
    REQUIRE(parsed.value);
    auto& trigger = *parsed.value->sprites.front().nodes.front().trigger;
    CHECK(trigger.start_time_ms == std::numeric_limits<std::int64_t>::min());
    trigger.group_number = 0;
    const auto emitted = emit_osb(*parsed.value);
    REQUIRE(emitted.value);
    CHECK(emitted.value->find(",9223372036854775807,0\n") != std::string::npos);
    trigger.name.value = "Unknown";
    CHECK_FALSE(emit_osb(*parsed.value).value);
}

TEST_CASE("Trigger properties are ignored at integer fractional and out-of-window times", "[storyboard][trigger]") {
    const auto baseline = parse_osb(trigger_source(" F,0,0,100,1,1\n"));
    const auto triggered = parse_osb(trigger_source(" F,0,0,100,1,1\n T,Failing,-100,1000\n  F,0,0,2000,0,0\n"));
    REQUIRE(baseline.value);
    REQUIRE(triggered.value);
    for (const auto time : {-101, 0, 25, 101, 1001}) {
        const storyboard_time_t clock{.milliseconds = time, .remainder = 1, .denominator = 2};
        const auto before = evaluate_storyboard_property(baseline.value->sprites.front(), storyboard_opcode_t::fade, clock);
        const auto after = evaluate_storyboard_property(triggered.value->sprites.front(), storyboard_opcode_t::fade, clock);
        CHECK(before.value == after.value);
    }
}
TEST_CASE("Ignored trigger nodes consume bounds even when malformed raw suppression is active", "[storyboard][trigger][limits]") {
    const auto raw = trigger_source(" T,Unknown\n  F,0,0,10,0,1\n  M,0,0,10,0,0,1,1\n");
    CHECK_FALSE(parse_osb(raw, {.maximum_nodes = 2}).value);
    CHECK_FALSE(parse_osb(raw, {.maximum_lines = 3}).value);
    CHECK_FALSE(parse_osb(raw, {.maximum_input_bytes = 10}).value);
    const auto valid = parse_osb(trigger_source(" T,Passing\n  F,0,0,10,0,1\n"));
    REQUIRE(valid.value);
    const auto ignored = compile_storyboard_schedule(*valid.value, {.maximum_events = 0});
    REQUIRE(ignored.value);
    CHECK(ignored.value->events.empty());
}
TEST_CASE("Ignored trigger validation rejects programmatic hidden metadata and children", "[storyboard][trigger][limits]") {
    auto parsed = parse_osb(trigger_source(" T,Passing\n  F,0,0,10,0,1\n"));
    REQUIRE(parsed.value);
    auto& node = parsed.value->sprites.front().nodes.front();
    SECTION("missing header") { node.trigger.reset(); }
    SECTION("wrong header owner") { node.kind = storyboard_node_kind_t::loop; }
    SECTION("reversed bounds") { node.trigger->start_time_ms = 10; node.trigger->end_time_ms = -1; }
    SECTION("hidden child") { node.children.front().children.push_back(fade(0, 10, 0, 1)); }
    CHECK_FALSE(emit_osb(*parsed.value).value);
    CHECK_FALSE(compile_storyboard_schedule(*parsed.value).value);
    CHECK_FALSE(evaluate_storyboard_property(parsed.value->sprites.front(), storyboard_opcode_t::fade, 5).succeeded);
}
TEST_CASE("Malformed overflow in a trigger sequential child retains the whole block", "[storyboard][trigger]") {
    check_raw_trigger(" T,Passing\n  F,0,0,9223372036854775807,0,1,0\n");
}

namespace {
void check_trigger_windows(const storyboard_trigger_scenario_t& scenario, const storyboard_trigger_name_t& name) {
    CHECK_FALSE(scenario.latest(name, storyboard_time_t{.milliseconds = 15}, {}, {.milliseconds = 19}));
    CHECK_FALSE(scenario.latest(*storyboard_trigger_name_t::from("Failing"), {}, {}, {.milliseconds = 99}));
}
}
TEST_CASE("Explicit trigger scenarios seal exact named firings once", "[storyboard][scenario]") {
    const auto name = *storyboard_trigger_name_t::from("Passing");
    const std::array input{storyboard_trigger_firing_t{.name = name, .time = {.milliseconds = 20}},
        storyboard_trigger_firing_t{.name = name, .time = {.milliseconds = -10}},
        storyboard_trigger_firing_t{.name = name, .time = {.milliseconds = 10, .remainder = 1, .denominator = 2}}};
    const auto scenario = storyboard_trigger_scenario_t::from(input);
    REQUIRE(scenario);
    CHECK(input[0].time.milliseconds == 20);
    CHECK(scenario->latest(name, {}, {}, {.milliseconds = 10}) == storyboard_time_t{.milliseconds = -10});
    CHECK(scenario->latest(name, {}, {}, {.milliseconds = 11}) == input[2].time);
    check_trigger_windows(*scenario, name);
    CHECK_FALSE(storyboard_trigger_scenario_t::from(input, 2));
}
TEST_CASE("Trigger scenarios reject invalid data and preserve empty explicit input", "[storyboard][scenario]") {
    CHECK(storyboard_trigger_scenario_t::from({}));
    std::array input{storyboard_trigger_firing_t{.name = {.value = "Unknown"}}};
    CHECK_FALSE(storyboard_trigger_scenario_t::from(input));
    input[0].name = *storyboard_trigger_name_t::from("HitSound");
    input[0].time.denominator = 0;
    CHECK_FALSE(storyboard_trigger_scenario_t::from(input));
}
