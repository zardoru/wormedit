#include <wormedit/core/chart_import.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/song_preview.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using wormedit::song_preview_t;

auto osu_source(std::string_view preview_line) -> std::string {
    return "osu file format v14\n[General]\nMode:3\n" + std::string(preview_line) +
           "\n[Editor]\nDistanceSpacing:1\n[Metadata]\nVersion:Preview\n[Difficulty]\nCircleSize:4\n"
           "[TimingPoints]\n0,500,4,0,0,100,1,0\n[HitObjects]\n64,192,0,1,0,0:0:0:0:\n";
}

auto parse_osu(std::string_view source) -> wormedit::song_document_t {
    auto parsed = wormedit::parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    REQUIRE(parsed.value.has_value());
    return std::move(*parsed.value);
}

auto sm_source(std::string_view sample_fields) -> std::string {
    return "#TITLE:Preview;" + std::string(sample_fields) +
           "#OFFSET:0;#BPMS:0=120;#NOTES:\ndance-single:\nTest:\nHard:\n5:\n0,0,0,0,0:\n"
           "1000\n0000\n0000\n0000\n;";
}

auto parse_sm(std::string_view source) -> wormedit::song_document_t {
    auto options = wormedit::stepmania_parse_options_t{};
    options.profile = wormedit::stepmania_profile_t::sm;
    auto parsed = wormedit::parse_stepmania_bytes(std::as_bytes(std::span{source.data(), source.size()}), options);
    REQUIRE(parsed.value.has_value());
    return std::move(*parsed.value);
}

auto write_osu(const wormedit::song_document_t& song) -> wormedit::osu_mania_write_outcome_t {
    auto written = wormedit::write_osu_mania(song, {.chart = song.read_charts().front()->id});
    REQUIRE(written.value.has_value());
    return written;
}

auto write_sm(const wormedit::song_document_t& song) -> std::string {
    const auto written = wormedit::write_stepmania(song, {.profile = wormedit::stepmania_profile_t::sm});
    REQUIRE(written.value.has_value());
    return *written.value;
}

auto count(std::string_view text, std::string_view needle) -> std::size_t {
    std::size_t found{};
    for (auto at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + 1)) ++found;
    return found;
}

auto is_between(std::string_view text, std::string_view before, std::string_view needle,
                std::string_view after) -> bool {
    const auto at = text.find(needle);
    return at != std::string_view::npos && text.find(before) < at && at < text.find(after);
}

auto import_chart(wormedit::document_history_t& history, const wormedit::song_document_t& source)
    -> wormedit::chart_import_proposal_t {
    const std::vector<wormedit::chart_id_t> selected{source.read_charts().front()->id};
    auto plan = wormedit::plan_chart_import(history.document, source, {}, selected);
    const auto result = wormedit::apply_chart_import(history, source, {}, selected);
    REQUIRE(result.command.applied);
    return plan;
}

auto imported_override(const wormedit::document_history_t& history) -> std::optional<song_preview_t> {
    return history.document.read_charts().back()->metadata.preview_override;
}

} // namespace

TEST_CASE("Song preview reads osu! PreviewTime", "[unit][format][song-preview]") {
    CHECK(song_preview_t::from_osu_preview_time(" 12500") == song_preview_t{.start_milliseconds = 12500});
    CHECK_FALSE(song_preview_t::from_osu_preview_time("-1"));
    CHECK_FALSE(song_preview_t::from_osu_preview_time("soon"));
}

TEST_CASE("Song preview reads StepMania sample seconds", "[unit][format][song-preview]") {
    CHECK(song_preview_t::from_stepmania_sample("12.500", "15.000") ==
          song_preview_t{.start_milliseconds = 12500, .length_milliseconds = 15000});
    CHECK(song_preview_t::from_stepmania_sample("12.5004", std::nullopt) ==
          song_preview_t{.start_milliseconds = 12500});
    CHECK(song_preview_t::from_stepmania_sample("3", "0") == song_preview_t{.start_milliseconds = 3000});
    CHECK_FALSE(song_preview_t::from_stepmania_sample("-2", "10"));
}

