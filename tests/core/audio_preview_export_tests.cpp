#include "sys/audio/preview_audio_export.h"
#include <wormedit/audio/ogg_encoder.h>
#include <wormedit/audio/slice_render.h>
#include <wormedit/audio/decoder.h>
#include <array>
#include <cmath>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/bmson.h>
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <algorithm>

namespace {
using namespace wormedit;
auto preview_request(const std::filesystem::path& destination) -> sys::preview_audio_export_request_t {
    const std::string source = "osu file format v14\n[General]\nMode:3\nAudioFilename:sliced-seek-source.ogg\n"
        "[Difficulty]\nCircleSize:4\n[TimingPoints]\n0,500,4,1,0,100,1,0\n"
        "[HitObjects]\n64,192,1000,1,2,0:0:0:0:\n192,192,20000,1,0,0:0:0:0:\n";
    auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    REQUIRE(parsed.value);
    const auto& chart = parsed.value->charts.front();
    const auto index = measure_index_t::from(chart);
    REQUIRE(index);
    return {.document = *parsed.value,
        .settings = {.project = parsed.value->id.value, .chart = chart.id.value,
            .revision = parsed.value->revision, .end_row = index->total_rows,
            .filename = destination.string(), .duration_seconds = 20, .fade_out_ms = 100},
        .asset_base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio",
        .bundle_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR}.parent_path().parent_path() / "assets/hitsounds"};
}
auto continuation_preview_request(const std::filesystem::path& destination)
    -> sys::preview_audio_export_request_t {
    const std::string source = R"({"version":"1.0.0","info":{"title":"Cut continuations","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[{"name":"sliced-seek-source.ogg","notes":[{"x":1,"y":0,"c":false},{"x":1,"y":240,"c":true}]}]})";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    REQUIRE((parsed.ok() && parsed.value));
    auto request = preview_request(destination);
    request.document = *parsed.value;
    const auto& chart = request.document.charts.front();
    const auto index = measure_index_t::from(chart);
    REQUIRE(index);
    request.settings.project = request.document.id.value;
    request.settings.chart = chart.id.value;
    request.settings.revision = request.document.revision;
    request.settings.end_row = index->total_rows;
    request.settings.duration_seconds = 2;
    return request;
}

