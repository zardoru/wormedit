#include <wormedit/formats/slice_spans.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/audio/audition_resolver.h>
#include <wormedit/audio/offline_mix.h>
#include <wormedit/audio/slice_render.h>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
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
    auto plan() const -> slice_span_plan_outcome_t { return slice_span_plan_payload_t::from(song, chart_id_t{3}, sources); }
    auto render(const slice_span_plan_outcome_t& plan) const -> slice_span_render_outcome_t {
        return create_slice_span_files(song, slice_span_render_input_t::from(plan),
            {.source_directory = directory, .output_directory = directory / "out"});
    }
};

void check_exact_rendered_pcm(const render_fixture_t& fixture, const slice_span_plan_outcome_t& plan,
                              const slice_span_render_outcome_t& rendered) {
    audio::decoder_t decoder;
    require_span(decoder.open(fixture.directory / "stem.wav"));
    for (std::size_t index = 0; index < plan.value.spans.size(); ++index) {
        const auto& span = plan.value.spans[index];
        const auto frames = static_cast<std::size_t>(span.end_frame - span.begin_frame);
        std::vector<float> pcm(frames * 2);
        std::size_t done{};
        while (done < frames) {
            const auto count = std::min(audio::maximum_read_frames, frames - done);
            const auto read = decoder.read_at(span.begin_frame + done, std::span(pcm).subspan(done * 2, count * 2), count);
            require_span(read.status == audio::read_status_t::success && read.frames_read == count);
            done += count;
        }
        check_span(bytes_at(rendered.value().files[index]) == audio::encode_wav_pcm16(pcm, audio::output_sample_rate));
    }
}

} // namespace

TEST_CASE("slice_spans repeated identical stem slices render one exact WAV", "[slice_spans][audio]") {
    render_fixture_t fixture;
    const auto original = bytes_at(fixture.directory / "stem.wav");
    const auto plan = fixture.plan();
    require_span(plan.ok());
    require_span(plan.value.spans.size() == 1 && plan.value.mappings.size() == 2);
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    require_span(rendered.value().files.size() == 1);
    check_span(rendered.value().files[0].filename() == plan.value.spans[0].filename);
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
    require_span(plan.ok());
    require_span(plan.value.spans.size() == 3);
    check_span(plan.value.spans[0].begin_frame == 0 && plan.value.spans[2].begin_frame == 0);
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    check_exact_rendered_pcm(fixture, plan, rendered);
}

TEST_CASE("slice_spans later undecodable asset rejects before any output writes", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto plan = fixture.plan();
    fixture.song.audio_assets.push_back({.id = asset_id_t{9}, .source_path = "broken.wav"});
    std::ofstream{fixture.directory / "broken.wav"} << "not an audio file";
    plan.value.sources.push_back({asset_id_t{9}, 44'100, 100, 100});
    plan.value.spans.push_back({asset_id_t{9}, 0, 100, "broken_002.wav"});
    const auto rendered = fixture.render(plan);
    require_span(!rendered);
    check_span(rendered.error().asset == asset_id_t{9});
    check_span(rendered.error().error.find("broken.wav") != std::string::npos);
    check_span(rendered.error().files.empty());
    check_span(!std::filesystem::exists(fixture.directory / "out"));
}

TEST_CASE("slice_spans existing output refuses overwrite and reserved names permit rerender", "[slice_spans][audio]") {
    render_fixture_t fixture;
    const auto plan = fixture.plan();
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    const auto bytes = bytes_at(rendered.value().files.front());
    const auto rejected = fixture.render(plan);
    check_span(!rejected && rejected.error().files.empty());
    check_span(bytes_at(rendered.value().files.front()) == bytes);
    const auto second = slice_span_plan_payload_t::from(fixture.song, chart_id_t{3}, fixture.sources,
        {.occupied_filenames = {plan.value.spans.front().filename}});
    const auto next = fixture.render(second);
    require_span(static_cast<bool>(next));
    check_span(next.value().files.front() != rendered.value().files.front());
    check_span(bytes_at(rendered.value().files.front()) == bytes);
}

TEST_CASE("slice_spans invalid range and cancellation leave no files", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto plan = fixture.plan();
    plan.value.spans.front().end_frame = 44'101;
    check_span(!fixture.render(plan));
    check_span(!std::filesystem::exists(fixture.directory / "out"));
    plan = fixture.plan();
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = create_slice_span_files(fixture.song, slice_span_render_input_t::from(plan),
        {.source_directory = fixture.directory, .output_directory = fixture.directory / "out", .stop = stop.get_token()});
    check_span(!cancelled && cancelled.error().files.empty());
    check_span(!std::filesystem::exists(fixture.directory / "out"));
}

TEST_CASE("slice_spans renders audio once and keeps EOF continuation silent", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto& event = fixture.song.charts.front().sound_channels.front().events.back();
    event.position.row = 480;
    event.continuation = true;
    const auto plan = fixture.plan();
    require_span(plan.ok());
    require_span(plan.value.spans.size() == 1 && plan.value.mappings.size() == 2);
    check_span(!plan.value.mappings.back().span_index);
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    check_span(rendered.value().files.size() == 1);
    check_exact_rendered_pcm(fixture, plan, rendered);
}

TEST_CASE("slice_spans soundless gameplay needs no source geometry or files", "[slice_spans][audio]") {
    render_fixture_t fixture;
    auto& chart = fixture.song.charts.front();
    chart.sound_channels.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{13}, .name = "Packed"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{11}, .position = {measure_id_t{4}, 0}, .source_lane = object_id_t{13}}};
    const auto plan = slice_span_plan_payload_t::from(fixture.song, chart_id_t{3}, {});
    require_span(plan.ok());
    require_span(plan.value.spans.empty() && plan.value.mappings.size() == 1);
    check_span(!plan.value.mappings.front().span_index);
    const auto rendered = fixture.render(plan);
    require_span(static_cast<bool>(rendered));
    check_span(rendered.value().files.empty());
    check_span(!std::filesystem::exists(fixture.directory / "out"));
}