TEST_CASE("Song preview writes StepMania seconds and validates its span", "[unit][format][song-preview]") {
    const song_preview_t span{.start_milliseconds = 1005, .length_milliseconds = 20};
    CHECK(span.stepmania_sample_start_text() == "1.005");
    CHECK(span.stepmania_sample_length_text() == "0.020");
    CHECK(span.end_milliseconds() == 1025);
    CHECK_FALSE((song_preview_t{.start_milliseconds = 0, .length_milliseconds = 0}).is_valid());
}

TEST_CASE("osu! import sets the song preview and keeps an unchanged spelling",
          "[unit][format][song-preview][osu-mania]") {
    const auto song = parse_osu(osu_source("PreviewTime:12500"));
    CHECK(song.metadata.preview == song_preview_t{.start_milliseconds = 12500});
    CHECK(song.read_charts().front()->metadata.preview_override == std::nullopt);
    const auto written = write_osu(song);
    CHECK(count(*written.value, "PreviewTime") == 1);
    CHECK(written.value->find("PreviewTime:12500") != std::string::npos);
}

TEST_CASE("osu! writes an edited preview in General", "[unit][format][song-preview][osu-mania]") {
    auto song = parse_osu(osu_source("PreviewTime:12500"));
    song.metadata.preview = song_preview_t{.start_milliseconds = 4000};
    const auto written = write_osu(song);
    CHECK(count(*written.value, "PreviewTime") == 1);
    CHECK(is_between(*written.value, "[General]", "PreviewTime: 4000", "[Editor]"));
}

TEST_CASE("osu! drops a cleared preview and keeps a retained -1", "[unit][format][song-preview][osu-mania]") {
    auto song = parse_osu(osu_source("PreviewTime:12500"));
    song.metadata.preview.reset();
    CHECK(count(*write_osu(song).value, "PreviewTime") == 0);
    const auto none = parse_osu(osu_source("PreviewTime: -1"));
    CHECK_FALSE(none.metadata.preview);
    CHECK(write_osu(none).value->find("PreviewTime:-1") != std::string::npos);
}

TEST_CASE("osu! reports a preview length it cannot store", "[unit][format][song-preview][osu-mania]") {
    auto song = parse_osu(osu_source("PreviewTime:12500"));
    song.metadata.preview = song_preview_t{.start_milliseconds = 4000, .length_milliseconds = 9000};
    const auto written = write_osu(song);
    CHECK(written.value->find("PreviewTime: 4000") != std::string::npos);
    CHECK(std::ranges::any_of(written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::osu_mania_write_diagnostic_code_t::lossy_transform &&
               diagnostic.location == "General.PreviewTime";
    }));
}

TEST_CASE("osu! writes a chart preview override", "[unit][format][song-preview][osu-mania]") {
    auto song = parse_osu(osu_source("PreviewTime:12500"));
    song.charts.front().metadata.preview_override = song_preview_t{.start_milliseconds = 777};
    CHECK(write_osu(song).value->find("PreviewTime: 777") != std::string::npos);
}

TEST_CASE("StepMania import sets the preview and keeps unchanged spellings",
          "[unit][format][song-preview][stepmania]") {
    const auto song = parse_sm(sm_source("#SAMPLESTART:12.5004;#SAMPLELENGTH:15;"));
    CHECK(song.metadata.preview == song_preview_t{.start_milliseconds = 12500, .length_milliseconds = 15000});
    const auto written = write_sm(song);
    CHECK(written.find("#SAMPLESTART:12.5004;") != std::string::npos);
    CHECK(written.find("#SAMPLELENGTH:15;") != std::string::npos);
}

