#include <wormedit/core/metadata_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
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

struct temporary_output_drop_directory_t {
    std::filesystem::path path;

    temporary_output_drop_directory_t() {
        static std::atomic<std::uint64_t> sequence{0};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-output-drop-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }

    ~temporary_output_drop_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

} // namespace

TEST_CASE("normalized song and difficulty metadata edits are atomic and undoable",
          "[unit][core][metadata-editor]") {
    auto history = document_history_t::from(metadata_song());
    REQUIRE(apply_metadata_edit(
        history, set_song_metadata_action_t{"New title", "New artist", {"tag one", "tag two"}}));
    CHECK(history.document.metadata.title == "New title");
    CHECK(history.document.metadata.ordered_search_tags ==
          std::vector<std::string>{"tag one", "tag two"});
    REQUIRE(apply_metadata_edit(
        history, set_chart_metadata_action_t{chart_id_t{2}, "Challenge", "New mapper", {{15, 1}}}));
    CHECK(history.document.charts.front().metadata.name == "Challenge");
    CHECK(history.document.charts.front().metadata.rating == signed_rational_t{15, 1});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().metadata.name == "Normal");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.metadata.title == "Old title");
}

TEST_CASE("ordered search-tag occurrence edits preserve reordering and duplicates",
          "[unit][core][metadata-editor][tags]") {
    auto history = document_history_t::from(metadata_song());
    const std::vector<std::string> reordered{"third", "first", "third", "second"};
    const auto edited = apply_metadata_edit(
        history, set_song_metadata_action_t{"Old title", "Old artist", reordered});
    REQUIRE(edited);
    CHECK(history.document.metadata.ordered_search_tags == reordered);
    CHECK(history.undo.size() == 1);

    const auto no_op = apply_metadata_edit(
        history, set_song_metadata_action_t{"Old title", "Old artist", reordered});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.undo.size() == 1);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.metadata.ordered_search_tags.empty());
    REQUIRE(redo_document_command(history));
    CHECK(history.document.metadata.ordered_search_tags == reordered);
}

TEST_CASE("output group path edits are safe and undoable",
          "[unit][core][metadata-editor][output-path]") {
    auto song = metadata_song();
    song.output_groups.push_back({.name = "BMS output",
                                  .family = format_family_t::bms,
                                  .charts = {chart_id_t{2}},
                                  .relative_output_path = "exports/old.bms"});
    auto history = document_history_t::from(std::move(song));
    CHECK_FALSE(apply_metadata_edit(history, set_output_group_path_action_t{1, "exports/new.bms"}));
    for (const auto path : {"/outside.bms", "../outside.bms", "exports/../outside.bms",
                            "exports\\outside.bms", "C:outside.bms", ""}) {
        const auto rejected = apply_metadata_edit(history, set_output_group_path_action_t{0, path});
        CHECK_FALSE(rejected);
        CHECK(has_error(rejected));
    }
    CHECK(history.document.output_groups.front().relative_output_path == "exports/old.bms");
    CHECK(history.undo.empty());
    REQUIRE(apply_metadata_edit(history, set_output_group_path_action_t{0, "exports/new.bms"}));
    CHECK(history.document.output_groups.front().relative_output_path == "exports/new.bms");
    CHECK(history.undo.size() == 1);
    const auto no_op =
        apply_metadata_edit(history, set_output_group_path_action_t{0, "exports/new.bms"});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    REQUIRE(undo_document_command(history));
    CHECK(history.document.output_groups.front().relative_output_path == "exports/old.bms");
    REQUIRE(redo_document_command(history));
    CHECK(history.document.output_groups.front().relative_output_path == "exports/new.bms");
}

