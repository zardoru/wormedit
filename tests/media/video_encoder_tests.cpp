#include <wormedit/media/video_encoder.h>
#include <wormedit/core/atomic_file_storage.h>
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
}

namespace {
using namespace wormedit;
using namespace wormedit::media;

class frame_fixture_t {
    video_encoder_options_t options_;
    storyboard_raster_t raster_;
    frame_arena_t arena_;
    std::array<const std::uint8_t*, 2> slots_{};
    std::uint64_t next_{};
    std::array<storyboard_raster_image_t, 1> images_{{{.path = "red", .image =
        std::make_shared<const visual::rgba_image_t>(visual::rgba_image_t{
            .width = 1, .height = 1, .pixels = {255, 0, 0, 255}})}}};
    std::array<storyboard_draw_instance_t, 1> red_{{{.stable_id = 1, .layer = "Foreground", .origin = "TopLeft", .path = "red",
        .transform = {.values = {640, 0, 0, 480, 0, 0}}}}};
    frame_fixture_t(video_encoder_options_t options, storyboard_raster_t raster)
        : options_(std::move(options)), raster_(std::move(raster)) {}
  public:
    frame_fixture_t(frame_fixture_t&&) noexcept = default;
    static auto from(video_encoder_options_t options) -> result_t<frame_fixture_t, wormedit::error_t<failure_code_t>> {
        auto raster = storyboard_raster_t::from({.width = options.width, .height = options.height, .threads = 2, .frame_slots = 2});
        if (!raster) return std::unexpected(raster.error());
        return frame_fixture_t{std::move(options), std::move(*raster)};
    }
    auto source() -> video_frame_source_t {
        return [this](std::uint64_t index, storyboard_time_t time, storyboard_conditional_layer_t choice,
            std::stop_token stop) -> result_t<storyboard_raster_frame_t, wormedit::error_t<failure_code_t>> {
            if (stop.stop_requested() || index != next_ || options_.scene_time_at(index) != time || choice != options_.conditional_layer)
                return std::unexpected(wormedit::error_t{failure_code_t::invalid_data, std::string{"Frame clock, scenario or back-pressure order differed"}});
            arena_.reset();
            const auto draw = index == 0 ? std::span<const storyboard_draw_instance_t>{red_} : std::span<const storyboard_draw_instance_t>{};
            auto frame = raster_.render(draw, images_, arena_, choice);
            if (!frame) return frame;
            auto& slot = slots_[index % slots_.size()];
            if (slot && slot != frame->pixels.data()) return std::unexpected(wormedit::error_t{
                failure_code_t::invalid_data, std::string{"Frame ring reallocated"}});
            slot = frame->pixels.data();
            ++next_;
            return frame;
        };
    }
    auto frames() const -> std::uint64_t { return next_; }
};

class audio_fixture_t {
    audio::decoded_source_registry_t registry_;
    preview_schedule_t schedule_{.sample_rate = audio::output_sample_rate};
    std::array<audio::preview_object_source_binding_t, 1> bindings_;
    const video_encoder_options_t& options_;
  public:
    explicit audio_fixture_t(const video_encoder_options_t& options) : options_(options) {}
    auto initialize() -> bool {
        const auto count = options_.begin_audio_frame + *options_.input_audio_frames() + audio::output_sample_rate;
        std::vector<float> pcm(static_cast<std::size_t>(count) * audio::output_channel_count);
        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
            const auto value = static_cast<float>(0.25 * std::sin(2 * std::numbers::pi * 440 * static_cast<double>(i) / audio::output_sample_rate));
            pcm[i * 2] = value;
            pcm[i * 2 + 1] = value;
        }
        const auto source = registry_.create_generated_source({.source = {"video-test", "tone440"}, .interleaved_stereo = std::move(pcm)});
        if (!source) return false;
        schedule_.music = preview_schedule_t::music_event_t{.resource = resource_id_t{1},
            .sample_frame = audio::output_sample_rate / 4, .frame_count = source->output_frame_count};
        bindings_[0] = {.object = audio::preview_music_object,
            .voices = {{.source = source->source, .frame_count = source->output_frame_count}}};
        return true;
    }
    auto source() -> video_audio_source_t {
        return [this](const audio::offline_mix_sink_t& sink, std::stop_token stop) {
            return audio::render_offline_interval_to(registry_, schedule_, bindings_, options_.begin_audio_frame,
                options_.begin_audio_frame + *options_.input_audio_frames(), sink, {}, stop);
        };
    }
};

