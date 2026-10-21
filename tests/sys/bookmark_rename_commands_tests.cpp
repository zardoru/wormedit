#include "../support/stale_action_assertions.h"
#include "sys/bookmarks/bookmark_navigation.h"

#include <wormedit/core/marker_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <source_location>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

void check_rename(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

void require_rename(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}

auto rename_song() -> song_document_t {
    return {
        .id = song_id_t{1},
        .revision = 7,
        .charts = {{
            .id = chart_id_t{2},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{3}}},
            .bookmarks = {
                {.id = object_id_t{4}, .label = "Beat", .anchor = marker_anchor_t::beat,
                 .position = local_position_t{measure_id_t{3}, 315}},
                {.id = object_id_t{5}, .label = "Audio", .anchor = marker_anchor_t::audio_time,
                 .audio_milliseconds = -1234},
            },
        }},
    };
}

} // namespace

TEST_CASE("bookmark rename preserves stable identity and exact beat or audio anchor",
          "[unit][sys][bookmark-rename]") {
    document_history_t history{document_history_t::from(rename_song())};
    const auto beat = bookmark_navigation_t{}.rename_bookmark(history, 7, chart_id_t{2}, object_id_t{4}, "Étage 🎵");
    require_rename(beat.accepted);
    require_rename(history.document.charts.front().bookmarks.size() == 2);
    check_rename(history.undo.size() == 1);
    const auto& renamed_beat = history.document.charts.front().bookmarks.front();
    check_rename(renamed_beat.id == object_id_t{4});
    check_rename(renamed_beat.label == "Étage 🎵");
    check_rename(renamed_beat.anchor == marker_anchor_t::beat);
    check_rename((renamed_beat.position == local_position_t{measure_id_t{3}, 315}));
    check_rename(!renamed_beat.audio_milliseconds);
    check_rename(history.document.charts.front().measures.size() == 1);
    require_rename(undo_document_command(history));
    check_rename(history.document.charts.front().bookmarks.front().label == "Beat");
    check_rename(history.document.charts.front().bookmarks.back().audio_milliseconds == -1234);
    check_rename(history.document.charts.front().measures.size() == 1);
    require_rename(redo_document_command(history));
    check_rename(history.document.charts.front().bookmarks.front().label == "Étage 🎵");
    check_rename((history.document.charts.front().bookmarks.front().position ==
                  local_position_t{measure_id_t{3}, 315}));

    const auto revision = history.document.revision;
    const auto audio = bookmark_navigation_t{}.rename_bookmark(history, revision, chart_id_t{2}, object_id_t{5}, "Intro");
    require_rename(audio.accepted);
    const auto& renamed_audio = history.document.charts.front().bookmarks.back();
    check_rename(renamed_audio.id == object_id_t{5});
    check_rename(renamed_audio.label == "Intro");
    check_rename(renamed_audio.anchor == marker_anchor_t::audio_time);
    check_rename(!renamed_audio.position);
    check_rename(renamed_audio.audio_milliseconds == -1234);
}

TEST_CASE("bookmark rename rejects stale revision and missing bookmark without mutation",
          "[unit][sys][bookmark-rename]") {
    document_history_t history{document_history_t::from(rename_song())};
    const auto stale = bookmark_navigation_t{}.rename_bookmark(history, 6, chart_id_t{2}, object_id_t{4}, "Stale");
    check_rename(!stale.accepted);
    test::check_stale_action_message(stale.message, "Bookmark rename");
    check_rename(history.document.revision == 7);
    check_rename(history.document.charts.front().bookmarks.front().label == "Beat");

    const auto missing = bookmark_navigation_t{}.rename_bookmark(history, 7, chart_id_t{2}, object_id_t{99}, "Missing");
    check_rename(!missing.accepted);
    check_rename(history.document.revision == 7);
    check_rename(history.document.charts.front().bookmarks.front().label == "Beat");
}

TEST_CASE("bookmark rename uses existing empty and UTF-8 label validation",
          "[unit][sys][bookmark-rename]") {
    document_history_t history{document_history_t::from(rename_song())};
    const auto empty = bookmark_navigation_t{}.rename_bookmark(history, 7, chart_id_t{2}, object_id_t{4}, "");
    require_rename(empty.accepted);
    check_rename(history.document.charts.front().bookmarks.front().label.empty());

    const auto revision = history.document.revision;
    const auto invalid = bookmark_navigation_t{}.rename_bookmark(history, revision, chart_id_t{2}, object_id_t{4}, "bad\xFF");
    check_rename(!invalid.accepted);
    check_rename(history.document.revision == revision);
    check_rename(history.document.charts.front().bookmarks.front().label.empty());
}
