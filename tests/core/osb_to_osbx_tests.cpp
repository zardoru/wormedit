#include <wormedit/core/osb_to_osbx.h>
#include <wormedit/core/osbx_edit.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/osbx_osb_export.h>
#include <wormedit/core/storyboard_preview.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <numbers>
#include <source_location>

using namespace wormedit;
namespace {
template<bool Expected = true, class Value>
void require_conversion(const Value& value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    REQUIRE(static_cast<bool>(value) == Expected);
}
template<bool Expected = true, class Value>
void check_conversion(const Value& value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    CHECK(static_cast<bool>(value) == Expected);
}
auto parse_source(std::string_view text) -> storyboard_document_model_t {
    const auto result = parse_osb(text);
    for (const auto& diagnostic : result.diagnostics) INFO(diagnostic.message);
    require_conversion(result.value);
    return *result.value;
}
auto convert_source(const storyboard_document_model_t& source) -> osbx_document_t {
    const auto result = convert_osb_to_osbx(source, {});
    for (const auto& diagnostic : result.diagnostics) INFO(diagnostic.message);
    require_conversion(result.value);
    return *result.value;
}
void compare_frame(const storyboard_document_model_t& source, const osbx_document_t& target,
    storyboard_time_t time) {
    const auto original = storyboard_preview_index_t::from(source);
    const auto converted = osbx_evaluation_index_t::from(target);
    require_conversion(original.value);
    require_conversion(converted.value);
    frame_arena_t old_arena, new_arena;
    const auto before = evaluate_storyboard_frame(source, *original.value, time, old_arena);
    const auto after = evaluate_osbx_frame(*converted.value, time, new_arena);
    require_conversion(before.is_success());
    require_conversion(after.is_success());
    require_conversion(before.instances.size() == after.instances.size());
    for (std::size_t i = 0; i < before.instances.size(); ++i) {
        const auto& old = before.instances[i];
        const auto& now = after.instances[i];
        const auto image = std::ranges::find(target.images, now.image, &osbx_image_t::id);
        require_conversion(image != target.images.end());
        check_conversion(old.path == image->file);
        check_conversion(old.origin == now.origin);
        for (std::size_t component = 0; component < old.transform.values.size(); ++component)
            check_conversion(old.transform[component] == Catch::Approx(now.world[component]).margin(1e-9));
        check_conversion(old.opacity == Catch::Approx(now.opacity).margin(1e-12));
        check_conversion(old.red / 255 == Catch::Approx(now.color[0]).margin(1e-12));
        check_conversion(old.green / 255 == Catch::Approx(now.color[1]).margin(1e-12));
        check_conversion(old.blue / 255 == Catch::Approx(now.color[2]).margin(1e-12));
    }
}
void compare_samples(const storyboard_document_model_t& source, const osbx_document_t& target,
    std::span<const std::int64_t> times) {
    for (const auto time : times) {
        INFO("time=" << time);
        compare_frame(source, target, {.milliseconds = time});
    }
}
auto source_song(const storyboard_document_model_t& source) -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1,
        .storyboards = {{.id = storyboard_id_t{2}, .name = "Imported", .retained_source_lines = {"original bytes"},
            .model = source}}};
}
auto native_bytes(song_document_t song) -> std::string {
    song.revision = 1;
    auto result = write_native_project(song);
    require_conversion(result.value);
    return std::move(*result.value);
}
void check_refusal(std::string_view text, std::size_t line, std::string_view sprite = {}) {
    const auto source = parse_source(text);
    const auto result = convert_osb_to_osbx(source, {});
    check_conversion<false>(result.value);
    require_conversion<false>(result.diagnostics.empty());
    check_conversion(result.diagnostics.front().code == failure_code_t::unsupported);
    check_conversion(result.diagnostics.front().location.provenance.line == line);
    check_conversion(result.diagnostics.front().location.id.find(sprite) != std::string::npos);
}
} // namespace

