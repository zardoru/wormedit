#include "sys/project_open.h"
#include "sys/project_save_paths.h"

#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

struct temporary_project_file_t {
    std::filesystem::path path;

    explicit temporary_project_file_t(std::string_view extension) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("wormedit-project-open-" + std::to_string(nonce) + std::string(extension));
    }

    ~temporary_project_file_t() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    void write(std::string_view source) const {
        std::ofstream output(path, std::ios::binary);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
    }
};

struct temporary_project_directory_t {
    std::filesystem::path path;

    temporary_project_directory_t() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("wormedit-chart-assets-" + std::to_string(nonce));
        std::filesystem::create_directories(path / "audio");
    }

    ~temporary_project_directory_t() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

auto fixture(std::string_view family, std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / family / name;
}

void check_imported_chart(const std::filesystem::path& path) {
    const auto result = wormedit::sys::open_project_file(path);
    INFO(result.error);
    REQUIRE(result.document.has_value());
    CHECK_FALSE(result.native_project);
    CHECK(result.source_path == std::filesystem::absolute(path));
    CHECK(result.asset_base == std::filesystem::absolute(path).parent_path());
    CHECK_FALSE(result.document->charts.empty());
}

void check_native_identity(const wormedit::sys::project_open_result_t& opened,
                           const wormedit::song_document_t& source,
                           const std::filesystem::path& path) {
    CHECK(opened.native_project);
    CHECK(opened.document->id == source.id);
    CHECK(opened.document->revision == source.revision);
    CHECK(opened.source_path == path);
    CHECK(opened.asset_base == path.parent_path());
}

} // namespace

TEST_CASE("opening chart files keeps their source directory and leaves them unsaved",
          "[unit][sys][project-open]") {
    struct fixture_case_t {
        std::string_view family;
        std::string_view name;
    };
    for (const auto item : {fixture_case_t{"bms", "repeated-bgm-ratio-stop-collision.bms"},
                            fixture_case_t{"bms", "custom-channel.bme"},
                            fixture_case_t{"bms", "legacy-long-note.bml"},
                            fixture_case_t{"bms", "pms-nine-button.pms"},
                            fixture_case_t{"bmson", "zero-resolution-default.bmson"},
                            fixture_case_t{"stepmania", "matched-timing.sm"},
                            fixture_case_t{"stepmania", "matched-timing.ssc"},
                            fixture_case_t{"osu", "mania-tags-bookmarks-kiai.osu"}}) {
        DYNAMIC_SECTION(item.name) {
            check_imported_chart(fixture(item.family, item.name));
        }
    }
}

TEST_CASE("dropped project extension filter matches every supported opener",
          "[unit][sys][project-open]") {
    constexpr std::array accepted{".wormedit", ".bms", ".bme", ".bml", ".pms",
                                  ".bmson", ".sm", ".ssc", ".osu"};
    for (const auto* extension : accepted)
        CHECK(wormedit::sys::is_supported_project_file_extension(
            std::filesystem::path{"chart" + std::string{extension}}));
    CHECK(wormedit::sys::is_supported_project_file_extension("UPPER.SSC"));
    CHECK_FALSE(wormedit::sys::is_supported_project_file_extension("song.mp3"));
    CHECK_FALSE(wormedit::sys::is_supported_project_file_extension("chart.qua"));
}

TEST_CASE("opened chart resolves audio beside its source before a native project exists",
          "[integration][sys][project-open]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "song.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Asset source\n#BPM 120\n"
                           "#WAV01 audio/keysound-c4.ogg\n#00111:01\n";
    const auto source = fixture("audio", "keysound-c4.ogg");
    REQUIRE(std::filesystem::copy_file(source, temporary.path / "audio/keysound-c4.ogg"));

    const auto opened = wormedit::sys::open_project_file(chart);
    INFO(opened.error);
    REQUIRE(opened.document);
    REQUIRE_FALSE(opened.document->audio_assets.empty());
    CHECK(opened.asset_base == temporary.path);
    CHECK(std::filesystem::exists(opened.asset_base /
                                  opened.document->audio_assets.front().source_path));
}

TEST_CASE("opening a native project preserves its saved document identity",
          "[unit][sys][project-open]") {
    const auto imported = wormedit::sys::open_project_file(
        fixture("bmson", "zero-resolution-default.bmson"));
    REQUIRE(imported.document.has_value());
    const auto serialized = wormedit::write_native_project(*imported.document);
    REQUIRE(serialized.bytes.has_value());
    temporary_project_file_t file{".wormedit"};
    file.write(*serialized.bytes);

    const auto opened = wormedit::sys::open_project_file(file.path);
    INFO(opened.error);
    REQUIRE(opened.document.has_value());
    check_native_identity(opened, *imported.document, file.path);
}

