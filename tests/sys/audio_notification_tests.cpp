#include "sys/audio_source_registration.h"
#include "sys/waveform_analysis_runtime.h"

#include <catch2/catch_test_macros.hpp>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <chrono>
#include <thread>
#include <source_location>
#include <array>
#include <memory>
#include <vector>

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
    while (runtime.waveform_job().is_busy() && std::chrono::steady_clock::now() < deadline) {
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

struct stem_pool_fixture_t {
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::song_document_t document;
    wormedit::workspace_controller_t workspace;
    wormedit::editor_interaction_state_t interaction;
    wormedit::sys::compose_scene_cache_t cache;
    std::vector<const wormedit::sound_channel_t*> channels;

    stem_pool_fixture_t() {
        using namespace wormedit;
        document.id = song_id_t{80};
        document.revision = 1;
        chart_document_t chart;
        chart.id = chart_id_t{81};
        chart.source_family = format_family_t::bmson;
        chart.measures.push_back({.id = measure_id_t{1}});
        for (std::uint64_t index = 0; index < 4; ++index) {
            const auto asset = asset_id_t{90 + index};
            document.audio_assets.push_back({.id = asset,
                .source_path = index == 3 ? "missing-stem-test.ogg" : "silence.ogg"});
            chart.sound_channels.push_back({.id = object_id_t{100 + index},
                .name = "Stem", .source_clip = {.id = clip_id_t{110 + index},
                    .source = asset}, .events = {{.id = object_id_t{120 + index},
                    .position = {measure_id_t{1}, 0}, .lane = lane_id_t{1}}}});
            chart.bgm_source_lanes.push_back({.id = object_id_t{130 + index},
                .name = "Stem", .source_order = static_cast<std::size_t>(index),
                .stem_channel = object_id_t{100 + index}});
        }
        document.charts.push_back(std::move(chart));
        auto& stored = document.charts.front().sound_channels;
        for (const auto& channel : stored) channels.push_back(&channel);
        workspace.active_tab = workspace_tab_t::compose;
        workspace.selected_chart = chart_id_t{81};
        interaction.chart = chart_id_t{81};
            const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
        check_audio_worker(sys::register_audition_sources(document, base, 1, sources) == 1);
    }

    auto select(std::initializer_list<std::size_t> indices)
        -> std::vector<const wormedit::sound_channel_t*> {
        std::vector<const wormedit::sound_channel_t*> result;
        for (const auto index : indices) result.push_back(channels.at(index));
        return result;
    }
};

void service_stem_pool(wormedit::sys::runtime::waveform_analysis_t& runtime,
                       stem_pool_fixture_t& fixture,
                       const std::vector<const wormedit::sound_channel_t*>& visible) {
    runtime.service_compose_stem_waveforms(fixture.document, fixture.workspace,
        fixture.cache, &fixture.interaction, visible);
}

void service_stem_until_terminal(wormedit::sys::runtime::waveform_analysis_t& runtime,
                                 stem_pool_fixture_t& fixture,
                                 const std::vector<const wormedit::sound_channel_t*>& visible,
                                 wormedit::asset_id_t target) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto rejected_before = runtime.stem_waveform_cache_stats().rejected_results;
    while (std::chrono::steady_clock::now() < deadline) {
        service_stem_pool(runtime, fixture, visible);
        const auto stats = runtime.stem_waveform_cache_stats();
        if (runtime.stem_waveform_analysis(target) || stats.rejected_results > rejected_before) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE((runtime.stem_waveform_analysis(target) ||
             runtime.stem_waveform_cache_stats().rejected_results > rejected_before));
}

auto one_stem_charge(stem_pool_fixture_t& fixture) -> std::size_t {
    wormedit::sys::runtime::waveform_analysis_t runtime{fixture.sources};
    const auto visible = fixture.select({0});
    service_stem_until_terminal(runtime, fixture, visible, wormedit::asset_id_t{90});
    const auto stats = runtime.stem_waveform_cache_stats();
    REQUIRE(stats.entries == 1);
    return stats.resident_bytes;
}

auto one_stem_charge_for(stem_pool_fixture_t& fixture, std::size_t channel_index)
    -> std::size_t {
    wormedit::sys::runtime::waveform_analysis_t runtime{fixture.sources};
    const auto visible = fixture.select({channel_index});
    service_stem_until_terminal(runtime, fixture, visible,
        wormedit::asset_id_t{90 + channel_index});
    const auto stats = runtime.stem_waveform_cache_stats();
    REQUIRE(stats.entries == 1);
    return stats.resident_bytes;
}

void service_stems_until_entries(wormedit::sys::runtime::waveform_analysis_t& runtime,
                                 stem_pool_fixture_t& fixture,
                                 const std::vector<const wormedit::sound_channel_t*>& visible,
                                 std::size_t expected_entries) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        service_stem_pool(runtime, fixture, visible);
        if (runtime.stem_waveform_cache_stats().entries >= expected_entries) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(runtime.stem_waveform_cache_stats().entries >= expected_entries);
}

