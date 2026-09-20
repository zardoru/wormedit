#include <wormedit/core/audio_offset_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <ranges>

using namespace wormedit;

namespace {

auto offset_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 9,
        .charts = {{.id = chart_id_t{2},
                    .beat_zero_audio_offset_seconds = {1, 2},
                    .measures = {{.id = measure_id_t{3}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics, [code](const auto& item) { return item.code == code; });
}

} // namespace

TEST_CASE("audio offset drag remains sample-frame exact and commits atomically",
          "[unit][core][audio-offset][drag]") {
    auto history = make_document_history(offset_song());
    const auto begun = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun);
    CHECK(begun.drag->original_offset_seconds == signed_rational_t{1, 2});

    const auto later = update_audio_offset_drag(history.document, *begun.drag, 24'000);
    REQUIRE(later);
    CHECK(later.drag->proposed_offset_seconds == signed_rational_t{1, 1});
    const auto earlier = update_audio_offset_drag(history.document, *begun.drag, -48'000);
    REQUIRE(earlier);
    CHECK(earlier.drag->proposed_offset_seconds == signed_rational_t{-1, 2});

    const auto committed = commit_audio_offset_drag(history, *later.drag);
    REQUIRE(committed);
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds ==
          signed_rational_t{1, 1});
    CHECK(history.document.revision == 10);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds ==
          signed_rational_t{1, 2});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds ==
          signed_rational_t{1, 1});

    const auto revision = history.document.revision;
    const auto unchanged =
        apply_audio_offset_edit(history, revision, chart_id_t{2},
                                history.document.charts.front().beat_zero_audio_offset_seconds);
    CHECK_FALSE(unchanged.applied);
    CHECK(unchanged.diagnostics.empty());
    CHECK(history.document.revision == revision);
}

TEST_CASE("audio offset drag rejects stale locked invalid and overflowing requests",
          "[unit][core][audio-offset][bounds]") {
    auto history = make_document_history(offset_song());
    CHECK_FALSE(begin_audio_offset_drag(history.document, chart_id_t{2}, 0));
    const auto locked = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000, true);
    CHECK_FALSE(locked);
    CHECK(has_code(locked.diagnostics, diagnostic_code_t::unsupported_feature));
    const auto begun = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun);

    const auto locked_commit = commit_audio_offset_drag(history, *begun.drag, true);
    CHECK_FALSE(locked_commit);
    CHECK(has_code(locked_commit.diagnostics, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.revision == 9);

    history.document.revision = 10;
    const auto stale = update_audio_offset_drag(history.document, *begun.drag, 1);
    CHECK_FALSE(stale);
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_id));
    history.document.revision = 9;

    auto extreme = *begun.drag;
    extreme.original_offset_seconds = {std::numeric_limits<std::int64_t>::max(),
                                       std::numeric_limits<std::uint64_t>::max()};
    history.document.charts.front().beat_zero_audio_offset_seconds =
        extreme.original_offset_seconds;
    const auto overflow = update_audio_offset_drag(history.document, extreme,
                                                   std::numeric_limits<std::int64_t>::max());
    CHECK_FALSE(overflow);
    CHECK(has_code(overflow.diagnostics, diagnostic_code_t::invalid_rational));

    const auto revision = history.document.revision;
    const auto rejected = apply_audio_offset_edit(history, revision, chart_id_t{2}, {1, 0});
    CHECK_FALSE(rejected);
    CHECK(history.document.revision == revision);
}