namespace {
auto sliced_pcm_song(bool off_period = false) -> song_document_t {
    std::string text = R"({"version":"1.0.0","info":{"title":"PCM slices","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[{"name":"stem.wav","notes":[{"x":1,"y":0,"c":false},{"x":1,"y":240,"c":true},{"x":0,"y":480,"c":false}]}]})";
    if (off_period) text.replace(text.find("\"y\":240"), 7, "\"y\":241");
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    require_span(parsed.value.has_value());
    require_span(parsed.ok());
    return *parsed.value;
}

auto sliced_preview_pcm(const song_document_t& song, const std::filesystem::path& directory)
    -> std::vector<float> {
    audio::decoded_source_registry_t sources;
    for (const auto& asset : song.audio_assets) {
        const auto path = directory / asset.source_path;
        const auto registered = sources.create_source({asset.id, path,
            {path.string(), "conversion-pcm-v1"}});
        require_span(static_cast<bool>(registered));
    }
    const auto& chart = song.charts.front();
    const auto timing = build_timing_index(chart);
    require_span(static_cast<bool>(timing));
    const auto schedule = build_preview_schedule(chart, *timing.index,
        {.include_metronome = false});
    require_span(static_cast<bool>(schedule));
    const auto bindings = audio::resolve_preview_bindings(song, chart.id, *schedule.schedule,
        sources.bindings());
    require_span(static_cast<bool>(bindings));
    const auto mixed = audio::render_offline_interval(sources, *schedule.schedule, bindings.bindings,
        0, 2 * slice_span_sample_rate);
    require_span(static_cast<bool>(mixed));
    return mixed->interleaved_stereo;
}

void check_pcm_tolerance(const std::vector<float>& before, const std::vector<float>& after) {
    require_span(before.size() == after.size());
    float maximum{};
    std::size_t maximum_index{};
    for (std::size_t index = 0; index < before.size(); ++index) {
        const auto error = std::abs(before[index] - after[index]);
        if (error > maximum) { maximum = error; maximum_index = index; }
    }
    INFO("sample " << maximum_index << " original=" << before[maximum_index] << " reimport=" << after[maximum_index]);
    INFO("maximum preview PCM difference " << maximum);
    check_span(maximum <= 2.0F / 32768.0F);
}
} // namespace

TEST_CASE("sliced BMSON conversion reimport matches production preview PCM",
          "[slice_spans][audio][conversion][sliced-conversion]") {
    render_fixture_t fixture;
    const auto song = sliced_pcm_song();
    conversion_request_t request{.target = format_family_t::osu_mania,
        .target_profile = "lazer-v128", .loss_policy = loss_policy_t::warn};
    require_span(!prepare_conversion_slice_sources(song, request, fixture.directory));
    const auto converted = convert_document(song, request);
    require_span(static_cast<bool>(converted));
    require_span(converted.outputs.size() == 1);
    std::filesystem::create_directories(fixture.directory / "out");
    require_span(!render_conversion_slice_plans(song, converted,
        {fixture.directory, fixture.directory / "out", {}}));
    const auto& bytes = converted.outputs.front().bytes;
    const auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    require_span(parsed.value.has_value());
    check_pcm_tolerance(sliced_preview_pcm(song, fixture.directory),
        sliced_preview_pcm(*parsed.value, fixture.directory / "out"));
    check_span(song.charts.front().sound_channels.size() == 1);
}