TEST_CASE("OSB conversion preserves all command families and canonical easing", "[osb-to-osbx]") {
    const auto easing = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34);
    auto source = parse_source("[Events]\n// formatting\nSprite,Foreground,TopLeft,\"a.png\",320,240\n"
        " F,0,0,100,0,1\n MX,0,0,100,20,100\n MY,0,0,100,40,200\n"
        " V,0,0,100,1,2,3,4\n R,0,0,100,0,1.5707963267948966\n C,0,0,100,255,0,10,5,200,210\n");
    for (auto& node : source.sprites[0].nodes) node.command.easing = easing;
    const auto converted = convert_source(source);
    const std::array<std::int64_t, 7> times{-1, 0, 1, 25, 50, 100, 101};
    compare_samples(source, converted, times);
    const auto original_rotation = evaluate_storyboard_property(source.sprites[0], storyboard_opcode_t::rotate, 50);
    const auto rotation = sample_osbx_numeric_track(converted.nodes[0].ops[1].track, 50);
    check_conversion(rotation[0] == Catch::Approx(original_rotation.value.values[0] * 180 / std::numbers::pi));
    check_conversion(converted.nodes[0].ops[0].kind == osbx_transform_kind_t::scale);
    check_conversion(converted.nodes[0].ops[1].kind == osbx_transform_kind_t::rotate);
    check_conversion(converted.nodes[0].ops[2].kind == osbx_transform_kind_t::translate);
}

TEST_CASE("OSB conversion preserves defaults gaps shorthand and source precedence", "[osb-to-osbx]") {
    auto source = parse_source("Sprite,Background,Centre,a.png,320,240\n"
        " F,0,-100,300,1,1\n M,0,0,100,10,20,100,200\n MX,0,0,100,30,130\n"
        " MY,0,150,200,50,70\n S,0,0,50,2\n V,0,100,150,3,4\n R,0,0,,1\n"
        " M,0,250,300,2,3,4,5\n");
    const auto converted = convert_source(source);
    const std::array<std::int64_t, 14> times{-101, -100, -1, 0, 1, 50, 100, 125, 150, 175, 200, 249, 250, 300};
    compare_samples(source, converted, times);
    std::ranges::reverse(source.sprites[0].nodes);
    compare_samples(source, convert_source(source), times);
}

TEST_CASE("OSB conversion preserves exact fractional evaluation and lifetime", "[osb-to-osbx]") {
    const auto source = parse_source("Sprite,Foreground,Centre,a.png,0,0\n M,0,-10,10,0,0,20,40\n");
    const auto converted = convert_source(source);
    for (const auto time : std::array{storyboard_time_t{.milliseconds = -11, .remainder = 999, .denominator = 1000},
        storyboard_time_t{.milliseconds = -10}, storyboard_time_t{.milliseconds = 0, .remainder = 1, .denominator = 2},
        storyboard_time_t{.milliseconds = 10}, storyboard_time_t{.milliseconds = 10, .remainder = 1, .denominator = 1000}})
        compare_frame(source, converted, time);
}

TEST_CASE("OSB conversion retains layer and source draw order and hides keyless sprites", "[osb-to-osbx]") {
    auto source = parse_source("Sprite,Foreground,BottomRight,front.png,10,20\n F,0,0,100,1,1\n"
        "Sprite,Background,TopLeft,back.png,30,40\n F,0,0,100,1,1\n"
        "Sprite,Foreground,Centre,hidden.png,50,60\n");
    std::ranges::reverse(source.sprites);
    const auto converted = convert_source(source);
    check_conversion(converted.nodes.size() == 3);
    const std::array<std::int64_t, 3> times{-1, 50, 101};
    compare_samples(source, converted, times);
}

TEST_CASE("OSB conversion shares continuous endpoints without duplicate keys", "[osb-to-osbx]") {
    const auto source = parse_source("Sprite,Foreground,Centre,a.png,0,0\n"
        " M,0,0,50,0,10,100,100\n M,0,50,100,100,100,200,200\n");
    const auto converted = convert_source(source);
    const std::array<std::int64_t, 5> times{0, 25, 50, 75, 100};
    compare_samples(source, converted, times);
    check_conversion(validate_osbx_document(converted).value);
}

