#include <wormedit/core/storyboard_preview.h>
#include <wormedit/core/osbx_evaluator.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <ranges>
#include <string>
#include <utility>

namespace {

using namespace wormedit;

void check_animation(bool value) { CHECK(value); }
void require_animation(bool value) { REQUIRE(value); }

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

    const auto built = storyboard_preview_index_t::from(document);
    REQUIRE(built.value);
    REQUIRE(built.value->sources.size() == 2);
    CHECK(built.value->sources[0].sprite_index == 1);
    CHECK(built.value->sources[1].sprite_index == 0);

    storyboard_frame_arena_t arena;
    const auto frame = evaluate_storyboard_frame(document, *built.value, 150, arena);
    REQUIRE(frame.is_success());
    REQUIRE(frame.instances.size() == 2);
    CHECK(frame.instances[0].path == "back.png");
    CHECK(frame.instances[0].layer_order == 0);
    CHECK(frame.instances[1].path == "front.png");
    CHECK(frame.instances[1].layer_order == 3);
    CHECK(frame.instances[1].transform[4] == 60.0);
    CHECK(frame.instances[1].transform[5] == 60.0);
    CHECK(frame.instances[1].opacity == 0.5);
    CHECK(frame.instances[1].transform[0] == 1.5);
    CHECK(frame.instances[1].transform[3] == 3.0);
    CHECK(frame.instances[1].red == 130.0);
    CHECK(frame.instances[1].green == 100.0);
    CHECK(frame.instances[1].blue == 110.0);

    const auto before = evaluate_storyboard_frame(document, *built.value, 99, arena);
    REQUIRE(before.is_success());
    CHECK(before.instances.empty());
    const auto after = evaluate_storyboard_frame(document, *built.value, 201, arena);
    REQUIRE(after.is_success());
    CHECK(after.instances.empty());
}

TEST_CASE("storyboard preview rejects stale, ambiguous, and over-capacity state",
          "[unit][storyboard][preview][negative]") {
    storyboard_document_model_t duplicate;
    auto first = sprite(1, 1, "Background", "a.png");
    first.nodes = {command(storyboard_opcode_t::fade, 0, 10, {0.0}, {1.0}, 1)};
    auto second = sprite(1, 2, "Foreground", "b.png");
    second.nodes = {command(storyboard_opcode_t::fade, 0, 10, {0.0}, {1.0}, 2)};
    duplicate.sprites = {first, second};
    CHECK_FALSE(storyboard_preview_index_t::from(duplicate).value);

    duplicate.sprites[1].stable_id = 2;
    const auto built = storyboard_preview_index_t::from(duplicate);
    REQUIRE(built.value);
    storyboard_frame_arena_t arena;
    const auto capped =
        evaluate_storyboard_frame(duplicate, *built.value, 5, arena, {.maximum_instances = 1});
    CHECK_FALSE(capped.is_success());
    REQUIRE(capped.diagnostic.has_value());
    CHECK(capped.diagnostic->code == storyboard_diagnostic_code_t::capacity_exceeded);

    auto stale = *built.value;
    stale.sources.front().stable_id = 99;
    const auto rejected = evaluate_storyboard_frame(duplicate, stale, 5, arena);
    CHECK_FALSE(rejected.is_success());
    REQUIRE(rejected.diagnostic.has_value());
    CHECK(rejected.diagnostic->code == storyboard_diagnostic_code_t::invalid_model);
}

TEST_CASE("storyboard frame preserves quiet empty success", "[core][storyboard][preview][results]") {
    const storyboard_document_model_t document;
    const storyboard_preview_index_t index;
    storyboard_frame_arena_t arena;
    auto frame = evaluate_storyboard_frame(document, index, 0, arena);
    REQUIRE(frame.is_success());
    CHECK(frame.instances.empty());
}

namespace {
template <class Value> void require_osb_quad(const Value& value) { REQUIRE(static_cast<bool>(value)); }
void check_osb_quad(bool value) { CHECK(value); }
}