struct decoded_picture_t {
    AVCodecContext* context{};
    AVFrame* frame{};
    ~decoded_picture_t() { av_frame_free(&frame); avcodec_free_context(&context); }
    auto open(const AVStream& stream) -> bool {
        const auto* decoder = avcodec_find_decoder(stream.codecpar->codec_id);
        if (!decoder) return false;
        context = avcodec_alloc_context3(decoder);
        frame = av_frame_alloc();
        if (!context || !frame) return false;
        context->thread_count = 1;
        return avcodec_parameters_to_context(context, stream.codecpar) >= 0 && avcodec_open2(context, decoder, nullptr) >= 0;
    }
};

class file_probe_t {
    AVFormatContext* format_{};
    AVPacket* packet_{};
    int video_{-1}, audio_{-1};
    std::uint64_t video_packets_{};
    int first_luma_{-1};
    float audio_peak_{};
    decoded_picture_t video_decoder_, audio_decoder_;
    auto read_audio() -> bool {
        if (avcodec_send_packet(audio_decoder_.context, packet_) < 0) return false;
        for (;;) {
            const auto status = avcodec_receive_frame(audio_decoder_.context, audio_decoder_.frame);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return true;
            if (status < 0) return false;
            const auto* frame = audio_decoder_.frame;
            const auto format = static_cast<AVSampleFormat>(frame->format);
            if (av_get_packed_sample_fmt(format) != AV_SAMPLE_FMT_FLT) return false;
            const auto channels = av_sample_fmt_is_planar(format) ? 1 : frame->ch_layout.nb_channels;
            const auto* samples = reinterpret_cast<const float*>(frame->data[0]);
            for (int i = 0; i < frame->nb_samples; ++i) audio_peak_ = std::max(audio_peak_, std::abs(samples[i * channels]));
            av_frame_unref(audio_decoder_.frame);
        }
    }
    auto read_video() -> bool {
        ++video_packets_;
        if (first_luma_ >= 0) return true;
        if (avcodec_send_packet(video_decoder_.context, packet_) < 0) return false;
        const auto status = avcodec_receive_frame(video_decoder_.context, video_decoder_.frame);
        if (status == AVERROR(EAGAIN)) return true;
        if (status < 0) return false;
        const auto* frame = video_decoder_.frame;
        first_luma_ = frame->data[0][frame->height / 2 * frame->linesize[0] + frame->width / 2];
        av_frame_unref(video_decoder_.frame);
        return true;
    }
  public:
    ~file_probe_t() { av_packet_free(&packet_); avformat_close_input(&format_); }
    auto open(const std::filesystem::path& path) -> bool {
        const auto filename = path.generic_string();
        if (avformat_open_input(&format_, filename.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(format_, nullptr) < 0) return false;
        video_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        audio_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        packet_ = av_packet_alloc();
        return video_ >= 0 && audio_ >= 0 && packet_ && video_decoder_.open(*format_->streams[video_]) && audio_decoder_.open(*format_->streams[audio_]);
    }
    auto inspect() -> bool {
        int status{};
        while ((status = av_read_frame(format_, packet_)) >= 0) {
            if (packet_->stream_index == video_ && !read_video()) return false;
            if (packet_->stream_index == audio_ && !read_audio()) return false;
            av_packet_unref(packet_);
        }
        return status == AVERROR_EOF;
    }
    auto video() const -> const AVStream& { return *format_->streams[video_]; }
    auto audio_stream() const -> const AVStream& { return *format_->streams[audio_]; }
    auto duration() const -> double { return static_cast<double>(format_->duration) / AV_TIME_BASE; }
    auto frames() const -> std::uint64_t { return video_packets_; }
    auto first_luma() const -> int { return first_luma_; }
    auto audio_peak() const -> float { return audio_peak_; }
};

void check_colour_metadata(const AVCodecParameters& parameters, AVColorSpace space,
    AVColorPrimaries primaries, AVColorTransferCharacteristic transfer) {
    CHECK(parameters.color_space == space);
    CHECK(parameters.color_primaries == primaries);
    CHECK(parameters.color_trc == transfer);
    CHECK(parameters.color_range == AVCOL_RANGE_MPEG);
}
void check_colour(const AVCodecParameters& parameters, video_colour_matrix_t matrix) {
    if (matrix == video_colour_matrix_t::bt709)
        check_colour_metadata(parameters, AVCOL_SPC_BT709, AVCOL_PRI_BT709, AVCOL_TRC_BT709);
    else check_colour_metadata(parameters, AVCOL_SPC_SMPTE170M, AVCOL_PRI_SMPTE170M, AVCOL_TRC_SMPTE170M);
}
void check_geometry(const file_probe_t& probe, const video_encoder_options_t& options) {
    CHECK(probe.video().codecpar->width == static_cast<int>(options.width));
    CHECK(probe.video().codecpar->height == static_cast<int>(options.height));
    CHECK(probe.frames() == options.frame_count);
    CHECK(probe.video().codecpar->format == AV_PIX_FMT_YUV420P);
}
void check_timing(const file_probe_t& probe, const video_encoder_options_t& options) {
    const auto frame_duration = 1.0 / options.clock.numerator;
    const auto video_start = probe.video().start_time * av_q2d(probe.video().time_base);
    const auto audio_start = probe.audio_stream().start_time * av_q2d(probe.audio_stream().time_base);
    CHECK(std::abs(video_start) <= frame_duration);
    CHECK(std::abs(video_start - audio_start) <= frame_duration);
    CHECK(std::abs(probe.duration() - static_cast<double>(options.frame_count) / options.clock.numerator) <= frame_duration);
    CHECK(probe.audio_peak() > 0.02F);
}
void check_profile(const file_probe_t& probe, const video_encoder_options_t& options) {
    const auto mp4 = options.profile == video_profile_t::mp4_h264_aac;
    CHECK(probe.video().codecpar->codec_id == (mp4 ? AV_CODEC_ID_H264 : AV_CODEC_ID_VP9));
    CHECK(probe.audio_stream().codecpar->codec_id == (mp4 ? AV_CODEC_ID_AAC : AV_CODEC_ID_OPUS));
    if (mp4) CHECK(probe.video().codecpar->level == options.h264_level());
}
void check_decoded_red_luma(const file_probe_t& probe, const video_encoder_options_t& options) {
    const auto red_y = options.colour_matrix() == video_colour_matrix_t::bt709 ? 63 : 81;
    CHECK(std::abs(probe.first_luma() - red_y) <= 3);
}
void inspect_output(const video_encoder_options_t& options) {
    file_probe_t probe;
    REQUIRE(probe.open(options.destination));
    REQUIRE(probe.inspect());
    check_geometry(probe, options);
    check_colour(*probe.video().codecpar, options.colour_matrix());
    check_timing(probe, options);
    check_profile(probe, options);
    check_decoded_red_luma(probe, options);
}

auto encode_clip(const video_encoder_options_t& options, std::stop_token stop = {},
    std::function<void(video_encoder_progress_t)> progress = {})
    -> result_t<video_encoder_report_t, wormedit::error_t<failure_code_t>> {
    auto frames = frame_fixture_t::from(options);
    REQUIRE(frames);
    audio_fixture_t audio{options};
    REQUIRE(audio.initialize());
    const video_encoder_sources_t sources{.frame = frames->source(), .audio = audio.source()};
    auto result = video_encoder_t::encode(options, sources, stop, std::move(progress));
    if (result) CHECK(frames->frames() == options.frame_count);
    return result;
}
void check_report(const result_t<video_encoder_report_t, wormedit::error_t<failure_code_t>>& result,
    const video_encoder_options_t& options) {
    INFO((result ? "Video encode succeeded" : result.error().message));
    REQUIRE(result);
    CHECK(result->video_frames == options.frame_count);
    CHECK(result->audio_frames == options.frame_count * (result->audio_sample_rate / options.clock.numerator));
    std::printf("VID-2 %s %ux%u %u/1: video=%s audio=%s rate=%u level=%d FFmpeg=%s\n",
        options.profile == video_profile_t::mp4_h264_aac ? "MP4" : "WebM", options.width, options.height,
        options.clock.numerator, result->video_encoder.c_str(), result->audio_encoder.c_str(),
        result->audio_sample_rate, result->h264_level, result->ffmpeg_version.c_str());
}
}

