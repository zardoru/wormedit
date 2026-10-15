#include "sys/project_open.h"
#include "sys/project_save_paths.h"
#include "sys/project_view.h"

#include <wormedit/core/command.h>
#include <wormedit/core/startup_session.h>
#include <wormedit/core/workspace.h>

#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

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

void check_single_audio_asset(const wormedit::sys::project_open_result_t& opened,
                              const std::filesystem::path& resolved_path,
                              std::string_view authored_name) {
    INFO(opened.error);
    REQUIRE(opened.document);
    REQUIRE(opened.document->audio_assets.size() == 1);
    REQUIRE(opened.document->resources.size() == 1);
    CHECK(opened.document->audio_assets.front().source_path == resolved_path);
    CHECK(opened.document->resources.front().name == authored_name);
}

auto bmson_layer_source(std::string channel_name = "audio/shared.wav") -> std::string {
    using json = nlohmann::json;
    const auto note = [](std::uint32_t x, std::uint32_t y) {
        return nlohmann::json{{"x", x}, {"y", y}, {"l", 0}, {"c", false}};
    };
    const json channels{
        {{"name", channel_name}, {"notes", json::array({note(1, 0), note(0, 240)})}},
        {{"name", channel_name}, {"notes", json::array({note(2, 480), note(0, 720)})}},
    };
    const json source{
        {"version", "1.0.0"},
        {"info", {{"title", "Layer import"}, {"artist", "a"}, {"genre", "g"},
                   {"mode_hint", "beat-7k"}, {"chart_name", ""}, {"level", 1},
                   {"init_bpm", 120}, {"resolution", 240}, {"judge_rank", 100},
                   {"total", 100}, {"subtitle", ""}, {"subartists", json::array()},
                   {"back_image", ""}, {"banner_image", ""}, {"eyecatch_image", ""}}},
        {"lines", json::array({{{"y", 0}}, {{"y", 960}}})},
        {"bpm_events", json::array()}, {"stop_events", json::array()},
        {"sound_channels", channels},
    };
    return source.dump();
}

auto layer_for_sound_event(const wormedit::chart_document_t& chart,
                           wormedit::object_id_t event_id)
    -> std::optional<wormedit::layer_id_t> {
    for (std::size_t index = 0; index < chart.sound_channels.size(); ++index) {
        const auto& events = chart.sound_channels[index].events;
        if (std::ranges::find(events, event_id, &wormedit::sound_channel_event_t::id) !=
            events.end())
            return wormedit::layer_id_t{index + 1};
    }
    return std::nullopt;
}

auto channel_membership_matches(const wormedit::chart_document_t& chart) -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.sound_channel_event) {
                const auto layer = layer_for_sound_event(chart, *note.sound_channel_event);
                if (!layer || note.semantic_layers != std::vector{*layer}) return false;
            }
        }
        for (const auto& event : measure.bgm_events) {
            if (event.sound_channel_event) {
                const auto layer = layer_for_sound_event(chart, *event.sound_channel_event);
                if (!layer || event.semantic_layers != std::vector{*layer}) return false;
            }
        }
    }
    return true;
}

auto same_layers_and_membership(const wormedit::chart_document_t& left,
                               const wormedit::chart_document_t& right) -> bool {
    if (left.semantic_layers.size() != right.semantic_layers.size() ||
        left.measures.size() != right.measures.size()) return false;
    for (std::size_t index = 0; index < left.semantic_layers.size(); ++index) {
        const auto& a = left.semantic_layers[index];
        const auto& b = right.semantic_layers[index];
        if (a.id != b.id || a.name != b.name || a.color != b.color ||
            a.visible != b.visible || a.locked != b.locked) return false;
    }
    for (std::size_t index = 0; index < left.measures.size(); ++index) {
        const auto& a = left.measures[index];
        const auto& b = right.measures[index];
        if (a.notes.size() != b.notes.size() || a.bgm_events.size() != b.bgm_events.size())
            return false;
        for (std::size_t object = 0; object < a.notes.size(); ++object)
            if (a.notes[object].semantic_layers != b.notes[object].semantic_layers) return false;
        for (std::size_t object = 0; object < a.bgm_events.size(); ++object)
            if (a.bgm_events[object].semantic_layers != b.bgm_events[object].semantic_layers)
                return false;
    }
    return true;
}

void clear_layer_membership(wormedit::song_document_t& document) {
    for (auto& chart : document.charts) {
        chart.semantic_layers.clear();
        for (auto& measure : chart.measures) {
            for (auto& note : measure.notes) note.semantic_layers.clear();
            for (auto& event : measure.bgm_events) event.semantic_layers.clear();
        }
    }
}

auto has_default_layer_warning(const wormedit::sys::project_open_result_t& opened) -> bool {
    return std::ranges::any_of(opened.warnings, [](const auto& warning) {
        return warning.find("BMSON semantic layers were not initialized:") != std::string::npos;
    });
}

auto opened_stepmania_music_is_selected() -> bool {
    const auto opened = wormedit::sys::open_project_file(
        fixture("stepmania", "matched-timing.sm"));
    if (!opened.document || opened.document->audio_assets.size() != 1 ||
        opened.document->resources.size() != 1)
        return false;
    const auto& resource = opened.document->resources.front();
    if (opened.document->audio_assets.front().source_path != "fixture.ogg" ||
        resource.roles.size() != 1 || resource.roles.front() != wormedit::resource_role_t::music)
        return false;
    const auto binding = wormedit::workspace_controller_t::from(*opened.document);
    return binding.workspace &&
           binding.workspace->transport.audio_asset == opened.document->audio_assets.front().id;
}

