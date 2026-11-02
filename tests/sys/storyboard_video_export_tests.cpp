#include "sys/storyboard/storyboard_video_export.h"
#include "core/text_util.h"
#include "sys/notifications/notification_history_runtime.h"
#include "../support/temporary_directory.h"
#include <wormedit/audio/slice_render.h>
#include <wormedit/core/atomic_file_storage.h>
#include <wormedit/formats/osu_mania.h>
#include <cstdlib>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <latch>
#include <mutex>
#include <thread>

using namespace wormedit;
using namespace wormedit::sys;
#if defined(WORMEDIT_HAS_AUDIO_DECODER) && defined(WORMEDIT_HAS_IMAGE_DECODER)
namespace {
auto video_request() -> storyboard_video_request_t {
    storyboard_video_request_t request;
    request.document.id = song_id_t{11}; request.document.revision = 7;
    request.document.charts = {{.id = chart_id_t{12}, .divisions_per_standard_measure = 192,
        .measures = {{.id = measure_id_t{13}}}}};
    request.document.storyboards = {{.id = storyboard_id_t{14}, .osbx = osbx_document_t{
        .nodes = {{.id = "group", .tracks = {.opacity = {.default_value = {0.25}}}}}}}};
    request.chart = chart_id_t{12};
    request.identity = {.stamp = document_stamp_t::from(request.document), .storyboard = storyboard_id_t{14}, .session = 5};
    request.selection = ui::storyboard_loop_region_t{.begin_ms = 101, .end_ms = 199};
    return request;
}
struct worker_control_t {
    std::latch entered{1}, release{1};
    bool hold{}, published{}, uhd{};
    std::atomic<bool> released{};
    void unblock() { if (!released.exchange(true)) release.count_down(); }
    std::mutex mutex;
    std::optional<storyboard_video_request_t> captured;
};
std::atomic<worker_control_t*> control;
auto controlled_work(const storyboard_video_request_t& request, std::stop_token stop)
    -> storyboard_video_job_t::work_outcome_t {
    auto& state = *control.load();
    storyboard_video_result_t result{.destination = "snapshot.webm", .total = 6, .estimated_bytes = 512,
        .free_bytes = 4096, .estimated_seconds = 4};
    result.capabilities = {{.width = 1920, .height = 1080, .available = true},
        {.width = 3840, .height = 2160, .available = state.uhd, .reason = state.uhd ? "" : "linked encoder refused UHD"}};
    if (request.probe) return result;
    { std::lock_guard lock{state.mutex}; state.captured = request; }
    state.entered.count_down();
    if (state.hold) state.release.wait();
    if (stop.stop_requested() && !state.published)
        return std::unexpected(wormedit::error_t{failure_code_t::cancelled, std::string{"Export cancelled before publication"}});
    result.encoded = media::video_encoder_report_t{.video_frames = 6};
    return result;
}
auto settle(storyboard_video_export_t& runtime, bool probe) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    do {
        runtime.poll();
        if (probe ? !runtime.view().probing : !runtime.is_busy()) return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
struct video_runtime_fixture_t {
    worker_control_t state;
    task_scheduler_t scheduler{1, 4};
    notification_center_t notifications;
    storyboard_video_export_t runtime{scheduler, notification_sink_t{notifications}, controlled_work};
    video_runtime_fixture_t() { control.store(&state); }
    ~video_runtime_fixture_t() { state.unblock(); scheduler.shutdown(); }
    void open(const storyboard_video_request_t& request) {
        runtime.open(request);
        REQUIRE(settle(runtime, true));
    }
};
struct video_view_storage_t {
    const void *filename, *status, *capabilities, *blockers, *capability_reason, *blocker_reason;
    static auto from(const ui::storyboard_video_export_view_t& view) -> video_view_storage_t {
        return {view.settings.filename.data(), view.status.data(), view.capabilities.data(), view.blockers.data(),
            view.capabilities.empty() ? nullptr : view.capabilities.back().reason.data(),
            view.blockers.empty() ? nullptr : view.blockers.front().data()};
    }
    void check_text(const ui::storyboard_video_export_view_t& view) const {
        CHECK(view.settings.filename.data() == filename);
        CHECK(view.status.data() == status);
        if (!view.capabilities.empty()) CHECK(view.capabilities.back().reason.data() == capability_reason);
        if (!view.blockers.empty()) CHECK(view.blockers.front().data() == blocker_reason);
    }
    void check_lists(const ui::storyboard_video_export_view_t& view) const {
        CHECK(view.capabilities.data() == capabilities);
        CHECK(view.blockers.data() == blockers);
    }
};
}

TEST_CASE("video export UHD availability follows the matching worker probe", "[sys][storyboard-video]") {
    video_runtime_fixture_t fixture;
    auto request = video_request();
    request.settings.width = 3840; request.settings.height = 2160;
    fixture.open(request);
    const auto refused = fixture.runtime.view();
    REQUIRE(refused.capabilities.size() == 2);
    CHECK_FALSE(refused.can_start);
    CHECK(refused.capabilities.back().reason == "linked encoder refused UHD");
    fixture.state.uhd = true;
    request.settings.profile = media::video_profile_t::webm_vp9_opus;
    request.settings.fps = 30;
    fixture.runtime.configure(request.settings);
    REQUIRE(settle(fixture.runtime, true));
    CHECK(fixture.runtime.view().can_start);
}

TEST_CASE("video job owns the document and prepared snapshot while live edits continue", "[sys][storyboard-video][snapshot]") {
    video_runtime_fixture_t fixture;
    fixture.state.hold = true;
    auto request = video_request();
    auto prepared = std::make_shared<std::vector<osbx_lua_prepared_scene_t>>();
    request.prepared = prepared;
    fixture.open(request);
    REQUIRE(fixture.runtime.start(request));
    fixture.state.entered.wait();
    request.document.storyboards.front().osbx->nodes.front().tracks.opacity.default_value = {0.9};
    ++request.document.revision;
    request.prepared = std::make_shared<const std::vector<osbx_lua_prepared_scene_t>>();
    fixture.state.unblock();
    REQUIRE(settle(fixture.runtime, false));
    std::lock_guard lock{fixture.state.mutex};
    REQUIRE(fixture.state.captured);
    CHECK(fixture.state.captured->document.revision == 7);
    CHECK(fixture.state.captured->document.storyboards.front().osbx->nodes.front().tracks.opacity.default_value == std::vector<double>{0.25});
    CHECK(fixture.state.captured->prepared == prepared);
}

TEST_CASE("Warmed video export frame projection retains its owned buffers", "[sys][storyboard-video][frame]") {
    video_runtime_fixture_t fixture;
    auto request = video_request();
    request.settings.filename = std::string(160, 'v') + ".mp4";
    SECTION("available capabilities") {}
    SECTION("blocking reason") { request.chart = {}; }
    fixture.open(request);
    ui::storyboard_video_export_view_t view;
    fixture.runtime.refill(view);
    const auto expected = view;
    const auto storage = video_view_storage_t::from(view);
    for (unsigned frame = 0; frame < 100; ++frame) {
        fixture.runtime.refill(view);
        CHECK(view == expected);
        storage.check_text(view);
        storage.check_lists(view);
    }
}

TEST_CASE("video cancellation retains an authoritative worker publication report and its folder target", "[sys][storyboard-video][cancel]") {
    video_runtime_fixture_t fixture;
    fixture.state.hold = true; fixture.state.published = true;
    const auto request = video_request(); fixture.open(request);
    REQUIRE(fixture.runtime.start(request));
    fixture.state.entered.wait(); fixture.runtime.cancel(); fixture.state.unblock();
    REQUIRE(settle(fixture.runtime, false));
    fixture.notifications.drain_inbox({});
    const auto entries = fixture.notifications.borrowed_snapshot(notification_capacity);
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().severity == notification_severity_t::success);
    REQUIRE(std::holds_alternative<notification_file_t>(entries.front().target));
    CHECK(std::get<notification_file_t>(entries.front().target).path == "snapshot.webm");
}

