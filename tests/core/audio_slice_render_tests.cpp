#include <wormedit/audio/slice_render.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <source_location>
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
    const auto result = rendered_audio_slices_t::from(
        source, ranges,
        {.directory = scratch.path, .base_name = "part",
         .progress = [&](std::size_t done, std::size_t) { last_done = done; }});
    REQUIRE(result);
    REQUIRE(result->files.size() == 2);
    CHECK(result->files[0].filename() == "part_001.wav");
    CHECK(last_done == 2);

    decoder_t first;
    REQUIRE(first.open(result->files[0]));
    CHECK(first.info()->output_frame_count == total / 3);
    decoder_t second;
    REQUIRE(second.open(result->files[1]));
    CHECK(second.info()->output_frame_count == total - total / 3);

    std::vector<float> expected(64 * 2), actual(64 * 2);
    REQUIRE(source.read_frames_at(total / 3 + 100, expected, 64).value().frames_read == 64);
    REQUIRE(second.read_frames_at(100, actual, 64).value().frames_read == 64);
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
    auto cancelled_outcome = rendered_audio_slices_t::from(
        source, ranges,
        {.directory = scratch.path, .base_name = "x", .stop = stop.get_token(),
         .progress = [&](std::size_t done, std::size_t) {
             published = done;
             if (done == 2) stop.request_stop();
         }});
    CHECK(cancelled_outcome == std::unexpected(wormedit::error_t<failure_code_t>{
        failure_code_t::cancelled, "cancelled"}));
    CHECK(published == 2);
    CHECK_FALSE(cancelled_outcome.has_value());
    CHECK(file_count(scratch.path) == 0);

    const auto rejects = [&](std::vector<audio_slice_range_t> bad) {
        const auto result = rendered_audio_slices_t::from(source, bad,
                                                {.directory = scratch.path, .base_name = "bad"});
        CHECK(result.error().code == failure_code_t::invalid_request);
        CHECK_FALSE(result.error().message.empty());
        CHECK(file_count(scratch.path) == 0);
    };
    rejects({});
    rejects({{10, 10}});
    rejects({{0, total + 1}});
    rejects({{0, 100}, {50, 200}});
    const auto no_name = rendered_audio_slices_t::from(source, std::vector<audio_slice_range_t>{{0, 100}}, {});
    CHECK(no_name.error().code == failure_code_t::invalid_request);
}

namespace {

auto read_u16(const std::string& bytes, std::size_t offset) -> std::uint16_t {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset])) |
           static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset + 1]) << 8U);
}

auto read_u32(const std::string& bytes, std::size_t offset) -> std::uint32_t {
    std::uint32_t value{};
    for (std::size_t byte = 0; byte < 4; ++byte)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + byte]))
                 << (byte * 8U);
    return value;
}

auto write_bytes(const std::filesystem::path& path, const std::string& bytes) -> bool {
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}

auto read_bytes(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

auto stereo_tone(std::size_t frames, std::uint32_t sample_rate) -> std::vector<float> {
    std::vector<float> samples(frames * output_channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto phase = 2.0 * std::numbers::pi * 997.0 *
                           static_cast<double>(frame) / sample_rate;
        const auto value = static_cast<float>(1.2 * std::sin(phase));
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = -value * 0.65F;
    }
    return samples;
}

auto max_abs(std::span<const float> samples) -> float {
    float maximum{};
    for (const auto sample : samples) maximum = std::max(maximum, std::abs(sample));
    return maximum;
}

auto samples_close(std::span<const float> lhs, std::span<const float> rhs,
                   float tolerance) -> bool {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index)
        if (std::abs(lhs[index] - rhs[index]) > tolerance) return false;
    return true;
}

auto decoded_samples(decoder_t& decoder) -> std::optional<std::vector<float>> {
    const auto info = decoder.info();
    if (!info || info->output_frame_count == 0 ||
        info->output_frame_count > maximum_read_frames) return std::nullopt;
    std::vector<float> samples(static_cast<std::size_t>(info->output_frame_count) * 2);
    const auto read = decoder.read_frames_at(0, samples, static_cast<std::size_t>(info->output_frame_count));
    if (!read || read->frames_read != info->output_frame_count)
        return std::nullopt;
    return samples;
}

void check_float_pcm(std::span<const float> expected, std::span<const float> actual) {
    REQUIRE(expected.size() == actual.size());
    float maximum{};
    std::size_t first = expected.size(), last{}, different{};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto error = std::abs(expected[index] - actual[index]);
        maximum = std::max(maximum, error);
        if (error <= 1.0e-6F) continue;
        first = std::min(first, index); last = index; ++different;
    }
    INFO("maximum " << maximum << " differing " << different << " first " << first << " last " << last);
    CHECK(maximum <= 1.0e-6F);
}

void check_slice_float(bool condition, std::source_location location = std::source_location::current()) {
    INFO("check at " << location.line());
    CHECK(condition);
}
void require_slice_float(bool condition) { REQUIRE(condition); }

} // namespace

