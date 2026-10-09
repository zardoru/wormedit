#include "sys/project_save_paths.h"
#include "sys/application_commands.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace {

auto sample_document() -> wormedit::song_document_t {
    wormedit::song_document_t document;
    document.audio_assets.push_back({.source_path = "samples/kick.wav",
                                     .waveform_cache_path = "cache/kick.waveform"});
    document.visual_assets.push_back({.source_path = "images/background.png"});
    document.midi_assets.push_back({.source_path = "midi/notes.mid"});
    document.generator_recipes.push_back({.source_path = "scripts/generate.lua"});
    return document;
}

void check_rebased_path(const std::filesystem::path& destination,
                        const std::filesystem::path& actual,
                        const std::filesystem::path& source,
                        const std::filesystem::path& expected) {
    CHECK(actual.is_relative());
    CHECK((destination / actual).lexically_normal() == (source / expected).lexically_normal());
}

} // namespace

TEST_CASE("project Save command ID resolves to the application Save action",
          "[unit][sys][project-save-paths]") {
    const auto command = wormedit::sys::parse_application_command("document.save_project");
    REQUIRE(command);
    CHECK(command->kind == wormedit::sys::application_command_kind_t::document_save_project);
}

TEST_CASE("Save As keeps every relative asset path pointed at its original file",
          "[unit][sys][project-save-paths]") {
    const auto source = std::filesystem::temp_directory_path() / "wormedit-source";
    const auto destination = std::filesystem::temp_directory_path() / "wormedit-export";
    auto document = sample_document();

    wormedit::sys::rebase_project_paths(document, source, destination);

    check_rebased_path(destination, document.audio_assets[0].source_path,
                       source, "samples/kick.wav");
    REQUIRE(document.audio_assets[0].waveform_cache_path);
    check_rebased_path(destination, *document.audio_assets[0].waveform_cache_path,
                       source, "cache/kick.waveform");
    check_rebased_path(destination, document.visual_assets[0].source_path,
                       source, "images/background.png");
    check_rebased_path(destination, document.midi_assets[0].source_path,
                       source, "midi/notes.mid");
    check_rebased_path(destination, document.generator_recipes[0].source_path,
                       source, "scripts/generate.lua");
}

TEST_CASE("Save As preserves absolute and empty asset paths",
          "[unit][sys][project-save-paths]") {
    const auto source = std::filesystem::temp_directory_path() / "wormedit-source";
    const auto destination = std::filesystem::temp_directory_path() / "wormedit-export";
    const auto absolute = source / "audio.wav";
    auto document = sample_document();
    document.audio_assets[0].source_path = absolute;
    document.audio_assets[0].waveform_cache_path = absolute;
    document.visual_assets[0].source_path.clear();
    document.midi_assets[0].source_path = absolute;
    document.generator_recipes[0].source_path.clear();

    wormedit::sys::rebase_project_paths(document, source, destination);

    CHECK(document.audio_assets[0].source_path == absolute);
    CHECK(document.audio_assets[0].waveform_cache_path == absolute);
    CHECK(document.visual_assets[0].source_path.empty());
    CHECK(document.midi_assets[0].source_path == absolute);
    CHECK(document.generator_recipes[0].source_path.empty());
}

TEST_CASE("Save As leaves relative paths unchanged without a known source base",
          "[unit][sys][project-save-paths]") {
    const auto destination = std::filesystem::temp_directory_path() / "wormedit-export";
    auto document = sample_document();
    const auto original = document;

    wormedit::sys::rebase_project_paths(document, {}, destination);

    CHECK(document.audio_assets[0].source_path == original.audio_assets[0].source_path);
    CHECK(document.audio_assets[0].waveform_cache_path ==
          original.audio_assets[0].waveform_cache_path);
    CHECK(document.visual_assets[0].source_path == original.visual_assets[0].source_path);
    CHECK(document.midi_assets[0].source_path == original.midi_assets[0].source_path);
    CHECK(document.generator_recipes[0].source_path == original.generator_recipes[0].source_path);
}

TEST_CASE("Portability warning detects absolute and escaping paths across asset kinds", "[sys][project-save-paths][portability]") {
    auto document = sample_document();
    CHECK(wormedit::sys::nonportable_project_asset_count(document) == 0);
    document.audio_assets.front().source_path = "/outside/kick.wav";
    document.visual_assets.front().source_path = "../background.png";
    document.midi_assets.front().source_path = "C:/sounds/song.mid";
    document.generator_recipes.front().source_path = "assets/../../recipe.lua";
    CHECK(wormedit::sys::nonportable_project_asset_count(document) == 4);
    document.audio_assets.front().source_path = "assets/../kick.wav";
    CHECK(wormedit::sys::nonportable_project_asset_count(document) == 3);
}

TEST_CASE("Portability warning scans revisions and avoids repeated notices on ordinary edits", "[sys][project-save-paths][portability]") {
    auto document = sample_document();
    wormedit::sys::project_portability_notice_t notice;
    CHECK_FALSE(notice.update(document,1));
    document.audio_assets.front().source_path = "/outside/kick.wav";
    ++document.revision;
    CHECK(notice.update(document,1));
    CHECK_FALSE(notice.update(document,1));
    ++document.revision;
    CHECK_FALSE(notice.update(document,1));
    CHECK(notice.update(document,2));
}