TEST_CASE("closing the exporting project cancels work and closes its dialog", "[sys][storyboard-video][cancel]") {
    video_runtime_fixture_t fixture; fixture.state.hold = true;
    const auto request = video_request(); fixture.open(request);
    REQUIRE(fixture.runtime.start(request)); fixture.state.entered.wait();
    fixture.runtime.cancel_project(request.document.id);
    CHECK_FALSE(fixture.runtime.view().open);
    fixture.state.unblock(); REQUIRE(settle(fixture.runtime, false));
    CHECK(fixture.runtime.view().status == "Export cancelled before publication");
}

TEST_CASE("video settings stay in bounded project view state without changing the document", "[sys][storyboard-video]") {
    video_runtime_fixture_t fixture;
    auto request = video_request(); fixture.open(request);
    const auto original = request.document;
    auto settings = request.settings;
    settings.fps = 30; settings.layers = storyboard_conditional_layer_t::neither;
    settings.profile = media::video_profile_t::webm_vp9_opus; settings.filename = "retained.webm";
    fixture.runtime.configure(settings); fixture.runtime.close(); fixture.runtime.open(request);
    CHECK(fixture.runtime.view().settings == settings);
    CHECK(request.document.storyboards == original.storyboards);
    CHECK(request.document.revision == original.revision);
    request.document.id = song_id_t{91}; fixture.runtime.close(); fixture.runtime.open(request);
    CHECK(fixture.runtime.view().settings == request.settings);
}