namespace {
auto authored_note_count(const song_document_t& song) -> std::size_t {
    std::size_t count{};
    for (const auto& measure : song.charts.front().measures) count += measure.notes.size();
    return count;
}

void check_native_conversion_pcm(bool off_period, bool bms = false) {
    render_fixture_t fixture;
    std::vector<float> samples(48'000 * 2);
    for (std::size_t frame = 0; frame < 48'000; ++frame) {
        const auto value = 1.2F * std::sin(static_cast<float>(frame) * 0.01F);
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = -value;
    }
    const auto encoded = audio::encode_wav_float32(samples, 48'000);
    std::ofstream stream(fixture.directory / "stem.wav", std::ios::binary | std::ios::trunc);
    stream.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    stream.close();
    const auto song = sliced_pcm_song(off_period);
    conversion_request_t request{.target = bms ? format_family_t::bms : format_family_t::osu_mania,
        .target_profile = bms ? "bme-7k-outside-scratch" : "lazer-v128", .loss_policy = loss_policy_t::warn};
    require_span(!prepare_conversion_slice_sources(song, request, fixture.directory));
    require_span(request.slice_sources.front().native_sample_rate == 48'000);
    const auto converted = convert_document(song, request);
    require_span(static_cast<bool>(converted));
    if (off_period) require_span(converted.slice_plans.front().value.spans.at(1).begin_frame % 147 != 0);
    std::filesystem::create_directories(fixture.directory / "out");
    require_span(!render_conversion_slice_plans(song, converted,
        {fixture.directory, fixture.directory / "out", {}}));
    const auto& bytes = converted.outputs.front().bytes;
    const auto raw = std::as_bytes(std::span{bytes.data(), bytes.size()});
    const auto extension = std::filesystem::path{converted.outputs.front().suggested_filename}.extension().string();
    const auto reimported = bms ? parse_bms_bytes(raw, {.file_extension = extension, .lane_profile_id = request.target_profile}).song : parse_osu_mania_bytes(raw).value;
    require_span(reimported.has_value());
    require_span(authored_note_count(*reimported) == authored_note_count(song));
    check_pcm_tolerance(sliced_preview_pcm(song, fixture.directory),
        sliced_preview_pcm(*reimported, fixture.directory / "out"));
}
} // namespace

TEST_CASE("sliced conversion preserves native-rate float source overshoots in preview",
          "[slice_spans][audio][conversion][sliced-conversion]") {
    check_native_conversion_pcm(false);
    check_native_conversion_pcm(true);
}

TEST_CASE("sliced BMS conversion preserves native-rate and off-period preview PCM",
          "[slice_spans][audio][conversion][sliced-conversion]") {
    check_native_conversion_pcm(false, true);
    check_native_conversion_pcm(true, true);
}

TEST_CASE("BMS silent gameplay materializes one zero float sample",
          "[slice_spans][audio][conversion][sliced-conversion]") {
    render_fixture_t fixture;
    auto song = sliced_pcm_song();
    auto& chart = song.charts.front();
    chart.sound_channels.clear();
    for (auto& lane : chart.bgm_source_lanes) lane.stem_channel.reset();
    for (auto& measure : chart.measures) {
        measure.bgm_events.clear();
        for (auto& note : measure.notes) { note.sound.reset(); note.sound_channel_event.reset(); }
    }
    const auto converted = convert_document(song,
        {.target = format_family_t::bms, .loss_policy = loss_policy_t::warn});
    require_span(static_cast<bool>(converted));
    require_span(converted.silent_files.size() == 1);
    std::filesystem::create_directory(fixture.directory / "out");
    require_span(!render_conversion_slice_plans(song, converted,
        {fixture.directory, fixture.directory / "out", {}}));
    audio::decoder_t decoder;
    require_span(decoder.open(fixture.directory / "out" / converted.silent_files.front()));
    require_span(decoder.info()->output_frame_count == 1);
    std::array<float, 2> sample{1, 1};
    require_span(decoder.read_at(0, sample, 1).frames_read == 1);
    check_span(sample[0] == 0 && sample[1] == 0);
}

TEST_CASE("slice render expected failures retain metadata with an empty message", "[unit][slice-spans][results]") {
    slice_span_render_outcome_t outcome = std::unexpected(slice_span_render_payload_t::from({}, asset_id_t{7}));
    require_span(!outcome.has_value());
    check_span(outcome.error().error.empty());
    check_span(outcome.error().asset == asset_id_t{7});
}

TEST_CASE("slice primary render publishes canonical planned audio", "[unit][slice-spans][results]") {
    render_fixture_t fixture;
    const auto plan = slice_span_plan_payload_t::from(fixture.song, chart_id_t{3}, fixture.sources);
    require_span(plan.ok());
    auto rendered = create_slice_span_files(fixture.song, slice_span_render_input_t::from(plan),
        {.source_directory = fixture.directory, .output_directory = fixture.directory / "canonical"});
    require_span(rendered.has_value());
    require_span(!rendered->files.empty());
    check_span(std::filesystem::exists(rendered->files.front()));
}