auto stepmania_music_case_fallback_exports_resolved_path() -> bool {
    temporary_project_directory_t temporary;
    const auto chart_path = temporary.path / "case.sm";
    {
        std::ofstream music(temporary.path / "song.ogg", std::ios::binary);
        music.put('\0');
        std::ofstream chart(chart_path, std::ios::binary);
        chart << "#MUSIC:Song.OGG;#BPMS:0=120;#NOTES:\n"
                 "dance-single:\nTest:\nHard:\n5:\n0,0,0,0,0:\n1000\n0000\n0000\n0000\n;";
    }
    const auto opened = wormedit::sys::open_project_file(chart_path);
    if (!opened.document || opened.document->audio_assets.size() != 1 ||
        opened.document->resources.size() != 1 ||
        opened.document->audio_assets.front().source_path != "song.ogg" ||
        opened.document->resources.front().missing ||
        opened.document->resources.front().target_identities.empty() ||
        opened.document->resources.front().target_identities.front().identifier != "Song.OGG")
        return false;
    const auto written = wormedit::write_stepmania(*opened.document);
    return written.value && written.value->find("#MUSIC:song.ogg;") != std::string::npos;
}

} // namespace

namespace {
template <typename T>
void check_bmson_condition(const T& condition) { CHECK(static_cast<bool>(condition)); }
template <typename T>
void require_bmson_condition(const T& condition) { REQUIRE(static_cast<bool>(condition)); }
} // namespace

TEST_CASE("BMSON import creates channel layers and native open preserves authored layer state",
          "[integration][sys][project-open][bmson-layer]") {
    temporary_project_file_t source{".bmson"};
    source.write(bmson_layer_source());
    const auto imported = wormedit::sys::open_project_file(source.path);
    INFO(imported.error);
    require_bmson_condition(imported.document);
    require_bmson_condition(imported.document->charts.size() == 1);
    const auto& chart = imported.document->charts.front();
    require_bmson_condition(chart.sound_channels.size() == 2);
    check_bmson_condition(chart.sound_channels[0].source_clip.source ==
                          chart.sound_channels[1].source_clip.source);
    require_bmson_condition(chart.semantic_layers.size() == 2);
    check_bmson_condition(chart.semantic_layers[0].name == "shared");
    check_bmson_condition(chart.semantic_layers[1].name == "shared");
    check_bmson_condition(chart.semantic_layers[0].id != chart.semantic_layers[1].id);
    check_bmson_condition(chart.semantic_layers[0].color != chart.semantic_layers[1].color);
    check_bmson_condition(channel_membership_matches(chart));

    const auto with_layers = wormedit::write_bmson(*imported.document);
    require_bmson_condition(with_layers.value);
    auto without_layers = *imported.document;
    clear_layer_membership(without_layers);
    const auto without_layer_data = wormedit::write_bmson(without_layers);
    require_bmson_condition(without_layer_data.value);
    check_bmson_condition(with_layers.value == without_layer_data.value);

    temporary_project_file_t native{".wormedit"};
    const auto saved = wormedit::write_native_project(*imported.document);
    require_bmson_condition(saved.value);
    native.write(*saved.value);
    const auto reopened = wormedit::sys::open_project_file(native.path);
    INFO(reopened.error);
    require_bmson_condition(reopened.document);
    check_bmson_condition(reopened.native_project);
    check_bmson_condition(same_layers_and_membership(
        chart, reopened.document->charts.front()));

    temporary_project_file_t native_without_layers{".wormedit"};
    const auto unlayered_saved = wormedit::write_native_project(without_layers);
    require_bmson_condition(unlayered_saved.value);
    native_without_layers.write(*unlayered_saved.value);
    const auto unlayered_reopened = wormedit::sys::open_project_file(native_without_layers.path);
    INFO(unlayered_reopened.error);
    require_bmson_condition(unlayered_reopened.document);
    check_bmson_condition(unlayered_reopened.document->charts.front().semantic_layers.empty());
}

TEST_CASE("BMSON import warns and remains open when semantic layer defaults cannot be named",
          "[integration][sys][project-open][bmson-layer]") {
    temporary_project_file_t source{".bmson"};
    source.write(bmson_layer_source(std::string(4097, 'x') + ".wav"));
    const auto opened = wormedit::sys::open_project_file(source.path);
    INFO(opened.error);
    require_bmson_condition(opened.document);
    check_bmson_condition(opened.document->charts.front().semantic_layers.empty());
    check_bmson_condition(has_default_layer_warning(opened));
}

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

