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
    const auto begun = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000);
    if (!begun.value.has_value()) return false;
    constexpr std::int64_t target_later_frames = 137;
    const auto preview = begun.value->revise(history.document, target_later_frames);
    if (!preview.value.has_value() || preview.value->payload.delta_frames != target_later_frames) return false;
    const auto committed = apply_proposal(history, *preview.value);
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
    const auto cancel_begin = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000);
    if (!cancel_begin.value.has_value()) return false;
    const auto canceled_preview = cancel_begin.value->revise(history.document, 137);
    if (!canceled_preview.value.has_value() || history.document.charts.front().beat_zero_audio_offset_seconds != original)
        return false;
    return !audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000, true).value.has_value() &&
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
    const auto begun = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun.value.has_value());
    CHECK(begun.value->payload.original_offset_seconds == signed_rational_t{1, 2});

    const auto later = begun.value->revise(history.document, 24'000);
    REQUIRE(later.value.has_value());
    CHECK(later.value->payload.proposed_offset_seconds == signed_rational_t{1, 1});
    const auto earlier = begun.value->revise(history.document, -48'000);
    REQUIRE(earlier.value.has_value());
    CHECK(earlier.value->payload.proposed_offset_seconds == signed_rational_t{-1, 2});

    const auto committed = apply_proposal(history, *later.value);
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
    CHECK_FALSE(audio_offset_drag_t::propose(history.document, chart_id_t{2}, 0).value.has_value());
    const auto locked = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000, true);
    CHECK_FALSE(locked.value.has_value());
    CHECK(has_code(locked.diagnostics, diagnostic_code_t::unsupported_feature));
    const auto begun = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun.value.has_value());

    const auto locked_commit = apply_proposal(history, *begun.value, true);
    CHECK_FALSE(locked_commit);
    CHECK(has_code(locked_commit.diagnostics, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.revision == 9);

    history.document.revision = 10;
    const auto stale = begun.value->revise(history.document, 1);
    test::check_stale_action_message(stale.diagnostics, "Audio offset drag");
    CHECK_FALSE(stale.value.has_value());
    CHECK(has_code(stale.diagnostics, diagnostic_code_t::invalid_id));
    history.document.revision = 9;

    auto extreme = *begun.value;
    extreme.payload.original_offset_seconds = {std::numeric_limits<std::int64_t>::max(),
                                       std::numeric_limits<std::uint64_t>::max()};
    history.document.charts.front().beat_zero_audio_offset_seconds =
        extreme.payload.original_offset_seconds;
    const auto overflow = extreme.revise(history.document, std::numeric_limits<std::int64_t>::max());
    CHECK_FALSE(overflow.value.has_value());
    CHECK(has_code(overflow.diagnostics, diagnostic_code_t::invalid_rational));

    const auto revision = history.document.revision;
    const auto rejected = apply_audio_offset_edit(history, chart_id_t{2}, {1, 0});
    CHECK_FALSE(rejected);
    CHECK(history.document.revision == revision);
}

TEST_CASE("Offset proposal stamps preserve equal-revision project replacement",
          "[unit][core][audio-offset][proposal-stamp]") {
    auto history = document_history_t::from(offset_song());
    const auto begun = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun.value.has_value());
    auto revised = begun.value->revise(history.document, 4'800);
    REQUIRE(revised.value.has_value());
    CHECK(revised.value->stamp == chart_stamp_t::from(history.document, chart_id_t{2}));
    history.document.id = song_id_t{99};
    CHECK(apply_proposal(history, *revised.value).applied);
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds == signed_rational_t{3, 5});
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Offset Apply keeps captured expiry before timing lock",
          "[unit][core][audio-offset][proposal-stamp]") {
    auto history = document_history_t::from(offset_song());
    const auto begun = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun.value.has_value());
    ++history.document.revision;
    const auto result = apply_proposal(history, *begun.value, true);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_id);
    test::check_stale_action_message(result.diagnostics, "Audio offset drag");
    CHECK_FALSE(result.applied);
    CHECK(history.undo.empty());
}

TEST_CASE("Offset Apply keeps original-offset drift separate and ahead of lock",
          "[unit][core][audio-offset][proposal-stamp]") {
    auto history = document_history_t::from(offset_song());
    const auto begun = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000);
    REQUIRE(begun.value.has_value());
    history.document.charts.front().beat_zero_audio_offset_seconds = {1, 3};
    const auto result = apply_proposal(history, *begun.value, true);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().message == "audio offset drag no longer matches the selected chart");
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_id);
    CHECK_FALSE(result.applied);
    CHECK(history.undo.empty());
}

TEST_CASE("Offset builder failures stay owned diagnostics without a valid capture",
          "[unit][core][audio-offset][proposal-stamp]") {
    auto history = document_history_t::from(offset_song());
    const auto rejected = audio_offset_drag_t::propose(history.document, chart_id_t{2}, 48'000, true);
    CHECK_FALSE(rejected.value.has_value());
    REQUIRE(rejected.diagnostics.size() == 1);
    CHECK(rejected.diagnostics.front().code == diagnostic_code_t::unsupported_feature);
    CHECK(rejected.diagnostics.front().message ==
          "audio offset dragging is disabled while timing is locked");
    CHECK(history.document.revision == 9);
    CHECK(history.undo.empty());
}
