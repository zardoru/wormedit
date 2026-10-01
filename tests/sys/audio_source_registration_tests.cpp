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
#include <chrono>
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
#endif