TEST_CASE("video export blocks missing stale and last-good-only Lua preparation", "[sys][storyboard-video][lua]") {
    auto request = video_request();
    auto& source = *request.document.storyboards.front().osbx;
    source.nodes.front().tracks.opacity.lua = {.link = {.id = "opacity-link"}, .begin_ms = 0, .end_ms = 100};
    CHECK_FALSE(storyboard_video_blockers(request).empty());
    osbx_lua_prepared_scene_t prepared{.stamp = request.identity.stamp, .storyboard = request.identity.storyboard,
        .source = source, .scene = std::make_shared<const osbx_document_t>(source),
        .links = {{.node_id = "group", .target = "opacity", .is_last_good = true}}, .complete = true};
    request.prepared = std::make_shared<const std::vector<osbx_lua_prepared_scene_t>>(std::vector{prepared});
    request.prepared_generation = 9;
    const auto last_good = storyboard_video_blockers(request);
    REQUIRE_FALSE(last_good.empty()); CHECK(last_good.front().find("group/opacity") != std::string::npos);
    ++prepared.stamp.revision;
    request.prepared = std::make_shared<const std::vector<osbx_lua_prepared_scene_t>>(std::vector{prepared});
    CHECK_FALSE(storyboard_video_blockers(request).empty());
}


namespace {
auto actual_video_request(const std::filesystem::path& directory, std::uint32_t width, std::uint32_t height)
    -> storyboard_video_request_t {
    const std::string source = "osu file format v14\n[General]\nMode:3\nAudioFilename:tone.wav\n"
        "[Difficulty]\nCircleSize:4\n[TimingPoints]\n0,500,4,1,0,100,1,0\n"
        "[HitObjects]\n64,192,100,1,0,0:0:0:0:\n";
    const auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    REQUIRE(parsed.value);
    auto request = video_request(); request.document = *parsed.value;
    request.chart = request.document.charts.front().id;
    osbx_document_t scene{.images = {{.id = "white", .file = "white.ppm"}}};
    osbx_node_t background{.id = "blue", .kind = osbx_node_kind_t::sprite, .image = "white", .origin = "TopLeft",
        .ops = {{.id = "full", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {640,480}}}}};
    background.tracks.color.default_value = {0,0,1,1}; scene.nodes.push_back(background);
    osbx_node_t sprite{.id = "red", .kind = osbx_node_kind_t::sprite, .layer = osbx_layer_t::pass,
        .image = "white", .origin = "TopLeft", .ops = {{.id = "motion", .kind = osbx_transform_kind_t::matrix,
            .track = {.default_value = {80,0,0,80,100,120}, .keys = {
                {.id = "left", .time_ms = 100, .value = {80,0,0,80,100,120}},
                {.id = "right", .time_ms = 200, .value = {80,0,0,80,200,120}}}}}}};
    sprite.tracks.color.default_value = {1,0,0,1}; scene.nodes.push_back(sprite);
    request.document.storyboards = {{.id = storyboard_id_t{14}, .osbx = std::move(scene)}};
    request.identity.stamp = document_stamp_t::from(request.document);
    request.paths.project = directory;
    request.settings = {.filename = text_util::path_utf8(directory / (std::to_string(height) + ".mp4")),
        .width = width, .height = height, .fps = 30, .selection = true};
    const std::vector<float> pcm(audio::output_sample_rate * 2, 0.2F);
    const auto wav = audio::encode_wav_float32(pcm, audio::output_sample_rate);
    REQUIRE(create_file_atomic(directory / "tone.wav", wav, wav.size()));
    std::ofstream image{directory / "white.ppm", std::ios::binary}; image << "P6\n1 1\n255\n";
    const std::array<unsigned char,3> white{255,255,255};
    image.write(reinterpret_cast<const char*>(white.data()), static_cast<std::streamsize>(white.size()));
    image.close(); REQUIRE(image.good());
    return request;
}
void check_actual_video(const storyboard_video_request_t& request) {
    const auto outcome = run_storyboard_video(request, {});
    INFO((outcome ? "Validated atomic export" : outcome.error().message));
    REQUIRE(outcome); REQUIRE(outcome->encoded);
    CHECK(outcome->encoded->video_frames == 3);
    CHECK(outcome->encoded->audio_sample_rate == audio::output_sample_rate);
    CHECK(std::filesystem::file_size(outcome->destination) > 0);
    CHECK(outcome->estimated_bytes > 0);
}
}

TEST_CASE("Storyboard snapshot exports a real fractional selection at 1080p", "[sys][storyboard-video][integration]") {
    auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    const auto* evidence = std::getenv("WORMEDIT_VID3_EVIDENCE_DIRECTORY");
    const auto path = evidence ? std::filesystem::path{evidence} / "1080p" : directory->path();
    if (evidence) REQUIRE(std::filesystem::create_directories(path));
    check_actual_video(actual_video_request(path, 1920, 1080));
}

