#include <wormedit/audio/resident_samples.h>
#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/transport.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace wormedit::audio;

constexpr std::size_t sample_frames = 1'000;
constexpr std::size_t sample_bytes = sample_frames * output_channel_count * sizeof(float);

auto small_options() -> resident_sample_options_t {
    return {.maximum_frames_per_sample = 4'000,
            .budget_bytes = 64U * sample_bytes,
            .stale_threshold = 1.0 / 3.0,
            .chunk_bytes = 4U * sample_bytes};
}

auto make_pcm(std::size_t frames, float seed) -> std::vector<float> {
    std::vector<float> pcm(frames * output_channel_count);
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        pcm[index] = seed + static_cast<float>(index) * 0.0001F;
    }
    return pcm;
}

auto request(std::string name, std::size_t frames = sample_frames) -> resident_request_t {
    return {.source = {"asset:" + name, "binding:1:" + name},
            .content_key = "content:" + name,
            .frame_count = frames};
}

auto load_pcm(resident_sample_store_t& store, const resident_request_t& wanted, float seed)
    -> resident_load_status_t {
    const auto pcm = make_pcm(static_cast<std::size_t>(wanted.frame_count), seed);
    decoder_t decoder;
    REQUIRE(decoder.open_pcm(pcm));
    return store.load(wanted, decoder);
}

auto block_equals(const resident_snapshot_t& snapshot, const resident_request_t& wanted,
                  float seed) -> bool {
    const auto pcm = make_pcm(static_cast<std::size_t>(wanted.frame_count), seed);
    const auto block = snapshot.block({wanted.source, 0});
    return block.size() == std::min(pcm.size(), maximum_read_frames * output_channel_count) &&
           std::equal(block.begin(), block.end(), pcm.begin());
}

} // namespace

TEST_CASE("resident samples decode once into contiguous arena spans in load order",
          "[unit][audio][resident]") {
    resident_sample_store_t store{small_options()};
    const std::array wanted{request("a"), request("b"), request("c")};
    CHECK(store.reconcile(wanted).size() == 3);
    for (std::size_t index = 0; index < wanted.size(); ++index) {
        CHECK(load_pcm(store, wanted[index], static_cast<float>(index)) ==
              resident_load_status_t::loaded);
    }
    CHECK(load_pcm(store, wanted[0], 0.0F) == resident_load_status_t::already_resident);

    const auto snapshot = store.snapshot();
    REQUIRE(snapshot->size() == 3);
    const auto* first = snapshot->find(wanted[0].source);
    const auto* second = snapshot->find(wanted[1].source);
    const auto* third = snapshot->find(wanted[2].source);
    REQUIRE((first && second && third));
    CHECK(second->data == first->data + sample_frames * output_channel_count);
    CHECK(third->data == second->data + sample_frames * output_channel_count);
    CHECK(store.stats().decode_count == 3);
    for (std::size_t index = 0; index < wanted.size(); ++index) {
        CHECK(block_equals(*snapshot, wanted[index], static_cast<float>(index)));
    }
}

TEST_CASE("resident blocks are served without decoding or touching the block cache",
          "[unit][audio][resident]") {
    resident_sample_store_t store{small_options()};
    const auto wanted = request("hit");
    REQUIRE(store.reconcile(std::array{wanted}).size() == 1);
    REQUIRE(load_pcm(store, wanted, 0.5F) == resident_load_status_t::loaded);

    decoded_block_cache_t cache;
    transport_t transport{cache};
    transport.set_resident_snapshot(store.snapshot());
    const auto token = transport.start();
    REQUIRE(token.has_value());
    decoded_source_registry_t registry;
    for (int index = 0; index < 100; ++index) {
        const auto hit = registry.decode_block(transport, *token, {wanted.source, 0});
        REQUIRE(hit.status == block_decode_status_t::cache_hit);
        CHECK(hit.frames == sample_frames);
    }
    CHECK(store.stats().decode_count == 1);
    CHECK(cache.stats().entries == 0);
    CHECK(registry.open_decoder_count() == 0);

    // Not resident: falls through to the registry, which does not know the source.
    const auto unknown = registry.decode_block(
        transport, *token, {.source = {"asset:other", "binding:1:other"}, .block_index = 0});
    CHECK(unknown.status == block_decode_status_t::invalid_key);
}

TEST_CASE("the mixer reads resident samples and prefers them over the block cache",
          "[unit][audio][resident]") {
    resident_sample_store_t store{small_options()};
    const auto wanted = request("mix");
    REQUIRE(store.reconcile(std::array{wanted}).size() == 1);
    REQUIRE(load_pcm(store, wanted, 0.25F) == resident_load_status_t::loaded);

    decoded_block_cache_t cache;
    transport_t transport{cache};
    transport.set_resident_snapshot(store.snapshot());
    const auto token = transport.start();
    REQUIRE(token.has_value());
    REQUIRE(transport.schedule_voice(*token, {.id = 1,
                                              .source = wanted.source,
                                              .frame_count = sample_frames,
                                              .gain = 0.5F}) ==
            voice_schedule_status_t::scheduled);
    std::vector<float> output(sample_frames * output_channel_count);
    const auto mixed = transport.mix(*token, 0, output);
    CHECK(mixed.missing_blocks == 0);
    const auto pcm = make_pcm(sample_frames, 0.25F);
    for (std::size_t index = 0; index < output.size(); ++index) {
        REQUIRE(output[index] == std::clamp(pcm[index] * 0.5F, -1.0F, 1.0F));
    }
}