TEST_CASE("osu imports surface bounded timing quantization warnings",
          "[unit][sys][project-open][osu-mania]") {
    temporary_project_file_t temporary{".osu"};
    temporary.write("osu file format v14\n[General]\nMode:3\n"
                    "[Metadata]\nTitle:Quantized\nArtist:Test\nCreator:Test\nVersion:Test\n"
                    "[Difficulty]\nCircleSize:4\n[TimingPoints]\n"
                    "0,500,4,2,0,100,1,0\n1000.123456789,400,4,2,0,100,1,0\n"
                    "[HitObjects]\n64,192,1000,1,0,0:0:0:0:\n");
    const auto opened = wormedit::sys::open_project_file(temporary.path);
    INFO(opened.error);
    REQUIRE(opened.document);
    CHECK(opened.error.empty());
    REQUIRE(opened.warnings.size() == 1);
    CHECK(opened.warnings.front().find("960") != std::string::npos);
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

TEST_CASE("opened StepMania music becomes the selected transport asset",
          "[integration][sys][project-open][stepmania-audio]") {
    CHECK(opened_stepmania_music_is_selected());
}

TEST_CASE("StepMania music path fallback clears missing and exports resolved spelling",
          "[integration][sys][project-open][stepmania-audio]") {
    CHECK(stepmania_music_case_fallback_exports_resolved_path());
}

TEST_CASE("BMS audio fallback finds a differently cased Ogg beside its chart",
          "[integration][sys][project-open][bms-audio]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "song.bme";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Audio fallback\n#BPM 120\n"
                           "#WAV01 AuDiO\\KEYSOUND-C4.WAV\n#00111:01\n";
    REQUIRE(std::filesystem::copy_file(fixture("audio", "keysound-c4.ogg"),
                                       temporary.path / "audio/keysound-c4.ogg"));

    const auto opened = wormedit::sys::open_project_file(chart);
    check_single_audio_asset(opened, "audio/keysound-c4.ogg", "AuDiO\\KEYSOUND-C4.WAV");
    CHECK(opened.warnings.empty());
    const auto exported = wormedit::write_bms(*opened.document);
    REQUIRE(exported.bytes);
    CHECK(exported.bytes->find("#WAV01 AuDiO\\KEYSOUND-C4.WAV") != std::string::npos);
}

TEST_CASE("BMS audio fallback keeps an existing exact source",
          "[integration][sys][project-open][bms-audio]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "song.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Exact audio\n#BPM 120\n"
                           "#WAV01 audio/source.wav\n#00111:01\n";
    std::ofstream(temporary.path / "audio/source.wav") << "exact source";
    std::ofstream(temporary.path / "audio/source.ogg") << "alternate source";

    const auto opened = wormedit::sys::open_project_file(chart);
    check_single_audio_asset(opened, "audio/source.wav", "audio/source.wav");
    CHECK(opened.warnings.empty());
}

TEST_CASE("BMS audio fallback reports competing matches and keeps the authored path",
          "[integration][sys][project-open][bms-audio]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "song.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Ambiguous audio\n#BPM 120\n"
                           "#WAV01 audio/source.mp3\n#00111:01\n";
    std::ofstream(temporary.path / "audio/source.wav") << "first candidate";
    std::ofstream(temporary.path / "audio/SOURCE.OGG") << "second candidate";

    const auto opened = wormedit::sys::open_project_file(chart);
    check_single_audio_asset(opened, "audio/source.mp3", "audio/source.mp3");
    REQUIRE(opened.warnings.size() == 1);
    CHECK(opened.warnings.front().find("source.wav") != std::string::npos);
    CHECK(opened.warnings.front().find("SOURCE.OGG") != std::string::npos);
}

TEST_CASE("BMS audio fallback accepts a uniquely matched FLAC file",
          "[integration][sys][project-open][bms-audio]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "song.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE FLAC fallback\n#BPM 120\n"
                           "#WAV01 audio/sample.wav\n#00111:01\n";
    std::ofstream(temporary.path / "audio/SAMPLE.FLAC") << "sample";

    const auto opened = wormedit::sys::open_project_file(chart);
    check_single_audio_asset(opened, "audio/SAMPLE.FLAC", "audio/sample.wav");
    CHECK(opened.warnings.empty());
}

TEST_CASE("BMS audio fallback reports case-folded duplicate filenames",
          "[integration][sys][project-open][bms-audio]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "song.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Duplicate audio\n#BPM 120\n"
                           "#WAV01 audio/sample.wav\n#00111:01\n";
    std::ofstream(temporary.path / "audio/SAMPLE.OGG") << "first candidate";
    if (std::filesystem::exists(temporary.path / "audio/sample.ogg")) {
        SUCCEED("This filesystem folds filename case and cannot represent both candidates");
        return;
    }
    std::ofstream(temporary.path / "audio/sample.ogg") << "second candidate";

    const auto opened = wormedit::sys::open_project_file(chart);
    check_single_audio_asset(opened, "audio/sample.wav", "audio/sample.wav");
    REQUIRE(opened.warnings.size() == 1);
    CHECK(opened.warnings.front().find("SAMPLE.OGG") != std::string::npos);
    CHECK(opened.warnings.front().find("sample.ogg") != std::string::npos);
}

TEST_CASE("BMS audio fallback rejects traversal and drive paths",
          "[integration][sys][project-open][bms-audio]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "song.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Unsafe audio\n#BPM 120\n"
                           "#WAV01 ..\\outside.wav\n#WAV02 C:\\outside.wav\n"
                           "#00111:0102\n";

    const auto opened = wormedit::sys::open_project_file(chart);
    INFO(opened.error);
    REQUIRE(opened.document);
    REQUIRE(opened.document->audio_assets.size() == 2);
    CHECK((opened.document->audio_assets[0].source_path == "..\\outside.wav" &&
           opened.document->audio_assets[1].source_path == "C:\\outside.wav"));
    REQUIRE(opened.warnings.size() == 2);
    CHECK((opened.warnings[0].find("Unsafe") != std::string::npos &&
           opened.warnings[1].find("Unsafe") != std::string::npos));
}

TEST_CASE("BMS image resolver finds one alternate extension and keeps the authored name",
          "[integration][sys][project-open][bms-image]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "image.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Image fallback\n#BPM 120\n"
                            "#BMP01 cover.png\n#BMP02 exact.png\n#00104:0102\n";
    std::ofstream(temporary.path / "cover.bmp") << "image";
    std::ofstream(temporary.path / "exact.png") << "exact image";
    std::ofstream(temporary.path / "exact.bmp") << "alternate image";

    const auto opened = wormedit::sys::open_project_file(chart);
    INFO(opened.error);
    require_bmson_condition(opened.document);
    require_bmson_condition(opened.document->visual_assets.size() == 2);
    check_bmson_condition(opened.document->visual_assets[0].source_path == "cover.bmp");
    check_bmson_condition(opened.document->visual_assets[1].source_path == "exact.png");
    check_bmson_condition(opened.warnings.empty());
    CHECK_FALSE(opened.document->resources[0].missing);
    check_bmson_condition(opened.document->resources[0].name == "cover.png");
    const auto exported = wormedit::write_bms(*opened.document);
    require_bmson_condition(exported.bytes);
    check_bmson_condition(exported.bytes->find("#BMP01 cover.png") != std::string::npos);
}

TEST_CASE("BMS image resolver leaves ambiguous alternate extensions unresolved",
          "[integration][sys][project-open][bms-image]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "ambiguous.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Ambiguous image\n#BPM 120\n"
                            "#BMP01 scene.png\n#00104:01\n";
    std::ofstream(temporary.path / "scene.bmp") << "bitmap";
    std::ofstream(temporary.path / "scene.JPG") << "jpeg";

    const auto opened = wormedit::sys::open_project_file(chart);
    INFO(opened.error);
    require_bmson_condition(opened.document);
    require_bmson_condition(opened.document->visual_assets.size() == 1);
    check_bmson_condition(opened.document->visual_assets.front().source_path == "scene.png");
    require_bmson_condition(opened.warnings.size() == 1);
    check_bmson_condition(opened.warnings.front().find("scene.bmp") != std::string::npos);
    check_bmson_condition(opened.warnings.front().find("scene.JPG") != std::string::npos);
    check_bmson_condition(opened.warnings.front().find("Ambiguous") != std::string::npos);
    check_bmson_condition(opened.document->resources.front().missing);
}

TEST_CASE("BMS image resolver matches directory and extension case safely",
          "[integration][sys][project-open][bms-image]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "case.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Case image\n#BPM 120\n"
                            "#BMP01 Folder/ICON.PNG\n#00104:01\n";
    std::filesystem::create_directories(temporary.path / "folder");
    std::ofstream(temporary.path / "folder/icon.JpEg") << "image";

    const auto opened = wormedit::sys::open_project_file(chart);
    INFO(opened.error);
    require_bmson_condition(opened.document);
    require_bmson_condition(opened.document->visual_assets.size() == 1);
    check_bmson_condition(opened.document->visual_assets.front().source_path == "folder/icon.JpEg");
    check_bmson_condition(opened.warnings.empty());
    CHECK_FALSE(opened.document->resources.front().missing);
}

TEST_CASE("opening a native project preserves its saved document identity",
          "[unit][sys][project-open]") {
    const auto imported = wormedit::sys::open_project_file(
        fixture("bmson", "zero-resolution-default.bmson"));
    REQUIRE(imported.document.has_value());
    const auto serialized = wormedit::write_native_project(*imported.document);
    REQUIRE(serialized.value.has_value());
    temporary_project_file_t file{".wormedit"};
    file.write(*serialized.value);

    const auto opened = wormedit::sys::open_project_file(file.path);
    INFO(opened.error);
    REQUIRE(opened.document.has_value());
    check_native_identity(opened, *imported.document, file.path);
}

TEST_CASE("BMSON beat source x lanes survive export and native project reopen",
          "[integration][sys][project-open][bmson-layout]") {
    temporary_project_directory_t temporary;
    const auto chart_path = temporary.path / "beat.bmson";
    std::ofstream(chart_path) << R"({"version":"1.0.0","info":{"title":"Lane order",
        "artist":"a","genre":"g","mode_hint":"beat-7k","chart_name":"",
        "level":1,"init_bpm":120,"resolution":240,"judge_rank":100,"total":100,
        "subtitle":"","subartists":[],"back_image":"","banner_image":"",
        "eyecatch_image":""},"lines":[{"y":0},{"y":960}],"bpm_events":[],
        "stop_events":[],"sound_channels":[{"name":"sound.wav","notes":[
        {"x":8,"y":0,"l":0,"c":false},{"x":1,"y":240,"l":0,"c":false}]}]})";
    const auto imported = wormedit::sys::open_project_file(chart_path);
    INFO(imported.error);
    require_bmson_condition(imported.document);
    const auto& chart = imported.document->charts.front();
    require_bmson_condition(chart.measures.front().notes.size() == 2);
    check_bmson_condition(chart.measures.front().notes[0].lane.value == 100008);
    check_bmson_condition(chart.measures.front().notes[1].lane.value == 100001);
    const auto& layout = chart.lane_layouts.front().layout;
    require_bmson_condition(layout.lanes.size() == 8);
    check_bmson_condition(layout.lanes.front().id.value == 100008);
    check_bmson_condition(layout.lanes.front().display_order == 0);

    const auto exported = wormedit::write_bmson(*imported.document);
    INFO((exported.diagnostics.empty() ? "" : exported.diagnostics.front().message));
    require_bmson_condition(exported.value);
    const auto reimported = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{exported.value->data(), exported.value->size()}));
    require_bmson_condition(reimported.value);
    const auto& exported_chart = reimported.value->charts.front();
    require_bmson_condition(exported_chart.measures.front().notes.size() == 2);
    check_bmson_condition(exported_chart.measures.front().notes[0].lane.value == 100008);
    check_bmson_condition(exported_chart.measures.front().notes[1].lane.value == 100001);

    temporary_project_file_t native{".wormedit"};
    const auto saved = wormedit::write_native_project(*imported.document);
    require_bmson_condition(saved.value);
    native.write(*saved.value);
    const auto reopened = wormedit::sys::open_project_file(native.path);
    INFO(reopened.error);
    require_bmson_condition(reopened.document);
    const auto& reopened_chart = reopened.document->charts.front();
    require_bmson_condition(reopened_chart.measures.front().notes.size() == 2);
    check_bmson_condition(reopened_chart.measures.front().notes[0].lane.value == 100008);
    check_bmson_condition(reopened_chart.measures.front().notes[1].lane.value == 100001);
    require_bmson_condition(reopened_chart.lane_layouts.front().layout.lanes.size() == 8);
    check_bmson_condition(reopened_chart.lane_layouts.front().layout.lanes.front().id.value == 100008);
    check_bmson_condition(reopened_chart.lane_layouts.front().layout.lanes.front().display_order == 0);
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
    const auto saved = wormedit::create_native_project_file(file.path, saved_document);
    INFO((saved.diagnostics.empty() ? "" : saved.diagnostics.front().message));
    REQUIRE(saved.value);

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