TEST_CASE("Storyboard snapshot exports the same real source selection at UHD", "[sys][storyboard-video][integration]") {
    auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    const auto* evidence = std::getenv("WORMEDIT_VID3_EVIDENCE_DIRECTORY");
    const auto path = evidence ? std::filesystem::path{evidence} / "2160p" : directory->path();
    if (evidence) REQUIRE(std::filesystem::create_directories(path));
    check_actual_video(actual_video_request(path, 3840, 2160));
}

namespace {
auto collect_preview_interval(offline_preview_mix_t& mix, std::uint64_t begin_frame, std::uint64_t end_frame)
    -> std::vector<float> {
    std::vector<float> collected;
    auto next = begin_frame;
    const auto result = mix.render_to(begin_frame, end_frame, [&](std::uint64_t begin, std::span<const float> block)
        -> result_t<void, wormedit::error_t<failure_code_t>> {
        if (begin != next) return std::unexpected(wormedit::error_t{failure_code_t::invalid_data, std::string{"Clock discontinuity"}});
        next += block.size() / audio::output_channel_count;
        collected.insert(collected.end(), block.begin(), block.end());
        return {};
    }, {});
    REQUIRE(result);
    CHECK(next == end_frame);
    return collected;
}
}
TEST_CASE("shared offline preparation retains identical native samples for both export consumers", "[sys][storyboard-video][audio]") {
    auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    auto request = actual_video_request(directory->path(), 1920, 1080);
    request.document.charts.front().beat_zero_audio_offset_seconds = {1,4};
    auto retained = offline_preview_mix_t::from(request.document, request.chart, request.paths, false, true);
    auto streamed = offline_preview_mix_t::from(request.document, request.chart, request.paths, false, true);
    REQUIRE(retained); REQUIRE(streamed);
    const auto pcm = (*retained)->render(4454, 8776, {}, {}); REQUIRE(pcm);
    const auto collected = collect_preview_interval(**streamed, 4454, 8776);
    CHECK(collected == pcm->interleaved_stereo);
}

TEST_CASE("UHD disk budget refusal preserves an already confirmed destination", "[sys][storyboard-video][atomic][disk]") {
    auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    auto request = actual_video_request(directory->path(), 3840, 2160);
    request.settings.replace_existing = true;
    request.selection->end_ms = 86400000;
    REQUIRE(create_file_atomic(text_util::path_from_utf8(request.settings.filename), "previous", 8));
    const auto result = run_storyboard_video(request, {});
    REQUIRE_FALSE(result);
    CHECK(result.error().code == failure_code_t::capacity_exceeded);
    std::ifstream file{text_util::path_from_utf8(request.settings.filename), std::ios::binary};
    const std::string bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    CHECK(bytes == "previous");
}

#else
TEST_CASE("Storyboard video export reports the actual decoder-disabled boundary", "[sys][storyboard-video][unavailable]") {
    task_scheduler_t scheduler{1,2};
    storyboard_video_export_t runtime{scheduler};
    const storyboard_video_request_t request;
    const auto result = run_storyboard_video(request, {});
    REQUIRE_FALSE(result);
    CHECK(result.error().code == failure_code_t::unavailable);
    CHECK_FALSE(runtime.view().available);
    CHECK_FALSE(runtime.view().can_start);
}
#endif

namespace {
void check_media_guard(bool value) { CHECK(value); }
void require_media_guard(bool value) { REQUIRE(value); }
}
TEST_CASE("video refuses complete OSBX Animation and Sample nodes before media preparation", "[sys][storyboard-video][osbx][media-guard]") {
    storyboard_video_request_t request;
    request.document.id = song_id_t{1};
    request.document.charts = {{.id = chart_id_t{2}}};
    request.chart = chart_id_t{2};
    request.identity.storyboard = storyboard_id_t{3};
    osbx_node_t animation{.id = "animation", .kind = osbx_node_kind_t::animation, .image = "frames"};
    animation.animation = storyboard_animation_t{.frame_count = 2, .frame_delay_ms = 100};
    request.document.storyboards = {{.id = storyboard_id_t{3}, .osbx = osbx_document_t{
        .images = {{.id = "frames", .file = "missing-base.png"}}, .nodes = {animation}}}};
    const auto animated = storyboard_video_blockers(request);
    require_media_guard(!animated.empty());
    check_media_guard(animated.front().find("Animation") != std::string::npos);
    auto& node = request.document.storyboards.front().osbx->nodes.front();
    node.kind = osbx_node_kind_t::sample;
    node.image.reset(); node.animation.reset(); node.sample.emplace();
    node.sample->path = "missing-sample.wav";
    const auto sampled = storyboard_video_blockers(request);
    require_media_guard(!sampled.empty());
    check_media_guard(sampled.front().find("Sample") != std::string::npos);
}