TEST_CASE("output path drops resolve only existing project-contained targets",
          "[unit][core][metadata-editor][output-path][drop]") {
    temporary_output_drop_directory_t temporary;
    const auto project = temporary.path / "project";
    const auto exports = project / "exports";
    const auto outside = temporary.path / "outside.bms";
    std::filesystem::create_directories(exports);
    std::ofstream(exports / "chart.bms") << "chart";
    std::ofstream(outside) << "outside";
    CHECK(output_path_from_drop(project, exports) == std::filesystem::path{"exports"});
    CHECK(output_path_from_drop(project, exports / "chart.bms") ==
          std::filesystem::path{"exports/chart.bms"});
    CHECK_FALSE(output_path_from_drop(project, outside));
    CHECK_FALSE(output_path_from_drop(project, project));
    CHECK_FALSE(output_path_from_drop(project, exports / "missing.bms"));
    std::filesystem::create_directories(project / "version.2");
    CHECK_FALSE(output_path_from_drop(project, project / "version.2"));
    std::ofstream(project / "extensionless") << "file";
    CHECK_FALSE(output_path_from_drop(project, project / "extensionless"));
    std::error_code symlink_error;
    std::filesystem::create_symlink(outside, project / "outside-link.bms", symlink_error);
    if (!symlink_error)
        CHECK_FALSE(output_path_from_drop(project, project / "outside-link.bms"));
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

TEST_CASE("rating action helper supports exact set and explicit unset",
          "[unit][core][metadata-editor][rating]") {
    const auto set = chart_rating_edit_t::from(chart_id_t{2}, false, "-9", "12");
    REQUIRE(set);
    CHECK(set->chart == chart_id_t{2});
    CHECK(set->rating == signed_rational_t{-9, 12});
    CHECK_FALSE(chart_rating_edit_t::from(chart_id_t{2}, false, "1", "0"));
    CHECK_FALSE(chart_rating_edit_t::from(chart_id_t{}, true, "", ""));

    auto history = document_history_t::from(metadata_song());
    const set_chart_metadata_action_t apply_set{set->chart, "Staged name", "Staged mapper",
                                                set->rating};
    CHECK_FALSE(history.document.charts.front().metadata.rating);

    REQUIRE(apply_metadata_edit(history, apply_set));
    CHECK(history.document.charts.front().metadata.name == "Staged name");
    CHECK(history.document.charts.front().metadata.author == "Staged mapper");
    CHECK(history.document.charts.front().metadata.rating == signed_rational_t{-9, 12});
    const auto unset = chart_rating_edit_t::from(chart_id_t{2}, true, "ignored", "0");
    REQUIRE(unset);
    const set_chart_metadata_action_t apply_unset{unset->chart, "Still staged", "Still mapper",
                                                  unset->rating};
    REQUIRE(apply_metadata_edit(history, apply_unset));
    CHECK(history.document.charts.front().metadata.name == "Still staged");
    CHECK(history.document.charts.front().metadata.author == "Still mapper");
    CHECK_FALSE(history.document.charts.front().metadata.rating);
}

TEST_CASE("format metadata keeps duplicate occurrences and source identities aligned",
          "[unit][core][metadata-editor]") {
    auto history = document_history_t::from(metadata_song());
    const format_metadata_target_t song_bms{std::nullopt, format_family_t::bms};

    REQUIRE(
        apply_metadata_edit(history, edit_format_metadata_field_action_t{
                                         .target = song_bms,
                                         .index = 1,
                                         .name = "SUBARTIST",
                                         .value = "second guest",
                                         .mode = wormedit::format_metadata_field_mode_t::replace}));
    const auto& replaced = history.document.metadata.format_groups.front();
    CHECK(replaced.ordered_fields[1] == std::pair<std::string, std::string>{"SUBARTIST", "second guest"});
    CHECK(replaced.ordered_field_source_orders == std::vector<std::size_t>{10, 11, 12});

    REQUIRE(
        apply_metadata_edit(history, edit_format_metadata_field_action_t{
                                         .target = song_bms,
                                         .index = 3,
                                         .name = "SUBARTIST",
                                         .value = "third guest",
                                         .mode = wormedit::format_metadata_field_mode_t::insert}));
    const auto& inserted = history.document.metadata.format_groups.front();
    REQUIRE(inserted.ordered_fields.size() == 4);
    CHECK(inserted.ordered_fields[1].first == "SUBARTIST");
    CHECK(inserted.ordered_fields[3] == std::pair<std::string, std::string>{"SUBARTIST", "third guest"});
    CHECK(inserted.ordered_field_source_orders == std::vector<std::size_t>{10, 11, 12});

    REQUIRE(apply_metadata_edit(history, erase_format_metadata_field_action_t{song_bms, 0}));
    const auto& erased = history.document.metadata.format_groups.front();
    CHECK(erased.ordered_fields.front().first == "SUBARTIST");
    CHECK(erased.ordered_field_source_orders == std::vector<std::size_t>{11, 12});

    const format_metadata_target_t chart_osu{chart_id_t{2}, format_family_t::osu_mania};
    REQUIRE(
        apply_metadata_edit(history, edit_format_metadata_field_action_t{
                                         .target = chart_osu,
                                         .index = 0,
                                         .name = "Difficulty.Version",
                                         .value = "Insane",
                                         .mode = wormedit::format_metadata_field_mode_t::insert}));
    const auto& chart_groups = history.document.charts.front().metadata.format_groups;
    REQUIRE(chart_groups.size() == 1);
    CHECK(chart_groups.front().family == format_family_t::osu_mania);
    CHECK(chart_groups.front().ordered_fields ==
          std::vector<std::pair<std::string, std::string>>{{"Difficulty.Version", "Insane"}});
}

TEST_CASE("metadata edits reject unsafe, over-cap, and provenance-breaking actions",
          "[unit][core][metadata-editor][negative]") {
    auto history = document_history_t::from(metadata_song());
    const format_metadata_target_t song_bms{std::nullopt, format_family_t::bms};

    CHECK(history.document.metadata.title == "Old title");

    const auto before_prefix =
        apply_metadata_edit(history, edit_format_metadata_field_action_t{
                                         .target = song_bms,
                                         .index = 1,
                                         .name = "GENRE",
                                         .value = "test",
                                         .mode = wormedit::format_metadata_field_mode_t::insert});
    CHECK_FALSE(before_prefix.applied);
    CHECK(has_error(before_prefix));

    const std::string invalid_utf8{"\xc3\x28", 2};
    const auto invalid =
        apply_metadata_edit(history, edit_format_metadata_field_action_t{
                                         .target = song_bms,
                                         .index = 0,
                                         .name = "SUBTITLE",
                                         .value = invalid_utf8,
                                         .mode = wormedit::format_metadata_field_mode_t::replace});
    CHECK_FALSE(invalid.applied);
    CHECK(has_error(invalid));

    auto limits = metadata_edit_limits_t{};
    limits.maximum_total_metadata_bytes = 4;
    const auto capped = apply_metadata_edit(
        history, set_song_metadata_action_t{"long title", "artist", {}}, limits);
    CHECK_FALSE(capped.applied);
    CHECK(has_error(capped));
    CHECK(history.undo.empty());

    limits = metadata_edit_limits_t{};
    limits.maximum_fields_per_group = 2;
    const auto existing_group_capped = apply_metadata_edit(
        history, set_song_metadata_action_t{"Changed", "Old artist", {}}, limits);
    CHECK_FALSE(existing_group_capped.applied);
    CHECK(has_error(existing_group_capped));
    CHECK(history.document.metadata.title == "Old title");
}

TEST_CASE("semantic metadata no-ops do not dirty or advance the document",
          "[unit][core][metadata-editor]") {
    auto history = document_history_t::from(metadata_song());
    const auto no_op =
        apply_metadata_edit(history, set_song_metadata_action_t{"Old title", "Old artist", {}});
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
    auto history = document_history_t::from(std::move(song));
    const format_metadata_target_t song_bms{std::nullopt, format_family_t::bms};

    REQUIRE(
        apply_metadata_edit(history, edit_format_metadata_field_action_t{
                                         .target = song_bms,
                                         .index = 1,
                                         .name = "SUBARTIST",
                                         .value = "updated",
                                         .mode = wormedit::format_metadata_field_mode_t::replace}));
    CHECK(history.document.metadata.format_groups.front().ordered_fields[1].second == "updated");
    CHECK(history.document.charts.front().metadata.format_groups.front().ordered_fields[1].second ==
          "updated");

    REQUIRE(apply_metadata_edit(history, erase_format_metadata_field_action_t{song_bms, 0}));
    CHECK(history.document.metadata.format_groups.front().ordered_field_source_orders ==
          std::vector<std::size_t>{11, 12});
    CHECK(history.document.charts.front()
              .metadata.format_groups.front()
              .ordered_field_source_orders == std::vector<std::size_t>{11, 12});
}

namespace {
void expect_metadata_batch(bool value) { CHECK(value); }
void require_metadata_batch(bool value) { REQUIRE(value); }
}

TEST_CASE("Chart metadata batch commits selected charts once and preserves names and retained fields", "[unit][core][metadata-editor]") {
    auto song = metadata_song();
    song.charts.push_back({.id = chart_id_t{6}, .metadata = {.name = "Hard", .author = "Other"},
        .measures = {{.id = measure_id_t{7}}},
        .lane_layouts = {{.id = object_id_t{8}, .position = {measure_id_t{7}, 0},
            .layout = {.profile_id = "metadata-test", .lanes = {{.id = lane_id_t{9}, .name = "Lane"}}}}}});
    auto history = document_history_t::from(song);
    const batch_chart_metadata_action_t action{.charts = {chart_id_t{2}, chart_id_t{6}},
        .author = "Batch author", .edit_rating = true, .rating = signed_rational_t{7, 2}};
    require_metadata_batch(static_cast<bool>(apply_batch_chart_metadata_edit(history, action)));
    expect_metadata_batch(static_cast<bool>(history.document.revision == 5));
    expect_metadata_batch(static_cast<bool>(history.undo.size() == 1));
    expect_metadata_batch(static_cast<bool>(history.document.charts[0].metadata.name == "Normal"));
    expect_metadata_batch(static_cast<bool>(history.document.charts[1].metadata.name == "Hard"));
    expect_metadata_batch(static_cast<bool>(history.document.charts[0].metadata.author == "Batch author"));
    expect_metadata_batch(static_cast<bool>(history.document.charts[1].metadata.author == "Batch author"));
    expect_metadata_batch(static_cast<bool>(history.document.charts[0].metadata.rating == signed_rational_t{7, 2}));
    expect_metadata_batch(static_cast<bool>(history.document.charts[1].metadata.rating == signed_rational_t{7, 2}));
    expect_metadata_batch(static_cast<bool>(history.document.metadata.format_groups[0].ordered_fields == song.metadata.format_groups[0].ordered_fields));
    require_metadata_batch(static_cast<bool>(undo_document_command(history)));
    expect_metadata_batch(static_cast<bool>(history.document.charts[0].metadata.author == "Mapper"));
    expect_metadata_batch(static_cast<bool>(history.document.charts[1].metadata.author == "Other"));
    expect_metadata_batch(!static_cast<bool>(history.document.charts[0].metadata.rating));
    require_metadata_batch(static_cast<bool>(redo_document_command(history)));
    expect_metadata_batch(static_cast<bool>(history.document.charts[1].metadata.author == "Batch author"));
}

TEST_CASE("Chart metadata batch rejects the whole selection on invalid target or value", "[unit][core][metadata-editor]") {
    auto history = document_history_t::from(metadata_song());
    batch_chart_metadata_action_t action{.charts = {chart_id_t{2}, chart_id_t{999}}, .author = "Changed"};
    expect_metadata_batch(!static_cast<bool>(apply_batch_chart_metadata_edit(history, action)));
    action.charts = {chart_id_t{2}, chart_id_t{2}};
    expect_metadata_batch(!static_cast<bool>(apply_batch_chart_metadata_edit(history, action)));
    action.charts = {chart_id_t{2}};
    action.author = std::string{"bad\0text", 8};
    expect_metadata_batch(!static_cast<bool>(apply_batch_chart_metadata_edit(history, action)));
    action.author = "Changed";
    action.edit_rating = true;
    action.rating = signed_rational_t{3, 0};
    expect_metadata_batch(!static_cast<bool>(apply_batch_chart_metadata_edit(history, action)));
    action.rating = signed_rational_t{3, 2};
    expect_metadata_batch(!static_cast<bool>(
        apply_batch_chart_metadata_edit(history, action, {.maximum_total_metadata_bytes = 1})));
    expect_metadata_batch(static_cast<bool>(history.document.revision == 4));
    expect_metadata_batch(static_cast<bool>(history.document.charts[0].metadata.author == "Mapper"));
    expect_metadata_batch(!static_cast<bool>(history.document.charts[0].metadata.rating));
    expect_metadata_batch(static_cast<bool>(history.undo.empty()));
    expect_metadata_batch(static_cast<bool>(history.redo.empty()));
}

TEST_CASE("Chart metadata batch supports explicit rating clearing and preserves omitted fields", "[unit][core][metadata-editor]") {
    auto song = metadata_song();
    song.charts[0].metadata.rating = signed_rational_t{11, 3};
    auto history = document_history_t::from(song);
    require_metadata_batch(static_cast<bool>(apply_batch_chart_metadata_edit(
        history, {.charts = {chart_id_t{2}}, .author = "New author"})));
    expect_metadata_batch(static_cast<bool>(history.document.charts[0].metadata.rating == signed_rational_t{11, 3}));
    require_metadata_batch(static_cast<bool>(apply_batch_chart_metadata_edit(
        history, {.charts = {chart_id_t{2}}, .edit_rating = true})));
    expect_metadata_batch(!static_cast<bool>(history.document.charts[0].metadata.rating));
    expect_metadata_batch(static_cast<bool>(history.document.charts[0].metadata.author == "New author"));
    expect_metadata_batch(!static_cast<bool>(apply_batch_chart_metadata_edit(
        history, {.charts = {chart_id_t{2}}, .edit_rating = true})));
    expect_metadata_batch(static_cast<bool>(history.document.revision == 6));
    expect_metadata_batch(static_cast<bool>(history.undo.size() == 2));
}

TEST_CASE("StepMania background fields keep BGA as their single editing owner",
          "[unit][core][metadata-editor]") {
    auto song = metadata_song();
    song.metadata.format_groups.push_back({.family = format_family_t::stepmania,
        .ordered_fields = {{"BACKGROUND", "original.png"}}});
    auto history = document_history_t::from(std::move(song));
    const format_metadata_target_t target{.family = format_family_t::stepmania};
    for (const auto mode : {format_metadata_field_mode_t::insert, format_metadata_field_mode_t::replace}) {
        const auto result = apply_metadata_edit(history, edit_format_metadata_field_action_t{
            .target = target, .index = 0, .name = "background", .value = "other.png", .mode = mode});
        require_metadata_batch(!result);
        require_metadata_batch(!result.diagnostics.empty());
        expect_metadata_batch(result.diagnostics.front().code == diagnostic_code_t::unsupported_feature);
    }
    expect_metadata_batch(!apply_metadata_edit(history, erase_format_metadata_field_action_t{target, 0}));
    expect_metadata_batch(history.document.metadata.format_groups.back().ordered_fields.front().second == "original.png");
}