namespace {
void check_normalized_native_document(const wormedit::song_document_t& document,
                                      std::uint64_t saved_revision) {
    CHECK(document.charts.front().input_profile.bindings.size() == 4);
    CHECK(document.revision == saved_revision);
    const auto history = wormedit::document_history_t::from(document);
    CHECK_FALSE(wormedit::is_document_dirty(history));
    CHECK(history.undo.empty());
}
} // namespace

TEST_CASE("native load normalizes empty lane keys without making an edit", "[project-open][default-input]") {
    auto imported = wormedit::sys::open_project_file(fixture("stepmania", "matched-timing.sm"));
    REQUIRE(imported.document);
    imported.document->charts.front().input_profile = {};
    const auto saved = wormedit::write_native_project(*imported.document);
    REQUIRE(saved.value);
    temporary_project_file_t file{".wormedit"};
    file.write(*saved.value);
    const auto opened = wormedit::sys::open_project_file(file.path);
    REQUIRE(opened.document);
    check_normalized_native_document(*opened.document, imported.document->revision);
}

TEST_CASE("native load preserves deliberately unbound custom profiles", "[project-open][default-input]") {
    auto imported = wormedit::sys::open_project_file(fixture("stepmania", "matched-timing.sm"));
    REQUIRE(imported.document);
    imported.document->charts.front().input_profile = {.profile_id = "custom-unbound"};
    const auto saved = wormedit::write_native_project(*imported.document);
    REQUIRE(saved.value);
    temporary_project_file_t file{".wormedit"};
    file.write(*saved.value);
    const auto opened = wormedit::sys::open_project_file(file.path);
    REQUIRE(opened.document);
    CHECK(opened.document->charts.front().input_profile.profile_id == "custom-unbound");
    CHECK(opened.document->charts.front().input_profile.bindings.empty());
}