// Full profile cases deliberately encode full-size two-second clips. Tiny
// tests cannot establish required codec/profile/colour/timebase acceptance.
TEST_CASE("Video encoder muxes two seconds at1080p30 in both approved profiles", "[video-encoder][profiles][slow]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    for (const auto profile : {video_profile_t::mp4_h264_aac, video_profile_t::webm_vp9_opus}) {
        video_encoder_options_t options{.destination = directory->path() / (profile == video_profile_t::mp4_h264_aac ? "clip.mp4" : "clip.webm"),
            .profile = profile, .clock = {.numerator = 30}, .frame_count = 60, .begin_audio_frame = audio::output_sample_rate / 2};
        options.conditional_layer = profile == video_profile_t::mp4_h264_aac ? storyboard_conditional_layer_t::pass : storyboard_conditional_layer_t::fail;
        const auto result = encode_clip(options);
        check_report(result, options);
        inspect_output(options);
    }
}
TEST_CASE("Video encoder probes UHD30 and UHD60 without silently changing configuration", "[video-encoder][UHD][slow]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    for (const auto profile : {video_profile_t::mp4_h264_aac, video_profile_t::webm_vp9_opus}) {
        for (const auto fps : {30U, 60U}) {
            video_encoder_options_t options{.destination = directory->path() / "probe", .profile = profile,
                .width = 3840, .height = 2160, .clock = {.numerator = fps}, .frame_count = 1};
            const auto probe = video_encoder_t::probe(options, true);
            if (!probe && profile == video_profile_t::mp4_h264_aac) {
                std::printf("VID-2 MP4 UHD%u unavailable: %s\n", fps, probe.error().message.c_str());
                CHECK(probe.error().code == failure_code_t::unavailable);
                continue;
            }
            check_report(probe, options);
            options.destination = directory->path() / (profile == video_profile_t::mp4_h264_aac ? "UHD.mp4" : "UHD.webm");
            options.begin_audio_frame = audio::output_sample_rate / 2;
            const auto result = encode_clip(options);
            check_report(result, options);
            inspect_output(options);
        }
    }
}
TEST_CASE("Video encoder uses BT601 conversion for native640x480", "[video-encoder][colour]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const video_encoder_options_t options{.destination = directory->path() / "native.webm", .profile = video_profile_t::webm_vp9_opus,
        .width = 640, .height = 480, .clock = {.numerator = 30}, .frame_count = 3, .begin_audio_frame = audio::output_sample_rate / 2};
    check_report(encode_clip(options), options);
    inspect_output(options);
}

