#include "sys/preview_audio_export.h"
#include <wormedit/audio/ogg_encoder.h>
#include <wormedit/formats/osu_mania.h>
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <thread>

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