namespace {
void check_preserved_lane_binding(const wormedit::lane_binding_t& expected,
                                  const wormedit::lane_binding_t& actual) {
    CHECK(actual.key == expected.key);
    CHECK(actual.lane == expected.lane);
    CHECK(actual.focused_stage == expected.focused_stage);
}
void check_preserved_input_profile(const wormedit::input_profile_t& expected,
                                   const wormedit::input_profile_t& actual) {
    REQUIRE_FALSE(expected.bindings.empty());
    CHECK(actual.profile_id == expected.profile_id);
    REQUIRE(actual.bindings.size() == expected.bindings.size());
    CHECK(actual.stage_count == expected.stage_count);
    CHECK(actual.requires_focused_stage == expected.requires_focused_stage);
    for (std::size_t index = 0; index < expected.bindings.size(); ++index)
        check_preserved_lane_binding(expected.bindings[index], actual.bindings[index]);
}
} // namespace

TEST_CASE("project opening keeps imported BMS lane profiles", "[project-open][default-input]") {
    const auto path = fixture("bms", "profiles/bms-5k-free-zone.bms");
    std::ifstream stream(path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    wormedit::bms_parse_options_t options;
    options.lane_profile_id = "bms-5k-outside-scratch-free-zone";
    const auto parsed = wormedit::parse_bms_bytes(
        std::as_bytes(std::span{source.data(), source.size()}), options);
    REQUIRE(parsed.song);
    const auto saved = wormedit::write_native_project(*parsed.song);
    REQUIRE(saved.value);
    temporary_project_file_t file{".wormedit"};
    file.write(*saved.value);
    const auto opened = wormedit::sys::open_project_file(file.path);
    REQUIRE(opened.document);
    check_preserved_input_profile(parsed.song->charts.front().input_profile,
                                  opened.document->charts.front().input_profile);
}

namespace {
auto load_validation_fixture() -> wormedit::sys::project_open_result_t {
    auto opened = wormedit::sys::open_project_file(fixture("stepmania", "matched-timing.sm"));
    REQUIRE(opened.document);
    opened.document->charts.front().metadata.name = "Hard validation fixture";
    return opened;
}

auto native_bytes(const wormedit::song_document_t& document) -> std::string {
    const auto saved = wormedit::write_native_project(document);
    REQUIRE(saved.value);
    return *saved.value;
}

void misfile_last_note(wormedit::song_document_t& document) {
    auto& measures = document.charts.front().measures;
    REQUIRE(measures.size() >= 2);
    REQUIRE_FALSE(measures.front().notes.empty());
    auto note = std::move(measures.front().notes.back());
    measures.front().notes.pop_back();
    measures.back().notes.push_back(std::move(note));
}

void check_repaired_document(const wormedit::sys::project_open_result_t& opened,
                             const std::string& before) {
    REQUIRE(opened.document);
    CHECK(opened.error.empty());
    CHECK(native_bytes(*opened.document) == before);
    REQUIRE(opened.warnings.size() == 1);
    CHECK(opened.warnings.front() == "Repaired 1 objects stored in the wrong measure");
}

void check_contextual_validation_message(const std::string& message) {
    CHECK(message.find("Hard validation fixture") != std::string::npos);
    CHECK(message.find("measure 1") != std::string::npos);
    CHECK(message.find("row ") != std::string::npos);
    CHECK(message.find("lane ") != std::string::npos);
    CHECK(message.find("object ") != std::string::npos);
}
} // namespace

TEST_CASE("project_open validates and safely rehomes parsed notes without changing positions",
          "[project-open][validation]") {
    auto opened = load_validation_fixture();
    const auto before = native_bytes(*opened.document);
    misfile_last_note(*opened.document);
    REQUIRE_FALSE(wormedit::validate_document(*opened.document).empty());
    wormedit::sys::validate_opened_document(opened);
    check_repaired_document(opened, before);
}

TEST_CASE("project_open refuses mixed storage and unrepairable graph errors",
          "[project-open][validation]") {
    auto opened = load_validation_fixture();
    misfile_last_note(*opened.document);
    opened.document->charts.front().measures.back().notes.back().lane = wormedit::lane_id_t{99999};
    wormedit::sys::validate_opened_document(opened);
    CHECK_FALSE(opened.document);
    CHECK_FALSE(opened.native_project);
    CHECK(opened.warnings.empty());
    check_contextual_validation_message(opened.error);
}

TEST_CASE("project_open validation leaves valid documents and load identity unchanged",
          "[project-open][validation]") {
    auto opened = load_validation_fixture();
    const auto before = native_bytes(*opened.document);
    opened.native_project = true;
    opened.warnings.push_back("existing asset warning");
    const auto source = opened.source_path;
    wormedit::sys::validate_opened_document(opened);
    REQUIRE(opened.document);
    CHECK(native_bytes(*opened.document) == before);
    CHECK(opened.native_project);
    CHECK(opened.source_path == source);
    CHECK(opened.warnings == std::vector<std::string>{"existing asset warning"});
}

TEST_CASE("project_open rehomes BGM storage without altering authored slots or provenance",
          "[project-open][validation]") {
    auto opened = wormedit::sys::open_project_file(fixture("bms", "repeated-bgm-ratio-stop-collision.bms"));
    REQUIRE(opened.document);
    const auto before = native_bytes(*opened.document);
    auto& measures = opened.document->charts.front().measures;
    const auto source = std::ranges::find_if(measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    REQUIRE(source != measures.end());
    auto& target = source == measures.begin() ? measures.back() : measures.front();
    auto event = std::move(source->bgm_events.back());
    source->bgm_events.pop_back();
    target.bgm_events.push_back(std::move(event));
    wormedit::sys::validate_opened_document(opened);
    check_repaired_document(opened, before);
}

TEST_CASE("project_open errors identify another chart and exact object location",
          "[project-open][validation]") {
    auto opened = load_validation_fixture();
    auto history = wormedit::document_history_t::from(*opened.document);
    const auto before = native_bytes(history.document);
    auto proposed = history.document;
    proposed.charts.front().measures.front().notes.front().lane = wormedit::lane_id_t{99999};
    const auto rejected = wormedit::commit_document_command(history, proposed, "Invalid proposal");
    CHECK_FALSE(rejected.applied);
    REQUIRE_FALSE(rejected.diagnostics.empty());
    check_contextual_validation_message(rejected.diagnostics.front().message);
    CHECK(native_bytes(history.document) == before);
}

TEST_CASE("project_open refuses invalid native files with contextual graph diagnostics",
          "[project-open][validation]") {
    const auto source = load_validation_fixture();
    auto json = nlohmann::json::parse(native_bytes(*source.document));
    json.at("document").at("charts").at(0).at("measures").at(0).at("notes").at(0)["lane"] = 99999;
    temporary_project_file_t file{".wormedit"};
    file.write(json.dump());
    const auto opened = wormedit::sys::open_project_file(file.path);
    CHECK_FALSE(opened.document);
    CHECK_FALSE(opened.native_project);
    check_contextual_validation_message(opened.error);
}

TEST_CASE("an opened BMSON chart gets number-row key bindings for its lanes",
          "[unit][sys][project-open]") {
    const auto opened = wormedit::sys::open_project_file(
        fixture("bmson", "zero-resolution-default.bmson"));
    INFO(opened.error);
    REQUIRE(opened.document.has_value());
    const auto& chart = opened.document->charts.front();
    CHECK(chart.input_profile.profile_id != "bmson-unbound");
    CHECK_FALSE(chart.input_profile.bindings.empty());
}

TEST_CASE("BMSON audio names resolve to the differently extended file beside the chart",
          "[integration][sys][project-open][bms-audio]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "song.bmson";
    std::ofstream(chart) << R"({"version":"1.0.0","info":{"title":"t","artist":"a","genre":"g",
        "mode_hint":"beat-7k","chart_name":"","level":1,"init_bpm":120,"resolution":240,
        "judge_rank":100,"total":100,"subtitle":"","subartists":[],"back_image":"","banner_image":"",
        "eyecatch_image":""},"lines":[{"y":0},{"y":960}],"bpm_events":[],"stop_events":[],
        "sound_channels":[{"name":"clap.wav","notes":[{"x":0,"y":0,"l":0,"c":false}]},
                          {"name":"gone.wav","notes":[{"x":0,"y":240,"l":0,"c":false}]}]})";
    REQUIRE(std::filesystem::copy_file(fixture("audio", "keysound-c4.ogg"), temporary.path / "clap.ogg"));

    const auto opened = wormedit::sys::open_project_file(chart);
    INFO(opened.error);
    REQUIRE(opened.document.has_value());
    const auto& assets = opened.document->audio_assets;
    const auto clap = std::ranges::find(assets, std::filesystem::path{"clap.ogg"}, &wormedit::audio_asset_t::source_path);
    REQUIRE(clap != assets.end());
    // Only the file that exists is redirected; an absent one keeps its authored name.
    CHECK(std::ranges::any_of(assets, [](const auto& asset) { return asset.source_path == "gone.wav"; }));
    const auto resource = std::ranges::find(opened.document->resources, clap->id, &wormedit::resource_entry_t::asset);
    REQUIRE(resource != opened.document->resources.end());
    CHECK_FALSE(resource->missing);
}

