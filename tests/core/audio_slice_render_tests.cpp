#include <wormedit/audio/slice_render.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace wormedit;
using namespace wormedit::audio;

namespace {

auto fixture(const char* name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / name;
}

struct scratch_directory_t {
    std::filesystem::path path;
    explicit scratch_directory_t(const char* tag) {
        static std::atomic<int> counter{0};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-slice-" + std::string(tag) + "-" + std::to_string(counter++) + "-" +
                std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::remove_all(path);
    }
    ~scratch_directory_t() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

auto file_count(const std::filesystem::path& directory) -> std::size_t {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) return 0;
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(directory),
                                                  std::filesystem::directory_iterator{}));
}

} // namespace

TEST_CASE("WAV encoding writes a canonical 16-bit stereo header and clips", "[unit][audio][slicer]") {
    const std::vector<float> samples{0.0F, 1.0F, -1.0F, 2.5F};
    const auto bytes = encode_wav_pcm16(samples, 44'100);
    REQUIRE(bytes.size() == 44 + 8);
    CHECK(bytes.substr(0, 4) == "RIFF");
    CHECK(bytes.substr(8, 8) == "WAVEfmt ");
    CHECK(static_cast<std::uint8_t>(bytes[22]) == 2);
    CHECK(static_cast<std::uint8_t>(bytes[24]) == 0x44);
    CHECK(static_cast<std::uint8_t>(bytes[25]) == 0xAC);
    CHECK(bytes.substr(36, 4) == "data");
    CHECK(static_cast<std::uint8_t>(bytes[40]) == 8);
    // 1.0 -> 32767 (0x7FFF), -1.0 -> -32767 (0x8001), 2.5 clips to 32767.
    CHECK(static_cast<std::uint8_t>(bytes[46]) == 0xFF);
    CHECK(static_cast<std::uint8_t>(bytes[47]) == 0x7F);
    CHECK(static_cast<std::uint8_t>(bytes[48]) == 0x01);
    CHECK(static_cast<std::uint8_t>(bytes[49]) == 0x80);
    CHECK(static_cast<std::uint8_t>(bytes[51]) == 0x7F);
}

TEST_CASE("slices render to WAV files that decode to the same frames", "[unit][audio][slicer]") {
    decoder_t source;
    REQUIRE(source.open(fixture("keysound-c4.ogg")));
    const auto total = source.info()->output_frame_count;
    REQUIRE(total > 1'000);
    const std::vector<audio_slice_range_t> ranges{{0, total / 3}, {total / 3, total}};

    scratch_directory_t scratch{"ok"};
    std::size_t last_done = 0;
    const auto result = render_audio_slices(
        source, ranges,
        {.directory = scratch.path, .base_name = "part",
         .progress = [&](std::size_t done, std::size_t) { last_done = done; }});
    REQUIRE(result);
    REQUIRE(result.files.size() == 2);
    CHECK(result.files[0].filename() == "part_001.wav");
    CHECK(last_done == 2);

    decoder_t first;
    REQUIRE(first.open(result.files[0]));
    CHECK(first.info()->output_frame_count == total / 3);
    decoder_t second;
    REQUIRE(second.open(result.files[1]));
    CHECK(second.info()->output_frame_count == total - total / 3);

    std::vector<float> expected(64 * 2), actual(64 * 2);
    REQUIRE(source.read_at(total / 3 + 100, expected, 64).frames_read == 64);
    REQUIRE(second.read_at(100, actual, 64).frames_read == 64);
    for (std::size_t index = 0; index < expected.size(); ++index)
        CHECK(std::abs(expected[index] - actual[index]) < 1.0F / 16'000.0F);
}

TEST_CASE("cancelling or invalid requests leave no files behind", "[unit][audio][slicer]") {
    decoder_t source;
    REQUIRE(source.open(fixture("keysound-c4.ogg")));
    const auto total = source.info()->output_frame_count;
    scratch_directory_t scratch{"cancel"};

    std::stop_source stop;
    std::size_t published = 0;
    const std::vector<audio_slice_range_t> ranges{{0, 100}, {100, 200}, {200, 300}};
    const auto cancelled = render_audio_slices(
        source, ranges,
        {.directory = scratch.path, .base_name = "x", .stop = stop.get_token(),
         .progress = [&](std::size_t done, std::size_t) {
             published = done;
             if (done == 2) stop.request_stop();
         }});
    CHECK(cancelled.status == slice_render_status_t::cancelled);
    CHECK(published == 2);
    CHECK(cancelled.files.empty());
    CHECK(file_count(scratch.path) == 0);

    const auto rejects = [&](std::vector<audio_slice_range_t> bad) {
        const auto result = render_audio_slices(source, bad,
                                                {.directory = scratch.path, .base_name = "bad"});
        CHECK(result.status == slice_render_status_t::invalid_request);
        CHECK_FALSE(result.error.empty());
        CHECK(file_count(scratch.path) == 0);
    };
    rejects({});
    rejects({{10, 10}});
    rejects({{0, total + 1}});
    rejects({{0, 100}, {50, 200}});
    const auto no_name = render_audio_slices(source, std::vector<audio_slice_range_t>{{0, 100}}, {});
    CHECK(no_name.status == slice_render_status_t::invalid_request);
}