auto waveform_capacity_bytes(const wormedit::audio::waveform_analysis_result_t& analysis)
    -> std::size_t {
    if (!analysis.waveform) return 0;
    const auto& waveform = *analysis.waveform;
    auto bytes = sizeof(analysis) +
        waveform.bins.capacity() * sizeof(wormedit::audio::waveform_bin_t) +
        waveform.coarser_levels.capacity() * sizeof(wormedit::audio::waveform_level_t) +
        waveform.key.source.asset_identity.capacity() +
        waveform.key.source.source_identity.capacity() + 2;
    for (const auto& level : waveform.coarser_levels)
        bytes += level.bins.capacity() * sizeof(wormedit::audio::waveform_bin_t);
    return bytes;
}

void service_stem_frames(wormedit::sys::runtime::waveform_analysis_t& runtime,
                         stem_pool_fixture_t& fixture,
                         const std::vector<const wormedit::sound_channel_t*>& visible,
                         std::size_t count) {
    for (std::size_t index = 0; index < count; ++index)
    {
        service_stem_pool(runtime, fixture, visible);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void check_retained_stem(const wormedit::sys::runtime::waveform_analysis_t& runtime,
                         const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>& held,
                         const wormedit::sys::runtime::stem_waveform_cache_stats_t& before) {
    check_audio_worker(runtime.stem_waveform_analysis(wormedit::asset_id_t{90}) == held);
    check_audio_worker(runtime.stem_waveform_cache_stats().resident_bytes == before.resident_bytes);
}

void check_stem_budget_charge(
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>& analysis,
    const wormedit::sys::runtime::stem_waveform_cache_stats_t& stats) {
    check_audio_worker(stats.entries == 1);
    check_audio_worker(stats.resident_bytes >= waveform_capacity_bytes(*analysis));
    check_audio_worker(stats.resident_bytes >
        analysis->waveform->bins.size() * sizeof(wormedit::audio::waveform_bin_t));
    check_audio_worker(stats.backing_cached_bytes <= 16U * 1024U * 1024U);
}

void check_reset_kept_external_snapshot(
    wormedit::sys::runtime::waveform_analysis_t& runtime,
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>& held) {
    runtime.reset_sources();
    check_audio_worker(runtime.stem_waveform_cache_stats().entries == 0);
    check_audio_worker(runtime.stem_waveform_cache_stats().resident_bytes == 0);
    check_audio_worker(held->waveform.has_value());
}

void check_lru_eviction(
    const wormedit::sys::runtime::waveform_analysis_t& runtime, std::size_t budget,
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>& retained,
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>& evicted_snapshot) {
    const auto stats = runtime.stem_waveform_cache_stats();
    check_audio_worker(stats.entries == 2);
    check_audio_worker(stats.resident_bytes <= budget);
    check_audio_worker(stats.evictions == 1);
    check_audio_worker(runtime.stem_waveform_analysis(wormedit::asset_id_t{90}) == retained);
    check_audio_worker(!runtime.stem_waveform_analysis(wormedit::asset_id_t{91}));
    check_audio_worker(evicted_snapshot->waveform.has_value());
    check_audio_worker(static_cast<bool>(runtime.stem_waveform_analysis(wormedit::asset_id_t{92})));
}

void check_visible_pressure(
    const wormedit::sys::runtime::waveform_analysis_t& runtime, std::size_t budget,
    const wormedit::sys::runtime::stem_waveform_cache_stats_t& pressured,
    const std::array<std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>, 2>& held) {
    const auto stats = runtime.stem_waveform_cache_stats();
    check_audio_worker(pressured.rejected_results > 0);
    check_audio_worker(stats.rejected_results == pressured.rejected_results);
    check_audio_worker(stats.entries == 2);
    check_audio_worker(stats.resident_bytes <= budget);
    check_audio_worker(runtime.stem_waveform_analysis(wormedit::asset_id_t{90}) == held[0]);
    check_audio_worker(runtime.stem_waveform_analysis(wormedit::asset_id_t{91}) == held[1]);
}

void check_oversized_stem_suppression(
    const wormedit::sys::runtime::waveform_analysis_t& runtime, std::size_t rejected) {
    check_audio_worker(!runtime.stem_waveform_analysis(wormedit::asset_id_t{90}));
    check_audio_worker(static_cast<bool>(runtime.stem_waveform_analysis(wormedit::asset_id_t{93})));
    check_audio_worker(runtime.stem_waveform_cache_stats().rejected_results == rejected);
}

struct mixed_stem_filter_fixture_t {
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::song_document_t document;
    wormedit::workspace_controller_t workspace;
    wormedit::editor_interaction_state_t interaction;
    wormedit::sys::compose_scene_cache_t cache;
    std::vector<const wormedit::sound_channel_t*> channels;
    std::array<wormedit::asset_id_t, 3> stem_assets{};
    std::vector<wormedit::asset_id_t> packed_assets;
};

void append_filter_channel(mixed_stem_filter_fixture_t& fixture,
                           wormedit::chart_document_t& chart, std::uint64_t index) {
    using namespace wormedit;
    const auto asset = asset_id_t{2000 + index};
    const auto resource = resource_id_t{3000 + index};
    const auto channel_id = object_id_t{4000 + index};
    const auto event_id = object_id_t{5000 + index};
    const bool stem = index < 3;
    fixture.document.audio_assets.push_back({.id = asset,
        .source_path = stem ? "silence.ogg" : "missing-packed-" + std::to_string(index) + ".ogg"});
    fixture.document.resources.push_back({.id = resource, .name = "filter fixture",
        .asset = asset, .roles = {resource_role_t::sample, resource_role_t::bgm}});
    const auto row = stem ? std::uint64_t{0} : index - 3;
    sound_channel_t channel{.id = channel_id, .name = "filter fixture",
        .source_clip = {.id = clip_id_t{6000 + index}, .source = asset},
        .events = {{.id = event_id, .position = {measure_id_t{1}, row},
            .playable = stem}}};
    chart.sound_channels.push_back(std::move(channel));
    if (stem) {
        chart.bgm_source_lanes.push_back({.id = object_id_t{7000 + index},
            .name = "Stem", .source_order = static_cast<std::size_t>(index),
            .stem_channel = channel_id});
        fixture.stem_assets[static_cast<std::size_t>(index)] = asset;
        return;
    }
    fixture.packed_assets.push_back(asset);
    chart.measures.front().bgm_events.push_back({.id = object_id_t{8000 + index},
        .position = {measure_id_t{1}, row}, .source_lane = object_id_t{7999},
        .sound = resource, .sound_channel_event = event_id});
}

void populate_mixed_stem_filter_fixture(mixed_stem_filter_fixture_t& fixture) {
    using namespace wormedit;
    fixture.document.id = song_id_t{700};
    fixture.document.revision = 1;
    chart_document_t chart;
    chart.id = chart_id_t{701};
    chart.source_family = format_family_t::bmson;
    chart.measures.push_back({.id = measure_id_t{1}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{7999}, .name = "BGM", .source_order = 3});
    for (std::uint64_t index = 0; index < 1003; ++index)
        append_filter_channel(fixture, chart, index);
    const auto selected = object_id_t{8003};
    fixture.document.charts.push_back(std::move(chart));
    const auto& channels = fixture.document.charts.front().sound_channels;
    for (std::size_t index = 3; index < channels.size(); ++index)
        fixture.channels.push_back(&channels[index]);
    for (std::size_t index = 0; index < 3; ++index)
        fixture.channels.push_back(&channels[index]);
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.workspace.selected_chart = chart_id_t{701};
    fixture.interaction.chart = chart_id_t{701};
    fixture.interaction.selected_objects = {{editor_selection_object_kind_t::bgm_event, selected}};
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    (void)sys::register_audition_sources(fixture.document, base, 1, fixture.sources);
}

auto mixed_filter_subset(const mixed_stem_filter_fixture_t& fixture)
    -> std::vector<const wormedit::sound_channel_t*> {
    return {fixture.channels.front(), fixture.channels.back(), fixture.channels[1],
            fixture.channels[fixture.channels.size() - 2]};
}

auto selected_packed_marker_is_linked(const mixed_stem_filter_fixture_t& fixture) -> bool {
    const auto& chart = fixture.document.charts.front();
    if (chart.measures.front().bgm_events.empty() || fixture.channels.size() != 1003) return false;
    const auto& marker = chart.measures.front().bgm_events.front();
    return marker.sound_channel_event == fixture.channels.front()->events.front().id &&
        marker.source_lane == chart.bgm_source_lanes.front().id;
}

auto mixed_filter_runtime_passes() -> bool {
    using namespace wormedit;
    mixed_stem_filter_fixture_t fixture;
    populate_mixed_stem_filter_fixture(fixture);
    if (fixture.packed_assets.size() != 1000 || !selected_packed_marker_is_linked(fixture))
        return false;
    notification_center_t notifications;
    sys::runtime::waveform_analysis_t runtime{fixture.sources, nullptr,
        notification_sink_t{notifications}};
    runtime.service(fixture.document, fixture.workspace, fixture.cache, &fixture.interaction);
    if (runtime.channel_waveform_asset() || runtime.channel_waveform_job().is_busy() ||
        runtime.channel_waveform_analysis()) return false;
    runtime.service_compose_stem_waveforms(fixture.document, fixture.workspace, fixture.cache,
        &fixture.interaction, mixed_filter_subset(fixture));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (runtime.stem_waveform_cache_stats().entries < fixture.stem_assets.size() &&
           std::chrono::steady_clock::now() < deadline) {
        runtime.service_compose_stem_waveforms(fixture.document, fixture.workspace, fixture.cache,
            &fixture.interaction, fixture.channels);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto stats = runtime.stem_waveform_cache_stats();
    if (stats.entries != fixture.stem_assets.size() || stats.rejected_results != 0) return false;
    for (const auto asset : fixture.stem_assets)
        if (!runtime.stem_waveform_analysis(asset)) return false;
    for (const auto asset : fixture.packed_assets)
        if (runtime.stem_waveform_analysis(asset)) return false;
    notifications.drain_inbox(notification_time_t{});
    return notifications.size() == 0;
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
    check_audio_worker(sys::register_audition_sources(document, base, 1, sources) == 0);
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
          "1 keysound(s) exceed the resident memory budget and keep streaming from disk. "
          "Raise Keysound memory in Options to load them.");
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
    check_audio_worker(!(runtime.waveform_job().is_busy()));
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
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    check_audio_worker(sys::register_audition_sources(document, base, 1, sources) == 0);

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
    audio::waveform_job_completion_outcome_t completed{
        std::unexpected(async_job_completion_error_t<asset_id_t>{
            .cause = {failure_code_t::task_threw, "Decoder task exception."}})};
    const auto failure = sys::runtime::waveform_completion_notification(completed);
    check_audio_worker(failure.has_value());
    check_audio_worker(failure->severity == notification_severity_t::error);
    check_audio_worker(failure->source == notification_source_t::audio);
    check_audio_worker(failure->message == "Decoder task exception.");
    completed.error().cause.code = failure_code_t::cancelled;
    check_audio_worker(!sys::runtime::waveform_completion_notification(completed));
    completed = async_job_completion_value_t<asset_id_t, audio::waveform_analysis_result_t>{};
    check_audio_worker(!sys::runtime::waveform_completion_notification(completed));
    auto cancelled = std::make_shared<audio::waveform_analysis_result_t>();
    cancelled->status = audio::waveform_analysis_status_t::cancelled;
    completed = async_job_completion_value_t<asset_id_t, audio::waveform_analysis_result_t>{
        .payload = std::move(cancelled)};
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
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    workspace_controller_t workspace;
    workspace.transport.audio_asset = asset_id_t{55};
    sys::compose_scene_cache_t cache;
    service_until_waveform_finishes(runtime, document, workspace, cache);
    check_audio_worker(runtime.waveform_analysis() && !runtime.waveform_analysis()->waveform);
    check_audio_worker(sys::register_audition_sources_deferred(document, base, 1, sources) == 0);

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
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    check_audio_worker(sys::register_audition_sources_deferred(document, base, 1, sources) == 0);
    sys::runtime::waveform_analysis_t runtime{sources, nullptr, notification_sink_t{center}};
    workspace_controller_t workspace;
    workspace.transport.audio_asset = asset_id_t{55};
    sys::compose_scene_cache_t cache;
    service_until_waveform_finishes(runtime, document, workspace, cache);
    check_audio_worker(runtime.waveform_analysis() && runtime.waveform_analysis()->waveform.has_value());
    center.drain_inbox(notification_time_t{});
    check_audio_worker(center.statistics().posted == 0);
}

TEST_CASE("BMSON stem analyses retain vector capacity and reuse snapshots across viewport exits",
          "[sys][audio-notification][stem-cache]") {
    using namespace wormedit;
    stem_pool_fixture_t fixture;
    sys::runtime::waveform_analysis_t analysis{fixture.sources};
    const auto visible = fixture.select({0});
    service_stem_until_terminal(analysis, fixture, visible, asset_id_t{90});
    const auto first = analysis.stem_waveform_analysis(asset_id_t{90});
    REQUIRE(first);
    REQUIRE(first->waveform);
    const auto stats = analysis.stem_waveform_cache_stats();
    check_stem_budget_charge(first, stats);

    const std::vector<const sound_channel_t*> offscreen;
    service_stem_pool(analysis, fixture, offscreen);
    service_stem_pool(analysis, fixture, visible);
    check_retained_stem(analysis, first, stats);
    check_reset_kept_external_snapshot(analysis, first);
}

TEST_CASE("BMSON stem cache evicts the least recently viewed offscreen result",
          "[sys][audio-notification][stem-cache]") {
    using namespace wormedit;
    stem_pool_fixture_t fixture;
    const auto entry_bytes = one_stem_charge(fixture);
    const auto budget = entry_bytes * 2;
    sys::runtime::waveform_analysis_t analysis{fixture.sources, nullptr,
                                               notification_sink_t::muted(), budget};
    const auto first_two = fixture.select({0, 1});
    service_stems_until_entries(analysis, fixture, first_two, 2);
    const auto first = analysis.stem_waveform_analysis(asset_id_t{90});
    const auto least_recent = analysis.stem_waveform_analysis(asset_id_t{91});
    REQUIRE(first);
    REQUIRE(least_recent);

    const auto touch_first = fixture.select({0});
    service_stem_pool(analysis, fixture, touch_first);
    const auto third = fixture.select({2});
    service_stem_until_terminal(analysis, fixture, third, asset_id_t{92});
    check_lru_eviction(analysis, budget, first, least_recent);
}

TEST_CASE("BMSON stem cache protects visible analyses and bounds same-window rejection retries",
          "[sys][audio-notification][stem-cache]") {
    using namespace wormedit;
    stem_pool_fixture_t fixture;
    const auto entry_bytes = one_stem_charge(fixture);
    const auto budget = entry_bytes * 2;
    sys::runtime::waveform_analysis_t analysis{fixture.sources, nullptr,
                                               notification_sink_t::muted(), budget};
    const auto first_two = fixture.select({0, 1});
    service_stems_until_entries(analysis, fixture, first_two, 2);
    const std::array retained{analysis.stem_waveform_analysis(asset_id_t{90}),
                              analysis.stem_waveform_analysis(asset_id_t{91})};
    REQUIRE(retained[0]);
    REQUIRE(retained[1]);
    const auto all_visible = fixture.select({0, 1, 2});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (analysis.stem_waveform_cache_stats().rejected_results == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        service_stem_pool(analysis, fixture, all_visible);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto pressured = analysis.stem_waveform_cache_stats();
    REQUIRE(pressured.rejected_results > 0);
    service_stem_frames(analysis, fixture, all_visible, 20);
    check_visible_pressure(analysis, budget, pressured, retained);

    const auto retry_visible = fixture.select({2});
    service_stem_until_terminal(analysis, fixture, retry_visible, asset_id_t{92});
    check_audio_worker(static_cast<bool>(analysis.stem_waveform_analysis(asset_id_t{92})) &&
                       analysis.stem_waveform_cache_stats().resident_bytes <= budget);
}

TEST_CASE("oversized BMSON stem results stay suppressed across viewport changes until reset",
          "[sys][audio-notification][stem-cache]") {
    using namespace wormedit;
    stem_pool_fixture_t fixture;
    const auto normal_bytes = one_stem_charge_for(fixture, 0);
    const auto unavailable_bytes = one_stem_charge_for(fixture, 3);
    REQUIRE(unavailable_bytes < normal_bytes);
    sys::runtime::waveform_analysis_t analysis{fixture.sources, nullptr,
                                               notification_sink_t::muted(), unavailable_bytes};
    const auto oversized_and_small = fixture.select({0, 3});
    service_stem_until_terminal(analysis, fixture, oversized_and_small, asset_id_t{90});
    service_stem_until_terminal(analysis, fixture, oversized_and_small, asset_id_t{93});
    const auto rejected = analysis.stem_waveform_cache_stats().rejected_results;
    REQUIRE(rejected == 1);
    check_oversized_stem_suppression(analysis, rejected);

    const auto oversized_only = fixture.select({0});
    service_stem_frames(analysis, fixture, oversized_only, 8);
    service_stem_pool(analysis, fixture, oversized_and_small);
    service_stem_frames(analysis, fixture, oversized_and_small, 8);
    check_oversized_stem_suppression(analysis, rejected);

    analysis.reset_sources();
    check_audio_worker(analysis.stem_waveform_cache_stats().entries == 0 &&
                       analysis.stem_waveform_cache_stats().resident_bytes == 0);
    service_stem_until_terminal(analysis, fixture, oversized_only, asset_id_t{90});
    check_audio_worker(analysis.stem_waveform_cache_stats().rejected_results == 1 &&
                       !analysis.stem_waveform_analysis(asset_id_t{90}));
}

TEST_CASE("BMSON stem runtime filters packed channels from actual mixed channel requests",
          "[sys][audio-notification][stem-cache][hybrid-lanes]") {
    check_audio_worker(mixed_filter_runtime_passes());
}

namespace {
struct t459_packed_runtime_fixture_t {
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::song_document_t document;
    wormedit::workspace_controller_t workspace;
    wormedit::editor_interaction_state_t interaction;
    wormedit::sys::compose_scene_cache_t cache;
};

void prepare_t459_packed_runtime_fixture(t459_packed_runtime_fixture_t& fixture) {
    using namespace wormedit;
    fixture.document.id = song_id_t{900};
    fixture.document.revision = 1;
    fixture.document.audio_assets = {
        {.id = asset_id_t{90}, .source_path = "silence.ogg"},
        {.id = asset_id_t{91}, .source_path = "silence.ogg"},
        {.id = asset_id_t{92}, .source_path = "sliced-seek-source.ogg"}};
    fixture.document.resources = {
        {.id = resource_id_t{190}, .name = "long packed", .asset = asset_id_t{90}},
        {.id = resource_id_t{191}, .name = "short packed", .asset = asset_id_t{91}},
        {.id = resource_id_t{192}, .name = "offscreen packed", .asset = asset_id_t{92}}};
    chart_document_t chart;
    chart.id = chart_id_t{901};
    chart.source_family = format_family_t::bmson;
    chart.divisions_per_standard_measure = 960;
    for (std::uint64_t measure = 1; measure <= 10; ++measure)
        chart.measures.push_back({.id = measure_id_t{measure}});
    chart.bpms = {{.id = object_id_t{902}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {600, 1}}};
    chart.bgm_source_lanes = {{.id = object_id_t{903}, .name = "BGM 1"}};
    chart.sound_channels = {
        {.id = object_id_t{100}, .source_clip = {.id = clip_id_t{110}, .source = asset_id_t{90}},
         .events = {{.id = object_id_t{1100}, .position = {measure_id_t{1}, 0}}}},
        {.id = object_id_t{101}, .source_clip = {.id = clip_id_t{111}, .source = asset_id_t{91},
             .end_frame = 1},
         .events = {{.id = object_id_t{1101}, .position = {measure_id_t{1}, 8}}}},
        {.id = object_id_t{102}, .source_clip = {.id = clip_id_t{112}, .source = asset_id_t{92}},
         .events = {{.id = object_id_t{1102}, .position = {measure_id_t{8}, 0}}}}};
    chart.measures[0].bgm_events = {
        {.id = object_id_t{1200}, .position = {measure_id_t{1}, 0},
         .source_lane = object_id_t{903}, .sound = resource_id_t{190},
         .sound_channel_event = object_id_t{1100}},
        {.id = object_id_t{1201}, .position = {measure_id_t{1}, 8},
         .source_lane = object_id_t{903}, .sound = resource_id_t{191},
         .sound_channel_event = object_id_t{1101}}};
    chart.measures[7].bgm_events = {{.id = object_id_t{1202},
        .position = {measure_id_t{8}, 0}, .source_lane = object_id_t{903},
        .sound = resource_id_t{192}, .sound_channel_event = object_id_t{1102}}};
    fixture.document.charts.push_back(std::move(chart));

    auto& stored_chart = fixture.document.charts.front();
    fixture.workspace.active_tab = workspace_tab_t::compose;
    fixture.workspace.selected_chart = stored_chart.id;
    fixture.interaction.chart = stored_chart.id;
    fixture.interaction.viewport_scroll_pixels = 0;
    fixture.interaction.viewport_height_pixels = 64;
    fixture.interaction.pixels_per_standard_measure = 960;
    fixture.interaction.selected_objects = {
        {editor_selection_object_kind_t::bgm_event, object_id_t{1200}}};
    const auto timing = build_timing_index(stored_chart);
    check_audio_worker(timing.index.has_value());
    if (timing.index) {
        fixture.cache.scene.chart = stored_chart.id;
        fixture.cache.scene.revision = fixture.document.revision;
        fixture.cache.timing.marker_timing_index = *timing.index;
    }
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    (void)sys::register_audition_sources(fixture.document, base, 1, fixture.sources);
}

auto t459_packed_runtime_filters_assets() -> bool {
    using namespace wormedit;
    t459_packed_runtime_fixture_t fixture;
    prepare_t459_packed_runtime_fixture(fixture);
    for (const auto asset : {asset_id_t{90}, asset_id_t{91}, asset_id_t{92}})
        if (!fixture.sources.binding(asset)) return false;
    sys::runtime::waveform_analysis_t runtime{fixture.sources};
    const std::vector<const sound_channel_t*> no_visible_stems;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!runtime.stem_waveform_analysis(asset_id_t{90}) &&
           std::chrono::steady_clock::now() < deadline) {
        runtime.service(fixture.document, fixture.workspace, fixture.cache, &fixture.interaction);
        runtime.service_compose_stem_waveforms(fixture.document, fixture.workspace,
            fixture.cache, &fixture.interaction, no_visible_stems);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (std::size_t frame = 0; frame < 40; ++frame) {
        runtime.service(fixture.document, fixture.workspace, fixture.cache, &fixture.interaction);
        runtime.service_compose_stem_waveforms(fixture.document, fixture.workspace,
            fixture.cache, &fixture.interaction, no_visible_stems);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return runtime.stem_waveform_analysis(asset_id_t{90}) &&
        !runtime.stem_waveform_analysis(asset_id_t{91}) &&
        !runtime.stem_waveform_analysis(asset_id_t{92}) &&
        runtime.stem_waveform_cache_stats().entries == 1 &&
        !runtime.channel_waveform_asset() && !runtime.channel_waveform_job().is_busy() &&
        !runtime.channel_waveform_analysis();
}
} // namespace

TEST_CASE("packed BMSON runtime analyzes only visible long packed BGM waveforms",
          "[sys][audio-notification][stem-cache][T459]") {
    check_audio_worker(t459_packed_runtime_filters_assets());
}
#endif
