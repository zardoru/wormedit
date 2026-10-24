#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/metadata_inheritance.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/core/autosave_loop.h>
#include <wormedit/formats/stepmania.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/osu_mania.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>
using namespace wormedit;
namespace {
auto inherited_song() -> song_document_t {
    return {.id = song_id_t{51}, .revision = 7,
        .metadata = {.title = "Shared title", .artist = "Shared artist"},
        .charts = {{.id = chart_id_t{52}, .measures = {{.id = measure_id_t{53}}}},
                   {.id = chart_id_t{54}, .measures = {{.id = measure_id_t{55}}}}}};
}
void register_override_audio(song_document_t& song, std::string_view path) {
    if (std::ranges::any_of(song.audio_assets, [&](const auto& asset) { return asset.source_path == std::filesystem::path{path}; })) return;
    const auto next = next_resource_asset_id(song);
    REQUIRE(next);
    song.audio_assets.push_back({.id = *next, .source_path = std::filesystem::path{path}});
}
void check_override(const song_document_t& song, inherited_metadata_field_t field, std::string_view value) {
    CHECK(resolved_metadata_t::from(song, song.charts.front()).value(field) == value);
}
auto fixture_bytes(std::string_view relative) -> std::vector<std::byte> {
    std::ifstream input(std::filesystem::path{WORMEDIT_FIXTURE_DIR} / relative, std::ios::binary);
    REQUIRE(input);
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    const auto view = std::as_bytes(std::span{bytes.data(), bytes.size()});
    return {view.begin(), view.end()};
}
template<class Outcome>
void require_export(const Outcome& output) {
    std::string messages;
    for (const auto& diagnostic : output.diagnostics) messages += diagnostic.message + "\n";
    INFO(messages);
    REQUIRE(output.ok());
    REQUIRE(output.value);
}
void check_export_fields(std::string_view output, std::initializer_list<std::string_view> fields) {
    for (const auto field : fields) CHECK(output.find(field) != std::string_view::npos);
}
void check_bmson_info(const std::string& output) {
    const auto info = nlohmann::json::parse(output).at("info");
    CHECK(info.at("title") == "Local title");
    CHECK(info.at("artist") == "Local artist");
    CHECK(info.at("subtitle") == "");
    CHECK(info.at("preview_music") == "preview.ogg");
}
template<class Outcome> void require_import(const Outcome& imported) {
    REQUIRE(imported.ok());
    if constexpr (requires { imported.value.song; }) REQUIRE(imported.value.song);
    else REQUIRE(imported.value);
}
auto converted_text(const conversion_result_t& converted) -> const std::string& {
    std::string diagnostics;
    for (const auto& diagnostic : converted.diagnostics) diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    REQUIRE(converted);
    REQUIRE(converted.outputs.size() == 1);
    return converted.outputs.front().bytes;
}
void check_anchored_music(song_document_t& song, asset_id_t asset) {
    native_project_autosave_target_t target{.asset_base = std::filesystem::temp_directory_path() / "wormedit-inherited-audio"};
    target.anchor_assets(song);
    CHECK(validate_document(song).empty());
    const auto references = metadata_audio_references_t::from(song, song.charts.front().metadata.overrides);
    REQUIRE(references.ok());
    CHECK(references.value.assets == std::vector<asset_id_t>{asset});
    CHECK(std::filesystem::path{*song.charts.front().metadata.overrides.value(inherited_metadata_field_t::music)}.is_absolute());
}
void require_applied(const document_command_result_t& result) {
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.applied);
}
void check_native_overrides(const song_document_t& song, const metadata_overrides_t& expected) {
    const auto encoded = write_native_project(song);
    REQUIRE(encoded.ok());
    REQUIRE(encoded.value);
    const auto parsed = parse_native_project(*encoded.value);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value);
    CHECK(parsed.value->charts.front().metadata.overrides == expected);
}
}
TEST_CASE("Metadata overrides distinguish inheritance and explicit empty with isolated Undo", "[core][metadata-inheritance]") {
    auto history = document_history_t::from(inherited_song());
    check_override(history.document, inherited_metadata_field_t::title, "Shared title");
    metadata_overrides_t overrides;
    overrides.value(inherited_metadata_field_t::title) = "Local title";
    overrides.value(inherited_metadata_field_t::artist) = "";
    require_applied(apply_metadata_edit(history, set_chart_metadata_overrides_action_t{.chart = chart_id_t{52}, .overrides = overrides}));
    check_override(history.document, inherited_metadata_field_t::title, "Local title");
    check_override(history.document, inherited_metadata_field_t::artist, "");
    CHECK(resolved_metadata_t::from(history.document, history.document.charts.back()).value(inherited_metadata_field_t::title) == "Shared title");
    CHECK(history.document.metadata.title == "Shared title");
    CHECK(undo_document_command(history));
    check_override(history.document, inherited_metadata_field_t::title, "Shared title");
    CHECK(redo_document_command(history));
    check_native_overrides(history.document, overrides);
    overrides.value(inherited_metadata_field_t::title).reset();
    require_applied(apply_metadata_edit(history, set_chart_metadata_overrides_action_t{.chart = chart_id_t{52}, .overrides = overrides}));
    check_override(history.document, inherited_metadata_field_t::title, "Shared title");
}
TEST_CASE("Native schema eleven metadata migrates to inherited slots without replacing authored values", "[core][metadata-inheritance][native_project]") {
    const auto original = inherited_song();
    const auto written = write_native_project(original);
    REQUIRE(written.value);
    auto root = nlohmann::json::parse(*written.value);
    root["document"]["schema_version"] = 11;
    for (auto& chart : root["document"]["charts"]) chart["metadata"].erase("overrides");
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value);
    CHECK(parsed.value->schema_version == current_document_schema_version);
    CHECK(parsed.value->charts.front().metadata.overrides == metadata_overrides_t{});
    CHECK(parsed.value->metadata.title == original.metadata.title);
}
TEST_CASE("Invalid and unsupported metadata overrides reject without publishing", "[core][metadata-inheritance]") {
    auto history = document_history_t::from(inherited_song());
    metadata_overrides_t overrides;
    SECTION("newline") { overrides.value(inherited_metadata_field_t::title) = "bad\nvalue"; }
    SECTION("escaping path") { overrides.value(inherited_metadata_field_t::music) = "../outside.ogg"; }
    SECTION("embedded NUL") { overrides.value(inherited_metadata_field_t::artist) = std::string{"a\0b", 3}; }
    const auto rejected = apply_metadata_edit(history, set_chart_metadata_overrides_action_t{.chart = chart_id_t{52}, .overrides = overrides});
    CHECK_FALSE(rejected.applied);
    CHECK_FALSE(rejected.diagnostics.empty());
    CHECK(history.document.revision == 7);
    CHECK(history.undo.empty());
}
TEST_CASE("StepMania inherits unchanged bytes and resolves common overrides or rejects conflicts", "[core][metadata-inheritance][round-trip]") {
    const auto imported = parse_stepmania_bytes(fixture_bytes("stepmania/dance-solo.ssc"), {.profile = stepmania_profile_t::ssc});
    require_import(imported);
    auto song = *imported.value;
    const auto before = write_stepmania(song, {.profile = stepmania_profile_t::ssc});
    REQUIRE(before.ok());
    song.charts.front().metadata.overrides.value(inherited_metadata_field_t::title) = "Local title";
    register_override_audio(song, "other.ogg");
    song.charts.front().metadata.overrides.value(inherited_metadata_field_t::music) = "other.ogg";
    const auto output = write_stepmania(song, {.profile = stepmania_profile_t::ssc, .chart = song.charts.front().id});
    require_export(output);
    check_export_fields(*output.value, {"#TITLE:Local title;", "#MUSIC:other.ogg;"});
    song.charts.front().metadata.overrides = {};
    CHECK(write_stepmania(song, {.profile = stepmania_profile_t::ssc}).value == before.value);
    auto second = song.charts.front();
    second.id = chart_id_t{999};
    second.metadata.overrides.value(inherited_metadata_field_t::title) = "Different";
    song.charts.push_back(std::move(second));
    CHECK_FALSE(write_stepmania(song, {.profile = stepmania_profile_t::ssc}).ok());
}
TEST_CASE("BMS exports explicit overrides even when untouched source preservation is enabled", "[core][metadata-inheritance][round-trip]") {
    const auto imported = parse_bms_bytes(fixture_bytes("bms/export-beatoraja-encoding.bms"), {.lane_profile_id = "bme-7k-outside-scratch"});
    require_import(imported);
    auto song = *imported.value.song;
    register_override_audio(song, "preview.ogg");
    auto history = document_history_t::from(std::move(song));
    const auto id = history.document.read_charts().front()->id;
    const auto before = write_bms(history.document);
    REQUIRE(before.ok());
    metadata_overrides_t overrides;
    overrides.value(inherited_metadata_field_t::title) = "Local title";
    overrides.value(inherited_metadata_field_t::subtitle) = "Local subtitle";
    overrides.value(inherited_metadata_field_t::genre) = "Local genre";
    overrides.value(inherited_metadata_field_t::preview) = "preview.ogg";
    REQUIRE(apply_metadata_edit(history, set_chart_metadata_overrides_action_t{id, overrides}).applied);
    const auto output = write_bms(history.document);
    require_export(output);
    check_export_fields(*output.value, {"#TITLE Local title", "#SUBTITLE Local subtitle", "#GENRE Local genre", "#PREVIEW preview.ogg"});
    REQUIRE(apply_metadata_edit(history, set_chart_metadata_overrides_action_t{id, {}}).applied);
    CHECK(write_bms(history.document).value == before.value);
    overrides = {};
    overrides.value(inherited_metadata_field_t::music) = "music.ogg";
    CHECK_FALSE(apply_metadata_edit(history, set_chart_metadata_overrides_action_t{id, overrides}).applied);
}
TEST_CASE("BMSON resolves overrides after retained info and resource metadata", "[core][metadata-inheritance][round-trip]") {
    const auto imported = parse_bmson_bytes(fixture_bytes("bmson/slicing-and-mixed-sounds.bmson"));
    require_import(imported);
    auto song = *imported.value;
    const auto before = write_bmson(song);
    REQUIRE(before.ok());
    auto& overrides = song.charts.front().metadata.overrides;
    overrides.value(inherited_metadata_field_t::title) = "Local title";
    overrides.value(inherited_metadata_field_t::artist) = "Local artist";
    overrides.value(inherited_metadata_field_t::subtitle) = "";
    register_override_audio(song, "preview.ogg");
    overrides.value(inherited_metadata_field_t::preview) = "preview.ogg";
    const auto output = write_bmson(song);
    require_export(output);
    check_bmson_info(*output.value);
    overrides = {};
    CHECK(write_bmson(song).value == before.value);
}
TEST_CASE("osu metadata resolves local artist title and music without changing siblings", "[core][metadata-inheritance][round-trip]") {
    const auto imported = parse_osu_mania_bytes(fixture_bytes("osu/mania-tags-bookmarks-kiai.osu"));
    require_import(imported);
    auto song = *imported.value;
    const auto before = write_osu_mania(song);
    REQUIRE(before.ok());
    auto& overrides = song.charts.front().metadata.overrides;
    overrides.value(inherited_metadata_field_t::title) = "Local title";
    overrides.value(inherited_metadata_field_t::artist) = "";
    register_override_audio(song, "other.mp3");
    overrides.value(inherited_metadata_field_t::music) = "other.mp3";
    const auto output = write_osu_mania(song);
    require_export(output);
    check_export_fields(*output.value, {"Title:Local title", "Artist:\n", "AudioFilename: other.mp3"});
    overrides = {};
    CHECK(write_osu_mania(song).value == before.value);
    register_override_audio(song, "preview.ogg");
    overrides.value(inherited_metadata_field_t::preview) = "preview.ogg";
    CHECK_FALSE(write_osu_mania(song).ok());
}
TEST_CASE("Recovery anchoring and export staging preserve override audio references", "[core][metadata-inheritance][autosave][conversion]") {
    const auto imported = parse_osu_mania_bytes(fixture_bytes("osu/mania-tags-bookmarks-kiai.osu"));
    REQUIRE(imported.value);
    auto song = *imported.value;
    REQUIRE_FALSE(song.audio_assets.empty());
    const auto asset = song.audio_assets.front().id;
    const auto source_path = song.audio_assets.front().source_path.generic_string();
    song.charts.front().metadata.overrides.value(inherited_metadata_field_t::music) = source_path;
    const auto converted = convert_document(song, {.target = format_family_t::osu_mania,
        .loss_policy = loss_policy_t::warn,
        .staged_assets = {{.asset = asset, .relative_path = "assets/local.mp3"}}});
    check_export_fields(converted_text(converted), {"AudioFilename: assets/local.mp3"});
    check_anchored_music(song, asset);
}
TEST_CASE("Scoped native metadata overrides follow retained conditional declarations", "[core][metadata-inheritance][round-trip]") {
    const std::string source = "#PLAYER 1\n#TITLE Base\n#ARTIST Artist\n#BPM 120\n#WAV01 tone.wav\n#00011:01\n"
        "#RANDOM 1\n#IF 1\n#GENRE Branch\n#ENDIF\n#ENDRANDOM\n";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch",
         .branch_choices = {{.control_source_order = 6, .selected_value = 1}}});
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value.song);
    auto history = document_history_t::from(*parsed.value.song);
    const auto id = history.document.read_charts().front()->id;
    metadata_overrides_t overrides;
    overrides.value(inherited_metadata_field_t::genre) = "Local";
    REQUIRE(apply_metadata_edit(history, set_chart_metadata_overrides_action_t{id, overrides}).applied);
    const auto preserved = write_bms(history.document);
    require_export(preserved);
    CHECK(preserved.value->find("#RANDOM 1") != std::string::npos);
    CHECK(preserved.value->rfind("#GENRE Local") > preserved.value->find("#GENRE Branch"));
    const auto flattened = write_bms(history.document, {.branch_policy = bms_branch_policy_t::flatten_active});
    require_export(flattened);
    CHECK(flattened.value->find("#GENRE Local") != std::string::npos);
}
TEST_CASE("Inherited audio paths follow current registered assets after saved path publication", "[core][metadata-inheritance]") {
    auto song = inherited_song();
    song.metadata.format_groups = {
        {.family = format_family_t::stepmania, .ordered_fields = {{"MUSIC", "old.ogg"}}},
        {.family = format_family_t::bms, .ordered_fields = {{"PREVIEW", "old-preview.ogg"}}}};
    song.audio_assets = {{.id = asset_id_t{1}, .source_path = "assets/music.ogg"},
        {.id = asset_id_t{2}, .source_path = "assets/preview.ogg"}};
    song.resources = {{.id = resource_id_t{1}, .name = "old.ogg", .asset = asset_id_t{1}, .roles = {resource_role_t::music}},
        {.id = resource_id_t{2}, .name = "old-preview.ogg", .asset = asset_id_t{2}, .roles = {resource_role_t::preview}}};
    const auto resolved = resolved_metadata_t::from(song, song.charts.front());
    CHECK(resolved.value(inherited_metadata_field_t::music) == "assets/music.ogg");
    CHECK(resolved.value(inherited_metadata_field_t::preview) == "assets/preview.ogg");
}
