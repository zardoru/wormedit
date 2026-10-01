#include "sys/audio_source_registration.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>


TEST_CASE("relative audio assets resolve from the opened chart directory",
          "[integration][sys][audio-source-registration]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{73};
    document.audio_assets.push_back({.id = wormedit::asset_id_t{5},
                                     .source_path = "keysound-c4.ogg"});
    const auto chart_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";

    wormedit::audio::decoded_source_registry_t sources;
    std::vector<wormedit::audio::preview_metronome_source_binding_t> metronome;
    CHECK(wormedit::sys::register_audition_sources(document, {}, 1, sources, metronome) == 1);
    CHECK_FALSE(sources.binding(wormedit::asset_id_t{5}));

    sources.clear();
    metronome.clear();
    CHECK(wormedit::sys::register_audition_sources(document, chart_directory, 2,
                                                    sources, metronome) == 0);
    CHECK(sources.binding(wormedit::asset_id_t{5}));
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

TEST_CASE("deferred audio registration returns before probing and probes in the background",
          "[integration][sys][audio-source-registration][lazy]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{74};
    document.audio_assets.push_back({.id = wormedit::asset_id_t{5},
                                     .source_path = "keysound-c4.ogg"});
    document.audio_assets.push_back({.id = wormedit::asset_id_t{6},
                                     .source_path = "does-not-exist.ogg"});
    document.audio_assets.push_back({.id = wormedit::asset_id_t{7}, .source_path = {}});
    const auto chart_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";

    wormedit::audio::decoded_source_registry_t sources;
    std::vector<wormedit::audio::preview_metronome_source_binding_t> metronome;
    CHECK(wormedit::sys::register_audition_sources_deferred(document, chart_directory, 1, sources,
                                                            metronome) == 1);
    CHECK(sources.pending_count() == 2);
    CHECK(sources.open_decoder_count() == 0);
    CHECK_FALSE(sources.binding(wormedit::asset_id_t{5}));
    CHECK(metronome.size() == 3);

    wormedit::task_scheduler_t scheduler(2, 4);
    wormedit::sys::start_audition_source_probes(sources, scheduler);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (sources.pending_count() != 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(sources.pending_count() == 0);
    CHECK(sources.binding(wormedit::asset_id_t{5}));
    CHECK_FALSE(sources.binding(wormedit::asset_id_t{6}));
    CHECK(sources.open_decoder_count() == 0);

    const auto reported = wormedit::sys::drain_audition_probe_failures(sources);
    REQUIRE(reported.has_value());
    CHECK(reported->find("does-not-exist.ogg") != std::string::npos);
    CHECK_FALSE(wormedit::sys::drain_audition_probe_failures(sources).has_value());
}

TEST_CASE("reconciling an unchanged audio inventory does not register or probe again",
          "[integration][sys][audio-source-registration][lazy]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{75};
    document.audio_assets.push_back({.id = wormedit::asset_id_t{5},
                                     .source_path = "keysound-c4.ogg"});
    const auto chart_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    wormedit::audio::decoded_source_registry_t sources;
    std::vector<wormedit::audio::preview_metronome_source_binding_t> metronome;
    std::optional<wormedit::song_id_t> source_project;
    std::vector<wormedit::sys::audition_asset_entry_t> inventory;
    std::uint64_t generation{};
    wormedit::task_scheduler_t scheduler(1, 4);
    int stops = 0;
    const auto reconcile = [&] {
        return wormedit::sys::reconcile_audition_sources(
            {document, chart_directory, source_project, inventory, generation, sources, metronome,
             [&] { ++stops; }, [] {}, [] {}, &scheduler},
            true);
    };

    CHECK(reconcile() == std::optional<std::size_t>{0});
    sources.await_pending();
    REQUIRE(sources.binding(wormedit::asset_id_t{5}));
    CHECK_FALSE(reconcile().has_value());
    CHECK(generation == 1);
    CHECK(stops == 1);
    CHECK(sources.pending_count() == 0);
}

TEST_CASE("first-use asset order follows the chart before unused assets",
          "[unit][sys][audio-source-registration][resident]") {
    using namespace wormedit;
    song_document_t document;
    for (std::uint64_t id = 1; id <= 4; ++id) {
        document.audio_assets.push_back({.id = asset_id_t{id}, .source_path = "x.ogg"});
        document.resources.push_back({.id = resource_id_t{10 + id}, .asset = asset_id_t{id}});
    }
    chart_document_t chart;
    measure_chunk_t measure;
    measure.notes.push_back({.position = {measure_id_t{1}, 48}, .sound = resource_id_t{13}});
    measure.bgm_events.push_back({.position = {measure_id_t{1}, 0}, .sound = resource_id_t{12}});
    chart.measures.push_back(std::move(measure));
    document.charts.push_back(std::move(chart));
    const auto order = sys::first_use_asset_order(document);
    CHECK(order == std::vector<asset_id_t>{asset_id_t{2}, asset_id_t{3}, asset_id_t{1},
                                           asset_id_t{4}});
}

TEST_CASE("the resident loader decodes keysounds once and rebinds them on reconcile",
          "[integration][sys][audio-source-registration][resident]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{76};
    document.audio_assets.push_back({.id = wormedit::asset_id_t{5},
                                     .source_path = "keysound-c4.ogg"});
    document.audio_assets.push_back({.id = wormedit::asset_id_t{6},
                                     .source_path = "does-not-exist.ogg"});
    const auto chart_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::audio::resident_sample_store_t store;
    wormedit::task_scheduler_t scheduler(2, 4);
    wormedit::sys::resident_sample_loader_t loader{store, scheduler};
    std::vector<wormedit::audio::preview_metronome_source_binding_t> metronome;
    std::optional<wormedit::song_id_t> source_project;
    std::vector<wormedit::sys::audition_asset_entry_t> inventory;
    std::uint64_t generation{};
    const auto reconcile = [&] {
        wormedit::sys::audition_source_reconciliation_context_t context{
            document, chart_directory, source_project, inventory, generation, sources, metronome,
            [] {}, [] {}, [] {}, &scheduler};
        context.resident_loader = &loader;
        return wormedit::sys::reconcile_audition_sources(context, true);
    };
    const auto wait = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (loader.progress().running && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE_FALSE(loader.progress().running);
    };

    REQUIRE(reconcile().has_value());
    wait();
    sources.await_pending();
    const auto binding = sources.binding(wormedit::asset_id_t{5});
    REQUIRE(binding.has_value());
    CHECK(store.snapshot()->find(binding->source) != nullptr);
    CHECK(store.stats().decode_count == 1);
    CHECK(loader.progress().done == 1);
    const auto progress = loader.drain_progress();
    REQUIRE(progress.has_value());
    CHECK(*progress == "Loaded 1 keysound(s) into memory.");
    CHECK_FALSE(loader.drain_progress().has_value());

    // A new inventory entry rebinds the unchanged keysound without decoding it again.
    document.audio_assets.push_back({.id = wormedit::asset_id_t{7},
                                     .source_path = "keysound-e4.ogg"});
    REQUIRE(reconcile().has_value());
    wait();
    sources.await_pending();
    const auto rebound = sources.binding(wormedit::asset_id_t{5});
    REQUIRE(rebound.has_value());
    CHECK(rebound->source != binding->source);
    CHECK(store.snapshot()->find(rebound->source) != nullptr);
    CHECK(store.snapshot()->find(binding->source) == nullptr);
    CHECK(store.snapshot()->size() == 2);
    CHECK(store.stats().decode_count == 2);
}

TEST_CASE("long sources get their head decoded into the block cache instead of going resident",
          "[integration][sys][audio-source-registration][resident]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{77};
    document.audio_assets.push_back({.id = wormedit::asset_id_t{5},
                                     .source_path = "keysound-c4.ogg"});
    const auto chart_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    wormedit::audio::decoded_source_registry_t sources;
    // A tiny residency limit makes the fixture count as a long, streaming source.
    wormedit::audio::resident_sample_store_t store{{.maximum_frames_per_sample = 16}};
    wormedit::audio::decoded_block_cache_t head_cache;
    wormedit::task_scheduler_t scheduler(2, 4);
    wormedit::sys::resident_sample_loader_t loader{store, scheduler};
    loader.set_head_cache(&head_cache, 441'000);
    std::vector<wormedit::audio::preview_metronome_source_binding_t> metronome;
    REQUIRE(wormedit::sys::register_audition_sources_deferred(document, chart_directory, 1,
                                                              sources, metronome) == 0);
    loader.start(document, chart_directory, 1, sources);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (loader.progress().running && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE_FALSE(loader.progress().running);
    sources.await_pending();
    const auto binding = sources.binding(wormedit::asset_id_t{5});
    REQUIRE(binding.has_value());
    CHECK(head_cache.contains({binding->source, 0}));
    CHECK(store.snapshot()->size() == 0);
    CHECK(store.stats().decode_count == 0);
}

TEST_CASE("prioritized assets load before the remaining first-use order",
          "[integration][sys][audio-source-registration][resident]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{78};
    document.audio_assets.push_back({.id = wormedit::asset_id_t{1}, .source_path = "keysound-c4.ogg"});
    document.audio_assets.push_back({.id = wormedit::asset_id_t{2}, .source_path = "keysound-e4.ogg"});
    document.audio_assets.push_back({.id = wormedit::asset_id_t{3}, .source_path = "keysound-g4.ogg"});
    const auto chart_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::audio::resident_sample_store_t store;
    std::vector<wormedit::asset_id_t> load_order;
    std::mutex order_mutex;
    // The listener fires once per published sample, in load order.
    store.set_listener([&](wormedit::audio::resident_snapshot_ptr_t snapshot) {
        const std::scoped_lock lock(order_mutex);
        for (const auto asset : {wormedit::asset_id_t{1}, wormedit::asset_id_t{2},
                                 wormedit::asset_id_t{3}}) {
            const auto binding = sources.binding(asset);
            if (binding && snapshot->find(binding->source) != nullptr &&
                std::ranges::find(load_order, asset) == load_order.end()) {
                load_order.push_back(asset);
            }
        }
    });
    wormedit::task_scheduler_t scheduler(2, 4);
    wormedit::sys::resident_sample_loader_t loader{store, scheduler};
    std::vector<wormedit::audio::preview_metronome_source_binding_t> metronome;
    REQUIRE(wormedit::sys::register_audition_sources_deferred(document, chart_directory, 1,
                                                              sources, metronome) == 0);
    sources.await_pending();
    loader.prioritize({wormedit::asset_id_t{3}});
    loader.start(document, chart_directory, 1, sources);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (loader.progress().running && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE_FALSE(loader.progress().running);
    CHECK(store.snapshot()->size() == 3);
    CHECK(loader.progress().done == 3);
    const std::scoped_lock lock(order_mutex);
    REQUIRE(load_order.size() == 3);
    CHECK(load_order.front() == wormedit::asset_id_t{3});
    CHECK(load_order[1] == wormedit::asset_id_t{1});
    CHECK(load_order[2] == wormedit::asset_id_t{2});
}
#endif
