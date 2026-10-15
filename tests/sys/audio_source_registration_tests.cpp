#include "sys/audio_source_registration.h"
#include "sys/project_audio_sources.h"
#include "sys/audio_fallback.h"
#include <wormedit/audio/object_audition.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

namespace {
void check_large_inventory_pending(wormedit::audio::decoded_source_registry_t& sources,
                                   std::uint64_t sound_count) {
    CHECK(sources.pending_count() == sound_count);
    CHECK(sources.open_decoder_count() == 0);
    REQUIRE(sources.open_source_binding(wormedit::asset_id_t{sound_count}));
}

void check_large_inventory_binding(const wormedit::audio::decoded_source_registry_t& sources,
                                   std::uint64_t sound_count) {
    const auto last = sources.binding(wormedit::asset_id_t{sound_count});
    REQUIRE(last);
    CHECK(last->output_frame_count > 0);
    CHECK(sources.open_decoder_count() <= 8);
}

void check_large_keysound_inventory(bool deferred, std::uint64_t sound_count) {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{79};
    for (std::uint64_t index = 1; index <= sound_count; ++index)
        document.audio_assets.push_back({.id = wormedit::asset_id_t{index},
                                         .source_path = "keysound-c4.ogg"});
    const auto directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    wormedit::audio::decoded_source_registry_t sources;
    const auto register_sources = deferred ? wormedit::sys::register_audition_sources_deferred
                                          : wormedit::sys::register_audition_sources;
    CHECK(register_sources(document, directory, 1, sources) == 0);
    wormedit::sys::metronome_t metronome;
    metronome.register_into(sources, 1);
    CHECK(sources.size() == sound_count + 3);
    CHECK(metronome.bindings().size() == 3);
    if (deferred) check_large_inventory_pending(sources, sound_count);
    check_large_inventory_binding(sources, sound_count);
}
}

namespace {
struct fallback_dir_t {
    std::filesystem::path path;
    explicit fallback_dir_t(const char* name)
        : path(std::filesystem::temp_directory_path() / name) {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        std::filesystem::create_directories(path / "sub");
    }
    ~fallback_dir_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    void touch(const char* relative) const { std::ofstream(path / relative) << "x"; }
};
} // namespace

TEST_CASE("audio fallback keeps an exact file and never searches around it",
          "[unit][sys][audio-fallback]") {
    const fallback_dir_t dir{"wormedit-fallback-exact"};
    dir.touch("kick.wav");
    dir.touch("kick.ogg");
    const auto result = wormedit::sys::resolve_audio_fallback("kick.wav", dir.path);
    CHECK(result.outcome == wormedit::sys::audio_fallback_outcome_t::exact);
    CHECK(result.chosen == std::filesystem::path{"kick.wav"});
    CHECK(result.candidates.empty());
}

TEST_CASE("audio fallback chooses the one other extension, case-insensitively",
          "[unit][sys][audio-fallback]") {
    const fallback_dir_t dir{"wormedit-fallback-single"};
    dir.touch("sub/Snare.OGG");
    const auto result = wormedit::sys::resolve_audio_fallback("sub/snare.wav", dir.path);
    CHECK(result.outcome == wormedit::sys::audio_fallback_outcome_t::fallback);
    CHECK(result.chosen == std::filesystem::path{"sub/Snare.OGG"});
    CHECK(result.candidates.size() == 1);
}

TEST_CASE("audio fallback reports every candidate when several match",
          "[unit][sys][audio-fallback]") {
    const fallback_dir_t dir{"wormedit-fallback-ambiguous"};
    dir.touch("hat.ogg");
    dir.touch("hat.flac");
    dir.touch("HAT.WAV");
    dir.touch("hat.mp3");
    const auto result = wormedit::sys::resolve_audio_fallback("hat.wave", dir.path);
    CHECK(result.outcome == wormedit::sys::audio_fallback_outcome_t::ambiguous);
    REQUIRE(result.candidates.size() == 3);
    CHECK(result.candidates[0] == std::filesystem::path{"HAT.WAV"});
    CHECK(result.candidates[1] == std::filesystem::path{"hat.ogg"});
    CHECK(result.candidates[2] == std::filesystem::path{"hat.flac"});
}

