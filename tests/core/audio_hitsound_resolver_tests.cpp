#include <wormedit/audio/hitsound_resolver.h>
#include <wormedit/audio/audition_resolver.h>
#include <wormedit/audio/offline_mix.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include "../support/temporary_directory.h"

namespace {
using namespace wormedit;
using namespace wormedit::audio;
auto bundled_samples() -> std::filesystem::path {
    return std::filesystem::path{WORMEDIT_FIXTURE_DIR}.parent_path().parent_path() / "assets/hitsounds";
}
struct hitsound_files_t {
    result_t<test::temporary_directory_t, std::string> directory = test::temporary_directory_t::from();
    hitsound_environment_t environment;
    hitsound_files_t() {
        REQUIRE(directory);
        environment = {.map_directory = directory->path() / "map", .skin_directory = directory->path() / "skin",
            .bundle_directory = bundled_samples()};
        REQUIRE(std::filesystem::create_directory(environment.map_directory));
        REQUIRE(std::filesystem::create_directory(environment.skin_directory));
    }
    void copy(const std::filesystem::path& path) const {
        REQUIRE(std::filesystem::copy_file(bundled_samples() / "normal-hitnormal.wav", path));
    }
};
void check_audible_hitsound_mix(decoded_source_registry_t& registry,
    const preview_schedule_t& schedule, const preview_bindings_resolve_outcome_t& resolved) {
    REQUIRE(resolved.diagnostics.empty());
    REQUIRE(resolved.value.bindings.size() == 1);
    CHECK(resolved.value.bindings[0].voices[0].gain == 0.5F);
    const auto mixed = render_offline_interval(registry, schedule, resolved.value.bindings, 0, 4410);
    REQUIRE(mixed);
    CHECK(std::ranges::any_of(mixed->interleaved_stereo, [](float value) { return std::abs(value) > 0.001F; }));
}
}

TEST_CASE("Osu hitsounds resolve map indexed files before skin and bundled samples", "[unit][audio][hitsound]") {
    hitsound_files_t files;
    files.copy(files.environment.map_directory / "normal-hitnormal2.wav");
    files.copy(files.environment.skin_directory / "drum-hitwhistle.wav");
    const auto resolved = hitsound_paths_t::from({.whistle = true, .finish = true, .clap = true,
        .normal_set = osu_sample_set_t::normal, .addition_set = osu_sample_set_t::drum,
        .custom_index = 2, .volume = 50}, files.environment);
    REQUIRE(resolved.paths.size() == 4);
    CHECK(resolved.paths[0] == files.environment.map_directory / "normal-hitnormal2.wav");
    CHECK(resolved.paths[1] == files.environment.skin_directory / "drum-hitwhistle.wav");
    CHECK(resolved.paths[2] == bundled_samples() / "drum-hitfinish.wav");
    CHECK(resolved.paths[3] == bundled_samples() / "drum-hitclap.wav");
    CHECK(resolved.gain == 0.5F);
}

TEST_CASE("Osu explicit custom filename wins and missing custom samples fall back", "[unit][audio][hitsound]") {
    hitsound_files_t files;
    osu_hitsample_t sample{.filename = "custom.wav"};
    bool present{};
    SECTION("present") {
        present = true;
        files.copy(files.environment.map_directory / "custom.wav");
    }
    SECTION("missing") {}
    SECTION("traversal") {
        sample.filename = "../outside.wav";
    }
    const auto resolved = hitsound_paths_t::from(sample, files.environment);
    REQUIRE(resolved.paths.size() == 1);
    CHECK(resolved.paths[0] == (present ? files.environment.map_directory / "custom.wav" : bundled_samples() / "normal-hitnormal.wav"));
    CHECK(resolved.fallback == !present);
}

TEST_CASE("Osu typed hitsounds schedule all voices without a resource and mix audible PCM", "[unit][audio][hitsound]") {
    decoded_source_registry_t registry;
    hitsound_resolver_t resolver;
    resolver.environment.bundle_directory = bundled_samples();
    song_document_t song{.id = song_id_t{1}, .charts = {{.id = chart_id_t{2},
        .measures = {{.id = measure_id_t{3}, .notes = {{.id = object_id_t{10},
            .position = {measure_id_t{3}, 0}, .lane = lane_id_t{1},
            .osu_hitsample = osu_hitsample_t{.whistle = true, .finish = true, .clap = true, .volume = 50}}}}}}}};
    preview_schedule_t schedule{.sample_rate = output_sample_rate};
    schedule.playable.push_back({.object = object_id_t{10}, .lane = lane_id_t{1}});
    const auto extras = resolver.bindings(song, song.charts[0], registry, 1);
    REQUIRE(extras.size() == 1);
    CHECK(extras[0].binding.voices.size() == 4);
    const auto resolved = preview_bindings_resolution_t::from(song, chart_id_t{2}, schedule, {}, {.hitsounds = extras});
    check_audible_hitsound_mix(registry, schedule, resolved);
    const auto limited = preview_bindings_resolution_t::from(song, chart_id_t{2}, schedule, {},
        {.maximum_total_clips = 3, .hitsounds = extras});
    CHECK_FALSE(limited.diagnostics.empty());
    CHECK(limited.value.bindings.empty());
}

TEST_CASE("Osu missing custom sample fallback is reported once per source environment", "[unit][audio][hitsound]") {
    decoded_source_registry_t registry;
    hitsound_resolver_t resolver;
    resolver.environment.bundle_directory = bundled_samples();
    song_document_t song{.charts = {{.measures = {{.id = measure_id_t{1}, .notes = {
        {.id = object_id_t{10}, .position = {measure_id_t{1}, 0},
         .osu_hitsample = osu_hitsample_t{.filename = "missing.wav"}}}}}}}};
    CHECK(resolver.bindings(song, song.charts[0], registry, 1).size() == 1);
    CHECK(resolver.take_fallback_notice());
    CHECK(resolver.bindings(song, song.charts[0], registry, 1).size() == 1);
    CHECK_FALSE(resolver.take_fallback_notice());
}