TEST_CASE("relinking marks the old content stale and rebinding keeps unchanged content",
          "[unit][audio][resident]") {
    resident_sample_store_t store{small_options()};
    auto first = request("relink");
    auto other = request("other");
    REQUIRE(store.reconcile(std::array{first, other}).size() == 2);
    REQUIRE(load_pcm(store, first, 1.0F) == resident_load_status_t::loaded);
    REQUIRE(load_pcm(store, other, 2.0F) == resident_load_status_t::loaded);
    const auto live_before = store.stats().live_bytes;

    // New binding generation for both; only `relink` changed on disk.
    auto rebound_other = other;
    rebound_other.source.source_identity = "binding:2:other";
    auto relinked = first;
    relinked.source.source_identity = "binding:2:relink";
    relinked.content_key = "content:relink:v2";
    const auto missing = store.reconcile(std::array{relinked, rebound_other});
    REQUIRE(missing.size() == 1);
    CHECK(missing.front() == 0);
    CHECK(store.stats().decode_count == 2);
    CHECK(store.stats().stale_bytes == sample_bytes);
    CHECK(block_equals(*store.snapshot(), rebound_other, 2.0F));
    CHECK(store.snapshot()->find(relinked.source) == nullptr);

    REQUIRE(load_pcm(store, relinked, 3.0F) == resident_load_status_t::loaded);
    CHECK(block_equals(*store.snapshot(), relinked, 3.0F));
    CHECK(store.stats().live_bytes == live_before);
}

TEST_CASE("compaction triggers exactly at the stale threshold and preserves live samples",
          "[unit][audio][resident]") {
    resident_sample_store_t store{small_options()};
    const std::array all{request("a"), request("b"), request("c"), request("d")};
    REQUIRE(store.reconcile(all).size() == 4);
    for (std::size_t index = 0; index < all.size(); ++index) {
        REQUIRE(load_pcm(store, all[index], static_cast<float>(index)) ==
                resident_load_status_t::loaded);
    }
    // One of four stale: 25 percent is below the one-third threshold.
    static_cast<void>(store.reconcile(std::span{all}.subspan(1)));
    CHECK_FALSE(store.compact_if_needed());
    CHECK(store.stats().compactions == 0);

    // Two of four stale: 50 percent reaches it. Hold the old snapshot and chunk.
    const auto old_snapshot = store.snapshot();
    std::weak_ptr<const std::byte[]> old_chunk = old_snapshot->find(all[2].source)->chunk;
    static_cast<void>(store.reconcile(std::span{all}.subspan(2)));
    REQUIRE(store.compact_if_needed());
    CHECK(store.stats().compactions == 1);
    CHECK(store.stats().stale_bytes == 0);
    CHECK(block_equals(*store.snapshot(), all[2], 2.0F));
    CHECK(block_equals(*store.snapshot(), all[3], 3.0F));
    // The old chunk lives until its last snapshot holder lets go.
    CHECK_FALSE(old_chunk.expired());
}

TEST_CASE("compaction at exactly one third stale", "[unit][audio][resident]") {
    resident_sample_store_t store{small_options()};
    const std::array all{request("a"), request("b"), request("c")};
    REQUIRE(store.reconcile(all).size() == 3);
    for (std::size_t index = 0; index < all.size(); ++index) {
        REQUIRE(load_pcm(store, all[index], static_cast<float>(index)) ==
                resident_load_status_t::loaded);
    }
    std::weak_ptr<const std::byte[]> old_chunk;
    {
        const auto old_snapshot = store.snapshot();
        old_chunk = old_snapshot->find(all[1].source)->chunk;
        static_cast<void>(store.reconcile(std::span{all}.subspan(1)));
        REQUIRE(store.compact_if_needed());
        CHECK_FALSE(old_chunk.expired());
    }
    // A listener-less store keeps only `current_`, which no longer references the old chunk.
    CHECK(old_chunk.expired());
}

