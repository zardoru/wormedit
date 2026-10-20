#include <wormedit/audio/ogg_encoder.h>
#include <wormedit/core/atomic_file_storage.h>
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <limits>
#include <vector>

namespace {
auto sine_pcm(std::size_t frames) -> std::vector<float> {
    std::vector<float> pcm(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        pcm[frame * 2] = static_cast<float>(0.2 * std::sin(2 * std::numbers::pi * 440 * static_cast<double>(frame) / 44100));
        pcm[frame * 2 + 1] = -pcm[frame * 2];
    }
    return pcm;
}
auto deterministic_encoding(std::span<const float> pcm) -> std::string {
    const auto encoded = wormedit::audio::encode_ogg_vorbis(pcm);
    REQUIRE(encoded);
    CHECK(encoded->starts_with("OggS"));
    const auto again = wormedit::audio::encode_ogg_vorbis(pcm);
    REQUIRE(again);
    CHECK(*encoded == *again);
    return *encoded;
}
}

TEST_CASE("Vorbis encodes a deterministic twenty second stereo preview and decodes it back", "[unit][audio][ogg-encoder]") {
    constexpr std::size_t frames = 44100 * 20;
    const auto encoded = deterministic_encoding(sine_pcm(frames));
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    auto prepared = wormedit::prepared_atomic_file_t::from(directory->path() / "preview.ogg", encoded, encoded.size(),
        [&](const std::filesystem::path& path) -> wormedit::atomic_file_save_outcome_t {
            const auto verified = wormedit::audio::verify_ogg_vorbis(path, frames);
            if (!verified) return std::unexpected(verified.error().message);
            return {};
        });
    INFO((prepared ? "verified" : prepared.error()));
    REQUIRE(prepared);
    REQUIRE(prepared->publish(false));
}

TEST_CASE("Vorbis rejects invalid PCM output limits and pre-cancelled encoding", "[unit][audio][ogg-encoder]") {
    std::vector<float> pcm(8820, 0.1F);
    wormedit::audio::ogg_encode_options_t options;
    std::stop_source stop;
    SECTION("odd PCM") { pcm.pop_back(); }
    SECTION("quality") { options.quality = 2; }
    SECTION("nonfinite") { pcm[0] = std::numeric_limits<float>::infinity(); }
    SECTION("output cap") { options.maximum_output_bytes = 32; }
    SECTION("cancel") { stop.request_stop(); }
    CHECK_FALSE(wormedit::audio::encode_ogg_vorbis(pcm, options, stop.get_token()));
}