namespace {
auto read_destination(const std::filesystem::path& path) -> std::string {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void check_preserved(const video_encoder_options_t& options, bool existed) {
    CHECK(std::filesystem::exists(options.destination) == existed);
    if (existed) CHECK(read_destination(options.destination) == "previous export");
    CHECK(std::distance(std::filesystem::directory_iterator{options.destination.parent_path()},
        std::filesystem::directory_iterator{}) == (existed ? 1 : 0));
}
auto small_options(const std::filesystem::path& path) -> video_encoder_options_t {
    return {.destination = path, .profile = video_profile_t::webm_vp9_opus, .width = 640, .height = 480,
        .clock = {.numerator = 30}, .frame_count = 6, .begin_audio_frame = audio::output_sample_rate / 2};
}
void check_cancelled_encode(const std::filesystem::path& path, bool existed) {
    const auto options = small_options(path);
    if (existed) REQUIRE(create_file_atomic(path, "previous export", 15));
    std::stop_source stop;
    std::uint64_t greatest_progress{};
    const auto result = encode_clip(options, stop.get_token(), [&](video_encoder_progress_t progress) {
        greatest_progress = std::max(greatest_progress, progress.completed_frames);
        if (progress.completed_frames == 2) stop.request_stop();
    });
    REQUIRE_FALSE(result);
    CHECK(result.error().code == failure_code_t::cancelled);
    CHECK(greatest_progress == 2);
    check_preserved(options, existed);
}
using encode_result_t = result_t<video_encoder_report_t, wormedit::error_t<failure_code_t>>;
void check_failure(const encode_result_t& result, const video_encoder_options_t& options, failure_code_t code) {
    REQUIRE_FALSE(result);
    CHECK(result.error().code == code);
    check_preserved(options, true);
}

enum class pcm_fault_t { wrong_origin, too_large, non_finite, short_interval, ignored_sink_error };
auto faulty_audio(const video_encoder_options_t& options, pcm_fault_t fault) -> video_audio_source_t {
    return [options, fault](const audio::offline_mix_sink_t& sink, std::stop_token)
        -> result_t<void, wormedit::error_t<failure_code_t>> {
        if (fault == pcm_fault_t::short_interval) return {};
        const auto count = fault == pcm_fault_t::too_large ? audio::maximum_read_frames + 1 : 64;
        std::vector<float> pcm(static_cast<std::size_t>(count) * 2);
        if (fault == pcm_fault_t::non_finite) pcm[0] = std::numeric_limits<float>::quiet_NaN();
        const auto first = options.begin_audio_frame + (fault == pcm_fault_t::wrong_origin || fault == pcm_fault_t::ignored_sink_error ? 1 : 0);
        auto result = sink(first, pcm);
        if (fault == pcm_fault_t::ignored_sink_error) return {};
        return result;
    };
}
void check_pcm_refusal(const std::filesystem::path& path, pcm_fault_t fault) {
    const auto options = small_options(path);
    REQUIRE(create_file_atomic(path, "previous export", 15));
    auto frames = frame_fixture_t::from(options);
    REQUIRE(frames);
    const video_encoder_sources_t sources{.frame = frames->source(), .audio = faulty_audio(options, fault)};
    const auto result = video_encoder_t::encode(options, sources);
    const auto code = fault == pcm_fault_t::short_interval ? failure_code_t::render_failed : failure_code_t::invalid_data;
    check_failure(result, options, code);
}
}

TEST_CASE("Video cancellation removes staging and preserves an existing destination", "[video-encoder][atomic][cancel]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    check_cancelled_encode(directory->path() / "clip.webm", true);
}
TEST_CASE("Video cancellation leaves no destination when none existed", "[video-encoder][atomic][cancel]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    check_cancelled_encode(directory->path() / "clip.webm", false);
}
TEST_CASE("Video output byte limit preserves the destination and removes staging", "[video-encoder][atomic][limit]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    auto options = small_options(directory->path() / "clip.webm");
    REQUIRE(create_file_atomic(options.destination, "previous export", 15));
    options.maximum_output_bytes = 32;
    check_failure(encode_clip(options), options, failure_code_t::io_failed);
}
TEST_CASE("A failing frame source cannot publish a partial video", "[video-encoder][atomic][failure]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto options = small_options(directory->path() / "clip.webm");
    REQUIRE(create_file_atomic(options.destination, "previous export", 15));
    auto frames = frame_fixture_t::from(options);
    REQUIRE(frames);
    const auto source = frames->source();
    const video_encoder_sources_t sources{.frame = [source](std::uint64_t index, storyboard_time_t time,
        storyboard_conditional_layer_t choice, std::stop_token stop) -> result_t<storyboard_raster_frame_t, wormedit::error_t<failure_code_t>> {
        if (index == 2) return std::unexpected(wormedit::error_t{failure_code_t::render_failed, std::string{"Injected frame source failure"}});
        return source(index, time, choice, stop);
    }};
    check_failure(video_encoder_t::encode(options, sources), options, failure_code_t::render_failed);
}
TEST_CASE("Video encoder refuses malformed and incomplete native preview PCM", "[video-encoder][audio][atomic]") {
    for (const auto fault : {pcm_fault_t::wrong_origin, pcm_fault_t::too_large, pcm_fault_t::non_finite,
             pcm_fault_t::short_interval, pcm_fault_t::ignored_sink_error}) {
        auto directory = wormedit::test::temporary_directory_t::from();
        REQUIRE(directory);
        check_pcm_refusal(directory->path() / "clip.webm", fault);
    }
}
TEST_CASE("Actual codec probe validates privately without replacing an existing destination", "[video-encoder][probe][atomic]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto options = small_options(directory->path() / "clip.webm");
    REQUIRE(create_file_atomic(options.destination, "previous export", 15));
    const auto probe = video_encoder_t::probe(options, true);
    REQUIRE(probe);
    check_preserved(options, true);
}
TEST_CASE("Failure in progress reporting before publication preserves the old output", "[video-encoder][atomic][progress]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto options = small_options(directory->path() / "clip.webm");
    REQUIRE(create_file_atomic(options.destination, "previous export", 15));
    const auto result = encode_clip(options, {}, [](video_encoder_progress_t) { throw std::runtime_error{"Observer failure"}; });
    check_failure(result, options, failure_code_t::task_threw);
}
TEST_CASE("The published progress notification cannot reverse a successful export", "[video-encoder][atomic][progress]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto options = small_options(directory->path() / "clip.webm");
    std::uint64_t previous{};
    bool valid_progress{true};
    const auto result = encode_clip(options, {}, [&](video_encoder_progress_t progress) {
        valid_progress = valid_progress && progress.completed_frames >= previous && progress.completed_frames <= progress.total_frames;
        previous = progress.completed_frames;
        if (progress.completed_frames == progress.total_frames) throw std::runtime_error{"Observer after publication"};
    });
    REQUIRE(result);
    CHECK(valid_progress);
    CHECK(previous == options.frame_count);
    inspect_output(options);
}

TEST_CASE("Late destination creation cannot bypass an explicit video replace refusal", "[video-encoder][atomic][replace]") {
    auto directory = wormedit::test::temporary_directory_t::from(); REQUIRE(directory);
    auto options = small_options(directory->path() / "clip.webm"); options.replace_existing = false;
    bool created{};
    const auto result = encode_clip(options, {}, [&](video_encoder_progress_t progress) {
        if (!created && progress.completed_frames == 1) {
            created = true;
            if (!create_file_atomic(options.destination, "previous export", 15)) throw std::runtime_error{"Fixture destination creation failed"};
        }
    });
    REQUIRE(created);
    check_failure(result, options, failure_code_t::publication_failed);
}

TEST_CASE("Explicit video replace refusal preserves an existing destination", "[video-encoder][atomic][replace]") {
    auto directory = wormedit::test::temporary_directory_t::from(); REQUIRE(directory);
    auto options = small_options(directory->path() / "clip.webm"); options.replace_existing = false;
    REQUIRE(create_file_atomic(options.destination, "previous export", 15));
    check_failure(encode_clip(options), options, failure_code_t::publication_failed);
}

TEST_CASE("Cancel after actual video publication retains the validated successful file", "[video-encoder][atomic][cancel]") {
    auto directory = wormedit::test::temporary_directory_t::from(); REQUIRE(directory);
    const auto options = small_options(directory->path() / "clip.webm");
    std::stop_source stop;
    const auto result = encode_clip(options, stop.get_token(), [&](video_encoder_progress_t progress) {
        if (progress.completed_frames == progress.total_frames) stop.request_stop();
    });
    REQUIRE(result); CHECK(stop.stop_requested());
    inspect_output(options);
}
