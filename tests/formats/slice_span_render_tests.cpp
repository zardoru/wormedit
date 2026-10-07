#include <wormedit/formats/slice_spans.h>
#include <wormedit/audio/slice_render.h>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <fstream>
#include <iterator>
#include <source_location>

using namespace wormedit;
namespace {

void check_span(bool value, std::source_location location = std::source_location::current()) {
    INFO("check at " << location.line());
    CHECK(value);
}
void require_span(bool value, std::source_location location = std::source_location::current()) {
    INFO("requirement at " << location.line());
    REQUIRE(value);
}

auto bytes_at(const std::filesystem::path& path) -> std::string {
    std::ifstream stream{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{stream}, {}};
}

struct render_fixture_t {
    std::filesystem::path directory;
    song_document_t song;
    std::array<slice_span_source_t, 1> sources{{{asset_id_t{1}, 44'100, 44'100, 44'100}}};
    render_fixture_t() {
        static std::atomic<std::uint64_t> next{};
        directory = std::filesystem::temp_directory_path() / ("wormedit-spans-" + std::to_string(next++) + "-" +
            std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(directory);
        std::vector<float> pcm(44'100 * 2);
        for (std::size_t index = 0; index < pcm.size(); ++index)
            pcm[index] = static_cast<float>(static_cast<int>(index % 500) - 250) / 1000.0F;
        const auto encoded = audio::encode_wav_pcm16(pcm, 44'100);
        std::ofstream stream{directory / "stem.wav", std::ios::binary};
        require_span(static_cast<bool>(stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()))));
        song.id = song_id_t{1};
        song.revision = 7;
        song.audio_assets = {{.id = asset_id_t{1}, .source_path = "stem.wav"}};
        song.resources = {{.id = resource_id_t{2}, .asset = asset_id_t{1}, .roles = {resource_role_t::sample}}};
        chart_document_t chart{.id = chart_id_t{3}, .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{4}}},
            .bpms = {{.id = object_id_t{5}, .position = {measure_id_t{4}, 0}, .beats_per_minute = {120, 1}}}};
        chart.sound_channels = {{.id = object_id_t{6}, .source_clip = {.source = asset_id_t{1}},
            .events = {{.id = object_id_t{7}, .position = {measure_id_t{4}, 0}},
                {.id = object_id_t{8}, .position = {measure_id_t{4}, 0}}}}};
        song.charts.push_back(std::move(chart));
    }
    ~render_fixture_t() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    auto plan() const -> slice_span_plan_t { return build_slice_span_plan(song, chart_id_t{3}, sources); }
    auto render(const slice_span_plan_t& plan) const -> slice_span_render_result_t {
        return render_slice_span_plan(song, plan, {.source_directory = directory, .output_directory = directory / "out"});
    }
};

void check_exact_rendered_pcm(const render_fixture_t& fixture, const slice_span_plan_t& plan,
                              const slice_span_render_result_t& rendered) {
    audio::decoder_t decoder;
    require_span(decoder.open(fixture.directory / "stem.wav"));
    for (std::size_t index = 0; index < plan.spans.size(); ++index) {
        const auto& span = plan.spans[index];
        const auto frames = static_cast<std::size_t>(span.end_frame - span.begin_frame);
        std::vector<float> pcm(frames * 2);
        std::size_t done{};
        while (done < frames) {
            const auto count = std::min(audio::maximum_read_frames, frames - done);
            const auto read = decoder.read_at(span.begin_frame + done, std::span(pcm).subspan(done * 2, count * 2), count);
            require_span(read.status == audio::read_status_t::success && read.frames_read == count);
            done += count;
        }
        check_span(bytes_at(rendered.files[index]) == audio::encode_wav_pcm16(pcm, audio::output_sample_rate));
    }
}

} // namespace

TEST_CASE("slice_spans repeated identical stem slices render one exact WAV", "[slice_spans][audio]") {
    render_fixture_t fixture;
    const auto original = bytes_at(fixture.directory / "stem.wav");
    const auto plan = fixture.plan();
    require_span(static_cast<bool>(plan));
    require_span(plan.spans.size() == 1 && plan.mappings.size() == 2);
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    require_span(rendered.files.size() == 1);
    check_span(rendered.files[0].filename() == plan.spans[0].filename);
    check_exact_rendered_pcm(fixture, plan, rendered);
    check_span(bytes_at(fixture.directory / "stem.wav") == original);
    check_span(fixture.song.revision == 7);
}

TEST_CASE("slice_spans overlapping ranges render independently and preserve exact PCM", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto& events = fixture.song.charts.front().sound_channels.front().events;
    events.back().position.row = 120;
    events.back().continuation = true;
    events.push_back({.id = object_id_t{10}, .position = {measure_id_t{4}, 240}});
    const auto plan = fixture.plan();
    require_span(static_cast<bool>(plan));
    require_span(plan.spans.size() == 3);
    check_span(plan.spans[0].begin_frame == 0 && plan.spans[2].begin_frame == 0);
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    check_exact_rendered_pcm(fixture, plan, rendered);
}

TEST_CASE("slice_spans later undecodable asset rejects before any output writes", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto plan = fixture.plan();
    fixture.song.audio_assets.push_back({.id = asset_id_t{9}, .source_path = "broken.wav"});
    std::ofstream{fixture.directory / "broken.wav"} << "not an audio file";
    plan.sources.push_back({asset_id_t{9}, 44'100, 100, 100});
    plan.spans.push_back({asset_id_t{9}, 0, 100, "broken_002.wav"});
    const auto rendered = fixture.render(plan);
    check_span(!rendered);
    check_span(rendered.asset == asset_id_t{9});
    check_span(rendered.error.find("broken.wav") != std::string::npos);
    check_span(rendered.files.empty());
    check_span(!std::filesystem::exists(fixture.directory / "out"));
}

TEST_CASE("slice_spans existing output refuses overwrite and reserved names permit rerender", "[slice_spans][audio]") {
    render_fixture_t fixture;
    const auto plan = fixture.plan();
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    const auto bytes = bytes_at(rendered.files.front());
    const auto rejected = fixture.render(plan);
    check_span(!rejected && rejected.files.empty());
    check_span(bytes_at(rendered.files.front()) == bytes);
    const auto second = build_slice_span_plan(fixture.song, chart_id_t{3}, fixture.sources,
        {.occupied_filenames = {plan.spans.front().filename}});
    const auto next = fixture.render(second);
    require_span(static_cast<bool>(next));
    check_span(next.files.front() != rendered.files.front());
    check_span(bytes_at(rendered.files.front()) == bytes);
}

TEST_CASE("slice_spans invalid range and cancellation leave no files", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto plan = fixture.plan();
    plan.spans.front().end_frame = 44'101;
    check_span(!fixture.render(plan));
    check_span(!std::filesystem::exists(fixture.directory / "out"));
    plan = fixture.plan();
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = render_slice_span_plan(fixture.song, plan,
        {.source_directory = fixture.directory, .output_directory = fixture.directory / "out", .stop = stop.get_token()});
    check_span(!cancelled && cancelled.files.empty());
    check_span(!std::filesystem::exists(fixture.directory / "out"));
}

TEST_CASE("slice_spans renders audio once and keeps EOF continuation silent", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto& event = fixture.song.charts.front().sound_channels.front().events.back();
    event.position.row = 480;
    event.continuation = true;
    const auto plan = fixture.plan();
    require_span(static_cast<bool>(plan));
    require_span(plan.spans.size() == 1 && plan.mappings.size() == 2);
    check_span(!plan.mappings.back().span_index);
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    check_span(rendered.files.size() == 1);
    check_exact_rendered_pcm(fixture, plan, rendered);
}

TEST_CASE("slice_spans soundless gameplay needs no source geometry or files", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto& chart = fixture.song.charts.front();
    chart.sound_channels.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{13}, .name = "Packed"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{11}, .position = {measure_id_t{4}, 0}, .source_lane = object_id_t{13}}};
    const auto plan = build_slice_span_plan(fixture.song, chart_id_t{3}, {});
    require_span(static_cast<bool>(plan));
    require_span(plan.spans.empty() && plan.mappings.size() == 1);
    check_span(!plan.mappings.front().span_index);
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    check_span(rendered.files.empty());
    check_span(!std::filesystem::exists(fixture.directory / "out"));
}