TEST_CASE("float32 WAV encoder preserves finite over-unity stereo samples and fact metadata",
          "[unit][audio][slicer]") {
    const std::vector<float> samples{0.25F, -0.5F, 1.2F, -1.2F, 0.0F, 0.875F};
    const auto bytes = encode_wav_float32(samples, output_sample_rate);
    require_slice_float(bytes.size() == 56 + samples.size() * sizeof(float));
    check_slice_float(bytes.substr(0, 4) == "RIFF");
    check_slice_float(read_u16(bytes, 20) == 3);
    check_slice_float(read_u16(bytes, 22) == 2);
    check_slice_float(read_u32(bytes, 24) == output_sample_rate);
    check_slice_float(bytes.substr(36, 4) == "fact");
    check_slice_float(read_u32(bytes, 44) == samples.size() / 2);
    check_slice_float(bytes.substr(48, 4) == "data");

    scratch_directory_t scratch{"float-codec"};
    require_slice_float(std::filesystem::create_directories(scratch.path));
    const auto path = scratch.path / "roundtrip.wav";
    require_slice_float(write_bytes(path, bytes));
    decoder_t decoded;
    require_slice_float(decoded.open(path));
    const auto actual = decoded_samples(decoded);
    require_slice_float(actual.has_value());
    check_slice_float(samples_close(samples, *actual, 1.0e-6F));
}

TEST_CASE("float32 slice export preserves a 48 kHz source after normalized decoding",
          "[unit][audio][slicer]") {
    constexpr std::uint32_t native_rate = 48'000;
    const auto native = stereo_tone(4'800, native_rate);
    const auto native_bytes = encode_wav_float32(native, native_rate);
    require_slice_float(!native_bytes.empty());
    scratch_directory_t scratch{"float-resample"};
    require_slice_float(std::filesystem::create_directories(scratch.path));
    const auto source_path = scratch.path / "native.wav";
    require_slice_float(write_bytes(source_path, native_bytes));

    decoder_t source;
    require_slice_float(source.open(source_path));
    const auto info = source.info();
    require_slice_float(info.has_value());
    check_slice_float(info->source_sample_rate == native_rate);
    check_slice_float(info->output_sample_rate == output_sample_rate);
    decoder_t reference;
    require_slice_float(reference.open(source_path));
    const auto expected = decoded_samples(reference);
    require_slice_float(expected.has_value());
    check_slice_float(max_abs(*expected) > 1.05F);

    const std::vector<audio_slice_range_t> range{{0, info->output_frame_count}};
    scratch_directory_t float_output{"float-render"};
    const auto rendered = rendered_audio_slices_t::from(source, range,
        {.directory = float_output.path, .base_name = "slice",
         .encoding = slice_wav_encoding_t::float32});
    require_slice_float(static_cast<bool>(rendered));
    require_slice_float(rendered->files.size() == 1);
    decoder_t slice;
    require_slice_float(slice.open(rendered->files.front()));
    const auto actual = decoded_samples(slice);
    require_slice_float(actual.has_value());
    check_float_pcm(*expected, *actual);
    check_slice_float(max_abs(*actual) > 1.05F);
}

TEST_CASE("slice float encoding rejects invalid payloads and PCM16 remains the default",
          "[unit][audio][slicer]") {
    const std::vector<float> empty;
    const std::vector<float> odd{0.0F};
    const std::vector<float> nonfinite{0.0F, std::numeric_limits<float>::infinity()};
    check_slice_float(encode_wav_float32(empty, output_sample_rate).empty());
    check_slice_float(encode_wav_float32(odd, output_sample_rate).empty());
    check_slice_float(encode_wav_float32(nonfinite, output_sample_rate).empty());
    check_slice_float(encode_wav_float32(std::vector<float>{0.0F, 0.0F}, 0).empty());
    check_slice_float(encode_wav_float32(std::vector<float>{0.0F, 0.0F},
        std::numeric_limits<std::uint32_t>::max()).empty());
    check_slice_float(slice_render_options_t{}.encoding == slice_wav_encoding_t::pcm16);

    decoder_t source;
    require_slice_float(source.open(fixture("keysound-c4.ogg")));
    const std::vector<audio_slice_range_t> range{{0, 64}};
    scratch_directory_t scratch{"bad-encoding"};
    const auto invalid = rendered_audio_slices_t::from(source, range,
        {.directory = scratch.path, .base_name = "invalid",
         .encoding = static_cast<slice_wav_encoding_t>(255)});
    check_slice_float(invalid.error().code == failure_code_t::invalid_request);
    check_slice_float(!invalid.error().message.empty());
    check_slice_float(file_count(scratch.path) == 0);

    scratch_directory_t default_output{"default-pcm"};
    const auto pcm = rendered_audio_slices_t::from(source, range,
        {.directory = default_output.path, .base_name = "pcm"});
    require_slice_float(static_cast<bool>(pcm));
    const auto header = read_bytes(pcm->files.front());
    check_slice_float(read_u16(header, 20) == 1);
}
