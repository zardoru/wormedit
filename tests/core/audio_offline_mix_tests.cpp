#include <wormedit/audio/offline_mix.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace {

using namespace wormedit;
using namespace wormedit::audio;

auto register_tone(decoded_source_registry_t& registry, std::uint64_t id,
                   std::size_t frames, float left, float right)
    -> decoded_source_binding_t {
    std::vector<float> pcm(frames * output_channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        pcm[frame * output_channel_count] = left;
        pcm[frame * output_channel_count + 1] = right;
    }
    const auto registered = registry.create_generated_source({
        .source = {"offline-generated:" + std::to_string(id), "offline-mix-test-v1"},
        .interleaved_stereo = std::move(pcm),
    });
    if (!registered) return {};
    return *registered;
}

auto make_music_schedule(std::uint64_t frames) -> preview_schedule_t {
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.music = preview_schedule_t::music_event_t{
        .resource = resource_id_t{101}, .frame_count = frames};
    return schedule;
}

auto music_binding(const decoded_source_binding_t& source)
    -> preview_object_source_binding_t {
    return {.object = object_id_t{std::numeric_limits<std::uint64_t>::max()},
            .voices = {{.source = source.source, .frame_count = source.output_frame_count}}};
}

auto bgm_binding(const decoded_source_binding_t& source, std::uint64_t object)
    -> preview_object_source_binding_t {
    return {.object = object_id_t{object},
            .voices = {{.source = source.source, .frame_count = source.output_frame_count}}};
}

auto valid_source_pair(const decoded_source_binding_t& first,
                       const decoded_source_binding_t& second) -> bool {
    return first.source.asset_identity == "offline-generated:1" &&
           second.source.asset_identity == "offline-generated:2";
}

auto expected_tail_mix(const std::vector<float>& samples) -> bool {
    if (samples.size() != 64U * output_channel_count) return false;
    for (std::size_t frame = 0; frame < 40; ++frame)
        if (samples[frame * 2] != 0.375F || samples[frame * 2 + 1] != 0.1875F) return false;
    for (std::size_t frame = 40; frame < 64; ++frame)
        if (samples[frame * 2] != 0.125F || samples[frame * 2 + 1] != 0.0625F) return false;
    return true;
}

auto expected_playable_mix(const std::vector<float>& samples) -> bool {
    if (samples.size() != 32U * output_channel_count) return false;
    for (std::size_t frame = 0; frame < 32; ++frame) {
        const auto expected_left = frame < 8 ? 0.0F : 0.0625F;
        const auto expected_right = frame < 8 ? 0.0F : 0.03125F;
        if (samples[frame * 2] != expected_left || samples[frame * 2 + 1] != expected_right)
            return false;
    }
    return true;
}

} // namespace

TEST_CASE("Offline interval uses preview voices and preserves pre-range tails", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    const auto music = register_tone(sources, 1, 160, 0.125F, 0.0625F);
    const auto bgm = register_tone(sources, 2, 64, 0.25F, 0.125F);
    REQUIRE(valid_source_pair(music, bgm));
    auto schedule = make_music_schedule(160);
    schedule.bgm = {{.sample_frame = 8, .object = object_id_t{2}, .resource = resource_id_t{202}}};
    std::array objects{music_binding(music), bgm_binding(bgm, 2)};
    offline_mix_options_t options{.maximum_read_frames = 32,
                                  .pump = {.lookahead_frames = 1}};
    const auto first = render_offline_interval(sources, schedule, objects, 32, 96, options);
    const auto second = render_offline_interval(sources, schedule, objects, 32, 96, options);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->interleaved_stereo == second->interleaved_stereo);
    REQUIRE(expected_tail_mix(first->interleaved_stereo));
}

TEST_CASE("Offline interval excludes an event at its exclusive end", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.bgm = {{.sample_frame = 64, .object = object_id_t{3}, .resource = resource_id_t{303}}};
    const auto result = render_offline_interval(sources, schedule, {}, 0, 64);
    REQUIRE(result);
    REQUIRE(std::ranges::all_of(result->interleaved_stereo, [](float value) { return value == 0.0F; }));
}

TEST_CASE("Offline interval mixes playable keysounds through the chart queue", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    const auto key = register_tone(sources, 5, 32, 0.0625F, 0.03125F);
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {{.sample_frame = 40, .object = object_id_t{5}, .resource = resource_id_t{505}}};
    const std::array objects{bgm_binding(key, 5)};
    const auto result = render_offline_interval(sources, schedule, objects, 32, 64,
                                                {.maximum_read_frames = 16});
    REQUIRE(result);
    REQUIRE(expected_playable_mix(result->interleaved_stereo));
}

