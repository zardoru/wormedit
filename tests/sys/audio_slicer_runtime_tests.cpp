#include "sys/audio/audio_slicer_runtime.h"
#include "../support/temporary_directory.h"
#include <wormedit/audio/decoder.h>
#include <wormedit/formats/osu_mania.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <thread>
using namespace wormedit;
namespace {
auto slicer_document() -> song_document_t {
    const std::string source = "osu file format v14\n[General]\nMode:3\nAudioFilename:keysound-c4.ogg\n"
        "[Difficulty]\nCircleSize:4\n[TimingPoints]\n0,500,4,1,0,100,1,0\n"
        "[HitObjects]\n64,192,100,1,0,0:0:0:0:keysound-c4.ogg\n";
    auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    REQUIRE(parsed.value);
    return std::move(*parsed.value);
}
auto slicer_source() -> std::filesystem::path {
    return std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio/keysound-c4.ogg";
}
void finish_slicing(sys::runtime::audio_slicer_t& runtime, document_history_t& history,
    workspace_controller_t& workspace) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (runtime.is_busy() && std::chrono::steady_clock::now() < deadline) {
        runtime.poll(history, workspace);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE_FALSE(runtime.is_busy());
}
void check_preview(const ui::audio_slicer_settings_t& settings) {
    auto plan = sys::build_audio_slicer_plan(settings, 48'000, 48'000);
    REQUIRE(plan);
    REQUIRE(plan->ranges.size() == 9);
    CHECK(plan->ranges.front().end_frame == 3'000);
    ui::audio_slicer_view_t view;
    sys::refill_audio_slicer_view(view, settings, 48'000, 48'000, true);
    CHECK(view.can_slice);
    REQUIRE(view.cut_frames.size() == 8);
    CHECK(view.cut_frames.front() == 3'000);
}
void check_slice(const bulk_sound_import_entry_t& entry) {
    CHECK(entry.kind == sound_kind_t::keysound);
    audio::decoder_t decoder;
    REQUIRE(decoder.open(entry.asset.source_path));
    CHECK(decoder.info()->output_frame_count == entry.asset.audio_stream->frame_count);
}
auto render_unpublished(const sys::audio_slicer_request_t& request) -> std::filesystem::path {
    auto prepared = sys::prepare_audio_slices(request, {});
    INFO((prepared ? "prepared" : prepared.error().message));
    REQUIRE(prepared);
    const auto output = (*prepared)->directory;
    REQUIRE_FALSE((*prepared)->import.entries.empty());
    for (const auto& entry : (*prepared)->import.entries) check_slice(entry);
    return output;
}
void check_discarded(const document_history_t& history, std::size_t before, const std::filesystem::path& directory) {
    CHECK(history.document.resources.size() == before);
    CHECK(history.undo.empty());
    CHECK(std::filesystem::is_empty(directory / "assets"));
}
void check_imported(document_history_t& history, std::size_t before) {
    REQUIRE(history.document.resources.size() > before);
    REQUIRE(history.undo.size() == 1);
    const auto imported = history.document.audio_assets.back().source_path;
    CHECK(std::filesystem::exists(imported));
    CHECK(undo_document_command(history));
    CHECK(history.document.resources.size() == before);
    CHECK(std::filesystem::exists(imported));
}
}
TEST_CASE("Slicer controls validate exact parameters and preview the native-rate cut grid", "[unit][audio][slicer-ui]") {
    ui::audio_slicer_settings_t settings{.bpm = "120", .division = "4", .offset = "-0.0625"};
    check_preview(settings);
    SECTION("zero tempo") { settings.bpm = "0"; }
    SECTION("nonfinite tempo") { settings.bpm = "nan"; }
    SECTION("fractional division") { settings.division = "1.5"; }
    SECTION("excessive division") { settings.division = "1025"; }
    SECTION("invalid offset") { settings.offset = "--1"; }
    SECTION("capacity bound") { settings.bpm = "60000"; settings.division = "4"; }
    CHECK_FALSE(sys::build_audio_slicer_plan(settings, 48'000, 48'000));
}
TEST_CASE("Slicer background rendering cleans unpublished WAVs and preserves its source", "[unit][audio][slicer-ui]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto original_size = std::filesystem::file_size(slicer_source());
    sys::audio_slicer_request_t request{.settings = {.bpm = "600"}, .source = slicer_source(),
        .project_directory = directory->path(), .relative_directory = "audio", .chart = chart_id_t{1},
        .progress = std::make_shared<job_progress_t>()};
    const auto output = render_unpublished(request);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK(std::filesystem::file_size(slicer_source()) == original_size);
    std::stop_source stop;
    stop.request_stop();
    CHECK_FALSE(sys::prepare_audio_slices(request, stop.get_token()));
    CHECK(std::filesystem::is_empty(directory->path() / "audio"));
}
TEST_CASE("Slicer imports one batch with Undo and discards stale or cancelled work", "[unit][audio][slicer-ui]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    std::filesystem::copy_file(slicer_source(), directory->path() / "keysound-c4.ogg");
    auto history = document_history_t::from(slicer_document());
    const auto before = history.document.resources.size();
    auto bound = workspace_controller_t::from(history);
    REQUIRE(bound.value);
    auto workspace = std::move(*bound.value);
    const auto& resource = history.document.resources.front();
    ui::audio_slicer_action_t action{.project = history.document.id, .chart = history.document.charts.front().id,
        .resource = resource.id, .asset = resource.asset, .revision = history.document.revision,
        .settings = {.bpm = "600"}};
    task_scheduler_t scheduler{1, 1};
    sys::runtime::audio_slicer_t runtime{scheduler};
    REQUIRE(runtime.start(history.document, action, directory->path()));
    bool discard{};
    SECTION("accepted") {}
    SECTION("stale revision") { ++history.document.revision; discard = true; }
    SECTION("changed chart") { workspace.selected_chart.reset(); discard = true; }
    SECTION("cancelled") { runtime.cancel(); discard = true; }
    finish_slicing(runtime, history, workspace);
    if (discard) check_discarded(history, before, directory->path());
    else check_imported(history, before);

}
