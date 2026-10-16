#include <wormedit/audio/audition_session.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <span>
#include <thread>
#include <string>
#include <utility>
#include <vector>

namespace wormedit::audio {
struct audition_session_test_access_t {
    struct captured_pump_state_t {
        std::optional<session_token_t> token;
        std::vector<audition_clip_t> clips;
        std::uint64_t timeline_frame{};
    };

    static auto snapshot(const audition_session_t& session) -> captured_pump_state_t {
        const auto state = session.snapshot_pump_state();
        return {state.token, state.clips, state.timeline_frame};
    }

    static auto complete(audition_session_t& session, session_token_t token,
                         audition_pump_outcome_t result) -> audition_pump_outcome_t {
        return session.complete_pump(token, std::move(result));
    }
};
} // namespace wormedit::audio

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto register_fixture(wormedit::audio::decoded_source_registry_t& registry, std::string_view name,
                      wormedit::asset_id_t asset = wormedit::asset_id_t{1})
    -> wormedit::audio::decoded_source_binding_t {
    const auto added = registry.create_source({
        .asset = asset,
        .path = fixture_path(name),
        .source = {"fixture:" + std::string(name), "generated-v1:" + std::string(name)},
    });
    REQUIRE(added);
    return *added;
}

auto all_zero(const std::vector<float>& samples) -> bool {
    return std::ranges::all_of(samples, [](float sample) { return sample == 0.0F; });
}

auto load_resident_fixture(wormedit::audio::decoded_source_registry_t& sources,
                           wormedit::audio::resident_sample_store_t& store,
                           std::string_view name,
                           wormedit::asset_id_t asset = wormedit::asset_id_t{1})
    -> wormedit::audio::decoded_source_binding_t {
    const auto binding = register_fixture(sources, name, asset);
    const wormedit::audio::resident_request_t request{binding.source,
        "resident-test:" + std::string(name), binding.output_frame_count};
    static_cast<void>(store.reconcile(std::span{&request, 1}));
    REQUIRE(sources.load_resident(binding.asset, store, request) ==
            wormedit::audio::resident_load_status_t::loaded);
    return binding;
}

auto matches_resident_clip(std::span<const float> rendered,
                           const wormedit::audio::resident_snapshot_t& snapshot,
                           const wormedit::audio::audio_source_identity_t& source,
                           std::uint64_t source_start, float gain) -> bool {
    using namespace wormedit::audio;
    const auto block = snapshot.block({source, source_start / maximum_read_frames});
    const auto offset = static_cast<std::size_t>(source_start % maximum_read_frames) *
                        output_channel_count;
    if (rendered.size() > block.size() || offset > block.size() - rendered.size()) return false;
    for (std::size_t i = 0; i < rendered.size(); ++i) {
        if (rendered[i] != block[offset + i] * gain) return false;
    }
    return true;
}

auto matches_cached_clip(std::span<const float> rendered,
                        const wormedit::audio::decoded_audio_block_t& block,
                        std::uint64_t source_start, float gain) -> bool {
    using namespace wormedit::audio;
    const auto offset = static_cast<std::size_t>(source_start % maximum_read_frames) *
                        output_channel_count;
    if (rendered.size() > block.interleaved_stereo.size() ||
        offset > block.interleaved_stereo.size() - rendered.size()) return false;
    for (std::size_t i = 0; i < rendered.size(); ++i) {
        if (rendered[i] != block.interleaved_stereo[offset + i] * gain) return false;
    }
    return true;
}

struct captured_pump_gate_t {
    std::mutex mutex;
    std::condition_variable changed;
    bool decoded{};
    bool release{};
    wormedit::audio::session_token_t old_token;
    wormedit::audio::audition_pump_outcome_t pump_result =
        std::unexpected(wormedit::audio::audition_pump_effect_t{});
    wormedit::audio::audition_pump_outcome_t completion_result =
        std::unexpected(wormedit::audio::audition_pump_effect_t{});
    std::thread worker;