void write_preview_test_source(const std::filesystem::path& path, std::size_t frames, float level) {
    const std::vector<float> pcm(frames * 2, level);
    const auto bytes = audio::encode_wav_float32(pcm, audio::output_sample_rate);
    REQUIRE(create_file_atomic(path, bytes, bytes.size()));
}
void check_effective_preview_audio(const std::filesystem::path& path) {
    CHECK(audio::verify_ogg_vorbis(path, 33'075));
    audio::decoder_t decoder;
    REQUIRE(decoder.open(path));
    std::array<float, 64> pcm{};
    const auto read = decoder.read_frames_at(4410, pcm, 32);
    REQUIRE(read.frames_read == 32);
    CHECK(std::abs(pcm.front()) > 0.2F);
}

void finish(sys::runtime::preview_audio_export_t& runtime, const document_history_t& history,
             const workspace_controller_t& workspace) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (runtime.is_busy() && std::chrono::steady_clock::now() < deadline) {
        (void)runtime.poll(history, workspace);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE_FALSE(runtime.is_busy());
}
void check_original_output(const std::filesystem::path& directory, const std::filesystem::path& path) {
    CHECK(std::filesystem::file_size(path) == 4);
    CHECK(std::distance(std::filesystem::directory_iterator{directory}, std::filesystem::directory_iterator{}) == 1);
}
void publish_verified_cut_preview(const sys::preview_audio_export_job_t::work_outcome_t& prepared,
    const std::filesystem::path& path, std::uint64_t frames) {
    INFO((prepared ? "prepared" : prepared.error().message));
    REQUIRE(prepared);
    CHECK_FALSE(std::filesystem::exists(path));
    REQUIRE(prepared->file->publish(false));
    CHECK(audio::verify_ogg_vorbis(path, frames));
}
}

TEST_CASE("Preview export mixes and verifies twenty seconds before publishing the selected output", "[unit][audio][preview-export]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "preview.ogg";
    const auto request = preview_request(path);
    auto prepared = sys::prepare_preview_audio_export(request, {});
    INFO((prepared ? "prepared" : prepared.error().message));
    REQUIRE(prepared);
    CHECK_FALSE(std::filesystem::exists(path));
    REQUIRE(prepared->file->publish(false));
    CHECK(audio::verify_ogg_vorbis(path, 44100 * 20));
}

TEST_CASE("Preview export rejects existing output asset collisions and cancellation without publication", "[unit][audio][preview-export]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "preview.ogg";
    REQUIRE(create_file_atomic(path, "keep", 1024));
    auto request = preview_request(path);
    std::stop_source stop;
    SECTION("existing output") {}
    SECTION("asset collision") {
        request.settings.filename = (request.asset_base / "sliced-seek-source.ogg").string();
        request.settings.replace_existing = true;
    }
    SECTION("cancel") { request.settings.replace_existing = true; stop.request_stop(); }
    CHECK_FALSE(sys::prepare_preview_audio_export(request, stop.get_token()));
    check_original_output(directory->path(), path);
}

TEST_CASE("Preview runtime discards stale and cancelled prepared exports at publication", "[unit][audio][preview-export]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "preview.ogg";
    REQUIRE(create_file_atomic(path, "keep", 1024));
    auto request = preview_request(path);
    request.settings.replace_existing = true;
    request.settings.duration_seconds = 0.1;
    auto history = document_history_t::from(request.document);
    request.settings.project = history.document.id.value;
    request.settings.revision = history.document.revision;
    auto bound = workspace_controller_t::from(history);
    REQUIRE(bound.value);
    auto workspace = std::move(*bound.value);
    task_scheduler_t scheduler{1, 4};
    sys::runtime::preview_audio_export_t runtime{scheduler};
    const auto started = runtime.start(history.document, request.settings, request.asset_base, {}, request.bundle_directory);
    INFO((started ? "started" : started.error().message));
    REQUIRE(started);
    SECTION("stale document") { ++history.document.revision; }
    SECTION("changed chart") { workspace.selected_chart.reset(); }
    SECTION("cancel") { runtime.cancel(); }
    finish(runtime, history, workspace);
    check_original_output(directory->path(), path);
}

TEST_CASE("Preview export permits an authored keysound crossing an export cut", "[unit][audio][preview-export][export-cut]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "crossing.ogg";
    auto request = preview_request(path);
    request.settings.duration_seconds = 2;
    request.document.export_cuts = {{.id = object_id_t{900001}, .begin_milliseconds = 1250, .end_milliseconds = 1500}};
    auto& notes = request.document.charts.front().measures.front().notes;
    REQUIRE_FALSE(notes.empty());
    const auto music = std::ranges::find_if(request.document.resources,
        [](const auto& resource) { return resource.has_role(resource_role_t::music); });
    REQUIRE(music != request.document.resources.end());
    notes.front().sound = music->id;
    const auto prepared = sys::prepare_preview_audio_export(request, {});
    publish_verified_cut_preview(prepared, path, 77'175);
}

TEST_CASE("Preview export verifies its actual frame count after removing source cuts", "[unit][audio][preview-export][export-cut]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "spliced.ogg";
    auto request = preview_request(path);
    request.settings.duration_seconds = 2;
    request.document.export_cuts = {{.id = object_id_t{900001}, .begin_milliseconds = 500, .end_milliseconds = 750}};
    const auto prepared = sys::prepare_preview_audio_export(request, {});
    INFO((prepared ? "prepared" : prepared.error().message));
    REQUIRE(prepared);
    CHECK_FALSE(std::filesystem::exists(path));
    REQUIRE(prepared->file->publish(false));
    CHECK(audio::verify_ogg_vorbis(path, 77'175));
}

TEST_CASE("Preview cut preflight materializes BMSON continuations through shared conversion preparation", "[unit][audio][preview-export][export-cut]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "continuation.ogg";
    auto request = continuation_preview_request(path);
    request.document.export_cuts = {{.id = object_id_t{900001}, .begin_milliseconds = 0, .end_milliseconds = 500}};
    const auto prepared = sys::prepare_preview_audio_export(request, {});
    INFO((prepared ? "prepared" : prepared.error().message));
    REQUIRE(prepared);
    REQUIRE(prepared->file->publish(false));
    CHECK(audio::verify_ogg_vorbis(path, 66'150));
    CHECK_FALSE(request.document.charts.front().sound_channels.empty());
}

TEST_CASE("Preview cut preflight permits a BMSON source slice crossing a cut", "[unit][audio][preview-export][export-cut]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "crossing-continuation.ogg";
    auto request = continuation_preview_request(path);
    request.document.export_cuts = {{.id = object_id_t{900001}, .begin_milliseconds = 250, .end_milliseconds = 375}};
    const auto prepared = sys::prepare_preview_audio_export(request, {});
    publish_verified_cut_preview(prepared, path, 82'687);
}

TEST_CASE("Cut preview audio renders effective chart Music instead of a different global source", "[unit][audio][preview-export][export-cut]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "effective-preview.ogg";
    auto request = preview_request(path);
    request.asset_base = directory->path();
    request.settings.duration_seconds = 1;
    request.document.audio_assets.front().source_path = "global.wav";
    request.document.audio_assets.push_back({.id = asset_id_t{900010}, .source_path = "override.wav"});
    auto& chart = request.document.charts.front();
    chart.metadata.overrides.value(inherited_metadata_field_t::music) = "override.wav";
    for (auto& measure : chart.measures) measure.notes.clear();
    request.document.export_cuts = {{.id = object_id_t{900001}, .begin_milliseconds = 250, .end_milliseconds = 500}};
    write_preview_test_source(directory->path() / "global.wav", 4410, 0.0F);
    write_preview_test_source(directory->path() / "override.wav", 88200, 0.6F);
    const auto prepared = sys::prepare_preview_audio_export(request, {});
    INFO((prepared ? "prepared" : prepared.error().message));
    REQUIRE(prepared);
    REQUIRE(prepared->file->publish(false));
    check_effective_preview_audio(path);
}