TEST_CASE("OSB conversion keeps canonical sequential endpoint-group expansion", "[osb-to-osbx]") {
    const auto source = parse_source("Sprite,Foreground,Centre,a.png,0,0\n"
        " F,0,0,100,0,1,0\n M,0,0,100,0,0,100,200,200,300\n");
    const auto converted = convert_source(source);
    const std::array<std::int64_t, 5> times{0, 50, 100, 150, 200};
    compare_samples(source, converted, times);
}

TEST_CASE("OSB conversion refuses unsupported retained content and loops with locations", "[osb-to-osbx]") {
    check_refusal("Animation,Foreground,Centre,a.png,0,0,2,100,LoopForever\n", 1);
    check_refusal("Sprite,Foreground,Centre,a.png,0,0\n P,0,0,10,H\n", 2, "1");
    check_refusal("Sprite,Foreground,Centre,a.png,0,0\n T,Passing,0,100\n", 2, "1");
    check_refusal("Sprite,Foreground,Centre,a.png,0,0\n L,0,1000000000\n  F,0,0,100,0,1\n", 2, "1");
}

TEST_CASE("OSB conversion refuses interrupted or discontinuous curves without approximation", "[osb-to-osbx]") {
    // The first case mirrors the overlapping M/MX fixture in storyboard_preview_tests.cpp.
    check_refusal("Sprite,Foreground,Centre,a.png,320,240\n M,0,100,200,0,10,100,110\n"
        " MX,0,125,175,50,70\n", 3, "1");
    check_refusal("Sprite,Foreground,Centre,a.png,0,0\n F,3,0,100,0,1\n F,0,50,60,0.5,1\n", 3, "1");
    check_refusal("Sprite,Foreground,Centre,a.png,0,0\n F,0,0,100,0,1\n F,0,100,200,0,1\n", 3, "1");
}

TEST_CASE("OSB conversion rejects malformed models even when a later command hides them", "[osb-to-osbx]") {
    auto source = parse_source("Sprite,Foreground,Centre,a.png,0,0\n F,0,0,10,0,1\n F,0,0,10,0,1\n");
    source.sprites[0].nodes[0].command.easing = 99;
    check_conversion<false>(convert_osb_to_osbx(source, {}).value);
    source.sprites[0].nodes[0].command.easing = 0;
    source.sprites[0].path = "../escape.png";
    check_conversion<false>(convert_osb_to_osbx(source, {}).value);
}

TEST_CASE("OSB conversion creates one editable copy with returned identity and exact Undo", "[osb-to-osbx]") {
    auto history = document_history_t::from(source_song(parse_source(
        "Sprite,Foreground,Centre,a.png,0,0\n F,0,0,100,0,1\n")));
    const auto original = history.document.storyboards[0];
    const auto before = native_bytes(history.document);
    const osbx_edit_t convert{.storyboard = original.id, .change = osbx_convert_storyboard_t{}};
    const auto result = apply_osbx_edit_with_result(history, convert);
    require_conversion(result);
    require_conversion(result.payload);
    check_conversion(history.undo.size() == 1);
    check_conversion(history.document.storyboards[0] == original);
    auto created = std::ranges::find(history.document.storyboards, *result.payload, &storyboard_document_t::id);
    require_conversion(created != history.document.storyboards.end());
    require_conversion(created->osbx);
    check_conversion<false>(created->model);
    check_conversion<false>(created->owning_generator_recipe);
    check_conversion(created->retained_source_lines.empty());
    const auto after = native_bytes(history.document);
    const auto loaded = parse_native_project(after);
    require_conversion(loaded.value);
    check_conversion(loaded.value->storyboards == history.document.storyboards);
    require_conversion(undo_document_command(history));
    check_conversion(native_bytes(history.document) == before);
    require_conversion(redo_document_command(history));
    check_conversion(native_bytes(history.document) == after);
    require_conversion(undo_document_command(history));
    const auto again = apply_osbx_edit_with_result(history, convert);
    require_conversion(again);
    check_conversion(again.payload != result.payload);
}

