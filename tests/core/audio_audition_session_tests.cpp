#include <wormedit/audio/audition_session.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto register_fixture(wormedit::audio::decoded_source_registry_t& registry, std::string_view name)
    -> wormedit::audio::decoded_source_binding_t {
    const auto added = registry.register_source({
        .asset = wormedit::asset_id_t{1},
        .path = fixture_path(name),
        .source = {"fixture:" + std::string(name), "generated-v1:" + std::string(name)},
    });
    REQUIRE(added);
    return *added.binding;
}

auto all_zero(const std::vector<float>& samples) -> bool {
    return std::ranges::all_of(samples, [](float sample) { return sample == 0.0F; });
}

} // namespace

TEST_CASE("audition session primes renders and completes one monotonic click preview",
          "[unit][audio][audition-session][render][fixture]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto binding = register_fixture(sources, "polyphonic-chord.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    const audition_clip_t clip{binding.source, maximum_read_frames - 128, 640, 1.0F};

    const auto started = session.start(std::span{&clip, 1});
    REQUIRE(started);
    REQUIRE(started.prime.has_value());
    CHECK(started.prime->requested_blocks == 2);
    CHECK(session.active());
    CHECK(session.timeline_frame() == 0);

    std::vector<float> first(512 * output_channel_count);
    const auto first_render = session.render(first);
    CHECK(first_render.status == audition_session_render_status_t::mixed);
    CHECK(first_render.first_timeline_frame == 0);
    CHECK(first_render.mix.missing_blocks == 0);
    CHECK_FALSE(all_zero(first));
    CHECK(session.timeline_frame() == 512);
    CHECK(session.pump().status == audition_pump_status_t::ready);

    std::vector<float> tail(128 * output_channel_count);
    const auto final_render = session.render(tail);
    CHECK(final_render.status == audition_session_render_status_t::complete);
    CHECK(final_render.first_timeline_frame == 512);
    CHECK(final_render.mix.missing_blocks == 0);
    CHECK_FALSE(all_zero(tail));
    CHECK_FALSE(session.active());

    std::ranges::fill(tail, 1.0F);
    CHECK(session.render(tail).status == audition_session_render_status_t::stopped);
    CHECK(all_zero(tail));
}

TEST_CASE("audition session hard Stop silences the next render and invalidates refill",
          "[unit][audio][audition-session][stop][generation]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto binding = register_fixture(sources, "polyphonic-chord.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    const audition_clip_t clip{binding.source, 0, binding.output_frame_count, 1.0F};
    REQUIRE(session.start(std::span{&clip, 1}));

    std::vector<float> before(256 * output_channel_count);
    CHECK(session.render(before).status == audition_session_render_status_t::mixed);
    CHECK_FALSE(all_zero(before));
    const auto stopped = session.stop();
    CHECK(stopped.scheduled_voices == 1);
    CHECK_FALSE(session.active());
    CHECK_FALSE(session.pump());

    std::ranges::fill(before, 1.0F);
    CHECK(session.render(before).status == audition_session_render_status_t::stopped);
    CHECK(all_zero(before));
}

TEST_CASE("audition session preserves a live generation on pre-start validation failure",
          "[unit][audio][audition-session][replacement][failure]") {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto binding = register_fixture(sources, "keysound-c4.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    const audition_clip_t clip{binding.source, 0, binding.output_frame_count, 1.0F};
    const auto first = session.start(std::span{&clip, 1});
    REQUIRE(first);

    const audition_clip_t invalid{binding.source, 0, 0, 1.0F};
    const auto rejected = session.start(std::span{&invalid, 1});
    CHECK_FALSE(rejected);
    CHECK(rejected.status == audition_session_start_status_t::transport_failed);
    CHECK(session.active());
    CHECK(transport.is_current(*first.token));

    const audition_clip_t unavailable{{"fixture:missing", "unknown:v1"}, 0, 512, 1.0F};
    const auto unprimed = session.start(std::span{&unavailable, 1});
    CHECK_FALSE(unprimed);
    CHECK(unprimed.status == audition_session_start_status_t::prime_failed);
    REQUIRE(unprimed.prime.has_value());
    CHECK(unprimed.prime->status == audition_pump_status_t::source_unavailable);
    CHECK_FALSE(session.active());
    CHECK(transport.scheduled_voice_count() == 0);

    std::vector<float> malformed(output_channel_count + 1, 1.0F);
    CHECK(session.render(malformed).status == audition_session_render_status_t::invalid_buffer);
    CHECK(all_zero(malformed));
}

namespace {
auto audition_playhead_rejects_external_generation() -> bool {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = register_fixture(sources, "keysound-c4.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    const audition_clip_t clip{source.source, 0, 512, 1.0F};
    const auto started = session.start(std::span{&clip, 1});
    if (!started) return false;
    const auto prior = session.playhead();
    static_cast<void>(transport.stop());
    if (session.active() || session.timeline_frame() != 0 || session.playhead().active) return false;
    const auto restarted = session.start(std::span{&clip, 1});
    const auto current = session.playhead();
    return restarted && current.active && current.generation > prior.generation &&
           current.transport_generation == restarted.token->generation;
}
}

TEST_CASE("audition playhead rejects transport generations stopped outside its session",
          "[unit][audio][audition-session][playhead]") {
    CHECK(audition_playhead_rejects_external_generation());
}
