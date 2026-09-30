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