TEST_CASE("StepMania writes an edited point-only preview without a length",
          "[unit][format][song-preview][stepmania]") {
    auto song = parse_sm(sm_source("#SAMPLESTART:12.5004;#SAMPLELENGTH:15;"));
    song.metadata.preview = song_preview_t{.start_milliseconds = 30250};
    const auto written = write_sm(song);
    CHECK(written.find("#SAMPLESTART:30.250;") != std::string::npos);
    CHECK(count(written, "SAMPLESTART") == 1);
    CHECK(count(written, "SAMPLELENGTH") == 0);
}

TEST_CASE("StepMania drops both fields for a cleared preview", "[unit][format][song-preview][stepmania]") {
    auto song = parse_sm(sm_source("#SAMPLESTART:12.5004;#SAMPLELENGTH:15;"));
    song.metadata.preview.reset();
    const auto written = write_sm(song);
    CHECK(count(written, "SAMPLESTART") + count(written, "SAMPLELENGTH") == 0);
}

TEST_CASE("Converting osu! to StepMania keeps a point-only preview point-only",
          "[unit][format][song-preview][conversion]") {
    const auto song = parse_osu(osu_source("PreviewTime:12500"));
    const auto written = write_sm(song);
    CHECK(written.find("#SAMPLESTART:12.500;") != std::string::npos);
    CHECK(count(written, "SAMPLELENGTH") == 0);
    CHECK(parse_sm(written).metadata.preview == song.metadata.preview);
}

TEST_CASE("Native projects round-trip the song preview and chart overrides",
          "[unit][format][song-preview][native-project]") {
    auto song = parse_osu(osu_source("PreviewTime:12500"));
    song.metadata.preview->length_milliseconds = 8000;
    song.charts.front().metadata.preview_override = song_preview_t{.start_milliseconds = 99};
    const auto bytes = wormedit::write_native_project(song);
    REQUIRE(bytes.value.has_value());
    const auto parsed = wormedit::parse_native_project(*bytes.value);
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->metadata.preview == song.metadata.preview);
    CHECK(parsed.value->charts.front().metadata.preview_override == song_preview_t{.start_milliseconds = 99});
}

TEST_CASE("Native schemas before 20 lift the preview out of retained osu! fields",
          "[unit][format][song-preview][native-project]") {
    const auto bytes = wormedit::write_native_project(parse_osu(osu_source("PreviewTime:12500")));
    REQUIRE(bytes.value.has_value());
    auto json = nlohmann::json::parse(*bytes.value);
    auto& document = json.at("document");
    document["schema_version"] = GENERATE(16, 17, 18, 19);
    document.at("metadata").erase("preview");
    document.at("charts").at(0).at("metadata").erase("preview_override");
    const auto parsed = wormedit::parse_native_project(json.dump());
    INFO((parsed.diagnostics.empty() ? std::string{} : parsed.diagnostics.front().message));
    REQUIRE(parsed.value.has_value());
    CHECK(parsed.value->metadata.preview == song_preview_t{.start_milliseconds = 12500});
    CHECK(parsed.value->charts.front().metadata.preview_override == std::nullopt);
}

TEST_CASE("Raw metadata edits cannot change preview-owned fields", "[unit][core][song-preview][metadata]") {
    auto history = wormedit::document_history_t::from(parse_osu(osu_source("PreviewTime:12500")));
    const auto& fields = history.document.metadata.format_groups.front().ordered_fields;
    const auto preview = std::ranges::find(fields, std::string{"General.PreviewTime"},
                                           &std::pair<std::string, std::string>::first);
    REQUIRE(preview != fields.end());
    const auto index = static_cast<std::size_t>(preview - fields.begin());
    const auto target = wormedit::format_metadata_target_t{.family = wormedit::format_family_t::osu_mania};
    const auto replaced = wormedit::apply_metadata_edit(history, wormedit::edit_format_metadata_field_action_t{
        .target = target, .index = index, .name = "General.PreviewTime", .value = "1",
        .mode = wormedit::format_metadata_field_mode_t::replace});
    CHECK(replaced.diagnostics.front().code == wormedit::diagnostic_code_t::unsupported_feature);
    const auto erased = wormedit::apply_metadata_edit(history,
        wormedit::erase_format_metadata_field_action_t{.target = target, .index = index});
    const auto inserted = wormedit::apply_metadata_edit(history, wormedit::edit_format_metadata_field_action_t{
        .target = target, .index = fields.size(), .name = "General.PreviewTime", .value = "1"});
    CHECK_FALSE((replaced.applied || erased.applied || inserted.applied));
}

