#include <wormedit/audio/block_cache.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto key(std::string_view asset, std::string_view source, std::uint64_t block)
    -> wormedit::audio::audio_block_key_t {
    return {
        .source = {std::string(asset), std::string(source)},
        .block_index = block,
    };
}

auto samples(std::size_t frames, float value) -> std::vector<float> {
    return std::vector<float>(frames * wormedit::audio::output_channel_count, value);
}

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

} // namespace

TEST_CASE("decoded block cache enforces exact byte and frame capacity", "[unit][audio][cache]") {
    const wormedit::audio::decoded_block_cache_t defaults;
    CHECK(defaults.limits().maximum_frames_per_asset == 65'536);
    CHECK(defaults.limits().maximum_bytes_per_asset == 512U * 1024U);

    const wormedit::audio::block_cache_limits_t limits{
        .maximum_bytes = 16,
        .maximum_frames = 2,
        .maximum_bytes_per_asset = 16,
        .maximum_frames_per_asset = 2,
    };
    wormedit::audio::decoded_block_cache_t cache(limits);
    const auto exact = samples(2, 0.25F);
    CHECK(cache.store(key("asset-a", "source-v1", 0), 2, exact).has_value());
    auto state = cache.stats();
    CHECK(state.entries == 1);
    CHECK(state.cached_frames == 2);
    CHECK(state.cached_bytes == 16);

    const auto too_large = samples(3, 0.5F);
    CHECK(cache.store(key("asset-b", "source-v1", 0), 3, too_large) ==
          std::unexpected(wormedit::failure_code_t::capacity_exceeded));
    state = cache.stats();
    CHECK(state.entries == 1);
    CHECK(state.cached_frames == 2);
    CHECK(state.cached_bytes == 16);
    CHECK(state.rejected_stores == 1);
}

TEST_CASE("decoded block cache applies deterministic global and per-asset LRU eviction",
          "[unit][audio][cache]") {
    SECTION("global LRU refreshes on hit") {
        wormedit::audio::decoded_block_cache_t cache({
            .maximum_bytes = 16,
            .maximum_frames = 2,
            .maximum_bytes_per_asset = 16,
            .maximum_frames_per_asset = 2,
        });
        const auto one_frame = samples(1, 0.75F);
        const auto oldest = key("asset-a", "source-v1", 0);
        const auto refreshed = key("asset-b", "source-v1", 0);
        const auto incoming = key("asset-c", "source-v1", 0);
        REQUIRE(cache.store(oldest, 1, one_frame).has_value());
        REQUIRE(cache.store(refreshed, 1, one_frame).has_value());
        CHECK(cache.has_block(oldest)); // Makes asset A newer than asset B.
        REQUIRE(cache.store(incoming, 1, one_frame).has_value());
        CHECK(cache.has_block(oldest));
        CHECK_FALSE(cache.has_block(refreshed));
        CHECK(cache.has_block(incoming));
        const auto state = cache.stats();
        CHECK(state.evictions == 1);
        CHECK(state.cached_frames == 2);
        CHECK(state.cached_bytes == 16);
    }

    SECTION("per-asset LRU groups source revisions but is isolated from other assets") {
        wormedit::audio::decoded_block_cache_t cache({
            .maximum_bytes = 40,
            .maximum_frames = 5,
            .maximum_bytes_per_asset = 16,
            .maximum_frames_per_asset = 2,
        });
        const auto one_frame = samples(1, 0.5F);
        const auto first = key("asset-a", "source-v1", 0);
        const auto second = key("asset-a", "source-v2", 1);
        const auto third = key("asset-a", "source-v2", 2);
        const auto other_asset = key("asset-b", "source-v1", 0);
        REQUIRE(cache.store(first, 1, one_frame).has_value());
        REQUIRE(cache.store(second, 1, one_frame).has_value());
        REQUIRE(cache.store(other_asset, 1, one_frame).has_value());
        REQUIRE(cache.has_block(second));
        REQUIRE(cache.store(third, 1, one_frame).has_value());
        CHECK_FALSE(cache.has_block(first));
        CHECK(cache.has_block(second));
        CHECK(cache.has_block(third));
        CHECK(cache.has_block(other_asset));
        CHECK(cache.stats().evictions == 1);
    }
}

TEST_CASE("decoded block keys distinguish asset and source revisions", "[unit][audio][cache]") {
    wormedit::audio::decoded_block_cache_t cache;
    const auto data_a = samples(1, 0.125F);
    const auto data_b = samples(1, 0.5F);
    const auto same_asset_old_source = key("asset-a", "/audio/key.ogg#v1", 0);
    const auto same_asset_new_source = key("asset-a", "/audio/key.ogg#v2", 0);
    const auto other_asset = key("asset-b", "/audio/key.ogg#v1", 0);
    REQUIRE(cache.store(same_asset_old_source, 1, data_a).has_value());
    REQUIRE(cache.store(same_asset_new_source, 1, data_b).has_value());
    REQUIRE(cache.store(other_asset, 1, data_b).has_value());

    const auto old_block = cache.find(same_asset_old_source);
    const auto revised_block = cache.find(same_asset_new_source);
    const auto other_block = cache.find(other_asset);
    REQUIRE(old_block.has_value());
    REQUIRE(revised_block.has_value());
    REQUIRE(other_block.has_value());
    CHECK(old_block->interleaved_stereo.front() == 0.125F);
    CHECK(revised_block->interleaved_stereo.front() == 0.5F);
    CHECK(other_block->interleaved_stereo.front() == 0.5F);
    CHECK(cache.stats().entries == 3);
}

TEST_CASE("decoded block cache exposes a synchronous non-copying view",
          "[unit][audio][cache][realtime]") {
    wormedit::audio::decoded_block_cache_t cache;
    const auto source_key = key("asset-a", "source-v1", 0);
    const auto block = samples(4, 0.375F);
    REQUIRE(cache.store(source_key, 4, block).has_value());

    auto invoked = false;
    CHECK(cache.with_block(source_key, [&](const auto& cached) {
        CHECK(cached.frame_count == 4);
        CHECK(cached.interleaved_stereo == block);
        invoked = true;
    }));
    CHECK(invoked);
    CHECK_FALSE(cache.with_block(key("missing", "source-v1", 0), [](const auto&) {}));
    const auto stats = cache.stats();
    CHECK(stats.hits == 1);
    CHECK(stats.misses == 1);
}

TEST_CASE("zero-capacity block cache rejects storage without retaining PCM",
          "[unit][audio][cache]") {
    wormedit::audio::decoded_block_cache_t cache({
        .maximum_bytes = 0,
        .maximum_frames = 0,
        .maximum_bytes_per_asset = 0,
        .maximum_frames_per_asset = 0,
    });
    const auto one_frame = samples(1, 0.25F);
    CHECK(cache.store(key("asset-a", "source-v1", 0), 1, one_frame) ==
          std::unexpected(wormedit::failure_code_t::capacity_exceeded));
    CHECK(cache.stats().entries == 0);
    CHECK(cache.stats().cached_bytes == 0);
}

TEST_CASE("Stop invalidates stale generations and clears only the session queue",
          "[unit][audio][session]") {
    wormedit::audio::decoded_block_cache_t cache;
    wormedit::audio::decode_session_t session(1);
    const auto source_key = key("asset-a", "source-v1", 0);
    const auto block = samples(1, 0.25F);
    REQUIRE(cache.store(source_key, 1, block).has_value());

    const auto first_session = session.start();
    REQUIRE(first_session.has_value());
    CHECK(session.publish_if_current(*first_session, source_key) ==
          wormedit::audio::session_publish_phase_t::accepted);
    CHECK(session.publish_if_current(*first_session, source_key) ==
          wormedit::audio::session_publish_phase_t::already_pending);
    CHECK(session.pending_count() == 1);
    CHECK(session.publish_if_current(*first_session, key("asset-b", "source-v1", 0)) ==
          wormedit::failure_code_t::capacity_exceeded);

    session.stop();
    CHECK(session.pending_count() == 0);
    CHECK_FALSE(session.is_current(*first_session));
    CHECK(session.take_ready(*first_session).empty());
    CHECK(session.publish_if_current(*first_session, source_key) ==
          wormedit::failure_code_t::stale_generation);
    auto revived = false;
    CHECK_FALSE(session.with_current(*first_session, [&] { revived = true; }));
    CHECK_FALSE(revived);

    // Stop cancels pending transport work, while source PCM remains a reusable
    // derived cache entry and cannot itself start playback.
    CHECK(cache.has_block(source_key));
    CHECK(cache.stats().entries == 1);
    const auto next_session = session.start();
    REQUIRE(next_session.has_value());
    CHECK(next_session->generation != first_session->generation);
    CHECK(session.publish_if_current(*first_session, source_key) ==
          wormedit::failure_code_t::stale_generation);
    CHECK(session.is_current(*next_session));
}

TEST_CASE("fixture block decode is bounded, cached, and session-gated",
          "[unit][audio][cache][fixture]") {
    wormedit::audio::decoder_t decoder;
    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.ogg"), &error));
    INFO("decoder open error: " << error);

    wormedit::audio::decoded_block_cache_t cache({
        .maximum_bytes = 2U * wormedit::audio::maximum_read_frames * 2U * sizeof(float),
        .maximum_frames = 2U * wormedit::audio::maximum_read_frames,
        .maximum_bytes_per_asset = 2U * wormedit::audio::maximum_read_frames * 2U * sizeof(float),
        .maximum_frames_per_asset = 2U * wormedit::audio::maximum_read_frames,
    });
    wormedit::audio::decode_session_t session;
    const auto token = session.start();
    REQUIRE(token.has_value());
    const auto block_key = key("fixture:sliced-seek", "ogg:generated-v1", 0);

    const auto first =
        wormedit::audio::decode_and_cache_block(decoder, cache, session, *token, block_key);
    CHECK(first.status == wormedit::audio::block_decode_phase_t::decoded);
    CHECK(first.frames == wormedit::audio::maximum_read_frames);
    CHECK(cache.stats().cached_frames == wormedit::audio::maximum_read_frames);
    CHECK(cache.stats().misses == 1);
    CHECK(cache.stats().hits == 0);
    const auto cached = cache.find(block_key);
    REQUIRE(cached.has_value());
    CHECK(cached->interleaved_stereo.size() ==
          wormedit::audio::maximum_read_frames * wormedit::audio::output_channel_count);
    const auto expected_samples = cached->interleaved_stereo;

    const auto second =
        wormedit::audio::decode_and_cache_block(decoder, cache, session, *token, block_key);
    CHECK(second.status == wormedit::audio::block_decode_phase_t::cache_hit);
    CHECK(second.decoder_status == wormedit::audio::read_phase_t::success);
    CHECK(cache.stats().hits == 2);
    const auto ready = session.take_ready(*token);
    REQUIRE(ready.size() == 1);
    CHECK(ready.front() == block_key);
    const auto cached_again = cache.find(block_key);
    REQUIRE(cached_again.has_value());
    CHECK(cached_again->interleaved_stereo == expected_samples);

    session.stop();
    CHECK(wormedit::audio::decode_and_cache_block(decoder, cache, session, *token, block_key)
              .status == wormedit::failure_code_t::stale_generation);
    CHECK(session.pending_count() == 0);
    CHECK(cache.has_block(block_key));
}

namespace {
void verify_cache_condition(bool value) { REQUIRE(value); }
auto current_read_while_handoff_locked(wormedit::audio::decode_session_t& session,
                                       wormedit::audio::session_token_t token) -> bool {
    std::atomic<bool> finished{};
    bool current{}, finished_before_release{};
    std::jthread reader;
    const auto locked = session.with_current(token, [&] {
        reader = std::jthread([&] {
            current = session.is_current(token);
            finished.store(true, std::memory_order_release);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        while (!finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        finished_before_release = finished.load(std::memory_order_acquire);
    });
    if (reader.joinable()) reader.join();
    return locked && finished_before_release && current;
}
}

TEST_CASE("current generation reads remain available during queue handoff",
          "[unit][audio][cache]") {
    wormedit::audio::decode_session_t session;
    const auto first = session.start();
    verify_cache_condition(first.has_value());
    verify_cache_condition(current_read_while_handoff_locked(session, *first));
    verify_cache_condition(session.publish_if_current(*first, key("asset", "source", 0)) ==
          wormedit::audio::session_publish_phase_t::accepted);
    session.stop();
    verify_cache_condition(!session.is_current(*first));
    verify_cache_condition(session.pending_count() == 0);
    const auto second = session.start();
    verify_cache_condition(second.has_value());
    verify_cache_condition(session.is_current(*second));
    verify_cache_condition(!session.is_current(*first));
    verify_cache_condition(session.publish_if_current(*first, key("asset", "source", 1)) ==
          wormedit::failure_code_t::stale_generation);
}

namespace {
auto expected_block_failure_keeps_decoded_frames_and_cached_pcm() -> bool {
    using namespace wormedit::audio;
    decoder_t decoder;
    const std::vector<float> pcm{0.25F, 0.5F, 0.75F, 1.0F};
    if (!decoder.open_pcm(pcm)) return false;
    decoded_block_cache_t cache;
    decode_session_t session{0};
    const auto token = session.start();
    if (!token) return false;
    const auto block_key = key("fixture:pcm", "generated:v1", 0);
    const auto decoded = decode_cached_block(decoder, cache, session, *token, block_key);
    if (decoded.is_success()) return false;
    return decoded.status == wormedit::failure_code_t::capacity_exceeded &&
           decoded.frames == 2 && decoded.decoder_status == read_phase_t::success &&
           cache.has_block(block_key) && session.pending_count() == 0;
}
} // namespace

TEST_CASE("block decode publication errors retain frames and cached PCM",
          "[unit][audio][cache][result][publication]") {
    CHECK(expected_block_failure_keeps_decoded_frames_and_cached_pcm());
}