TEST_CASE("BMS visual resolver preserves exact declarations over alternatives",
          "[integration][sys][project-open][bms-image]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "exact.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Exact\n#BPM 120\n#BMP01 scene.png\n#00104:01\n";
    std::ofstream(temporary.path / "scene.png") << "png";
    std::ofstream(temporary.path / "scene.bmp") << "bmp";
    const auto opened = wormedit::sys::open_project_file(chart);
    require_bmson_condition(opened.document);
    check_bmson_condition(opened.document->visual_assets.front().source_path == "scene.png");
    check_bmson_condition(opened.warnings.empty());
}

TEST_CASE("BMS video fallback preserves its declared basename for export",
          "[integration][sys][project-open][bms-video]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "video.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Video\n#BPM 120\n#BMP01 movie.mpg\n#00104:01\n";
    std::ofstream(temporary.path / "movie.MP4") << "video";
    const auto opened = wormedit::sys::open_project_file(chart);
    require_bmson_condition(opened.document);
    check_bmson_condition(opened.document->visual_assets.front().source_path == "movie.MP4");
    check_bmson_condition(opened.warnings.empty());
    const auto exported = wormedit::write_bms(*opened.document);
    require_bmson_condition(exported.bytes);
    check_bmson_condition(exported.bytes->find("#BMP01 movie.mpg") != std::string::npos);
    std::ofstream(temporary.path / "movie.webm") << "other";
    const auto ambiguous = wormedit::sys::open_project_file(chart);
    require_bmson_condition(ambiguous.document);
    check_bmson_condition(ambiguous.document->visual_assets.front().source_path == "movie.mpg");
    require_bmson_condition(ambiguous.warnings.size() == 1);
    check_bmson_condition(ambiguous.warnings.front().find("movie.MP4, ") != std::string::npos);
    check_bmson_condition(ambiguous.warnings.front().find("movie.webm") != std::string::npos);
}