TEST_CASE("OSB affine frames preserve legacy float quad points with signed scales and origins",
          "[core][storyboard][preview][affine]") {
    // Frozen pre-affine layout formula: compare the renderer's actual float coordinates.
    // The fixture exercises authored rotation, flips, off-centre origins and letterboxing.
    for (const double origin_x : {0.0, 0.5, 1.0}) {
        for (const double origin_y : {0.0, 0.5, 1.0}) {
            storyboard_document_model_t document;
            auto source = sprite(1, 1, "Foreground", "front.png");
            source.nodes = {
                command(storyboard_opcode_t::move, 0, 100, {123, 87}, {123, 87}, 1),
                command(storyboard_opcode_t::vector_scale, 0, 100, {-1.7, 2.3}, {-1.7, 2.3}, 2),
                command(storyboard_opcode_t::rotate, 0, 100, {0.73}, {0.73}, 3)};
            document.sprites.push_back(source);
            const auto index = storyboard_preview_index_t::from(document);
            require_osb_quad(index.value);
            storyboard_frame_arena_t arena;
            const auto frame = evaluate_storyboard_frame(document, *index.value, 50, arena);
            require_osb_quad(frame.is_success());
            require_osb_quad(frame.instances.size() == 1);
            const auto quad = visual_image_quad_t::from({
                .source_width = 73, .source_height = 41,
                .destination = {.left = 17, .top = 29, .width = 451, .height = 379},
                .origin_x = origin_x, .origin_y = origin_y,
                .transform = frame.instances.front().transform});
            require_osb_quad(quad);
            const auto fit = 451.0 / 640.0;
            const auto px = 17 + 123 * fit;
            const auto py = 29 + (379 - 480 * fit) * 0.5 + 87 * fit;
            const std::array<double, 2> xs{-origin_x * 73 * -1.7 * fit,
                                         (1 - origin_x) * 73 * -1.7 * fit};
            const std::array<double, 2> ys{-origin_y * 41 * 2.3 * fit,
                                         (1 - origin_y) * 41 * 2.3 * fit};
            const std::array<std::array<int, 2>, 4> corners{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
            for (std::size_t i = 0; i < corners.size(); ++i) {
                const auto x = xs[corners[i][0]], y = ys[corners[i][1]];
                check_osb_quad(static_cast<float>(quad->points[i].x) ==
                      static_cast<float>(px + x * std::cos(0.73) - y * std::sin(0.73)));
                check_osb_quad(static_cast<float>(quad->points[i].y) ==
                      static_cast<float>(py + x * std::sin(0.73) + y * std::cos(0.73)));
            }
        }
    }
}

namespace {
void check_conditional_instance(const storyboard_draw_instance_t& instance, storyboard_conditional_layer_t choice) {
    CHECK(is_storyboard_layer_visible(instance.layer, choice));
    CHECK(instance.transform[4] == 0.5);
    CHECK(instance.transform[5] == 1.0);
}
void check_conditional_frame(const storyboard_frame_effect_t& frame, storyboard_conditional_layer_t choice) {
    REQUIRE(frame.is_success());
    CHECK(frame.instances.size() == (choice == storyboard_conditional_layer_t::neither ? 2 : 3));
    for (const auto& instance : frame.instances) check_conditional_instance(instance, choice);
}
void check_fractional_boundaries(const storyboard_document_model_t& document, const storyboard_preview_index_t& index) {
    frame_arena_t arena;
    const auto default_frame = evaluate_storyboard_frame(document, index, 0, arena);
    REQUIRE(default_frame.instances.size() == 3);
    CHECK(default_frame.instances[1].layer == "Pass");
    const auto integer = evaluate_storyboard_property(document.sprites[0], storyboard_opcode_t::move, 0);
    CHECK(integer == evaluate_storyboard_property(document.sprites[0], storyboard_opcode_t::move,
        storyboard_time_t{.milliseconds = 0}));
    CHECK(evaluate_storyboard_frame(document, index,
        storyboard_time_t{.milliseconds = 2, .remainder = 1, .denominator = 2}, arena).instances.empty());
    CHECK_FALSE(evaluate_storyboard_frame(document, index,
        storyboard_time_t{.denominator = 0}, arena).is_success());
}
auto sampled_move_x(const storyboard_sprite_t& sprite, storyboard_time_t time) -> double {
    const auto sampled = evaluate_storyboard_property(sprite, storyboard_opcode_t::move_x, time);
    REQUIRE(sampled.is_success());
    REQUIRE(sampled.value.present);
    return sampled.value.values[0];
}
} // namespace

TEST_CASE("storyboard conditional choice and fractional times share preview sampling", "[storyboard][preview]") {
    storyboard_document_model_t document;
    for (const auto layer : {"Background", "Fail", "Pass", "Foreground"}) {
        auto value = sprite(document.sprites.size() + 1, document.sprites.size(), layer, "sprite.png");
        value.nodes = {command(storyboard_opcode_t::move, -1, 2, {0, 0}, {3, 6}, 1)};
        document.sprites.push_back(std::move(value));
    }
    const auto index = storyboard_preview_index_t::from(document);
    REQUIRE(index.value);
    frame_arena_t arena;
    const storyboard_time_t half{.milliseconds = -1, .remainder = 1, .denominator = 2};
    for (const auto choice : {storyboard_conditional_layer_t::pass, storyboard_conditional_layer_t::fail,
                              storyboard_conditional_layer_t::neither}) {
        const auto frame = evaluate_storyboard_frame(document, *index.value, half, arena, {.conditional_layer = choice});
        check_conditional_frame(frame, choice);
    }
    check_fractional_boundaries(document, *index.value);
}

TEST_CASE("fractional OSB loops retain integer boundaries and easing", "[storyboard][preview]") {
    auto value = sprite(1, 0, "Background", "sprite.png");
    auto move = command(storyboard_opcode_t::move_x, 0, 10, {0}, {100}, 1);
    move.command.easing = 3; // in-quadratic
    value.nodes = {{.kind = storyboard_node_kind_t::loop,
        .loop_start_time_ms = -10, .loop_iteration_count = 3, .children = {move}}};
    CHECK(sampled_move_x(value, {.milliseconds = 0}) == 0);
    CHECK(std::abs(sampled_move_x(value, {.milliseconds = 0, .remainder = 1, .denominator = 2}) - 0.25) < 1e-12);
    CHECK(sampled_move_x(value, {.milliseconds = 20, .remainder = 1, .denominator = 2}) == 100);
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    value.nodes = {command(storyboard_opcode_t::move_x, maximum - 1, maximum, {0}, {100}, 1)};
    CHECK(sampled_move_x(value, {.milliseconds = maximum - 1, .remainder = 1, .denominator = 2}) == 50);
}


TEST_CASE("structured OSB animations draw alongside static sprites", "[storyboard][preview][animation]") {
    const auto parsed = parse_osb("Animation,Foreground,Centre,anim.png,0,0,2,100,LoopForever\n F,0,0,100,1,1\n"
        "Sprite,Foreground,Centre,still.png,10,20\n F,0,0,100,1,1\n");
    require_animation(parsed.value.has_value());
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    require_animation(index.value.has_value());
    storyboard_frame_arena_t arena;
    const auto frame = evaluate_storyboard_frame(*parsed.value, *index.value, 50, arena);
    require_animation(frame.is_success());
    require_animation(frame.instances.size() == 2);
    check_animation(frame.instances.front().path == "anim0.png");
    check_animation(frame.instances.back().path == "still.png");
    check_animation(frame.instances.back().document_order == 1);
}

namespace {
void check_sample_preview(bool value) { CHECK(value); }
void require_sample_preview(bool value) { REQUIRE(value); }
}

TEST_CASE("preview index exposes sample cues without changing visual envelopes", "[storyboard][preview][sample]") {
    const auto parsed = parse_osb("Sample,5,2,\"pass.wav\"\nSample,5,1,\"fail.wav\",80\nSprite,Foreground,Centre,\"sprite.png\",0,0\n F,0,0,10,1,1\n");
    require_sample_preview(static_cast<bool>(parsed.value));
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    require_sample_preview(static_cast<bool>(index.value));
    require_sample_preview(static_cast<bool>(index.value->schedule.events.size() == 4));
    require_sample_preview(static_cast<bool>(index.value->sources.size() == 1));
    check_sample_preview(static_cast<bool>(index.value->sources.front().begin_time_ms == 0));
    const auto cue = std::ranges::find(index.value->schedule.events, storyboard_event_t::edge_t::sample, &storyboard_event_t::edge);
    require_sample_preview(static_cast<bool>(cue != index.value->schedule.events.end()));
    check_sample_preview(static_cast<bool>(index.value->sample_for(*parsed.value, *cue, storyboard_conditional_layer_t::pass)));
    check_sample_preview(!static_cast<bool>(index.value->sample_for(*parsed.value, *cue, storyboard_conditional_layer_t::fail)));
    check_sample_preview(!static_cast<bool>(index.value->sample_for(*parsed.value, *cue, storyboard_conditional_layer_t::neither)));
}

TEST_CASE("sample cue lookup refuses stale metadata wrong edges and changed model bounds", "[storyboard][preview][sample]") {
    auto parsed = parse_osb("Sample,5,0,\"hit.wav\"\n");
    require_sample_preview(static_cast<bool>(parsed.value));
    auto indexed = storyboard_preview_index_t::from(*parsed.value);
    require_sample_preview(static_cast<bool>(indexed.value));
    auto index = std::move(*indexed.value);
    auto cue = index.schedule.events.front();
    check_sample_preview(static_cast<bool>(index.sample_for(*parsed.value, cue)));
    check_sample_preview(index.sample_for(*parsed.value, cue) == &parsed.value->samples.front());
    ++cue.source_order;
    check_sample_preview(!static_cast<bool>(index.sample_for(*parsed.value, cue)));
    cue = index.schedule.events.front();
    cue.edge = storyboard_event_t::edge_t::begin;
    check_sample_preview(!static_cast<bool>(index.sample_for(*parsed.value, cue)));
    cue = index.schedule.events.front();
    cue.stable_object_id += 20;
    check_sample_preview(!index.sample_for(*parsed.value, cue));
    parsed.value->samples.front().path = "changed.wav";
    check_sample_preview(!index.sample_for(*parsed.value, index.schedule.events.front()));
    parsed.value->samples.clear();
    check_sample_preview(!static_cast<bool>(index.sample_for(*parsed.value, index.schedule.events.front())));
}

namespace {
auto animation_path(storyboard_document_model_t& model, const storyboard_preview_index_t& index,
    storyboard_time_t time, storyboard_frame_arena_t& arena) -> std::string {
    const auto frame = evaluate_storyboard_frame(model, index, time, arena);
    require_animation(frame.is_success() && frame.instances.size() == 1);
    return std::string{frame.instances.front().path};
}
}

TEST_CASE("Animation frame query clamps Once and wraps Forever at exact fractions", "[storyboard][animation]") {
    storyboard_animation_t animation{.frame_count = 3, .frame_delay_ms = 2.5};
    check_animation(animation.frame_index_at({.milliseconds = -11}, -10) == 0);
    check_animation(animation.frame_index_at({.milliseconds = -8, .remainder = 1, .denominator = 2}, -10) == 1);
    check_animation(animation.frame_index_at({.milliseconds = -3, .remainder = 1, .denominator = 2}, -10) == 0);
    animation.loop_type = storyboard_animation_loop_t::loop_once;
    check_animation(animation.frame_index_at({.milliseconds = 100}, -10) == 2);
    animation.frame_count = 1;
    check_animation(animation.frame_index_at({.milliseconds = INT64_MAX}, INT64_MIN) == 0);
    check_animation(!animation.frame_index_at({.denominator = 0}, 0));
    animation.frame_count = 0;
    check_animation(!animation.frame_index_at({}, 0));
}

TEST_CASE("Animation frame query treats stored binary64 delays exactly", "[storyboard][animation]") {
    storyboard_animation_t animation{.frame_count = 7, .frame_delay_ms = std::nextafter(1.0, 2.0)};
    check_animation(animation.frame_index_at({.milliseconds = 1}, 0) == 0);
    animation.frame_delay_ms = std::nextafter(1.0, 0.0);
    check_animation(animation.frame_index_at({.milliseconds = 1}, 0) == 1);
    animation.frame_delay_ms = std::numeric_limits<double>::denorm_min();
    // 2^1074 modulo 7 = 1. No rounded floating quotient is representable.
    check_animation(animation.frame_index_at({.milliseconds = 1}, 0) == 1);
    animation.frame_delay_ms = 0.5;
    // 2*(2^64-1) modulo 7 = 2.
    check_animation(animation.frame_index_at({.milliseconds = INT64_MAX}, INT64_MIN) == 2);
    animation.frame_delay_ms = std::numeric_limits<double>::max();
    check_animation(animation.frame_index_at({.milliseconds = INT64_MAX}, INT64_MIN) == 0);
    animation.frame_delay_ms = std::numeric_limits<double>::infinity();
    check_animation(!animation.frame_index_at({}, 0));
}

TEST_CASE("Animation paths use final filename extension and reused arena storage", "[storyboard][animation][preview]") {
    auto parsed = parse_osb("Animation,Foreground,Centre,dir.name/frame.multi.png,0,0,3,2.5,LoopForever\n F,0,-10,100,1,1\n");
    require_animation(parsed.value.has_value());
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    require_animation(index.value.has_value());
    storyboard_frame_arena_t arena;
    check_animation(animation_path(*parsed.value, *index.value, {.milliseconds = -8, .remainder = 1, .denominator = 2}, arena) == "dir.name/frame.multi1.png");
    const auto committed = arena.committed_bytes();
    check_animation(animation_path(*parsed.value, *index.value, {.milliseconds = -10}, arena) == "dir.name/frame.multi0.png");
    check_animation(arena.committed_bytes() == committed);
    parsed.value->sprites.front().path = "dir.name/frame";
    check_animation(animation_path(*parsed.value, *index.value, {.milliseconds = 0}, arena) == "dir.name/frame1");
    storyboard_frame_arena_t tiny{{.chunk_bytes = 1, .maximum_bytes = sizeof(storyboard_draw_instance_t), .maximum_objects = 10}};
    const auto failed = evaluate_storyboard_frame(*parsed.value, *index.value, 0, tiny);
    check_animation(!failed.is_success() && failed.instances.empty());
}

TEST_CASE("Animation exact frame query spans signed limits and bounded exponent extremes", "[storyboard][animation]") {
    storyboard_animation_t animation{.frame_count = 3, .frame_delay_ms = 0.5};
    check_animation(animation.frame_index_at({.milliseconds = INT64_MAX, .remainder = 1, .denominator = 2}, INT64_MIN) == 1);
    animation.frame_delay_ms = std::ldexp(1.0, 60);
    check_animation(animation.frame_index_at({.milliseconds = INT64_MAX}, INT64_MIN) == 0); // floor = 15
    animation.frame_delay_ms = std::numeric_limits<double>::denorm_min();
    animation.loop_type = storyboard_animation_loop_t::loop_once;
    check_animation(animation.frame_index_at({.remainder = 1, .denominator = UINT32_MAX}, 0) == 2);
    animation.frame_delay_ms = 1;
    check_animation(animation.frame_index_at({.milliseconds = INT64_MAX, .remainder = 1, .denominator = 2}, INT64_MAX) == 0);
    animation.loop_type = static_cast<storyboard_animation_loop_t>(99);
    check_animation(!animation.frame_index_at({}, 0));
}

TEST_CASE("Animation preview preserves conditional envelopes caps and stale validation", "[storyboard][animation][preview]") {
    auto parsed = parse_osb("Animation,Pass,Centre,frame.png,0,0,2,100,LoopOnce\n F,0,-10,10,1,1\nSample,0,2,sound.wav\n");
    require_animation(parsed.value.has_value());
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    require_animation(index.value.has_value());
    check_animation(index.value->schedule.events.size() == 3);
    storyboard_frame_arena_t arena;
    const auto endpoint = evaluate_storyboard_frame(*parsed.value, *index.value, {.milliseconds = 10, .remainder = 1, .denominator = 2}, arena);
    check_animation(endpoint.is_success() && endpoint.instances.empty());
    const auto hidden = evaluate_storyboard_frame(*parsed.value, *index.value, 0, arena,
        {.conditional_layer = storyboard_conditional_layer_t::neither});
    check_animation(hidden.is_success() && hidden.instances.empty());
    check_animation(!evaluate_storyboard_frame(*parsed.value, *index.value, 0, arena, {.maximum_instances = 0}).is_success());
    parsed.value->sprites.front().stable_id = 99;
    check_animation(!evaluate_storyboard_frame(*parsed.value, *index.value, 0, arena).is_success());
}

TEST_CASE("Animation selected path and metadata failures leave no partial frame", "[storyboard][animation][preview][bounds]") {
    auto parsed = parse_osb("Animation,Foreground,Centre,frame.png,0,0,2,1,LoopForever\n F,0,0,10,1,1\n");
    require_animation(parsed.value.has_value());
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    require_animation(index.value.has_value());
    storyboard_frame_arena_t arena;
    parsed.value->sprites.front().path = std::string(storyboard_default_maximum_path_bytes, 'x');
    const auto bounded = evaluate_storyboard_frame(*parsed.value, *index.value, 0, arena);
    check_animation(!bounded.is_success() && bounded.instances.empty());
    parsed.value->sprites.front().path = "dir.name\\\\frame.multi.png";
    check_animation(animation_path(*parsed.value, *index.value, {.milliseconds = 1}, arena) == "dir.name\\\\frame.multi1.png");
    parsed.value->sprites.front().animation->frame_delay_ms = 0;
    const auto invalid = evaluate_storyboard_frame(*parsed.value, *index.value, 0, arena);
    check_animation(!invalid.is_success() && invalid.instances.empty());
}

TEST_CASE("Animation query validates fraction and large count without frame enumeration", "[storyboard][animation][bounds]") {
    storyboard_animation_t animation{.frame_count = INT32_MAX, .frame_delay_ms = 0.5};
    check_animation(animation.frame_index_at({.remainder = UINT32_MAX - 1, .denominator = UINT32_MAX}, 0) == 1);
    check_animation(!animation.frame_index_at({.remainder = 2, .denominator = 2}, 0));
    animation.frame_delay_ms = std::nextafter(1.0, 2.0);
    check_animation(animation.frame_index_at({.milliseconds = 1, .denominator = UINT32_MAX}, 0) == 0);
    animation.frame_count = UINT32_MAX;
    check_animation(!animation.frame_index_at({}, 0));
}

TEST_CASE("Animation without authored command envelope stays inactive", "[storyboard][animation][preview]") {
    const auto parsed = parse_osb("Animation,Foreground,Centre,frame.png,0,0,2,1,LoopForever\n");
    require_animation(parsed.value.has_value());
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    require_animation(index.value.has_value());
    storyboard_frame_arena_t arena;
    const auto frame = evaluate_storyboard_frame(*parsed.value, *index.value, 100, arena);
    check_animation(frame.is_success() && frame.instances.empty());
}

namespace {
void check_ignored_trigger_preview(std::string_view block) {
    const auto parsed = parse_osb(std::string("Sprite,Foreground,Centre,a.png,0,0\n") + std::string(block));
    REQUIRE(parsed.value);
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    REQUIRE(index.value);
    CHECK(index.value->schedule.events.empty());
    CHECK(index.value->sources.empty());
}
}
TEST_CASE("Trigger branches do not create preview lifetime including through loops", "[storyboard][trigger][preview]") {
    for (const auto block : {" T,Passing\n  F,0,0,100,0,1\n", " L,0,2\n  T,Passing\n   F,0,0,100,0,1\n",
             " L,0,2\n  T,Unknown\n   F,0,0,100,0,1\n", " L,0,2\n  T,Passing\n   F,invalid,0,100,0,1\n"}) {
        check_ignored_trigger_preview(block);
    }
    const auto empty_loop = parse_osb("Sprite,Foreground,Centre,a.png,0,0\n L,100,2\n");
    REQUIRE(empty_loop.value);
    REQUIRE(compile_storyboard_schedule(*empty_loop.value).value);
    CHECK(compile_storyboard_schedule(*empty_loop.value).value->events.size() == 2);
}
namespace {
void check_trigger_animation_frame(const storyboard_document_model_t& document, const storyboard_preview_index_t& index,
    storyboard_time_t time, storyboard_conditional_layer_t choice) {
    storyboard_frame_arena_t arena;
    const auto frame = evaluate_storyboard_frame(document, index, time, arena, {.conditional_layer = choice});
    REQUIRE(frame.is_success());
    REQUIRE(frame.instances.size() == 1);
    CHECK(frame.instances.front().opacity == 1);
    CHECK(frame.instances.front().path == "a1.png");
    CHECK(index.sample_for(document, index.schedule.events[1], choice) != nullptr);
}
}
TEST_CASE("Trigger preview ignores values while retaining Animation and Sample clocks", "[storyboard][trigger][preview]") {
    const auto parsed = parse_osb("Sample,5,0,hit.wav,50\nAnimation,Foreground,Centre,a.png,0,0,2,10\n F,0,0,100,1,1\n T,Passing,0,500\n  F,0,0,1000,0,0\n");
    REQUIRE(parsed.value);
    const auto index = storyboard_preview_index_t::from(*parsed.value);
    REQUIRE(index.value);
    REQUIRE(index.value->schedule.events.size() == 3);
    for (const auto choice : {storyboard_conditional_layer_t::pass, storyboard_conditional_layer_t::fail, storyboard_conditional_layer_t::neither}) {
        check_trigger_animation_frame(*parsed.value, *index.value, {.milliseconds = 15}, choice);
        check_trigger_animation_frame(*parsed.value, *index.value, {.milliseconds = 15, .remainder = 1, .denominator = 2}, choice);
    }
}

TEST_CASE("shared Animation path builder and OSBX frame adapter use current arena storage", "[storyboard][osbx][animation]") {
    storyboard_frame_arena_t arena;
    const auto path = storyboard_animation_t::frame_path("dir.name/frame.multi.png", 12, arena);
    require_animation(path.has_value());
    check_animation(*path == "dir.name/frame.multi12.png");
    const storyboard_draw_instance_t source{.path = "frames.png"};
    const osbx_draw_instance_t evaluated{.frame_index = 1};
    const auto selected = source.with_evaluation(evaluated, arena);
    require_animation(selected.has_value());
    check_animation(selected->path == "frames1.png");
    arena.reset();
    const auto again = source.with_evaluation(evaluated, arena);
    require_animation(again.has_value());
    check_animation(again->path == "frames1.png");
}

TEST_CASE("Sample payload validation and effective volume keep one canonical policy", "[storyboard][osbx][sample]") {
    check_animation(storyboard_sample_t::is_valid_payload("sound file.wav", {}));
    check_animation(storyboard_sample_t::effective_volume({}) == 100);
    check_animation(storyboard_sample_t::effective_volume(80) == 80);
    check_animation(!storyboard_sample_t::is_valid_payload("bad,name.wav", {}));
    check_animation(!storyboard_sample_t::is_valid_payload("bad\\\"name.wav", {}));
    check_animation(!storyboard_sample_t::is_valid_payload("valid.wav", 0));
}