TEST_CASE("Offline interval fade reaches digital silence and empty schedules remain silent", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    const auto music = register_tone(sources, 4, 256, 0.25F, 0.125F);
    const auto schedule = make_music_schedule(256);
    const std::array objects{music_binding(music)};
    const auto faded = render_offline_interval(
        sources, schedule, objects, 0, 256,
        {.maximum_read_frames = 64, .fade_out_ms = 2});
    REQUIRE(faded);
    REQUIRE(faded->interleaved_stereo.front() != 0.0F);
    REQUIRE(faded->interleaved_stereo[faded->interleaved_stereo.size() - 1] == 0.0F);
    preview_schedule_t empty{.sample_rate = output_sample_rate};
    const auto silent = render_offline_interval(sources, empty, {}, 0, 100);
    REQUIRE(silent);
    REQUIRE(std::ranges::all_of(silent->interleaved_stereo, [](float value) { return value == 0.0F; }));
}

TEST_CASE("Offline interval rejects missing sources without returning partial PCM", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.bgm = {{.sample_frame = 0, .object = object_id_t{8}, .resource = resource_id_t{808}}};
    const auto missing = render_offline_interval(sources, schedule, {}, 0, 64);
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().code == failure_code_t::unavailable);
}

TEST_CASE("Offline interval reports missing decoder data instead of silent output", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.bgm = {{.sample_frame = 0, .object = object_id_t{9}, .resource = resource_id_t{909}}};
    const preview_object_source_binding_t unknown{
        .object = object_id_t{9},
        .voices = {{.source = {"unregistered:offline", "missing"}, .frame_count = 128}},
    };
    const std::array objects{unknown};
    const auto missing = render_offline_interval(sources, schedule, objects, 0, 64);
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().code == failure_code_t::decode_failed);
}

TEST_CASE("Offline interval rejects a bounded pump that cannot decode every scheduled voice", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    const auto key = register_tone(sources, 6, 256, 0.01F, 0.01F);
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    for (std::size_t i = 0; i < maximum_preview_pump_voices + 1; ++i)
        schedule.playable.push_back({.sample_frame = 0, .object = object_id_t{6}, .resource = resource_id_t{606}});
    const std::array objects{bgm_binding(key, 6)};
    const auto capped = render_offline_interval(sources, schedule, objects, 0, 64);
    REQUIRE_FALSE(capped.has_value());
    REQUIRE(capped.error().code == failure_code_t::capacity_exceeded);
}

TEST_CASE("Offline interval discards earlier chunks when a later source is missing", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.bgm = {{.sample_frame = 64, .object = object_id_t{10}, .resource = resource_id_t{1'010}}};
    const preview_object_source_binding_t unknown{
        .object = object_id_t{10},
        .voices = {{.source = {"unregistered:late", "missing"}, .frame_count = 128}},
    };
    const std::array objects{unknown};
    const auto missing = render_offline_interval(
        sources, schedule, objects, 0, 96, {.maximum_read_frames = 32});
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().code == failure_code_t::decode_failed);
}

TEST_CASE("Offline interval rejects an empty range", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    const auto invalid = render_offline_interval(sources, schedule, {}, 20, 20);
    REQUIRE_FALSE(invalid.has_value());
    REQUIRE(invalid.error().code == failure_code_t::invalid_request);
}

TEST_CASE("Offline interval bounds output frames and bytes", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    const auto capped = render_offline_interval(
        sources, schedule, {}, 0, 65,
        {.maximum_output_frames = 64, .maximum_output_bytes = 1024});
    REQUIRE_FALSE(capped.has_value());
    REQUIRE(capped.error().code == failure_code_t::capacity_exceeded);
    const auto byte_capped = render_offline_interval(
        sources, schedule, {}, 0, 64,
        {.maximum_output_frames = 64, .maximum_output_bytes = 64});
    REQUIRE_FALSE(byte_capped.has_value());
    REQUIRE(byte_capped.error().code == failure_code_t::capacity_exceeded);
}

TEST_CASE("Offline interval reports cancellation without a PCM value", "[unit][audio][offline]") {
    decoded_source_registry_t sources;
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    std::stop_source cancel;
    cancel.request_stop();
    const auto cancelled = render_offline_interval(sources, schedule, {}, 0, 64, {}, cancel.get_token());
    REQUIRE_FALSE(cancelled.has_value());
    REQUIRE(cancelled.error().code == failure_code_t::cancelled);
}