TEST_CASE("BMS visual fallback rejects traversal and drive declarations",
          "[integration][sys][project-open][bms-image]") {
    temporary_project_directory_t temporary;
    const auto chart = temporary.path / "unsafe.bms";
    std::ofstream(chart) << "#PLAYER 1\n#TITLE Unsafe\n#BPM 120\n"
        "#BMP01 ../outside.png\n#BMP02 C:\\outside.mpg\n#00104:0102\n";
    const auto opened = wormedit::sys::open_project_file(chart);
    require_bmson_condition(opened.document);
    require_bmson_condition(opened.warnings.size() == 2);
    check_bmson_condition(opened.warnings[0].find("Unsafe") != std::string::npos);
    check_bmson_condition(opened.warnings[1].find("Unsafe") != std::string::npos);
}

namespace {
void check_open_audio(bool condition, std::source_location source = std::source_location::current()) {
    INFO("Open audio check at " << source.line());
    CHECK(condition);
}
void require_open_audio(bool condition, std::source_location source = std::source_location::current()) {
    INFO("Open audio requirement at " << source.line());
    REQUIRE(condition);
}
} // namespace

TEST_CASE("Opening audio resources marks absent files without changing authored tokens",
          "[unit][sys][project-open][missing-audio]") {
    temporary_project_directory_t directory;
    const auto path = directory.path / "missing-audio.bms";
    std::ofstream(directory.path / "present.wav") << "present but not decodable";
    std::ofstream(path) << "#PLAYER 1\n#TITLE Missing audio\n#BPM 120\n"
                           "#WAV01 absent.wav\n#WAV02 present.wav\n#00111:0102\n";
    const auto opened = wormedit::sys::open_project_file(path);
    INFO(opened.error);
    require_open_audio(opened.document.has_value());
    require_open_audio(opened.document->resources.size() == 2);
    check_open_audio(opened.document->resources[0].missing);
    check_open_audio(!opened.document->resources[1].missing);
    check_open_audio(opened.document->audio_assets[0].source_path == "absent.wav");
    check_open_audio(opened.document->audio_assets[1].source_path == "present.wav");
    check_open_audio(!wormedit::is_document_dirty(wormedit::document_history_t::from(*opened.document)));
}

TEST_CASE("Native audio availability refreshes saved flags and aliases without an edit",
          "[unit][sys][project-open][missing-audio]") {
    temporary_project_directory_t directory;
    const auto source = directory.path / "source.bms";
    std::ofstream(directory.path / "present.wav") << "corrupt but present";
    std::ofstream(source) << "#PLAYER 1\n#TITLE Availability\n#BPM 120\n"
                             "#WAV01 absent.wav\n#WAV02 present.wav\n#00111:0102\n";
    auto imported = wormedit::sys::open_project_file(source);
    require_open_audio(imported.document.has_value());
    auto& document = *imported.document;
    document.resources[0].missing = false;
    document.resources[1].missing = true;
    auto alias = document.resources[0];
    alias.id = wormedit::resource_id_t{1000};
    alias.target_identities.clear();
    document.resources.push_back(alias);
    const auto saved = wormedit::write_native_project(document);
    require_open_audio(saved.value.has_value());
    const auto path = directory.path / "availability.wormedit";
    std::ofstream(path) << *saved.value;
    const auto opened = wormedit::sys::open_project_file(path);
    INFO(opened.error);
    require_open_audio(opened.document.has_value());
    check_open_audio(opened.document->resources[0].missing);
    check_open_audio(!opened.document->resources[1].missing);
    check_open_audio(opened.document->resources[2].missing);
    check_open_audio(opened.document->revision == document.revision);
    check_open_audio(!wormedit::is_document_dirty(wormedit::document_history_t::from(*opened.document)));
}