TEST_CASE("samples beyond the budget or the length limit stay on the streaming path",
          "[unit][audio][resident]") {
    auto options = small_options();
    options.budget_bytes = 2U * sample_bytes;
    resident_sample_store_t store{options};
    const std::array wanted{request("a"), request("b"), request("c"), request("long", 5'000)};
    REQUIRE(store.reconcile(wanted).size() == 4);
    CHECK(load_pcm(store, wanted[0], 0.0F) == resident_load_status_t::loaded);
    CHECK(load_pcm(store, wanted[1], 1.0F) == resident_load_status_t::loaded);
    CHECK(load_pcm(store, wanted[2], 2.0F) == resident_load_status_t::over_budget);
    CHECK(load_pcm(store, wanted[3], 3.0F) == resident_load_status_t::too_long);
    CHECK(store.stats().over_budget_count == 1);
    CHECK(store.snapshot()->size() == 2);
    // Resident samples are never evicted to make room.
    CHECK(block_equals(*store.snapshot(), wanted[0], 0.0F));
}

TEST_CASE("a failed decode is reported and leaves nothing resident", "[unit][audio][resident]") {
    resident_sample_store_t store{small_options()};
    const auto wanted = request("short", sample_frames);
    REQUIRE(store.reconcile(std::array{wanted}).size() == 1);
    const auto pcm = make_pcm(sample_frames / 2, 0.0F);
    decoder_t decoder;
    REQUIRE(decoder.open_pcm(pcm));
    CHECK(store.load(wanted, decoder) == resident_load_status_t::decode_failed);
    CHECK(store.snapshot()->size() == 0);
    CHECK(store.stats().failed_count == 1);
}

TEST_CASE("readers see consistent samples while the store publishes and compacts",
          "[unit][audio][resident][threads]") {
    resident_sample_store_t store{small_options()};
    std::atomic<bool> done{false};
    std::atomic<std::size_t> mismatches{0};
    const auto stable = request("stable");
    REQUIRE(store.reconcile(std::array{stable}).size() == 1);
    REQUIRE(load_pcm(store, stable, 7.0F) == resident_load_status_t::loaded);

    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index) {
        readers.emplace_back([&] {
            while (!done.load()) {
                const auto snapshot = store.snapshot();
                if (snapshot->find(stable.source) != nullptr &&
                    !block_equals(*snapshot, stable, 7.0F)) {
                    ++mismatches;
                }
            }
        });
    }
    for (int round = 0; round < 40; ++round) {
        const auto churn = request("churn" + std::to_string(round));
        static_cast<void>(store.reconcile(std::array{stable, churn}));
        static_cast<void>(load_pcm(store, churn, static_cast<float>(round)));
        static_cast<void>(store.reconcile(std::array{stable}));
        static_cast<void>(store.compact_if_needed());
    }
    done = true;
    for (auto& reader : readers) reader.join();
    CHECK(mismatches.load() == 0);
    CHECK(store.stats().compactions > 0);
}

namespace {
void check_resident_growth_preserves_snapshot(resident_sample_store_t& store,
                                             const resident_snapshot_ptr_t& held,
                                             const resident_request_t& stable) {
    CHECK(store.snapshot()->find(stable.source)->data == held->find(stable.source)->data);
    CHECK(block_equals(*held, stable, 7.0F));
    CHECK_FALSE(store.set_budget_bytes(5 * sample_bytes));
}

void check_resident_lowering_preserves_snapshot(resident_sample_store_t& store,
                                               const resident_snapshot_ptr_t& held,
                                               const std::array<resident_request_t, 2>& wanted) {
    CHECK(load_pcm(store, wanted[1], 3.0F) == resident_load_status_t::over_budget);
    CHECK(store.snapshot() == held);
    CHECK(block_equals(*held, wanted[0], 7.0F));
}
} // namespace

TEST_CASE("raising resident budget retries a sample beyond the original arena ceiling",
          "[unit][audio][resident][budget]") {
    auto options = small_options();
    options.budget_bytes = sample_bytes;
    options.chunk_bytes = sample_bytes;
    resident_sample_store_t store{options};
    const std::array wanted{request("stable"), request("large", 4 * sample_frames)};
    REQUIRE(store.reconcile(wanted).size() == 2);
    REQUIRE(load_pcm(store, wanted[0], 7.0F) == resident_load_status_t::loaded);
    const auto held = store.snapshot();
    CHECK(load_pcm(store, wanted[1], 3.0F) == resident_load_status_t::over_budget);
    CHECK(store.set_budget_bytes(5 * sample_bytes));
    CHECK(load_pcm(store, wanted[1], 3.0F) == resident_load_status_t::loaded);
    check_resident_growth_preserves_snapshot(store, held, wanted[0]);
}

TEST_CASE("lowering resident budget keeps snapshots and blocks future loads",
          "[unit][audio][resident][budget]") {
    resident_sample_store_t store{small_options()};
    const std::array wanted{request("stable"), request("later")};
    REQUIRE(store.reconcile(wanted).size() == 2);
    REQUIRE(load_pcm(store, wanted[0], 7.0F) == resident_load_status_t::loaded);
    const auto held = store.snapshot();
    const auto original_options = store.options();
    CHECK(store.set_budget_bytes(0));
    CHECK(store.options().budget_bytes == 0);
    CHECK(original_options.budget_bytes == small_options().budget_bytes);
    check_resident_lowering_preserves_snapshot(store, held, wanted);
}
