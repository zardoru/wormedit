#include "sys/project_save_paths.h"
#include "sys/application_commands.h"
#include "sys/application_defaults.h"
#include <wormedit/formats/native_project.h>
#include <catch2/generators/catch_generators.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <source_location>

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

TEST_CASE("Portability warning explains Save copying and failed-copy retention",
          "[sys][project-save-paths][portability]") {
    CHECK(wormedit::sys::project_portability_notice_t::warning_message() ==
          "Some assets are linked outside the project folder. Save or Save As copies them into "
          "assets/ and relinks the project. If a copy fails, the original link is retained.");
}

namespace {
using namespace wormedit;
using namespace wormedit::sys;

void check_save(bool condition, std::source_location at = std::source_location::current()) {
    INFO("save assertion at " << at.line());
    CHECK(condition);
}
template<class T>
void require_save(const T& condition, std::source_location at = std::source_location::current()) {
    INFO("save assertion at " << at.line());
    REQUIRE(static_cast<bool>(condition));
}

struct project_save_fixture_t {
    std::filesystem::path root;
    std::filesystem::path project;
    std::filesystem::path music;
    song_document_t document;

    project_save_fixture_t() {
        static std::atomic<unsigned> sequence{};
        root = std::filesystem::temp_directory_path() / ("wormedit-save-assets-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(sequence++));
        project = root / "project";
        music = root / "music.ogg";
        std::filesystem::create_directories(project);
        write(music, "song bytes");
        auto created = make_audio_project_document(song_id_t{100}, music, "Song", "Artist", 7);
        require_save(created);
        document = std::move(*created);
    }
    ~project_save_fixture_t() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    static void write(const std::filesystem::path& path, std::string_view text) {
        std::ofstream stream{path, std::ios::binary};
        stream << text;
        require_save(stream);
    }
    static auto read(const std::filesystem::path& path) -> std::string {
        std::ifstream stream{path, std::ios::binary};
        require_save(stream);
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }
    auto destination() const -> std::filesystem::path { return project / "song.wormedit"; }
    auto save() const -> project_save_report_t {
        return create_project_save({.document = document, .source_base = root, .destination = destination()});
    }
    auto reopen() const -> song_document_t {
        auto parsed = parse_native_project(read(destination()));
        require_save(parsed.value);
        return std::move(*parsed.value);
    }
};
}

TEST_CASE("project save copies every external asset kind and leaves internal files in place",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    fixture.document.audio_assets.front().source_path = "music.ogg";
    fixture.write(fixture.root / "image.png", "image bytes");
    fixture.write(fixture.root / "notes.mid", "MIDI bytes");
    fixture.write(fixture.root / "script.lua", "script bytes");
    fixture.write(fixture.project / "local.wav", "local bytes");
    fixture.document.visual_assets.push_back({.id = asset_id_t{8}, .source_path = "image.png"});
    fixture.document.midi_assets.push_back({.id = asset_id_t{9}, .source_path = "notes.mid", .ticks_per_quarter = 480});
    fixture.document.generator_recipes.push_back({.id = recipe_id_t{10}, .source_path = "script.lua"});
    fixture.document.audio_assets.push_back({.id = asset_id_t{11}, .source_path = fixture.project / "local.wav"});
    const auto saved = fixture.save();
    INFO(saved.status_message());
    require_save(saved.storage.value);
    check_save(saved.copy_errors.empty());
    check_save(saved.copied_files.size() == 4);
    const auto reopened = fixture.reopen();
    check_save(fixture.read(fixture.project / reopened.audio_assets[0].source_path) == "song bytes");
    check_save(fixture.read(fixture.project / reopened.visual_assets[0].source_path) == "image bytes");
    check_save(fixture.read(fixture.project / reopened.midi_assets[0].source_path) == "MIDI bytes");
    check_save(fixture.read(fixture.project / reopened.generator_recipes[0].source_path) == "script bytes");
    check_save(reopened.audio_assets[1].source_path == "local.wav");
    check_save(nonportable_project_asset_count(reopened) == 0);
    for (const auto& path : saved.copied_files)
        check_save(saved.status_message().find(path.generic_string()) != std::string::npos);
    const auto music_copy = fixture.project / reopened.audio_assets.front().source_path;
    const auto modified = std::filesystem::last_write_time(music_copy);
    const auto repeated = create_project_save({.document = reopened, .source_base = fixture.project,
                                               .destination = fixture.destination()});
    require_save(repeated.storage.value);
    check_save(repeated.copied_files.empty());
    check_save(std::filesystem::last_write_time(music_copy) == modified);
    check_save(repeated.status_message().find("Copied") == std::string::npos);
    check_save(fixture.read(fixture.music) == "song bytes");
}

TEST_CASE("project save reuses identical existing files and shared external sources",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    std::filesystem::create_directory(fixture.project / "assets");
    fixture.write(fixture.project / "assets/existing.ogg", "song bytes");
    fixture.write(fixture.root / "duplicate.ogg", "song bytes");
    fixture.document.audio_assets.push_back({.id = asset_id_t{8}, .source_path = fixture.music});
    fixture.document.audio_assets.push_back({.id = asset_id_t{9}, .source_path = fixture.root / "duplicate.ogg"});
    const auto saved = fixture.save();
    require_save(saved.storage.value);
    check_save(saved.copy_errors.empty());
    check_save(saved.copied_files.empty());
    for (const auto& asset : fixture.reopen().audio_assets)
        check_save(asset.source_path == "assets/existing.ogg");
}

TEST_CASE("project save keeps a free source basename and resolves a conflicting basename",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    const auto other_directory = fixture.root / "alternate";
    std::filesystem::create_directories(other_directory);
    const auto second_music = other_directory / fixture.music.filename();
    fixture.write(second_music, "different song bytes");
    fixture.document.audio_assets.push_back({.id = asset_id_t{8}, .source_path = second_music});
    const auto saved = fixture.save();
    require_save(saved.storage.value);
    check_save(saved.copy_errors.empty());
    const auto reopened = fixture.reopen();
    require_save(reopened.audio_assets.size() == 2);
    check_save(reopened.audio_assets[0].source_path == "assets/music.ogg");
    check_save(reopened.audio_assets[1].source_path == "assets/8-music.ogg");
    check_save(fixture.read(fixture.project / reopened.audio_assets[0].source_path) == "song bytes");
    check_save(fixture.read(fixture.project / reopened.audio_assets[1].source_path) ==
               "different song bytes");
    const auto again = create_project_save({.document = reopened, .source_base = fixture.project,
        .destination = fixture.destination()});
    require_save(again.storage.value);
    check_save(again.copied_files.empty());
}

TEST_CASE("project save retains external links and names missing or conflicting copies",
          "[sys][project-save-paths][project-save]") {
    const bool missing = GENERATE(true, false);
    project_save_fixture_t fixture;
    if (missing) std::filesystem::remove(fixture.music);
    else {
        std::filesystem::create_directory(fixture.project / "assets");
        fixture.write(fixture.project / "assets/music.ogg", "different bytes");
        fixture.write(fixture.project / "assets/6-music.ogg", "different bytes");
    }
    const auto saved = fixture.save();
    require_save(saved.storage.value);
    require_save(saved.copy_errors.size() == 1);
    check_save(saved.copy_errors[0].code == (missing ? failure_code_t::unavailable : failure_code_t::conflict));
    check_save(saved.status_message().find(fixture.music.string()) != std::string::npos);
    check_save(fixture.reopen().audio_assets.front().source_path == fixture.music);
    check_save(saved.relinks.empty());
}

TEST_CASE("project save does not copy assets when the initial native write fails",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    fixture.write(fixture.destination(), "previous project bytes");
    fixture.document.charts.front().id = {};
    const auto saved = fixture.save();
    check_save(!saved.storage.value);
    check_save(fixture.read(fixture.destination()) == "previous project bytes");
    check_save(!std::filesystem::exists(fixture.project / "assets"));
}

TEST_CASE("project save keeps the initial links if publishing copied links fails",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    const auto internal = fixture.project / "local.ogg";
    fixture.write(internal, "internal audio bytes");
    fixture.document.audio_assets.push_back({.id = asset_id_t{8}, .source_path = internal});
    unsigned publications{};
    const auto saved = create_project_save({.document = fixture.document, .source_base = fixture.root,
        .destination = fixture.destination(), .storage_options = {.allow_stage = [&](auto stage, const auto&) {
            return stage != native_project_storage_stage_t::replace_destination || ++publications != 2;
        }}});
    require_save(saved.storage.value);
    require_save(saved.copy_errors.size() == 1);
    check_save(saved.copy_errors[0].code == failure_code_t::publication_failed);
    check_save(saved.relinks.empty());
    check_save(fixture.reopen().audio_assets.front().source_path == fixture.music);
    auto history = document_history_t::from(fixture.document);
    saved.publish_paths(history, fixture.root);
    check_save(history.document.audio_assets.front().source_path == fixture.music);
    check_save(history.document.audio_assets[1].source_path == internal);
    check_save(fixture.reopen().audio_assets[1].source_path == internal);
}

TEST_CASE("project save relinks history without reverting newer edits or unrelated paths",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    const auto saved = fixture.save();
    require_save(saved.storage.value);
    auto history = document_history_t::from(fixture.document);
    history.undo.push_back({.label = "Title", .before = fixture.document, .after = fixture.document});
    auto different = fixture.document;
    different.audio_assets.front().source_path = fixture.root / "other.ogg";
    history.redo.push_back({.label = "Relink", .before = fixture.document, .after = different});
    history.document.metadata.title = "Newer title";
    ++history.document.revision;
    saved.publish_paths(history, fixture.root);
    check_save(history.document.metadata.title == "Newer title");
    check_save(history.document.revision == fixture.document.revision + 1);
    check_save(history.undo.front().before.audio_assets.front().source_path == "assets/music.ogg");
    check_save(history.redo.front().after.audio_assets.front().source_path == fixture.root / "other.ogg");
    check_save(history.document.audio_assets.front().source_path == "assets/music.ogg");
}

TEST_CASE("project save copies symlinks that resolve outside the project folder",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    std::error_code error;
    std::filesystem::create_symlink(fixture.music, fixture.project / "linked.ogg", error);
    if (error) SKIP("This filesystem cannot create test symlinks");
    fixture.document.audio_assets.front().source_path = fixture.project / "linked.ogg";
    const auto saved = fixture.save();
    require_save(saved.storage.value);
    check_save(saved.copied_files.size() == 1);
    check_save(fixture.reopen().audio_assets.front().source_path == "assets/music.ogg");
    check_save(!std::filesystem::is_symlink(fixture.project / "assets/music.ogg"));
}

TEST_CASE("project save persists successful copies while keeping a failed asset linked externally",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    const auto missing = fixture.root / "missing.png";
    fixture.document.visual_assets.push_back({.id = asset_id_t{8}, .source_path = missing});
    const auto saved = fixture.save();
    require_save(saved.storage.value);
    require_save(saved.copy_errors.size() == 1);
    check_save(saved.copy_errors.front().code == failure_code_t::unavailable);
    check_save(saved.copied_files.size() == 1);
    const auto reopened = fixture.reopen();
    check_save(reopened.audio_assets.front().source_path == "assets/music.ogg");
    check_save(reopened.visual_assets.front().source_path == missing);
    check_save(saved.status_message().find(missing.string()) != std::string::npos);
}

TEST_CASE("project Save As copies assets from the previous project into the new folder",
          "[sys][project-save-paths][project-save]") {
    project_save_fixture_t fixture;
    require_save(fixture.save().storage.value);
    const auto reopened = fixture.reopen();
    const auto other_project = fixture.root / "other-project";
    std::filesystem::create_directory(other_project);
    const auto destination = other_project / "song.wormedit";
    const auto saved = create_project_save({.document = reopened, .source_base = fixture.project,
                                            .destination = destination});
    require_save(saved.storage.value);
    check_save(saved.copy_errors.empty());
    check_save(saved.copied_files.size() == 1);
    const auto moved = parse_native_project(fixture.read(destination));
    require_save(moved.value);
    const auto relative = moved.value->audio_assets.front().source_path;
    check_save(relative.is_relative());
    check_save(relative.parent_path() == "assets");
    check_save(fixture.read(other_project / relative) == "song bytes");
    check_save(std::filesystem::exists(fixture.project / reopened.audio_assets.front().source_path));
}
