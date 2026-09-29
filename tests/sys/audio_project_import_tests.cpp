#include "sys/audio_project_import.h"
#include "sys/application_defaults.h"

#include <wormedit/core/command.h>
#include <wormedit/core/workspace.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string_view>
#include <utility>

namespace {

auto fixture(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path{WORMEDIT_FIXTURE_DIR} / "audio" / name;
}

auto tagged_audio_matches(std::string_view extension) -> bool {
    const auto imported = wormedit::sys::inspect_audio_for_project(
        fixture(std::string{"tagged-project."} + std::string{extension}));
    if (!imported || !imported.duration_seconds || !imported.stream.frame_count)
        return false;
    return imported.title == "Título 雪" && imported.artist == "Björk" &&
           *imported.duration_seconds > 0.1 && *imported.duration_seconds < 1.0 &&
           imported.stream.sample_rate == 44'100 && imported.stream.channels == 2 &&
           *imported.stream.frame_count > 0;
}

TEST_CASE("Audio project import reads embedded Unicode title and artist from MP3 and Ogg",
          "[unit][sys][audio-project-import]") {
    CHECK(tagged_audio_matches("mp3"));
    CHECK(tagged_audio_matches("ogg"));
}

TEST_CASE("Audio project import falls back to the file stem without title tags",
          "[unit][sys][audio-project-import]") {
    const auto result = wormedit::sys::inspect_audio_for_project(fixture("keysound-c4.ogg"));
    REQUIRE(result);
    CHECK(result.title == "keysound-c4");
    CHECK(result.artist.empty());
}

TEST_CASE("Audio project import rejects missing, unsupported and undecodable files",
          "[unit][sys][audio-project-import]") {
    CHECK_FALSE(wormedit::sys::inspect_audio_for_project(fixture("missing.mp3")));
    CHECK_FALSE(wormedit::sys::inspect_audio_for_project(fixture("keysound-c4.wav")));
    CHECK_FALSE(wormedit::sys::inspect_audio_for_project(fixture("README.md")));
}

auto tagged_audio_creates_selectable_project() -> bool {
    using namespace wormedit;
    const auto imported = sys::inspect_audio_for_project(fixture("tagged-project.ogg"));
    if (!imported) return false;
    auto document = sys::audio_project_document(song_id_t{42}, imported.source_path,
                                                imported.title, imported.artist, 4);
    if (!document || document->audio_assets.size() != 1) return false;
    document->audio_assets.front().stream = imported.stream;
    if (document->metadata.title != "Título 雪" || document->metadata.artist != "Björk" ||
        document->charts.size() != 1 || document->charts.front().lane_layouts.size() != 1 ||
        document->charts.front().lane_layouts.front().layout.lanes.size() != 4 ||
        !validate_document(*document).empty()) return false;

    auto history = make_document_history(std::move(*document), 128, true);
    auto bound = make_workspace_controller(history);
    if (!bound) return false;
    auto& workspace = *bound.workspace;
    if (workspace.history.dirty || !workspace.selected_chart) return false;
    const auto selected = apply_workspace_action(
        workspace, history.document, history.document.revision,
        workspace_select_audio_asset_action_t{asset_id_t{6}});
    return selected && workspace.transport.audio_asset == asset_id_t{6};
}

TEST_CASE("Tagged audio creates a valid selectable 4K project",
          "[integration][sys][audio-project-import]") {
    CHECK(tagged_audio_creates_selectable_project());
}

} // namespace
