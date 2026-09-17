#include <wormedit/core/metadata_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <ranges>

using namespace wormedit;

namespace {

auto metadata_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .metadata = {.title = "Old title",
                     .artist = "Old artist",
                     .format_groups = {{.family = format_family_t::bms,
                                        .ordered_fields = {{"SUBTITLE", "old"},
                                                           {"SUBARTIST", "guest"},
                                                           {"STAGEFILE", "stage.png"}},
                                        .ordered_field_source_orders = {10, 11, 12}}}},
        .charts = {{.id = chart_id_t{2},
                    .metadata = {.name = "Normal", .author = "Mapper"},
                    .measures = {{.id = measure_id_t{3}}},
                    .lane_layouts = {{.id = object_id_t{4},
                                      .position = {measure_id_t{3}, 0},
                                      .layout = {.profile_id = "metadata-test",
                                                 .lanes = {{.id = lane_id_t{5},
                                                            .name = "Lane"}}}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_error(const document_command_result_t& result) -> bool {
    return std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

} // namespace

TEST_CASE("normalized song and difficulty metadata edits are atomic and undoable",
          "[unit][core][metadata-editor]") {
    auto history = make_document_history(metadata_song());
    REQUIRE(apply_metadata_edit(
        history, 4, set_song_metadata_action_t{"New title", "New artist", {"tag one", "tag two"}}));
    CHECK(history.document.metadata.title == "New title");
    CHECK(history.document.metadata.ordered_search_tags ==
          std::vector<std::string>{"tag one", "tag two"});
    REQUIRE(apply_metadata_edit(
        history, 5,
        set_chart_metadata_action_t{chart_id_t{2}, "Challenge", "New mapper", {{15, 1}}}));
    CHECK(history.document.charts.front().metadata.name == "Challenge");
    CHECK(history.document.charts.front().metadata.rating == signed_rational_t{15, 1});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().metadata.name == "Normal");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.metadata.title == "Old title");
}

TEST_CASE("rating text parser preserves exact signed rational values within integer bounds",
          "[unit][core][metadata-editor][rating]") {
    CHECK(parse_metadata_rating("-6", "8") == signed_rational_t{-6, 8});
    CHECK(parse_metadata_rating("+7", "+3") == signed_rational_t{7, 3});
    CHECK(parse_metadata_rating("+7", "+18446744073709551615") == signed_rational_t{7, UINT64_MAX});
    CHECK(parse_metadata_rating("-0", "0002") == signed_rational_t{0, 2});
    CHECK(parse_metadata_rating("-9223372036854775808", "18446744073709551615") ==
          signed_rational_t{INT64_MIN, UINT64_MAX});

    CHECK_FALSE(parse_metadata_rating("", "1"));
    CHECK_FALSE(parse_metadata_rating("+", "1"));
    CHECK_FALSE(parse_metadata_rating("1", "0"));
    CHECK_FALSE(parse_metadata_rating("1", "-2"));
    CHECK_FALSE(parse_metadata_rating("9223372036854775808", "1"));
    CHECK_FALSE(parse_metadata_rating("-9223372036854775809", "1"));
    CHECK_FALSE(parse_metadata_rating("1", "18446744073709551616"));
    CHECK_FALSE(parse_metadata_rating("1x", "2"));
    CHECK_FALSE(parse_metadata_rating("1", "2 "));
    CHECK_FALSE(parse_metadata_rating("000000000000000000000", "1"));
}

TEST_CASE("rating action helper supports exact set and explicit unset through revision checks",
          "[unit][core][metadata-editor][rating]") {
    const auto set = make_chart_rating_metadata_edit(chart_id_t{2}, false, "-9", "12");
    REQUIRE(set);
    CHECK(set->chart == chart_id_t{2});
    CHECK(set->rating == signed_rational_t{-9, 12});
    CHECK_FALSE(make_chart_rating_metadata_edit(chart_id_t{2}, false, "1", "0"));
    CHECK_FALSE(make_chart_rating_metadata_edit(chart_id_t{}, true, "", ""));

    auto history = make_document_history(metadata_song());
    const set_chart_metadata_action_t apply_set{set->chart, "Staged name", "Staged mapper",
                                                set->rating};
    const auto stale = apply_metadata_edit(history, 3, apply_set);
    CHECK_FALSE(stale.applied);
    CHECK(has_error(stale));
    CHECK_FALSE(history.document.charts.front().metadata.rating);

    REQUIRE(apply_metadata_edit(history, 4, apply_set));
    CHECK(history.document.charts.front().metadata.name == "Staged name");
    CHECK(history.document.charts.front().metadata.author == "Staged mapper");
    CHECK(history.document.charts.front().metadata.rating == signed_rational_t{-9, 12});
    const auto unset = make_chart_rating_metadata_edit(chart_id_t{2}, true, "ignored", "0");
    REQUIRE(unset);
    const set_chart_metadata_action_t apply_unset{unset->chart, "Still staged", "Still mapper",
                                                  unset->rating};
    REQUIRE(apply_metadata_edit(history, 5, apply_unset));
    CHECK(history.document.charts.front().metadata.name == "Still staged");
    CHECK(history.document.charts.front().metadata.author == "Still mapper");
    CHECK_FALSE(history.document.charts.front().metadata.rating);
}

TEST_CASE("format metadata keeps duplicate occurrences and source identities aligned",
          "[unit][core][metadata-editor]") {
    auto history = make_document_history(metadata_song());
    const format_metadata_target_t song_bms{std::nullopt, format_family_t::bms};

    REQUIRE(apply_metadata_edit(
        history, 4,
        replace_format_metadata_field_action_t{song_bms, 1, "SUBARTIST", "second guest"}));
    const auto& replaced = history.document.metadata.format_groups.front();
    CHECK(replaced.ordered_fields[1] == std::pair{"SUBARTIST", "second guest"});
    CHECK(replaced.ordered_field_source_orders == std::vector<std::size_t>{10, 11, 12});

    REQUIRE(apply_metadata_edit(
        history, 5,
        insert_format_metadata_field_action_t{song_bms, 3, "SUBARTIST", "third guest"}));
    const auto& inserted = history.document.metadata.format_groups.front();
    REQUIRE(inserted.ordered_fields.size() == 4);
    CHECK(inserted.ordered_fields[1].first == "SUBARTIST");
    CHECK(inserted.ordered_fields[3] == std::pair{"SUBARTIST", "third guest"});
    CHECK(inserted.ordered_field_source_orders == std::vector<std::size_t>{10, 11, 12});

    REQUIRE(apply_metadata_edit(history, 6, erase_format_metadata_field_action_t{song_bms, 0}));
    const auto& erased = history.document.metadata.format_groups.front();
    CHECK(erased.ordered_fields.front().first == "SUBARTIST");
    CHECK(erased.ordered_field_source_orders == std::vector<std::size_t>{11, 12});

    const format_metadata_target_t chart_osu{chart_id_t{2}, format_family_t::osu_mania};
    REQUIRE(apply_metadata_edit(
        history, 7,
        insert_format_metadata_field_action_t{chart_osu, 0, "Difficulty.Version", "Insane"}));
    const auto& chart_groups = history.document.charts.front().metadata.format_groups;
    REQUIRE(chart_groups.size() == 1);
    CHECK(chart_groups.front().family == format_family_t::osu_mania);
    CHECK(chart_groups.front().ordered_fields ==
          std::vector<std::pair<std::string, std::string>>{{"Difficulty.Version", "Insane"}});
}

TEST_CASE("metadata edits reject stale, unsafe, over-cap, and provenance-breaking actions",
          "[unit][core][metadata-editor][negative]") {
    auto history = make_document_history(metadata_song());
    const format_metadata_target_t song_bms{std::nullopt, format_family_t::bms};

    const auto stale =
        apply_metadata_edit(history, 3, set_song_metadata_action_t{"ignored", "ignored", {}});
    CHECK_FALSE(stale.applied);
    CHECK(has_error(stale));
    CHECK(history.document.metadata.title == "Old title");

    const auto before_prefix = apply_metadata_edit(
        history, 4, insert_format_metadata_field_action_t{song_bms, 1, "GENRE", "test"});
    CHECK_FALSE(before_prefix.applied);
    CHECK(has_error(before_prefix));

    const std::string invalid_utf8{"\xc3\x28", 2};
    const auto invalid = apply_metadata_edit(
        history, 4, replace_format_metadata_field_action_t{song_bms, 0, "SUBTITLE", invalid_utf8});
    CHECK_FALSE(invalid.applied);
    CHECK(has_error(invalid));

    auto limits = metadata_edit_limits_t{};
    limits.maximum_total_metadata_bytes = 4;
    const auto capped = apply_metadata_edit(
        history, 4, set_song_metadata_action_t{"long title", "artist", {}}, limits);
    CHECK_FALSE(capped.applied);
    CHECK(has_error(capped));
    CHECK(history.undo.empty());

    limits = metadata_edit_limits_t{};
    limits.maximum_fields_per_group = 2;
    const auto existing_group_capped = apply_metadata_edit(
        history, 4, set_song_metadata_action_t{"Changed", "Old artist", {}}, limits);
    CHECK_FALSE(existing_group_capped.applied);
    CHECK(has_error(existing_group_capped));
    CHECK(history.document.metadata.title == "Old title");
}

TEST_CASE("semantic metadata no-ops do not dirty or advance the document",
          "[unit][core][metadata-editor]") {
    auto history = make_document_history(metadata_song());
    const auto no_op =
        apply_metadata_edit(history, 4, set_song_metadata_action_t{"Old title", "Old artist", {}});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 4);
    CHECK_FALSE(is_document_dirty(history));
}

TEST_CASE("metadata edits synchronize scoped copies of one retained source field",
          "[unit][core][metadata-editor][provenance]") {
    auto song = metadata_song();
    auto source =
        std::make_shared<const std::vector<std::byte>>(std::vector<std::byte>{std::byte{0x23}});
    auto& song_group = song.metadata.format_groups.front();
    song_group.encoding.original_bytes = source;
    song.charts.front().metadata.format_groups.push_back(song_group);
    auto history = make_document_history(std::move(song));
    const format_metadata_target_t song_bms{std::nullopt, format_family_t::bms};

    REQUIRE(apply_metadata_edit(
        history, 4, replace_format_metadata_field_action_t{song_bms, 1, "SUBARTIST", "updated"}));
    CHECK(history.document.metadata.format_groups.front().ordered_fields[1].second == "updated");
    CHECK(history.document.charts.front().metadata.format_groups.front().ordered_fields[1].second ==
          "updated");

    REQUIRE(apply_metadata_edit(history, 5, erase_format_metadata_field_action_t{song_bms, 0}));
    CHECK(history.document.metadata.format_groups.front().ordered_field_source_orders ==
          std::vector<std::size_t>{11, 12});
    CHECK(history.document.charts.front()
              .metadata.format_groups.front()
              .ordered_field_source_orders == std::vector<std::size_t>{11, 12});
}
