#include "../support/stale_action_assertions.h"
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
                    .measures = {{.id = measure_id_t{3}}},
                    .bpms = {{.id = object_id_t{4}, .position = {measure_id_t{3}, 0},
                              .beats_per_minute = {120, 1}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto offset_fit_commit_is_exact_and_undoable() -> bool {
    auto history = document_history_t::from(offset_song());
    const auto original_bpm = history.document.charts.front().bpms.front().beats_per_minute;
    const auto begun = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    if (!begun.is_valid()) return false;
    constexpr std::int64_t target_later_frames = 137;
    const auto preview = revise_audio_offset_drag(history.document, begun,
                                                 target_later_frames);
    if (!preview.is_valid() || preview.delta_frames != target_later_frames) return false;
    const auto committed = apply_audio_offset_drag(history, preview);
    if (!committed || history.document.charts.front().beat_zero_audio_offset_seconds !=
                          signed_rational_t{24'137, 48'000} ||
        history.document.charts.front().bpms.front().beats_per_minute != original_bpm) return false;
    if (!undo_document_command(history)) return false;
    return history.document.charts.front().beat_zero_audio_offset_seconds ==
               signed_rational_t{1, 2} &&
           history.document.charts.front().bpms.front().beats_per_minute == original_bpm;
}

auto offset_fit_cancel_and_lock_leave_timing_unchanged() -> bool {
    auto history = document_history_t::from(offset_song());
    const auto original = history.document.charts.front().beat_zero_audio_offset_seconds;
    const auto cancel_begin = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    if (!cancel_begin.is_valid()) return false;
    const auto canceled_preview = revise_audio_offset_drag(
        history.document, cancel_begin, 137);
    if (!canceled_preview.is_valid() || history.document.charts.front().beat_zero_audio_offset_seconds != original)
        return false;
    return !begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000, true).is_valid() &&
           history.document.charts.front().beat_zero_audio_offset_seconds == original;
}

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics, [code](const auto& item) { return item.code == code; });
}

TEST_CASE("offset fit applies a later beat target as a positive undoable offset",
          "[unit][core][audio-offset][drag]") {
    CHECK(offset_fit_commit_is_exact_and_undoable());
}

TEST_CASE("offset fit cancel and timing lock leave timing unchanged",
          "[unit][core][audio-offset][drag]") {
    CHECK(offset_fit_cancel_and_lock_leave_timing_unchanged());
}

} // namespace

TEST_CASE("audio offset drag remains sample-frame exact and commits atomically",
          "[unit][core][audio-offset][drag]") {
    auto history = document_history_t::from(offset_song());
    const auto begun = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun.is_valid());
    CHECK(begun.original_offset_seconds == signed_rational_t{1, 2});

    const auto later = revise_audio_offset_drag(history.document, begun, 24'000);
    REQUIRE(later.is_valid());
    CHECK(later.proposed_offset_seconds == signed_rational_t{1, 1});
    const auto earlier = revise_audio_offset_drag(history.document, begun, -48'000);
    REQUIRE(earlier.is_valid());
    CHECK(earlier.proposed_offset_seconds == signed_rational_t{-1, 2});

    const auto committed = apply_audio_offset_drag(history, later);
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
    const auto unchanged = apply_audio_offset_edit(
        history, chart_id_t{2}, history.document.charts.front().beat_zero_audio_offset_seconds);
    CHECK_FALSE(unchanged.applied);
    CHECK(unchanged.diagnostics.empty());
    CHECK(history.document.revision == revision);
}

TEST_CASE("audio offset drag rejects stale locked invalid and overflowing requests",
          "[unit][core][audio-offset][bounds]") {
    auto history = document_history_t::from(offset_song());
    CHECK_FALSE(begin_audio_offset_drag(history.document, chart_id_t{2}, 0).is_valid());
    const auto locked = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000, true);
    CHECK_FALSE(locked.is_valid());
    CHECK(has_code(locked.diagnostics, diagnostic_code_t::unsupported_feature));
    const auto begun = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun.is_valid());

    const auto locked_commit = apply_audio_offset_drag(history, begun, true);
    CHECK_FALSE(locked_commit);
    CHECK(has_code(locked_commit.diagnostics, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.revision == 9);

    history.document.revision = 10;
    const auto stale = revise_audio_offset_drag(history.document, begun, 1);
    test::check_stale_action_message(stale.diagnostics, "Audio offset drag");
    CHECK_FALSE(stale.is_valid());
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_id));
    history.document.revision = 9;

    auto extreme = begun;
    extreme.original_offset_seconds = {std::numeric_limits<std::int64_t>::max(),
                                       std::numeric_limits<std::uint64_t>::max()};
    history.document.charts.front().beat_zero_audio_offset_seconds =
        extreme.original_offset_seconds;
    const auto overflow = revise_audio_offset_drag(history.document, extreme,
                                                   std::numeric_limits<std::int64_t>::max());
    CHECK_FALSE(overflow.is_valid());
    CHECK(has_code(overflow.diagnostics, diagnostic_code_t::invalid_rational));

    const auto revision = history.document.revision;
    const auto rejected = apply_audio_offset_edit(history, chart_id_t{2}, {1, 0});
    CHECK_FALSE(rejected);
    CHECK(history.document.revision == revision);
}

TEST_CASE("Offset proposal stamps preserve equal-revision project replacement",
          "[unit][core][audio-offset][proposal-stamp]") {
    auto history = document_history_t::from(offset_song());
    const auto begun = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    auto revised = revise_audio_offset_drag(history.document, begun, 4'800);
    REQUIRE(revised.is_valid());
    CHECK(revised.stamp == chart_stamp_t::from(history.document, chart_id_t{2}));
    history.document.id = song_id_t{99};
    CHECK(apply_audio_offset_drag(history, revised).applied);
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds == signed_rational_t{3, 5});
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Offset Apply keeps captured expiry before timing lock",
          "[unit][core][audio-offset][proposal-stamp]") {
    auto history = document_history_t::from(offset_song());
    const auto begun = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    ++history.document.revision;
    const auto result = apply_audio_offset_drag(history, begun, true);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_id);
    test::check_stale_action_message(result.diagnostics, "Audio offset drag");
    CHECK_FALSE(result.applied);
    CHECK(history.undo.empty());
}

TEST_CASE("Offset Apply keeps original-offset drift separate and ahead of lock",
          "[unit][core][audio-offset][proposal-stamp]") {
    auto history = document_history_t::from(offset_song());
    const auto begun = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000);
    history.document.charts.front().beat_zero_audio_offset_seconds = {1, 3};
    const auto result = apply_audio_offset_drag(history, begun, true);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().message == "audio offset drag no longer matches the selected chart");
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_id);
    CHECK_FALSE(result.applied);
    CHECK(history.undo.empty());
}

TEST_CASE("Offset builder failures stay owned diagnostics without a valid capture",
          "[unit][core][audio-offset][proposal-stamp]") {
    auto history = document_history_t::from(offset_song());
    const auto rejected = begin_audio_offset_drag(history.document, chart_id_t{2}, 48'000, true);
    CHECK_FALSE(rejected.is_valid());
    REQUIRE(rejected.diagnostics.size() == 1);
    const auto result = apply_audio_offset_drag(history, rejected);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().message == rejected.diagnostics.front().message);
    CHECK(history.undo.empty());
}