TEST_CASE("an imported chart saves and reopens with its assets rooted at the source",
          "[unit][sys][project-open][project-save-paths]") {
    const auto imported = wormedit::sys::open_project_file(
        fixture("bms", "repeated-bgm-ratio-stop-collision.bms"));
    INFO(imported.error);
    REQUIRE(imported.document);
    REQUIRE_FALSE(imported.document->audio_assets.empty());
    temporary_project_file_t file{".wormedit"};
    auto saved_document = *imported.document;
    wormedit::sys::rebase_project_paths(saved_document, imported.asset_base,
                                         file.path.parent_path());
    const auto saved = wormedit::publish_native_project_atomic(file.path, saved_document);
    INFO((saved.diagnostics.empty() ? "" : saved.diagnostics.front().message));
    REQUIRE(saved.published);

    const auto reopened = wormedit::sys::open_project_file(file.path);
    INFO(reopened.error);
    REQUIRE(reopened.document);
    REQUIRE_FALSE(reopened.document->audio_assets.empty());
    CHECK((reopened.asset_base / reopened.document->audio_assets.front().source_path)
              .lexically_normal() ==
          (imported.asset_base / imported.document->audio_assets.front().source_path)
              .lexically_normal());
}

TEST_CASE("project opening rejects unsupported extensions",
          "[unit][sys][project-open]") {
    const auto unsupported = wormedit::sys::open_project_file(
        fixture("bmson", "zero-resolution-default.bmson").replace_extension(".txt"));
    CHECK_FALSE(unsupported.document);
    CHECK(unsupported.error.find("Unsupported") != std::string::npos);

}

TEST_CASE("project opening reports missing native files", "[unit][sys][project-open]") {
    temporary_project_file_t missing{".wormedit"};
    const auto missing_result = wormedit::sys::open_project_file(missing.path);
    CHECK_FALSE(missing_result.document);
    CHECK_FALSE(missing_result.error.empty());
}

TEST_CASE("project opening enforces the chart source size limit",
          "[unit][sys][project-open]") {
    temporary_project_file_t oversized{".bms"};
    oversized.write({});
    std::filesystem::resize_file(oversized.path, 16U * 1024U * 1024U + 1U);
    const auto result = wormedit::sys::open_project_file(oversized.path);
    CHECK_FALSE(result.document);
    CHECK(result.error.find("size limit") != std::string::npos);
}

TEST_CASE("project opening rejects malformed native files", "[unit][sys][project-open]") {
    temporary_project_file_t malformed{".wormedit"};
    malformed.write("not a project");
    const auto malformed_result = wormedit::sys::open_project_file(malformed.path);
    CHECK_FALSE(malformed_result.document);
    CHECK_FALSE(malformed_result.native_project);
    CHECK(malformed_result.error.find("Native project could not be opened") != std::string::npos);
}

TEST_CASE("ambiguous non-ASCII BMS can be retried with an explicit encoding",
          "[unit][sys][project-open][encoding]") {
    temporary_project_file_t file{".bms"};
    file.write("#PLAYER 1\n#TITLE Caf\xE9\n#BPM 120\n#WAV01 key.wav\n#00111:01\n");

    const auto automatic = wormedit::sys::open_project_file(file.path);
    REQUIRE_FALSE(automatic.document);
    CHECK(automatic.needs_encoding_choice);
    CHECK(automatic.source_path == file.path);

    if (!wormedit::is_bms_text_encoding_supported(wormedit::text_encoding_t::windows_1252))
        return;
    const auto explicit_encoding = wormedit::sys::open_project_file(
        file.path, wormedit::text_encoding_t::windows_1252);
    INFO(explicit_encoding.error);
    REQUIRE(explicit_encoding.document);
    CHECK_FALSE(explicit_encoding.needs_encoding_choice);
    CHECK(explicit_encoding.document->metadata.title == "Caf\xC3\xA9");
}

TEST_CASE("UTF-8 BMS without a BOM opens after the user chooses UTF-8",
          "[unit][sys][project-open][encoding]") {
    temporary_project_file_t file{".bms"};
    file.write("#PLAYER 1\n#TITLE Caf\xC3\xA9\n#BPM 120\n#WAV01 key.wav\n#00111:01\n");

    CHECK(wormedit::sys::open_project_file(file.path).needs_encoding_choice);
    const auto opened = wormedit::sys::open_project_file(file.path,
                                                         wormedit::text_encoding_t::utf8);
    INFO(opened.error);
    REQUIRE(opened.document);
    CHECK_FALSE(opened.needs_encoding_choice);
    CHECK(opened.document->metadata.title == "Caf\xC3\xA9");
}

TEST_CASE("a malformed explicit BMS encoding does not repeatedly request a choice",
          "[unit][sys][project-open][encoding]") {
    temporary_project_file_t file{".bms"};
    file.write("#PLAYER 1\n#TITLE Caf\xE9\n#BPM 120\n#WAV01 key.wav\n#00111:01\n");

    const auto malformed = wormedit::sys::open_project_file(file.path,
                                                             wormedit::text_encoding_t::utf8);
    CHECK_FALSE(malformed.document);
    CHECK_FALSE(malformed.needs_encoding_choice);
    CHECK(malformed.error.find("malformed UTF-8") != std::string::npos);
}
