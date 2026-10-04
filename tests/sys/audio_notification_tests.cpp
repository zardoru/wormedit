#include "sys/audio_source_registration.h"
#include "sys/waveform_analysis_runtime.h"

#include <catch2/catch_test_macros.hpp>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <chrono>
#include <thread>
#include <source_location>

namespace {
void check_audio_worker(bool value,
    std::source_location where = std::source_location::current()) {
    INFO("Audio worker check at " << where.line());
    REQUIRE(value);
}
void service_until_waveform_finishes(wormedit::sys::runtime::waveform_analysis_t& runtime,
                                     wormedit::song_document_t& document,
                                     wormedit::workspace_controller_t& workspace,
                                     wormedit::sys::compose_scene_cache_t& cache) {
    runtime.service(document, workspace, cache, nullptr);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (runtime.waveform_job().busy() && std::chrono::steady_clock::now() < deadline) {
        runtime.service(document, workspace, cache, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    runtime.service(document, workspace, cache, nullptr);
}

void wait_for_loader(wormedit::sys::resident_sample_loader_t& loader) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (loader.progress().running && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    REQUIRE_FALSE(loader.progress().running);
}
} // namespace

TEST_CASE("resident fallback and progress carry audio notification metadata once",
          "[sys][audio-notification]") {
    using namespace wormedit;
    song_document_t document;
    document.id = song_id_t{71};
    document.audio_assets.push_back(
        {.id = asset_id_t{5}, .source_path = "keysound-c4.ogg"});
    audio::decoded_source_registry_t sources;
    audio::resident_sample_store_t store{{.budget_bytes = 1}};
    task_scheduler_t scheduler{1, 4};
    sys::resident_sample_loader_t loader{store, scheduler};
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    std::vector<audio::preview_metronome_source_binding_t> metronome;
    check_audio_worker(sys::register_audition_sources(document, base, 1, sources, metronome) == 0);
    loader.start(document, base, 1, sources);
    wait_for_loader(loader);

    const auto progress = loader.drain_progress();
    check_audio_worker(progress.has_value());
    check_audio_worker(progress->severity == notification_severity_t::hint);
    check_audio_worker(progress->source == notification_source_t::audio);
    check_audio_worker(progress->message == "Loaded 1 keysound(s) into memory.");
    check_audio_worker(!(loader.drain_progress()));

    const auto fallback = loader.drain_report();
    check_audio_worker(fallback.has_value());
    check_audio_worker(fallback->severity == notification_severity_t::warning);
    check_audio_worker(fallback->source == notification_source_t::audio);
    check_audio_worker(fallback->message ==
          "1 keysound(s) exceed the resident memory budget and keep streaming from disk.");
    check_audio_worker(!(loader.drain_report()));
}

TEST_CASE("waveform failures post once and asset changes or resets permit a fresh result",
          "[sys][audio-notification]") {
    using namespace wormedit;
    notification_center_t center;
    notification_sink_t sink{center};
    audio::decoded_source_registry_t sources;
    sys::runtime::waveform_analysis_t runtime{sources, nullptr, sink};
    song_document_t document;
    workspace_controller_t workspace;
    sys::compose_scene_cache_t cache;
    workspace.transport.audio_asset = asset_id_t{42};

    service_until_waveform_finishes(runtime, document, workspace, cache);
    center.drain_inbox(notification_time_t{});
    auto entries = center.snapshot(8);
    check_audio_worker(entries.size() == 1);
    check_audio_worker(entries.front().severity == notification_severity_t::error);
    check_audio_worker(entries.front().source == notification_source_t::audio);
    check_audio_worker(entries.front().message ==
          "Waveform analysis failed: the audio source is unavailable.");

    runtime.service(document, workspace, cache, nullptr);
    center.drain_inbox(notification_time_t{});
    check_audio_worker(center.snapshot(8).size() == 1);

    runtime.reset_sources();
    service_until_waveform_finishes(runtime, document, workspace, cache);
    center.drain_inbox(notification_time_t{});
    entries = center.snapshot(8);
    check_audio_worker(entries.size() == 1);
    check_audio_worker(entries.front().count == 2);

    workspace.transport.audio_asset = asset_id_t{43};
    service_until_waveform_finishes(runtime, document, workspace, cache);
    center.drain_inbox(notification_time_t{});
    entries = center.snapshot(8);
    check_audio_worker(entries.size() == 1);
    check_audio_worker(entries.front().count == 3);

    workspace.transport.audio_asset.reset();
    runtime.service(document, workspace, cache, nullptr);
    center.drain_inbox(notification_time_t{});
    entries = center.snapshot(8);
    check_audio_worker(entries.size() == 1);
    check_audio_worker(entries.front().count == 3);
}

TEST_CASE("waveform runtime without a notification sink remains safe",
          "[sys][audio-notification]") {
    using namespace wormedit;
    audio::decoded_source_registry_t sources;
    sys::runtime::waveform_analysis_t runtime{sources};
    song_document_t document;
    workspace_controller_t workspace;
    sys::compose_scene_cache_t cache;
    workspace.transport.audio_asset = asset_id_t{44};
    service_until_waveform_finishes(runtime, document, workspace, cache);
    check_audio_worker(!(runtime.waveform_job().busy()));
    runtime.reset_sources();
    check_audio_worker(!(runtime.waveform_asset().has_value()));
}

TEST_CASE("superseded waveform work is silent after the selected source clears",
          "[sys][audio-notification]") {
    using namespace wormedit;
    notification_center_t center;
    audio::decoded_source_registry_t sources;
    song_document_t document;
    document.audio_assets.push_back(
        {.id = asset_id_t{55}, .source_path = "keysound-c4.ogg"});
    std::vector<audio::preview_metronome_source_binding_t> metronome;
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    check_audio_worker(sys::register_audition_sources(document, base, 1, sources, metronome) == 0);

    sys::runtime::waveform_analysis_t runtime{sources, nullptr, notification_sink_t{center}};
    workspace_controller_t workspace;
    sys::compose_scene_cache_t cache;
    workspace.transport.audio_asset = asset_id_t{55};
    runtime.service(document, workspace, cache, nullptr);
    workspace.transport.audio_asset.reset();
    runtime.service(document, workspace, cache, nullptr);
    center.drain_inbox(notification_time_t{});
    check_audio_worker(center.size() == 0);
}

TEST_CASE("Waveform task failures use native metadata while cancellations stay quiet",
          "[sys][audio-notification]") {
    using namespace wormedit;
    audio::waveform_job_completion_t completed{
        .task_error = task_error_t{task_error_code_t::task_threw, "Decoder task exception."}};
    const auto failure = sys::runtime::waveform_completion_notification(completed);
    check_audio_worker(failure.has_value());
    check_audio_worker(failure->severity == notification_severity_t::error);
    check_audio_worker(failure->source == notification_source_t::audio);
    check_audio_worker(failure->message == "Decoder task exception.");
    completed.task_error->code = task_error_code_t::cancelled;
    check_audio_worker(!sys::runtime::waveform_completion_notification(completed));
    completed.task_error.reset();
    auto cancelled = std::make_shared<audio::waveform_analysis_result_t>();
    cancelled->status = audio::waveform_analysis_status_t::cancelled;
    completed.analysis = cancelled;
    check_audio_worker(!sys::runtime::waveform_completion_notification(completed));
}

TEST_CASE("Failed waveform start is consumed once until selection changes",
          "[sys][audio-notification]") {
    using namespace wormedit;
    notification_center_t center;
    audio::decoded_source_registry_t sources;
    sys::runtime::waveform_analysis_t runtime{sources, nullptr, notification_sink_t{center}};
    workspace_controller_t workspace;
    song_document_t document;
    sys::compose_scene_cache_t cache;
    workspace.transport.audio_asset = asset_id_t{0};
    runtime.service(document, workspace, cache, nullptr);
    runtime.service(document, workspace, cache, nullptr);
    center.drain_inbox(notification_time_t{});
    check_audio_worker(center.statistics().posted == 1);
    workspace.transport.audio_asset.reset();
    runtime.service(document, workspace, cache, nullptr);
    workspace.transport.audio_asset = asset_id_t{0};
    runtime.service(document, workspace, cache, nullptr);
    center.drain_inbox(notification_time_t{});
    check_audio_worker(center.statistics().posted == 2);
}

TEST_CASE("Canceled resident loading has no terminal or progress notification",
          "[sys][audio-notification]") {
    using namespace wormedit;
    song_document_t document;
    document.id = song_id_t{71};
    document.audio_assets.push_back({.id = asset_id_t{5}, .source_path = "keysound-c4.ogg"});
    audio::decoded_source_registry_t sources;
    audio::resident_sample_store_t store;
    task_scheduler_t scheduler{1, 4};
    sys::resident_sample_loader_t loader{store, scheduler};
    loader.start(document, std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio", 1, sources);
    loader.cancel();
    check_audio_worker(!loader.drain_progress());
    check_audio_worker(!loader.drain_report());
}

TEST_CASE("Waveform recovers the same asset after deferred probing and source reset",
          "[sys][audio-notification]") {
    using namespace wormedit;
    notification_center_t center;
    audio::decoded_source_registry_t sources;
    sys::runtime::waveform_analysis_t runtime{sources, nullptr, notification_sink_t{center}};
    song_document_t document;
    document.id = song_id_t{71};
    document.audio_assets.push_back({.id = asset_id_t{55}, .source_path = "keysound-c4.ogg"});
    std::vector<audio::preview_metronome_source_binding_t> metronome;
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    workspace_controller_t workspace;
    workspace.transport.audio_asset = asset_id_t{55};
    sys::compose_scene_cache_t cache;
    service_until_waveform_finishes(runtime, document, workspace, cache);
    check_audio_worker(runtime.waveform_analysis() && !runtime.waveform_analysis()->waveform);
    check_audio_worker(sys::register_audition_sources_deferred(document, base, 1, sources, metronome) == 0);

    while (sources.probe_next_pending()) {}
    check_audio_worker(sources.binding(asset_id_t{55}).has_value());
    service_until_waveform_finishes(runtime, document, workspace, cache);
    check_audio_worker(runtime.waveform_analysis() && runtime.waveform_analysis()->waveform.has_value());
    runtime.reset_sources();
    service_until_waveform_finishes(runtime, document, workspace, cache);
    check_audio_worker(runtime.waveform_analysis() && runtime.waveform_analysis()->waveform.has_value());
    center.drain_inbox(notification_time_t{});
    check_audio_worker(center.statistics().posted == 1);
}

TEST_CASE("Pending source probing completes before waveform analysis without a false error",
          "[sys][audio-notification]") {
    using namespace wormedit;
    notification_center_t center;
    audio::decoded_source_registry_t sources;
    song_document_t document;
    document.id = song_id_t{71};
    document.audio_assets.push_back({.id = asset_id_t{55}, .source_path = "keysound-c4.ogg"});
    std::vector<audio::preview_metronome_source_binding_t> metronome;
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    check_audio_worker(sys::register_audition_sources_deferred(document, base, 1, sources, metronome) == 0);
    sys::runtime::waveform_analysis_t runtime{sources, nullptr, notification_sink_t{center}};
    workspace_controller_t workspace;
    workspace.transport.audio_asset = asset_id_t{55};
    sys::compose_scene_cache_t cache;
    service_until_waveform_finishes(runtime, document, workspace, cache);
    check_audio_worker(runtime.waveform_analysis() && runtime.waveform_analysis()->waveform.has_value());
    center.drain_inbox(notification_time_t{});
    check_audio_worker(center.statistics().posted == 0);
}
#endif