TEST_CASE("Native startup refreshes availability before creating clean saved history",
          "[unit][sys][project-open][missing-audio][startup-session]") {
    temporary_project_directory_t directory;
    const auto source = directory.path / "startup.bms";
    std::ofstream(directory.path / "present.wav") << "corrupt but present";
    std::ofstream(source) << "#PLAYER 1\n#TITLE Startup\n#BPM 120\n"
                             "#WAV01 absent.wav\n#WAV02 present.wav\n#00111:0102\n";
    auto imported = wormedit::sys::open_project_file(source);
    require_open_audio(imported.document.has_value());
    imported.document->resources[0].missing = false;
    imported.document->resources[1].missing = true;
    const auto path = directory.path / "startup.wormedit";
    require_open_audio(wormedit::create_native_project_file(path, *imported.document).value);
    auto startup = wormedit::prepare_native_project_startup({}, {.primary_path = path});
    require_open_audio(startup.loaded_primary);
    check_open_audio(!startup.document.resources[0].missing);
    wormedit::sys::refresh_project_audio_availability(startup.document, directory.path);
    check_open_audio(startup.document.resources[0].missing);
    check_open_audio(!startup.document.resources[1].missing);
    check_open_audio(startup.document.revision == imported.document->revision);
    check_open_audio(startup.document.audio_assets[0].source_path == "absent.wav");
    const auto history = wormedit::document_history_t::from(std::move(startup.document), 128, true);
    check_open_audio(!wormedit::is_document_dirty(history));
    check_open_audio(history.undo.empty());
}

TEST_CASE("Audio availability requires a base for relative sources and accepts absolute sources",
          "[unit][sys][project-open][missing-audio]") {
    temporary_project_directory_t directory;
    const auto absolute = directory.path / "present.wav";
    std::ofstream(absolute) << "corrupt but present";
    const auto relative = std::filesystem::relative(absolute, std::filesystem::current_path());
    wormedit::song_document_t document;
    document.audio_assets = {{.id = wormedit::asset_id_t{1}, .source_path = relative},
                             {.id = wormedit::asset_id_t{2}, .source_path = absolute}};
    document.resources = {{.id = wormedit::resource_id_t{1}, .asset = wormedit::asset_id_t{1}},
                          {.id = wormedit::resource_id_t{2}, .asset = wormedit::asset_id_t{2}}};
    require_open_audio(std::filesystem::is_regular_file(relative));
    wormedit::sys::refresh_project_audio_availability(document, {});
    check_open_audio(document.resources[0].missing);
    check_open_audio(!document.resources[1].missing);
}

namespace {
void check_project_boundary(bool value) { CHECK(value); }
}

TEST_CASE("Recovered session clears the external player source path", "[sys][ownership]") {
    wormedit::sys::project_identity_t current;
    current.session_loaded = true;
    current.external_player_source_path = "previous-chart.bms";
    wormedit::sys::project_chart_runtime_t charts;
    wormedit::sys::begin_project_session(current, charts, {.session_loaded = true});
    check_project_boundary(current.external_player_source_path.empty());
    check_project_boundary(current.session_loaded);
    check_project_boundary(!current.native_project_loaded);
}

TEST_CASE("Project session boundary resets identity and advances generation", "[sys][ownership]") {
    using namespace wormedit::sys;
    project_identity_t current;
    current.fresh_blank_project = true;
    current.pending_import.audio_project_path = "pending.wav";
    current.pending_import.bms_encoding_path = "pending.bms";
    current.pending_import.chart_project_path = "dropped.osu";
    current.active_project_path = "old.wormedit";
    current.asset_base = "old-assets";
    current.external_player_source_path = "old.bms";
    current.scanned_primary_version = wormedit::native_project_document_version_t{
        .project_id = wormedit::song_id_t{1}, .revision = 2};
    project_chart_runtime_t charts;
    charts.session_generation = 7;
    charts.cache.populated = true;
    begin_project_session(current, charts,
        {.session_loaded = true, .native_project_loaded = true,
         .active_project_path = "new.wormedit", .asset_base = "new-assets",
         .scanned_primary_version = wormedit::native_project_document_version_t{
             .project_id = wormedit::song_id_t{2}, .revision = 3}});
    check_project_boundary(charts.session_generation == 8);
    check_project_boundary(!charts.cache.populated);
    check_project_boundary(current.session_loaded && current.native_project_loaded);
    check_project_boundary(!current.fresh_blank_project);
    check_project_boundary(current.pending_import.audio_project_path.empty());
    check_project_boundary(current.pending_import.bms_encoding_path.empty());
    check_project_boundary(current.pending_import.chart_project_path == "dropped.osu");
    check_project_boundary(current.active_project_path == "new.wormedit");
    check_project_boundary(current.asset_base == "new-assets");
    check_project_boundary(current.scanned_primary_version->project_id == wormedit::song_id_t{2});
    check_project_boundary(current.external_player_source_path.empty());
}