TEST_CASE("OSB conversion rejection is atomic and respects source ownership", "[osb-to-osbx]") {
    auto song = source_song(parse_source("Sprite,Foreground,Centre,a.png,0,0\n P,0,0,100,A\n"));
    auto history = document_history_t::from(song);
    const auto before = native_bytes(history.document);
    const auto refused = apply_osbx_edit_with_result(history,
        {.storyboard = storyboard_id_t{2}, .change = osbx_convert_storyboard_t{}});
    check_conversion<false>(refused);
    check_conversion<false>(refused.payload);
    check_conversion(history.undo.empty());
    check_conversion(native_bytes(history.document) == before);
    song.storyboards[0].owning_generator_recipe = recipe_id_t{3};
    history = document_history_t::from(song);
    const auto owned = apply_osbx_edit(history, {.storyboard = storyboard_id_t{2}, .change = osbx_convert_storyboard_t{}});
    check_conversion<false>(owned.applied);
    require_conversion<false>(owned.diagnostics.empty());
    check_conversion(owned.diagnostics[0].code == diagnostic_code_t::invalid_binding);
    check_conversion(history.document.storyboards == song.storyboards);
}

TEST_CASE("Converted OSB lifetime is explicitly editable and key moves preserve it", "[osb-to-osbx][lifetime]") {
    auto history = document_history_t::from(source_song(parse_source(
        "Sprite,Foreground,Centre,a.png,0,0\n F,0,0,100,0,1\n")));
    const auto converted = apply_osbx_edit_with_result(history,
        {.storyboard = storyboard_id_t{2}, .change = osbx_convert_storyboard_t{}});
    require_conversion(converted.payload);
    const auto id = *converted.payload;
    const auto scene = *std::ranges::find(history.document.storyboards, id, &storyboard_document_t::id)->osbx;
    const auto node = scene.nodes[0].id;
    const auto before = native_bytes(history.document);
    require_conversion(apply_osbx_edit(history, {.storyboard = id, .change = osbx_set_lifetime_t{
        .node = node, .lifetime = osbx_lifetime_t{.begin_ms = -10, .end_ms = 200}}}));
    require_conversion(undo_document_command(history));
    check_conversion(native_bytes(history.document) == before);
    const auto count = history.undo.size();
    check_conversion<false>(apply_osbx_edit(history, {.storyboard = id, .change = osbx_set_lifetime_t{
        .node = node, .lifetime = osbx_lifetime_t{.begin_ms = 200, .end_ms = -10}}}));
    check_conversion(history.undo.size() == count);
    check_conversion(native_bytes(history.document) == before);
    require_conversion(apply_osbx_edit(history, {.storyboard = id, .change = osbx_move_key_t{
        .track = {.node = node, .kind = osbx_track_kind_t::opacity},
        .key = scene.nodes[0].tracks.opacity.keys.back().id, .time_ms = 300}}));
    check_conversion(std::ranges::find(history.document.storyboards, id, &storyboard_document_t::id)->osbx->nodes[0].lifetime == scene.nodes[0].lifetime);
    require_conversion(apply_osbx_edit(history, {.storyboard = id, .change = osbx_set_lifetime_t{.node = node}}));
    check_conversion<false>(std::ranges::find(history.document.storyboards, id, &storyboard_document_t::id)->osbx->nodes[0].lifetime);
    require_conversion(undo_document_command(history));
    check_conversion(std::ranges::find(history.document.storyboards, id, &storyboard_document_t::id)->osbx->nodes[0].lifetime == scene.nodes[0].lifetime);
}

