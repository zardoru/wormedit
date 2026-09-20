#include <wormedit/audio/preview_session.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <ranges>
#include <string>
#include <vector>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto register_fixture(wormedit::audio::decoded_source_registry_t& registry)
    -> wormedit::audio::decoded_source_binding_t {
    const auto added = registry.register_source({
        .asset = wormedit::asset_id_t{1},
        .path = fixture_path("polyphonic-chord.ogg"),
        .source = {"fixture:polyphonic-chord", "chart-session-v1"},
    });
    REQUIRE(added);
    return *added.binding;
}

auto any_nonzero(const std::vector<float>& samples) -> bool {
    return std::ranges::any_of(samples, [](float sample) { return sample != 0.0F; });
}

} // namespace

TEST_CASE("preview session primes advances refills and completes one chart timeline",
          "[unit][audio][preview-session][timeline][fixture]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = register_fixture(sources);
    decoded_block_cache_t cache;
    transport_t transport{cache, 8};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};

    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable = {
        {.sample_frame = 0, .object = object_id_t{1}, .resource = resource_id_t{1}},
        {.sample_frame = 256, .object = object_id_t{2}, .resource = resource_id_t{2}}};
    const std::vector<preview_object_source_binding_t> bindings{
        {.object = object_id_t{1},
         .voices = {{.source = source.source, .source_start_frame = 0, .frame_count = 512}}},
        {.object = object_id_t{2},
         .voices = {{.source = source.source, .source_start_frame = 512, .frame_count = 512}}},
    };
    const auto started = session.start(schedule, bindings, {}, 0, {.lookahead_frames = 512});
    REQUIRE(started);
    REQUIRE(started.materialize.has_value());
    CHECK(started.materialize->scheduled_events == 2);
    REQUIRE(started.prime.has_value());
    CHECK(started.prime->overlapping_voices == 2);
    CHECK(session.active());

    std::vector<float> first(512 * output_channel_count);
    const auto first_render = session.render(first);
    CHECK(first_render.status == preview_session_render_status_t::mixed);
    CHECK(first_render.first_timeline_frame == 0);
    CHECK(any_nonzero(first));
    CHECK(session.timeline_frame() == 512);

    const auto refilled = session.pump({.lookahead_frames = 512});
    REQUIRE(refilled);
    std::vector<float> tail(256 * output_channel_count);
    const auto completed = session.render(tail);
    CHECK(completed.status == preview_session_render_status_t::complete);
    CHECK(completed.first_timeline_frame == 512);
    CHECK(any_nonzero(tail));
    CHECK_FALSE(session.active());
    CHECK(session.timeline_frame() == 0);
}

TEST_CASE("preview session seek replaces generations and hard Stop silences later renders",
          "[unit][audio][preview-session][seek][stop]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = register_fixture(sources);
    decoded_block_cache_t cache;
    transport_t transport{cache, 4};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.bgm = {{.sample_frame = 100, .object = object_id_t{1}, .resource = resource_id_t{1}}};
    const preview_object_source_binding_t binding{
        .object = object_id_t{1},
        .voices = {{.source = source.source, .frame_count = 1'000}},
    };
    const auto started = session.start(schedule, std::span{&binding, 1});
    REQUIRE(started);
    const auto old_token = *started.token;

    const auto sought = session.seek(200);
    REQUIRE(sought);
    CHECK(sought.token != old_token);
    CHECK(session.timeline_frame() == 200);
    CHECK_FALSE(transport.is_current(old_token));

    const auto stopped = session.stop();
    CHECK(stopped.scheduled_voices == 0);
    std::vector<float> output(128 * output_channel_count, 1.0F);
    const auto rendered = session.render(output);
    CHECK(rendered.status == preview_session_render_status_t::stopped);
    CHECK(std::ranges::all_of(output, [](float sample) { return sample == 0.0F; }));
}
