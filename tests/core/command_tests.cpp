#include <wormedit/core/command.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <ranges>

using namespace wormedit;

namespace {

auto make_song(std::uint64_t row = 130) -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .charts =
            {
                {
                    .id = chart_id_t{1},
                    .measures = {{.id = measure_id_t{1}}},
                    .lane_layouts =
                        {
                            {
                                .id = object_id_t{1},
                                .position = {measure_id_t{1}, 0},
                                .layout =
                                    {
                                        .profile_id = "command-test",
                                        .lanes = {{.id = lane_id_t{1}, .name = "Lane"}},
                                    },
                            },
                        },
                },
            },
    };
    song.charts.front().measures.front().notes.push_back({
        .id = object_id_t{2},
        .position = {measure_id_t{1}, row},
        .lane = lane_id_t{1},
    });
    REQUIRE(validate_document(song).empty());
    return song;
}

auto note_row(const document_history_t& history) -> std::uint64_t {
    return history.document.charts.front().measures.front().notes.front().position.row;
}

} // namespace

TEST_CASE("quantization commits atomically through bounded document history",
          "[unit][core][command]") {
    auto history = make_document_history(make_song(), 2);
    CHECK(history.document.revision == 1);
    CHECK_FALSE(is_document_dirty(history));

    const auto applied = apply_quantize_command(
        history, chart_id_t{1}, {.divisions = 4});
    REQUIRE(applied);
    CHECK(note_row(history) == 240);
    CHECK(history.document.revision == 2);
    CHECK(is_document_dirty(history));
    REQUIRE(applied.quantized_moves.size() == 1);

    REQUIRE(undo_document_command(history));
    CHECK(note_row(history) == 130);
    CHECK(history.document.revision == 1);
    CHECK_FALSE(is_document_dirty(history));
    REQUIRE(redo_document_command(history));
    CHECK(note_row(history) == 240);
    CHECK(history.document.revision == 2);

    mark_document_saved(history);
    CHECK_FALSE(is_document_dirty(history));
    REQUIRE(undo_document_command(history));
    CHECK(is_document_dirty(history));
    REQUIRE(redo_document_command(history));
    CHECK_FALSE(is_document_dirty(history));
}

TEST_CASE("rejected and unknown-chart quantization cannot mutate document history",
          "[unit][core][command][negative]") {
    auto song = make_song(100);
    song.charts.front().measures.front().notes.push_back({
        .id = object_id_t{3},
        .position = {measure_id_t{1}, 110},
        .lane = lane_id_t{1},
    });
    auto history = make_document_history(std::move(song));

    const auto rejected = apply_quantize_command(
        history, chart_id_t{1}, {.divisions = 4});
    CHECK_FALSE(rejected);
    CHECK(history.document.revision == 1);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
    CHECK(history.document.charts.front().measures.front().notes[0].position.row == 100);
    CHECK(history.document.charts.front().measures.front().notes[1].position.row == 110);

    const auto unknown = apply_quantize_command(
        history, chart_id_t{99}, {.divisions = 4});
    CHECK_FALSE(unknown);
    CHECK(history.document.revision == 1);
    CHECK(history.undo.empty());
}

TEST_CASE("new edits clear redo and retain only the configured undo depth",
          "[unit][core][command]") {
    auto history = make_document_history(make_song(), 1);
    REQUIRE(apply_quantize_command(history, chart_id_t{1}, {.divisions = 4}));
    REQUIRE(undo_document_command(history));

    auto proposed = history.document;
    proposed.metadata.title = "branched edit";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename song"));
    CHECK(history.redo.empty());
    CHECK(history.undo.size() == 1);
    CHECK_FALSE(redo_document_command(history));

    proposed = history.document;
    proposed.metadata.artist = "second edit";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename artist"));
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.metadata.title == "branched edit");
    CHECK(history.document.metadata.artist.empty());
}

TEST_CASE("exhausted document revision rejects publication", "[unit][core][command][negative]") {
    auto song = make_song();
    song.revision = std::numeric_limits<std::uint64_t>::max();
    auto history = make_document_history(std::move(song));
    auto proposed = history.document;
    proposed.metadata.title = "not published";
    CHECK_FALSE(commit_document_command(history, std::move(proposed), "Rename song"));
    CHECK(history.document.metadata.title.empty());
    CHECK(history.undo.empty());
}