TEST_CASE("OSBX lifetime edits reject generator-owned targets", "[osb-to-osbx][lifetime]") {
    const auto source = parse_source("Sprite,Foreground,Centre,a.png,0,0\n F,0,0,100,0,1\n");
    auto song = source_song(source);
    song.generator_recipes = {{.id = recipe_id_t{4}}};
    song.storyboards = {{.id = storyboard_id_t{3}, .osbx = convert_source(source), .owning_generator_recipe = recipe_id_t{4}}};
    auto history = document_history_t::from(song);
    const auto before = native_bytes(song);
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard_id_t{3},
        .change = osbx_set_lifetime_t{.node = song.storyboards[0].osbx->nodes[0].id}});
    check_conversion<false>(result.applied);
    require_conversion<false>(result.diagnostics.empty());
    check_conversion(result.diagnostics[0].code == diagnostic_code_t::invalid_binding);
    check_conversion(native_bytes(history.document) == before);
    check_conversion(history.undo.empty());
}

TEST_CASE("OSB conversion lifetime survives native save and OSB re-export exactly at fractional edges", "[osb-to-osbx][lifetime]") {
    const auto source = parse_source("Sprite,Foreground,Centre,a.png,320,240\n M,0,-10,10,0,0,20,40\n");
    auto song = source_song(source);
    song.storyboards = {{.id = storyboard_id_t{3}, .osbx = convert_source(source)}};
    const auto loaded = parse_native_project(native_bytes(song));
    require_conversion(loaded.value);
    const auto& scene = *loaded.value->storyboards[0].osbx;
    const std::array sizes{osbx_osb_image_size_t{.image_id = scene.images[0].id, .width = 64, .height = 48}};
    const auto baked = convert_osbx_to_osb(scene, {.image_sizes = sizes,
        .playback_span = osbx_osb_span_t{.begin_time_ms = -100, .end_time_ms = 100}});
    for (const auto& diagnostic : baked.diagnostics) INFO(diagnostic.message);
    require_conversion(baked.value.model);
    const auto emitted = emit_osb(*baked.value.model);
    require_conversion(emitted.value);
    const auto reparsed = parse_source(*emitted.value);
    const auto index = storyboard_preview_index_t::from(reparsed);
    require_conversion(index.value);
    require_conversion(index.value->sources.size() == 1);
    check_conversion(index.value->sources[0].begin_time_ms == -10);
    check_conversion(index.value->sources[0].end_time_ms == 10);
    const auto roundtrip = convert_source(reparsed);
    compare_frame(reparsed, scene, {.milliseconds = -10});
    compare_frame(source, roundtrip, {.milliseconds = 10});
    compare_frame(source, roundtrip, {.milliseconds = 10, .remainder = 1, .denominator = 1000});
}


TEST_CASE("structured animations refuse static OSBX conversion without changing the source", "[osb-to-osbx][animation]") {
    const auto source = parse_source("Animation,Foreground,Centre,a.png,0,0,2,100,LoopOnce\n F,0,0,100,0,1\n");
    require_conversion(source.sprites.front().animation);
    const auto before = source;
    const auto converted = convert_osb_to_osbx(source, {});
    check_conversion<false>(converted.value);
    require_conversion<false>(converted.diagnostics.empty());
    check_conversion(converted.diagnostics.front().code == failure_code_t::unsupported);
    check_conversion(converted.diagnostics.front().location.provenance.line == 1);
    check_conversion(source == before);
}

TEST_CASE("structured OSB Samples refuse OSBX conversion until sample mapping exists", "[osb-to-osbx][sample]") {
    const auto source = parse_source("Sample,10,0,\"hit.wav\",80\n");
    const auto unchanged = source;
    const auto result = convert_osb_to_osbx(source, {});
    check_conversion<false>(result.value);
    check_conversion(!result.diagnostics.empty());
    check_conversion(source == unchanged);
}

TEST_CASE("OSB conversion refuses structured triggers atomically with source identity", "[osb-to-osbx][trigger]") {
    check_refusal("Sprite,Foreground,Centre,a.png,0,0\n T,Passing,0,100\n  F,0,0,10,0,1\n", 2, "1");
    check_refusal("Sprite,Foreground,Centre,a.png,0,0\n T,HitSound\n  L,0,2\n   F,0,0,10,0,1\n", 2, "1");
}