TEST_CASE("audio fallback finds nothing for missing, unsupported or unsafe paths",
          "[unit][sys][audio-fallback]") {
    const fallback_dir_t dir{"wormedit-fallback-none"};
    dir.touch("other.wav");
    dir.touch("clap.mp3");
    CHECK(wormedit::sys::resolve_audio_fallback("clap.wav", dir.path).outcome ==
          wormedit::sys::audio_fallback_outcome_t::missing);
    CHECK(wormedit::sys::resolve_audio_fallback("gone.wav", dir.path).outcome ==
          wormedit::sys::audio_fallback_outcome_t::missing);
    CHECK(wormedit::sys::resolve_audio_fallback("../other.wav", dir.path / "sub").candidates.empty());
    CHECK(wormedit::sys::resolve_audio_fallback("/nowhere/other.wav", dir.path).outcome ==
          wormedit::sys::audio_fallback_outcome_t::missing);
}

TEST_CASE("Large keysound inventories register every asset and metronome accent",
          "[integration][sys][audio-source-registration][regression]") {
    SECTION("full BMS namespace synchronous registration") { check_large_keysound_inventory(false, 1'295); }
    SECTION("full BMS namespace deferred registration") { check_large_keysound_inventory(true, 1'295); }
    SECTION("larger inventories") { check_large_keysound_inventory(true, 3'843); }
}


TEST_CASE("relative audio assets resolve from the opened chart directory",
          "[integration][sys][audio-source-registration]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{73};
    document.audio_assets.push_back({.id = wormedit::asset_id_t{5},
                                     .source_path = "keysound-c4.ogg"});
    const auto chart_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";

    wormedit::audio::decoded_source_registry_t sources;
    CHECK(wormedit::sys::register_audition_sources(document, {}, 1, sources) == 1);
    CHECK_FALSE(sources.binding(wormedit::asset_id_t{5}));

    sources.clear();
    CHECK(wormedit::sys::register_audition_sources(document, chart_directory, 2,
                                                    sources) == 0);
    CHECK(sources.binding(wormedit::asset_id_t{5}));
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <source_location>

namespace {
template <typename T>
void expect_registration(const T& value,
    std::source_location where = std::source_location::current()) {
    INFO("Registration check at " << where.line());
    REQUIRE(static_cast<bool>(value));
}
}

namespace {
void check_background_probe_results(wormedit::audio::decoded_source_registry_t& sources) {
    expect_registration(sources.binding(wormedit::asset_id_t{5}));
    expect_registration(!(sources.binding(wormedit::asset_id_t{6})));
    expect_registration(sources.open_decoder_count() == 0);

    const auto reported = wormedit::sys::drain_audition_probe_failures(sources);
    expect_registration(reported.has_value());
    expect_registration(reported->severity == wormedit::notification_severity_t::error);
    expect_registration(reported->source == wormedit::notification_source_t::audio);
    expect_registration(reported->message.find("does-not-exist.ogg") != std::string::npos);
    expect_registration(!(wormedit::sys::drain_audition_probe_failures(sources).has_value()));
}
} // namespace

namespace {
void await_background_probes(wormedit::audio::decoded_source_registry_t& sources) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (sources.pending_count() != 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(sources.pending_count() == 0);
}
}

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
    CHECK(wormedit::sys::register_audition_sources_deferred(document, chart_directory, 1, sources) == 1);
    CHECK(sources.pending_count() == 2);
    CHECK(sources.open_decoder_count() == 0);
    CHECK_FALSE(sources.binding(wormedit::asset_id_t{5}));
    wormedit::sys::metronome_t metronome;
    metronome.register_into(sources, 1);
    CHECK(metronome.bindings().size() == 3);

    wormedit::task_scheduler_t scheduler(2, 4);
    wormedit::sys::start_audition_source_probes(sources, scheduler);
    await_background_probes(sources);
    check_background_probe_results(sources);
}

TEST_CASE("reconciling an unchanged audio inventory does not register or probe again",
          "[integration][sys][audio-source-registration][lazy]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{75};
    document.audio_assets.push_back({.id = wormedit::asset_id_t{5},
                                     .source_path = "keysound-c4.ogg"});
    const auto chart_directory = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    std::uint64_t& generation = project_sources.generation;
    wormedit::task_scheduler_t scheduler(1, 4);
    const auto reconcile = [&] {
        project_sources.probe_scheduler = &scheduler;
        return project_sources.reconcile(document, chart_directory);
    };

    CHECK(reconcile() == std::optional<std::size_t>{0});
    sources.await_pending();
    REQUIRE(sources.binding(wormedit::asset_id_t{5}));
    CHECK_FALSE(reconcile().has_value());
    CHECK(generation == 1);
    CHECK(sources.pending_count() == 0);
}

namespace {
struct save_as_fixture_t {
    wormedit::song_document_t document;
    std::filesystem::path original_base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    std::filesystem::path alias_base;
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    std::uint64_t& generation = project_sources.generation;

    save_as_fixture_t() {
        document.id = wormedit::song_id_t{80};
        document.audio_assets.push_back({.id = wormedit::asset_id_t{1}, .source_path = "keysound-c4.ogg"});
        document.audio_assets.push_back({.id = wormedit::asset_id_t{2}, .source_path = "keysound-e4.ogg"});
        alias_base = std::filesystem::temp_directory_path() / "wormedit-save-as-alias-80";
        std::error_code error;
        std::filesystem::remove_all(alias_base, error);
        std::filesystem::create_directory_symlink(original_base, alias_base, error);
    }
    ~save_as_fixture_t() {
        std::error_code error;
        std::filesystem::remove_all(alias_base, error);
    }
    auto reconcile(const std::filesystem::path& base) {
        return project_sources.reconcile(document, base);
    }
};
} // namespace

TEST_CASE("Save As rebases unchanged audio sources in place and re-probes only changed files",
          "[integration][sys][audio-source-registration][lazy]") {
    save_as_fixture_t fixture;
    REQUIRE(fixture.reconcile(fixture.original_base) == std::optional<std::size_t>{0});
    const auto first = fixture.sources.binding(wormedit::asset_id_t{1});
    const auto second = fixture.sources.binding(wormedit::asset_id_t{2});
    REQUIRE(first);
    REQUIRE(second);
    const auto generation = fixture.generation;

    // Save As: the same files now resolve through a different project directory.
    CHECK_FALSE(fixture.reconcile(fixture.alias_base).has_value());
    CHECK(fixture.generation == generation);
    CHECK(fixture.sources.pending_count() == 0);
    CHECK(fixture.sources.binding(wormedit::asset_id_t{1}) == first);
    CHECK(fixture.sources.binding(wormedit::asset_id_t{2}) == second);
    CHECK(fixture.sources.source_path(wormedit::asset_id_t{1}) ==
          (fixture.alias_base / "keysound-c4.ogg").lexically_normal());

    // One asset now points at a different file: only that one is registered (and probed) again.
    fixture.document.audio_assets[1].source_path = "keysound-g4.ogg";
    REQUIRE(fixture.reconcile(fixture.alias_base) == std::optional<std::size_t>{0});
    CHECK(fixture.generation == generation + 1);
    CHECK(fixture.sources.binding(wormedit::asset_id_t{1}) == first);
    const auto changed = fixture.sources.binding(wormedit::asset_id_t{2});
    REQUIRE(changed);
    CHECK(changed->source != second->source);
}

TEST_CASE("audio sources are registered song first, then keysounds by first use",
          "[unit][sys][audio-source-registration][lazy]") {
    using namespace wormedit;
    song_document_t document;
    document.id = song_id_t{81};
    for (std::uint64_t id = 1; id <= 4; ++id) {
        document.audio_assets.push_back({.id = asset_id_t{id}, .source_path = "keysound-c4.ogg"});
        document.resources.push_back({.id = resource_id_t{10 + id}, .asset = asset_id_t{id}});
    }
    document.resources[3].roles.push_back(resource_role_t::music);
    chart_document_t chart;
    measure_chunk_t measure;
    measure.notes.push_back({.position = {measure_id_t{1}, 0}, .sound = resource_id_t{13}});
    chart.measures.push_back(std::move(measure));
    document.charts.push_back(std::move(chart));
    audio::decoded_source_registry_t sources;
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    REQUIRE(sys::register_audition_sources_deferred(document, base, 1, sources) == 0);
    // Probing claims the oldest pending source first, so completing one probe at a time
    // settles the song, then the first-used keysound, then the rest.
    for (const auto expected : {asset_id_t{4}, asset_id_t{3}, asset_id_t{1}, asset_id_t{2}}) {
        REQUIRE(sources.probe_next_pending());
        CHECK(sources.binding(expected).has_value());
    }
}

TEST_CASE("reconciling after linking an absolute asset into an unsaved project registers it",
          "[integration][sys][audio-source-registration][lazy]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{76};
    const std::filesystem::path unsaved_asset_base;
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    std::uint64_t& generation = project_sources.generation;
    wormedit::task_scheduler_t scheduler(1, 4);
    const auto reconcile = [&] {
        project_sources.probe_scheduler = &scheduler;
        return project_sources.reconcile(document, unsaved_asset_base);
    };

    CHECK(reconcile() == std::optional<std::size_t>{0});
    document.audio_assets.push_back(
        {.id = wormedit::asset_id_t{9},
         .source_path = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio" /
                        "sliced-seek-source.mp3"});
    CHECK(reconcile() == std::optional<std::size_t>{0});
    sources.await_pending();
    CHECK(sources.binding(wormedit::asset_id_t{9}));
    CHECK(generation == 2);
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
    wormedit::sys::project_audio_sources_t project_sources;
    auto& sources = project_sources.registry();
    wormedit::audio::resident_sample_store_t store;
    wormedit::task_scheduler_t scheduler(2, 4);
    wormedit::sys::resident_sample_loader_t loader{store, scheduler};
    std::uint64_t& generation = project_sources.generation;
    const auto reconcile = [&] {
        project_sources.probe_scheduler = &scheduler;
        project_sources.resident_loader = &loader;
        return project_sources.reconcile(document, chart_directory);
    };
    const auto wait = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (loader.progress().running && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        expect_registration(!(loader.progress().running));
    };

    expect_registration(reconcile().has_value());
    wait();
    sources.await_pending();
    const auto binding = sources.binding(wormedit::asset_id_t{5});
    expect_registration(binding.has_value());
    expect_registration(store.snapshot()->find(binding->source) != nullptr);
    expect_registration(store.stats().decode_count == 1);
    expect_registration(loader.progress().done == 1);
    const auto progress = loader.drain_progress();
    expect_registration(progress.has_value());
    expect_registration(progress->severity == wormedit::notification_severity_t::hint);
    expect_registration(progress->source == wormedit::notification_source_t::audio);
    expect_registration(progress->message == "Loaded 1 keysound(s) into memory.");
    expect_registration(!(loader.drain_progress().has_value()));

    // A new inventory entry rebinds the unchanged keysound without decoding it again.
    document.audio_assets.push_back({.id = wormedit::asset_id_t{7},
                                     .source_path = "keysound-e4.ogg"});
    expect_registration(reconcile().has_value());
    wait();
    sources.await_pending();
    const auto rebound = sources.binding(wormedit::asset_id_t{5});
    expect_registration(rebound.has_value());
    // The unchanged keysound keeps its identity, so its resident data stays valid.
    expect_registration(rebound->source == binding->source);
    expect_registration(store.snapshot()->find(rebound->source) != nullptr);
    expect_registration(store.snapshot()->size() == 2);
    expect_registration(store.stats().decode_count == 2);
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
    REQUIRE(wormedit::sys::register_audition_sources_deferred(document, chart_directory, 1,
                                                              sources) == 0);
    loader.start(document, chart_directory, 1, sources);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (loader.progress().running && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE_FALSE(loader.progress().running);
    sources.await_pending();
    const auto binding = sources.binding(wormedit::asset_id_t{5});
    REQUIRE(binding.has_value());
    CHECK(head_cache.has_block({binding->source, 0}));
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
    REQUIRE(wormedit::sys::register_audition_sources_deferred(document, chart_directory, 1,
                                                              sources) == 0);
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
namespace {
struct budget_loader_fixture_t {
    wormedit::song_document_t document;
    std::filesystem::path directory{std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio"};
    wormedit::audio::decoded_source_registry_t sources;
    wormedit::audio::resident_sample_store_t store{{.budget_bytes = 0}};
    wormedit::task_scheduler_t scheduler{2, 4};
    wormedit::sys::resident_sample_loader_t loader{store, scheduler};
    budget_loader_fixture_t(std::string first = "keysound-c4.ogg", std::string second = "keysound-e4.ogg") {
        document.id = wormedit::song_id_t{780};
        document.audio_assets.push_back({.id = wormedit::asset_id_t{1}, .source_path = std::move(first)});
        document.audio_assets.push_back({.id = wormedit::asset_id_t{2}, .source_path = std::move(second)});
        expect_registration(wormedit::sys::register_audition_sources(document, directory, 1, sources) == 0);
    }
    void start() { loader.start(document, directory, 1, sources); }
    void wait() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((loader.progress().running || scheduler.outstanding_tasks() != 0) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        expect_registration(!loader.progress().running);
        expect_registration(scheduler.outstanding_tasks() == 0);
    }
};
void check_budget_snapshot(budget_loader_fixture_t& fixture,
                           const wormedit::audio::resident_snapshot_ptr_t& held) {
    expect_registration(fixture.store.snapshot()->size() == 2);
    expect_registration(held->size() == 2);
    const auto source = fixture.sources.binding(wormedit::asset_id_t{1})->source;
    expect_registration(fixture.store.snapshot()->find(source)->data == held->find(source)->data);
    expect_registration(!held->block({source, 0}).empty());
}
}
TEST_CASE("raising a completed project budget retries skipped keysounds without reopening", "[resident][budget]") {
    budget_loader_fixture_t fixture;
    fixture.start();
    fixture.wait();
    expect_registration(fixture.store.snapshot()->size() == 0);
    const auto report = fixture.loader.drain_report();
    expect_registration(report.has_value());
    expect_registration(report->message.find("Raise Keysound memory in Options") != std::string::npos);
    fixture.loader.set_budget_bytes(16U * 1024U * 1024U);
    fixture.wait();
    const auto held = fixture.store.snapshot();
    fixture.loader.set_budget_bytes(0);
    check_budget_snapshot(fixture, held);
}
TEST_CASE("raising the budget during an active resident pass is not lost", "[resident][budget]") {
    budget_loader_fixture_t fixture{"silence.ogg", "keysound-c4.ogg"};
    std::atomic<bool> entered{}, released{};
    fixture.store.set_listener([&](wormedit::audio::resident_snapshot_ptr_t snapshot) {
        if (snapshot->size() != 1) return;
        entered = true;
        while (!released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    const auto small = fixture.sources.binding(wormedit::asset_id_t{2});
    fixture.loader.set_budget_bytes(static_cast<std::size_t>(small->output_frame_count) * 2U * sizeof(float));
    fixture.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!entered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto reached = entered.load();
    const auto held = fixture.store.snapshot();
    const auto* retained = held->find(small->source);
    fixture.loader.set_budget_bytes(16U * 1024U * 1024U);
    released = true;
    fixture.wait();
    expect_registration(reached);
    expect_registration(held->size() == 1);
    expect_registration(retained != nullptr);
    expect_registration(fixture.store.snapshot()->size() == 2);
    expect_registration(fixture.store.snapshot()->find(small->source)->data == retained->data);
    fixture.store.set_listener({});
}
TEST_CASE("budget retries use only the latest project after a canceled run", "[resident][budget]") {
    budget_loader_fixture_t fixture;
    fixture.start();
    fixture.wait();
    fixture.loader.cancel();
    fixture.sources.clear();
    fixture.document.id = wormedit::song_id_t{781};
    fixture.document.audio_assets.erase(fixture.document.audio_assets.begin());
    expect_registration(wormedit::sys::register_audition_sources(fixture.document, fixture.directory, 2,
        fixture.sources) == 0);
    fixture.start();
    fixture.wait();
    fixture.loader.set_budget_bytes(16U * 1024U * 1024U);
    fixture.wait();
    expect_registration(fixture.store.snapshot()->size() == 1);
    const auto binding = fixture.sources.binding(wormedit::asset_id_t{2});
    expect_registration(binding.has_value());
    expect_registration(fixture.store.snapshot()->find(binding->source) != nullptr);
}
namespace {
auto occupy_budget_scheduler(wormedit::task_scheduler_t& scheduler, std::atomic<bool>& release)
    -> wormedit::task_graph_handle_t {
    wormedit::task_graph_t graph{4};
    for (int index = 0; index < 4; ++index) {
        const auto added = graph.create_task({}, [&release](std::span<const wormedit::task_result_t>, std::stop_token) {
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return wormedit::task_result_t::success(wormedit::task_value_t::from(std::make_shared<const int>(0)));
        });
        expect_registration(static_cast<bool>(added));
    }
    auto submitted = scheduler.begin(std::move(graph));
    expect_registration(static_cast<bool>(submitted));
    return std::move(*submitted);
}
}
TEST_CASE("an unchanged budget retries deferred keysounds after scheduler capacity returns", "[resident][budget]") {
    budget_loader_fixture_t fixture;
    fixture.start();
    fixture.wait();
    std::atomic<bool> release{};
    const auto occupied = occupy_budget_scheduler(fixture.scheduler, release);
    fixture.loader.set_budget_bytes(16U * 1024U * 1024U);
    expect_registration(!fixture.loader.progress().running);
    expect_registration(fixture.store.snapshot()->size() == 0);
    release = true;
    expect_registration(occupied.wait());
    fixture.loader.set_budget_bytes(16U * 1024U * 1024U);
    fixture.wait();
    expect_registration(fixture.store.snapshot()->size() == 2);
}
#endif

TEST_CASE("same-frame asset edit is visible to object audition", "[sys][audio-source-registration][same-frame]") {
    using namespace wormedit;
    song_document_t document;
    document.id = song_id_t{900};
    document.revision = 1;
    sys::project_audio_sources_t project_sources;
    auto& sources = project_sources.registry();
    const auto base = std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio";
    REQUIRE(project_sources.reconcile(document, base) == std::optional<std::size_t>{0});
    // Audio phase has completed. A later action links an asset and auditions it.
    document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = "keysound-c4.ogg"});
    document.resources.push_back({.id = resource_id_t{1}, .asset = asset_id_t{1}});
    chart_document_t chart;
    chart.id = chart_id_t{1};
    measure_chunk_t measure;
    measure.id = measure_id_t{1};
    measure.notes.push_back({.id = object_id_t{1}, .position = {measure.id, 0}, .sound = resource_id_t{1}});
    chart.measures.push_back(std::move(measure));
    document.charts.push_back(std::move(chart));
    ++document.revision;
    audio::decoded_block_cache_t cache;
    audio::transport_t transport{cache};
    audio::audition_transport_bridge_t bridge{transport};
    audio::object_audition_controller_t controller{bridge};
    sources.await_pending();
    const auto prepared = controller.prepare_object(document, {document.revision, chart_id_t{1}, object_id_t{1}}, project_sources.bindings(document));
    REQUIRE(prepared);
    REQUIRE(prepared.value().prepared->clips.size() == 1);
}