TEST_CASE("Chart import into a song without a preview adopts the imported one",
          "[unit][core][song-preview][chart-import]") {
    auto history = wormedit::document_history_t::from(parse_osu(osu_source("PreviewTime:-1")));
    import_chart(history, parse_osu(osu_source("PreviewTime:5000")));
    CHECK(history.document.metadata.preview == song_preview_t{.start_milliseconds = 5000});
    CHECK(imported_override(history) == std::nullopt);
}

TEST_CASE("Chart import keeps a differing preview as the chart's override",
          "[unit][core][song-preview][chart-import]") {
    auto history = wormedit::document_history_t::from(parse_osu(osu_source("PreviewTime:1000")));
    const auto plan = import_chart(history, parse_osu(osu_source("PreviewTime:5000")));
    CHECK(history.document.metadata.preview == song_preview_t{.start_milliseconds = 1000});
    CHECK(imported_override(history) == song_preview_t{.start_milliseconds = 5000});
    CHECK(std::ranges::any_of(plan.timing_differences, [](const auto& difference) {
        return difference.message.find("preview") != std::string::npos;
    }));
}

TEST_CASE("Chart import with a matching preview needs no override", "[unit][core][song-preview][chart-import]") {
    auto history = wormedit::document_history_t::from(parse_osu(osu_source("PreviewTime:1000")));
    import_chart(history, parse_osu(osu_source("PreviewTime:1000")));
    CHECK(imported_override(history) == std::nullopt);
}

TEST_CASE("Setting the song preview is one undoable step", "[unit][core][song-preview][command]") {
    auto history = wormedit::document_history_t::from(parse_osu(osu_source("PreviewTime:1000")));
    const song_preview_t span{.start_milliseconds = 2000, .length_milliseconds = 15000};
    const auto set = wormedit::apply_song_preview_edit(history, {.preview = span});
    CHECK((set.applied && history.document.metadata.preview == span));
    CHECK(history.undo.back().label == "Set preview");
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.metadata.preview == song_preview_t{.start_milliseconds = 1000});
}

TEST_CASE("Clearing previews and overrides uses distinct Undo labels", "[unit][core][song-preview][command]") {
    auto song = parse_osu(osu_source("PreviewTime:1000"));
    song.charts.front().metadata.preview_override = song_preview_t{.start_milliseconds = 5};
    auto history = wormedit::document_history_t::from(std::move(song));
    const auto chart = history.document.read_charts().front()->id;
    CHECK(wormedit::apply_song_preview_edit(history, {.chart = chart}).applied);
    CHECK((history.undo.back().label == "Use song preview" && !imported_override(history)));
    CHECK(wormedit::apply_song_preview_edit(history, {}).applied);
    CHECK((history.undo.back().label == "Clear preview" && !history.document.metadata.preview));
}

TEST_CASE("Invalid or unchanged previews publish nothing", "[unit][core][song-preview][command]") {
    auto history = wormedit::document_history_t::from(parse_osu(osu_source("PreviewTime:1000")));
    const auto revision = history.document.revision;
    const auto empty = wormedit::apply_song_preview_edit(history,
        {.preview = song_preview_t{.start_milliseconds = 10, .length_milliseconds = 0}});
    const auto same = wormedit::apply_song_preview_edit(history,
        {.preview = song_preview_t{.start_milliseconds = 1000}});
    const auto unknown = wormedit::apply_song_preview_edit(history,
        {.chart = wormedit::chart_id_t{999'999}, .preview = song_preview_t{}});
    INFO(empty.applied << same.applied << unknown.applied);
    CHECK_FALSE((empty.applied || same.applied || unknown.applied));
    CHECK(history.document.revision == revision);
}
