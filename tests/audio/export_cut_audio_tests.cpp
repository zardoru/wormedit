#include <wormedit/formats/export_cut_audio.h>
#include <wormedit/audio/offline_mix.h>
#include <wormedit/audio/audition_resolver.h>
#include <wormedit/audio/slice_render.h>
#include <wormedit/core/export_cut_frames.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/audio/stream_encoder.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iterator>
#include <limits>
#include <source_location>

using namespace wormedit;
namespace {
void check_cut_audio(bool value, std::source_location location = std::source_location::current()) {
    INFO("check at " << location.line());
    CHECK(value);
}
void require_cut_audio(bool value, std::source_location location = std::source_location::current()) {
    INFO("requirement at " << location.line());
    REQUIRE(value);
}
auto read_cut_bytes(const std::filesystem::path& path) -> std::string {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, {}};
}
struct cut_audio_fixture_t {
    std::filesystem::path directory;
    song_document_t song;
    std::vector<float> original;
    cut_audio_fixture_t() {
        static std::atomic<std::uint64_t> next{};
        directory = std::filesystem::temp_directory_path() / ("wormedit-cut-audio-" +
            std::to_string(next++) + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(directory);
        original.resize(44'100 * 2);
        for (std::size_t index = 0; index < original.size() / 2; ++index) {
            original[index * 2] = static_cast<float>(index % 501) / 250.0F;
            original[index * 2 + 1] = -original[index * 2];
        }
        write_source(original);
        song.audio_assets = {{.id = asset_id_t{1}, .source_path = "source.wav"}};
        song.resources = {{.id = resource_id_t{2}, .asset = asset_id_t{1}, .roles = {resource_role_t::music}}};
        song.export_cuts = {{.id = object_id_t{3}, .begin_milliseconds = 1, .end_milliseconds = 2},
            {.id = object_id_t{4}, .begin_milliseconds = 300, .end_milliseconds = 450}};
    }
    ~cut_audio_fixture_t() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    void write_source(std::span<const float> samples) const {
        const auto bytes = audio::encode_wav_float32(samples, 44'100);
        std::ofstream file{directory / "source.wav", std::ios::binary};
        require_cut_audio(static_cast<bool>(file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))));
    }
    auto timeline() const -> export_cut_timeline_t {
        const auto built = export_cut_timeline_t::from(song.export_cuts);
        require_cut_audio(built.ok());
        return built.value;
    }
    auto plan() const -> export_cut_audio_plan_t {
        const auto sources = prepare_export_cut_sources(song, directory);
        require_cut_audio(sources.ok() && sources.value.size() == 1);
        const auto frames = export_cut_frame_timeline_t::from(timeline(), 48'000, 48'000);
        return {.asset = asset_id_t{1}, .filename = "cut.wav", .source = sources.value.front(),
            .output_frames = frames.value.kept_frame_count(0, 48'000), .encoding = {.codec = export_audio_codec_t::wav}};
    }
    auto render(std::span<const export_cut_audio_plan_t> plans, std::stop_token stop = {}) const
        -> slice_span_render_payload_t {
        return render_export_cut_audio_plans(song, plans, timeline(),
            {.source_directory = directory, .output_directory = directory / "stage", .stop = stop});
    }
};
auto expected_cut_pcm(const cut_audio_fixture_t& fixture) -> std::vector<float> {
    const auto frames = export_cut_frame_timeline_t::from(fixture.timeline(), 48'000, 48'000);
    std::vector<float> output;
    for (const auto& interval : frames.value.kept_intervals(0, 48'000)) {
        for (auto position = interval.begin_frame; position < interval.end_frame; ++position) {
            const auto input = position * 147 / 160;
            const auto next = std::min<std::uint64_t>(input + 1, 44'099);
            const auto fraction = static_cast<float>(position * 147 % 160) / 160.0F;
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto first = fixture.original[input * 2 + channel];
                output.push_back(first + (fixture.original[next * 2 + channel] - first) * fraction);
            }
        }
    }
    return output;
}
} // namespace

TEST_CASE("Export cut source preparation refreshes evidence and skips I/O without cuts", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    fixture.song.audio_assets.front().stream = {.sample_rate = 8'000, .channels = 1, .frame_count = 12};
    const auto prepared = prepare_export_cut_sources(fixture.song, fixture.directory);
    require_cut_audio(prepared.ok() && prepared.value.size() == 1);
    auto evidence = prepared.value.front();
    evidence.bitrate_bits_per_second = 0;
    check_cut_audio(evidence == export_cut_source_t{.asset = asset_id_t{1}, .native_sample_rate = 44'100,
        .native_frame_count = 44'100, .output_frame_count = 44'100, .source_path = "source.wav"});
    fixture.song.export_cuts.clear();
    std::filesystem::remove(fixture.directory / "source.wav");
    const auto identity = prepare_export_cut_sources(fixture.song, fixture.directory);
    check_cut_audio(identity.ok() && identity.value.empty());
}
TEST_CASE("Unknown referenced audio fails fresh export cut source inspection", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    fixture.song.resources.front().asset = asset_id_t{99};
    const auto prepared = prepare_export_cut_sources(fixture.song, fixture.directory);
    check_cut_audio(!prepared.ok());
    check_cut_audio(prepared.value.empty());
}
TEST_CASE("Streamed export cut WAV has exact canonical seams and preserves float samples", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    const auto source_bytes = read_cut_bytes(fixture.directory / "source.wav");
    const std::array plans{fixture.plan()};
    const auto rendered = fixture.render(plans);
    require_cut_audio(rendered.is_success() && rendered.files.size() == 1);
    const auto expected = expected_cut_pcm(fixture);
    check_cut_audio(expected.size() / 2 == 40'752);
    check_cut_audio(read_cut_bytes(rendered.files.front()) == audio::encode_wav_float32(expected, 48'000));
    check_cut_audio(read_cut_bytes(fixture.directory / "source.wav") == source_bytes);
    audio::decoder_t decoded;
    require_cut_audio(decoded.open(rendered.files.front()));
    check_cut_audio(decoded.info()->source_sample_rate == 48'000 && decoded.info()->source_frame_count == 40'752);
}
TEST_CASE("Encoded export cuts retain their shortened clock in OGG and MP3", "[audio][export-cut]") {
    const auto codec = GENERATE(export_audio_codec_t::ogg, export_audio_codec_t::mp3);
    if (!audio::stream_encoder_t::can_encode(codec)) SKIP("MP3 encoder unavailable in this build");
    cut_audio_fixture_t fixture;
    auto plan = fixture.plan();
    plan.encoding = {.codec = codec, .mp3_bitrate_kbps = 192};
    plan.filename = "cut" + std::string{plan.encoding.extension()};
    const auto original = read_cut_bytes(fixture.directory / "source.wav");
    const std::array plans{plan};
    const auto rendered = fixture.render(plans);
    UNSCOPED_INFO(rendered.error);
    require_cut_audio(rendered.is_success());
    require_cut_audio(rendered.files.size() == 1);
    audio::decoder_t decoder;
    require_cut_audio(decoder.open(rendered.files.front()));
    require_cut_audio(decoder.info().has_value());
    check_cut_audio(decoder.info()->source_sample_rate == export_cut_render_sample_rate);
    check_cut_audio(decoder.info()->source_frame_count == plan.output_frames);
    check_cut_audio(std::filesystem::file_size(rendered.files.front()) < 56 + plan.output_frames * 8);
    check_cut_audio(read_cut_bytes(fixture.directory / "source.wav") == original);
    if (codec == export_audio_codec_t::mp3)
        check_cut_audio(audio::stream_encoder_t::source_bitrate(rendered.files.front()) == 192'000);
}
TEST_CASE("Cut audio failure rolls back staged files without overwriting existing output", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    auto plan = fixture.plan();
    const std::array plans{plan, export_cut_audio_plan_t{.asset = asset_id_t{99}, .filename = "missing.wav",
        .source = plan.source, .output_frames = plan.output_frames}};
    check_cut_audio(!fixture.render(plans).is_success());
    check_cut_audio(!std::filesystem::exists(fixture.directory / "stage" / "cut.wav"));
    std::ofstream{fixture.directory / "stage" / "cut.wav"} << "existing";
    const std::array one{plan};
    check_cut_audio(!fixture.render(one).is_success());
    check_cut_audio(read_cut_bytes(fixture.directory / "stage" / "cut.wav") == "existing");
}
TEST_CASE("Changed decoded geometry and cancellation cannot publish cut audio", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    const std::array plans{fixture.plan()};
    SECTION("source changed since preparation") {
        fixture.original.resize(100);
        fixture.write_source(fixture.original);
        check_cut_audio(!fixture.render(plans).is_success());
    }
    SECTION("cancelled before rendering") {
        std::stop_source cancel;
        cancel.request_stop();
        check_cut_audio(!fixture.render(plans, cancel.get_token()).is_success());
    }
    check_cut_audio(!std::filesystem::exists(fixture.directory / "stage" / "cut.wav"));
}
TEST_CASE("Preview cut PCM uses rounded seams and clips a bounded selection", "[audio][export-cut]") {
    const std::array cuts{export_cut_t{.id = object_id_t{1}, .begin_milliseconds = 1, .end_milliseconds = 2}};
    const auto timeline = export_cut_timeline_t::from(cuts).value;
    std::vector<float> pcm(100 * 2);
    for (std::size_t index = 0; index < pcm.size(); ++index) pcm[index] = static_cast<float>(index);
    const auto cut = cut_preview_audio(pcm, 0, timeline, 44'100);
    require_cut_audio(cut.ok());
    check_cut_audio(cut.value.size() == 56 * 2);
    check_cut_audio(cut.value[86] == 86 && cut.value[88] == 176);
    const auto selection = cut_preview_audio(std::span{pcm}.subspan(50 * 2), 50, timeline, 44'100);
    check_cut_audio(selection.ok() && selection.value.size() == 12 * 2);
    check_cut_audio(selection.value.front() == 176);
}
TEST_CASE("Preview cuts reject malformed clocks and preserve the no-cut identity", "[audio][export-cut]") {
    const export_cut_timeline_t timeline;
    const std::array pcm{1.5F, -1.5F, 2.0F, -2.0F};
    const auto unchanged = cut_preview_audio(pcm, 0, timeline, 48'000);
    check_cut_audio(unchanged.ok() && unchanged.value == std::vector<float>(pcm.begin(), pcm.end()));
    check_cut_audio(!cut_preview_audio(pcm, 0, timeline, 0).ok());
    check_cut_audio(!cut_preview_audio(std::span{pcm}.first(3), 0, timeline, 48'000).ok());
    check_cut_audio(!cut_preview_audio(pcm, std::numeric_limits<std::uint64_t>::max(), timeline, 48'000).ok());
}
TEST_CASE("Preview fade follows the actual spliced output end", "[audio][export-cut]") {
    std::vector<float> pcm(100 * 2, 1.0F);
    const std::array cuts{export_cut_t{.id = object_id_t{1}, .begin_milliseconds = 2, .end_milliseconds = 100}};
    auto cut = cut_preview_audio(pcm, 0, export_cut_timeline_t::from(cuts).value, 44'100);
    require_cut_audio(cut.ok() && cut.value.size() == 88 * 2);
    audio::apply_offline_mix_fade(cut.value, 1);
    check_cut_audio(cut.value.front() == 1.0F);
    check_cut_audio(cut.value.back() == 0.0F);
    check_cut_audio(cut.value[86 * 2] > 0.0F && cut.value[86 * 2] < 0.1F);
}
TEST_CASE("Streaming WAV primitives share the bounded float32 encoder", "[audio][export-cut]") {
    const std::array samples{2.0F, -2.0F, 0.25F, -0.25F};
    check_cut_audio(audio::encode_wav_float32_header(2, 48'000) + audio::encode_wav_float32_data(samples) ==
        audio::encode_wav_float32(samples, 48'000));
    check_cut_audio(audio::encode_wav_float32_header(0, 48'000).empty());
    check_cut_audio(audio::encode_wav_float32_header(std::numeric_limits<std::uint64_t>::max(), 48'000).empty());
    check_cut_audio(audio::encode_wav_float32_data(std::array{1.0F}).empty());
    check_cut_audio(audio::encode_wav_float32_data(std::array{std::numeric_limits<float>::infinity(), 0.0F}).empty());
}

TEST_CASE("Export cut source inspection includes explicit music overrides without preview assets", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    fixture.song.resources.clear();
    chart_document_t chart;
    chart.metadata.overrides.value(inherited_metadata_field_t::music) = "source.wav";
    chart.metadata.overrides.value(inherited_metadata_field_t::preview) = "missing-preview.ogg";
    fixture.song.charts.push_back(chart);
    const auto prepared = prepare_export_cut_sources(fixture.song, fixture.directory);
    check_cut_audio(prepared.ok() && prepared.value.size() == 1);
    check_cut_audio(prepared.value.front().asset == asset_id_t{1});
}

TEST_CASE("Canonical cut rendering rounds native EOF once before clipping cut edges", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    fixture.original.resize(200);
    fixture.write_source(fixture.original);
    const auto prepared = prepare_export_cut_sources(fixture.song, fixture.directory);
    require_cut_audio(prepared.ok());
    // 100 native frames at44100 round to109 at48000; [48,96) is removed.
    const std::array plans{export_cut_audio_plan_t{.asset = asset_id_t{1}, .filename = "short.wav",
        .source = prepared.value.front(), .output_frames = 61, .encoding = {.codec = export_audio_codec_t::wav}}};
    const auto rendered = fixture.render(plans);
    require_cut_audio(rendered.is_success() && rendered.files.size() == 1);
    audio::decoder_t decoded;
    require_cut_audio(decoded.open(rendered.files.front()));
    check_cut_audio(decoded.info()->source_frame_count == 61);
    check_cut_audio(read_cut_bytes(rendered.files.front()).size() == 56 + 61 * 8);
}

TEST_CASE("Cut music interprets the shared decoder-frame cap as an exact duration", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    const std::array plans{fixture.plan()};
    const auto allowed = render_export_cut_audio_plans(fixture.song, plans, fixture.timeline(),
        {.source_directory = fixture.directory, .output_directory = fixture.directory / "allowed", .maximum_span_frames = 40'000});
    check_cut_audio(allowed.is_success());
    const auto rejected = render_export_cut_audio_plans(fixture.song, plans, fixture.timeline(),
        {.source_directory = fixture.directory, .output_directory = fixture.directory / "rejected", .maximum_span_frames = 37'000});
    check_cut_audio(!rejected.is_success());
    check_cut_audio(!std::filesystem::exists(fixture.directory / "rejected" / "cut.wav"));
}

TEST_CASE("Shared preview music selection follows explicit chart metadata and preserves identity", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    fixture.song.audio_assets.push_back({.id = asset_id_t{99}, .source_path = "override.wav"});
    chart_document_t chart;
    chart.metadata.overrides.value(inherited_metadata_field_t::music) = "override.wav";
    const auto selected = audio::preview_music_selection_t::from(fixture.song, chart);
    require_cut_audio(selected.ok() && selected.value.has_value());
    check_cut_audio(selected.value->resource == resource_id_t{2} && selected.value->asset == asset_id_t{99});
    chart.metadata.overrides.value(inherited_metadata_field_t::music) = "";
    const auto silent = audio::preview_music_selection_t::from(fixture.song, chart);
    check_cut_audio(silent.ok() && !silent.value);
}

TEST_CASE("Asset-only preview music receives a transient identity without document mutation", "[audio][export-cut]") {
    cut_audio_fixture_t fixture;
    fixture.song.resources.clear();
    chart_document_t chart;
    chart.metadata.overrides.value(inherited_metadata_field_t::music) = "source.wav";
    const auto selected = audio::preview_music_selection_t::from(fixture.song, chart);
    require_cut_audio(selected.ok() && selected.value.has_value());
    check_cut_audio(selected.value->resource && selected.value->asset == asset_id_t{1});
    check_cut_audio(fixture.song.resources.empty());
    fixture.song.audio_assets.push_back({.id = asset_id_t{99}, .source_path = "source.wav"});
    const auto ambiguous = audio::preview_music_selection_t::from(fixture.song, chart);
    check_cut_audio(!ambiguous.ok() && !ambiguous.value);
}