    template <typename start_t>
    void start(start_t& session, wormedit::audio::decoded_source_registry_t& sources,
               wormedit::audio::transport_t& transport) {
        auto captured = wormedit::audio::audition_session_test_access_t::snapshot(session);
        if (captured.token) old_token = *captured.token;
        worker = std::thread([this, &session, &sources, &transport,
                              captured = std::move(captured)]() mutable {
            auto result = wormedit::audio::pump_audition_blocks(
                sources, transport, *captured.token, captured.clips, captured.timeline_frame);
            {
                const std::scoped_lock lock(mutex);
                pump_result = result;
                decoded = true;
            }
            changed.notify_all();
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [this] { return release; });
            }
            completion_result = wormedit::audio::audition_session_test_access_t::complete(
                session, *captured.token, std::move(result));
        });
    }

    auto wait_decoded() -> bool {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(3), [this] { return decoded; });
    }

    void release_and_join() {
        {
            const std::scoped_lock lock(mutex);
            release = true;
        }
        changed.notify_all();
        if (worker.joinable()) worker.join();
    }

    ~captured_pump_gate_t() { release_and_join(); }
};

auto resident_first_pcm_is_exact() -> bool {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    resident_sample_store_t store;
    const auto binding = load_resident_fixture(sources, store, "keysound-c4.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    transport.set_resident_snapshot(store.snapshot());
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    constexpr std::uint64_t source_offset = 128;
    constexpr std::size_t frames = 128;
    constexpr float gain = 0.375F;
    const audition_clip_t clip{binding.source, source_offset, frames, gain};
    const auto started = session.start_session(std::span{&clip, 1});
    if (!started || !started.value().prime || started.value().prime->value().cached_blocks != 1) return false;
    const auto snapshot = store.snapshot();
    std::vector<float> rendered(frames * output_channel_count);
    const auto result = session.render_frames(rendered);
    return result.value().status.is_in_phase(session_render_phase_t::complete) &&
        result.value().first_timeline_frame == 0 && matches_resident_clip(
            rendered, *snapshot, binding.source, source_offset, gain) &&
        session.timeline_frame() == 0 && snapshot->find(binding.source) != nullptr;
}

auto resident_replacements_keep_only_latest_pcm() -> bool {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    resident_sample_store_t store;
    const auto binding = load_resident_fixture(sources, store, "keysound-c4.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    transport.set_resident_snapshot(store.snapshot());
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    constexpr std::uint64_t source_offset = 96;
    constexpr std::size_t frames = 64;
    std::uint64_t prior_generation{};
    for (std::uint32_t attempt = 1; attempt <= 10; ++attempt) {
        const auto gain = static_cast<float>(attempt) / 10.0F;
        const audition_clip_t clip{binding.source, source_offset, frames, gain};
        const auto started = session.start_session(std::span{&clip, 1});
        if (!started || !started.value().prime ||
            started.value().token.generation <= prior_generation) return false;
        prior_generation = started.value().token.generation;
    }
    const auto snapshot = store.snapshot();
    std::vector<float> rendered(frames * output_channel_count);
    const auto result = session.render_frames(rendered);
    return result.value().status.is_in_phase(session_render_phase_t::complete) &&
        matches_resident_clip(rendered, *snapshot, binding.source, source_offset, 1.0F) &&
        transport.scheduled_voice_count() == 0;
}

void expect_audition_session(bool value) { CHECK(value); }
template <typename T>
void require_audition_session(const T& value) { REQUIRE(static_cast<bool>(value)); }

auto old_pump_cannot_clear_replacement() -> bool {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto old_source = register_fixture(sources, "keysound-c4.ogg", wormedit::asset_id_t{1});
    const auto new_source = register_fixture(sources, "polyphonic-chord.ogg", wormedit::asset_id_t{2});
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    const audition_clip_t old_clip{old_source.source, 0, 512, 0.25F};
    if (!session.start_session(std::span{&old_clip, 1})) return false;
    captured_pump_gate_t gate;
    gate.start(session, sources, transport);
    if (!gate.wait_decoded()) {
        gate.release_and_join();
        return false;
    }
    const audition_clip_t new_clip{new_source.source, 64, 128, 0.75F};
    const auto replacement = session.start_session(std::span{&new_clip, 1});
    if (!replacement || replacement.value().prime) {
        gate.release_and_join();
        return false;
    }
    gate.release_and_join();
    if (!gate.pump_result || gate.pump_result->status != audition_pump_status_t::ready ||
        gate.completion_result ||
        gate.completion_result.error().status != audition_pump_status_t::stale_generation) return false;
    const auto stale_failure = audition_session_test_access_t::complete(session, gate.old_token,
        std::unexpected(audition_pump_effect_t{.status = audition_pump_status_t::source_unavailable}));
    if (stale_failure || stale_failure.error().status != audition_pump_status_t::stale_generation) return false;
    std::vector<float> waiting(32 * output_channel_count, 1.0F);
    if (!session.render_frames(waiting).value().status.is_in_phase(session_render_phase_t::priming) ||
        !all_zero(waiting) || session.timeline_frame() != 0) return false;
    if (session.pump().value().status != audition_pump_status_t::ready) return false;
    std::vector<float> rendered(128 * output_channel_count);
    const auto mixed = session.render_frames(rendered);
    const auto decoded = cache.find({new_source.source, 0});
    return mixed.value().status.is_in_phase(session_render_phase_t::complete) && decoded &&
        matches_cached_clip(rendered, *decoded, new_clip.source_start_frame, new_clip.gain);
}

auto old_pump_cannot_publish_after_stop() -> bool {
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    const auto source = register_fixture(sources, "keysound-c4.ogg");
    decoded_block_cache_t cache;
    transport_t transport{cache};
    audition_transport_bridge_t bridge{transport};
    audition_session_t session{sources, transport, bridge};
    const audition_clip_t clip{source.source, 0, 512, 1.0F};
    if (!session.start_session(std::span{&clip, 1})) return false;
    captured_pump_gate_t gate;
    gate.start(session, sources, transport);
    if (!gate.wait_decoded()) {
        gate.release_and_join();
        return false;
    }
    static_cast<void>(session.stop());
    gate.release_and_join();
    std::vector<float> output(64 * output_channel_count, 1.0F);
    return gate.pump_result && gate.pump_result->status == audition_pump_status_t::ready &&
        !gate.completion_result &&
        gate.completion_result.error().status == audition_pump_status_t::stale_generation &&
        !session.playhead().active && transport.scheduled_voice_count() == 0 &&
        session.render_frames(output).value().status.is_in_phase(session_render_phase_t::stopped) && all_zero(output);
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

    const auto started = session.start_session(std::span{&clip, 1});
    require_audition_session(started);
    expect_audition_session(!started.value().prime.has_value());
    expect_audition_session(session.is_active());
    expect_audition_session(session.timeline_frame() == 0);

    std::vector<float> first(512 * output_channel_count);
    const auto waiting = session.render_frames(first);
    expect_audition_session(waiting.value().status.is_in_phase(session_render_phase_t::priming));
    expect_audition_session(waiting.value().first_timeline_frame == 0);
    expect_audition_session(all_zero(first));
    expect_audition_session(session.timeline_frame() == 0);

    audition_pump_outcome_t pumped = std::unexpected(audition_pump_effect_t{});
    std::thread worker([&] { pumped = session.pump(); });
    worker.join();
    expect_audition_session(pumped.value().status == audition_pump_status_t::ready);
    expect_audition_session(pumped.value().requested_blocks == 2);
    const auto first_render = session.render_frames(first);
    expect_audition_session(first_render.value().status.is_in_phase(session_render_phase_t::mixed));
    expect_audition_session(first_render.value().first_timeline_frame == 0);
    expect_audition_session(first_render.value().mix.missing_blocks == 0);
    expect_audition_session(!all_zero(first));
    expect_audition_session(session.timeline_frame() == 512);
    expect_audition_session(session.pump().value().status == audition_pump_status_t::ready);

    std::vector<float> tail(128 * output_channel_count);
    const auto final_render = session.render_frames(tail);
    expect_audition_session(final_render.value().status.is_in_phase(session_render_phase_t::complete));
    expect_audition_session(final_render.value().first_timeline_frame == 512);
    expect_audition_session(final_render.value().mix.missing_blocks == 0);
    expect_audition_session(!all_zero(tail));
    expect_audition_session(!session.is_active());

    std::ranges::fill(tail, 1.0F);
    expect_audition_session(session.render_frames(tail).value().status.is_in_phase(session_render_phase_t::stopped));
    expect_audition_session(all_zero(tail));
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
    require_audition_session(session.start_session(std::span{&clip, 1}));
    require_audition_session(session.pump());

    std::vector<float> before(256 * output_channel_count);
    expect_audition_session(session.render_frames(before).value().status.is_in_phase(session_render_phase_t::mixed));
    expect_audition_session(!all_zero(before));
    const auto stopped = session.stop();
    expect_audition_session(stopped.scheduled_voices == 1);
    expect_audition_session(!session.is_active());
    expect_audition_session(!session.pump());

    std::ranges::fill(before, 1.0F);
    expect_audition_session(session.render_frames(before).value().status.is_in_phase(session_render_phase_t::stopped));
    expect_audition_session(all_zero(before));
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
    const auto first = session.start_session(std::span{&clip, 1});
    require_audition_session(first);

    const audition_clip_t invalid{binding.source, 0, 0, 1.0F};
    const auto rejected = session.start_session(std::span{&invalid, 1});
    expect_audition_session(!rejected);
    require_audition_session(!rejected);
    expect_audition_session(rejected.error().code == wormedit::failure_code_t::transport_failed);
    expect_audition_session(session.is_active());
    expect_audition_session(transport.is_current(first.value().token));

    const audition_clip_t unavailable{{"fixture:missing", "unknown:v1"}, 0, 512, 1.0F};
    const auto unprimed = session.start_session(std::span{&unavailable, 1});
    require_audition_session(unprimed);
    expect_audition_session(!unprimed.value().prime.has_value());
    std::vector<float> pending(64 * output_channel_count, 1.0F);
    expect_audition_session(session.render_frames(pending).value().status.is_in_phase(session_render_phase_t::priming));
    expect_audition_session(all_zero(pending));
    expect_audition_session(session.timeline_frame() == 0);
    const auto failed_pump = session.pump();
    require_audition_session(!failed_pump);
    expect_audition_session(failed_pump.error().status == audition_pump_status_t::source_unavailable);
    expect_audition_session(session.playhead().active);
    expect_audition_session(session.timeline_frame() == 0);
    static_cast<void>(session.stop());
    expect_audition_session(!session.playhead().active);
    expect_audition_session(transport.scheduled_voice_count() == 0);

    std::vector<float> malformed(output_channel_count + 1, 1.0F);
    const auto malformed_render = session.render_frames(malformed);
    require_audition_session(!malformed_render);
    expect_audition_session(malformed_render.error().status.has_failure(wormedit::failure_code_t::invalid_request));
    expect_audition_session(all_zero(malformed));
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
    const auto started = session.start_session(std::span{&clip, 1});
    if (!started) return false;
    const auto prior = session.playhead();
    static_cast<void>(transport.stop());
    if (session.is_active() || session.timeline_frame() != 0 || session.playhead().active) return false;
    const auto restarted = session.start_session(std::span{&clip, 1});
    const auto current = session.playhead();
    return restarted && current.active && current.generation > prior.generation &&
           current.transport_generation == restarted.value().token.generation;
}
}

TEST_CASE("audition playhead rejects transport generations stopped outside its session",
          "[unit][audio][audition-session][playhead]") {
    CHECK(audition_playhead_rejects_external_generation());
}

TEST_CASE("resident audition renders its first PCM exactly from an immutable snapshot",
          "[unit][audio][audition-session][resident][prime]") {
    expect_audition_session(resident_first_pcm_is_exact());
}

TEST_CASE("ten rapid resident audition replacements render only the newest clip",
          "[unit][audio][audition-session][resident][replacement]") {
    expect_audition_session(resident_replacements_keep_only_latest_pcm());
}

TEST_CASE("a completed old audition pump cannot clear or replace a newer session",
          "[unit][audio][audition-session][replacement][worker]") {
    expect_audition_session(old_pump_cannot_clear_replacement());
}

TEST_CASE("a completed audition pump cannot publish after Stop",
          "[unit][audio][audition-session][stop][worker]") {
    expect_audition_session(old_pump_cannot_publish_after_stop());
}

TEST_CASE("render phases preserve stopped acceptance and failed mix metadata",
          "[unit][audio][audition-session][result]") {
    using namespace wormedit::audio;
    const auto stopped = session_render_effect_t::from(
        session_render_phase_t::stopped, 17,
        {.status = wormedit::failure_code_t::stale_generation});
    REQUIRE(stopped);
    CHECK(stopped->status.is_in_phase(session_render_phase_t::stopped));
    CHECK(stopped->mix.status == wormedit::failure_code_t::stale_generation);

    const auto failed = session_render_effect_t::from(
        wormedit::failure_code_t::underrun, 17, {.frames = 4, .missing_blocks = 2});
    REQUIRE_FALSE(failed);
    CHECK(failed.error().status.has_failure(wormedit::failure_code_t::underrun));
    CHECK(failed.error().mix.missing_blocks == 2);
}
