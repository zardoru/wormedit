#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/conversion_publication.h>
#include "sys/application_defaults.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

struct temporary_directory_t {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("wormedit-publication-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    temporary_directory_t() {
        std::filesystem::create_directories(path);
    }
    ~temporary_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

auto converted(std::string filename = "song.bmson") -> wormedit::conversion_result_t {
    wormedit::conversion_result_t result;
    result.outputs.push_back({.target = wormedit::format_family_t::bmson,
                              .chart = wormedit::chart_id_t{1},
                              .suggested_filename = std::move(filename),
                              .bytes = "{}",
                              .reimport_validated = true});
    return result;
}

auto read(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("conversion publication resolves chart and staged asset paths",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports/assets");
    std::ofstream(temporary.path / "exports/assets/5-tone.wav") << "sample";
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .charts = {wormedit::chart_id_t{1}},
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bmson,
        .staged_assets = {{wormedit::asset_id_t{5}, "assets/5-tone.wav"}}};
    const auto plan =
        wormedit::plan_conversion_publication(temporary.path, group, request, converted());
    REQUIRE(plan);
    REQUIRE(plan.files.size() == 1);
    CHECK(plan.files.front().project_relative_path == "exports/song.bmson");
    REQUIRE(plan.assets.size() == 1);
    CHECK(plan.assets.front().project_relative_path == "exports/assets/5-tone.wav");
}

TEST_CASE("export asset discovery recovers verified resource copies after session state is lost",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::ofstream(temporary.path / "tone.wav") << "audio";
    std::ofstream(temporary.path / "bgm.wav") << "background";
    wormedit::song_document_t song;
    song.audio_assets.push_back({.id = wormedit::asset_id_t{5}, .source_path = "tone.wav"});
    song.audio_assets.push_back({.id = wormedit::asset_id_t{6}, .source_path = "bgm.wav"});
    song.resources.push_back(
        {.id = wormedit::resource_id_t{7}, .name = "tone.wav", .asset = wormedit::asset_id_t{5}});
    song.charts.push_back({.id = wormedit::chart_id_t{1}});
    song.charts.front().sound_channels.push_back(
        {.id = wormedit::object_id_t{9},
         .name = "bgm.wav",
         .source_clip = {.id = wormedit::clip_id_t{10}, .source = wormedit::asset_id_t{6}}});
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .charts = {wormedit::chart_id_t{1}},
                                         .relative_output_path = "exports/chart.bmson"};
    const auto absent = wormedit::discover_staged_export_assets(song, group, temporary.path);
    REQUIRE(absent);
    CHECK(absent.mappings.empty());
    const auto copied = wormedit::copy_resource_file_into_export(
        {.project_directory = temporary.path,
         .source_path = "tone.wav",
         .relative_output_path = group.relative_output_path,
         .asset = wormedit::asset_id_t{5}});
    REQUIRE(copied);
    const auto recovered = wormedit::discover_staged_export_assets(song, group, temporary.path);
    REQUIRE(recovered);
    REQUIRE(recovered.mappings.size() == 1);
    CHECK(recovered.mappings.front().asset == wormedit::asset_id_t{5});
    CHECK(recovered.mappings.front().relative_path == "assets/5-tone.wav");
    REQUIRE(wormedit::copy_resource_file_into_export(
        {.project_directory = temporary.path,
         .source_path = "bgm.wav",
         .relative_output_path = group.relative_output_path,
         .asset = wormedit::asset_id_t{6}}));
    const auto both = wormedit::discover_staged_export_assets(song, group, temporary.path);
    REQUIRE(both);
    REQUIRE(both.mappings.size() == 2);
    CHECK(both.mappings.back().relative_path == "assets/6-bgm.wav");
    std::ofstream(temporary.path / "tone.wav") << "changed";
    const auto stale = wormedit::discover_staged_export_assets(song, group, temporary.path);
    CHECK_FALSE(stale);
    CHECK(stale.mappings.empty());
}

TEST_CASE("StepMania output discovery maps a staged music file beside the chart",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::ofstream(temporary.path / "music.ogg") << "audio";
    wormedit::song_document_t song;
    song.audio_assets.push_back({.id = wormedit::asset_id_t{5}, .source_path = "music.ogg"});
    song.resources.push_back({.id = wormedit::resource_id_t{6},
                              .name = "music.ogg",
                              .asset = wormedit::asset_id_t{5},
                              .roles = {wormedit::resource_role_t::music}});
    song.charts.push_back({.id = wormedit::chart_id_t{1}});
    const wormedit::output_group_t group{.family = wormedit::format_family_t::stepmania,
                                         .charts = {wormedit::chart_id_t{1}},
                                         .relative_output_path = "ssc"};
    REQUIRE(wormedit::copy_resource_file_into_export(
        {.project_directory = temporary.path,
         .source_path = "music.ogg",
         .relative_output_path = group.relative_output_path,
         .asset = wormedit::asset_id_t{5}}));
    const auto discovered = wormedit::discover_staged_export_assets(song, group, temporary.path);
    REQUIRE(discovered);
    REQUIRE(discovered.mappings.size() == 1);
    CHECK(discovered.mappings.front().relative_path == "assets/5-music.ogg");
}

TEST_CASE("new audio project publishes osu, SM, and SSC with the staged music file",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    const auto music = temporary.path / "music.ogg";
    std::ofstream(music) << "audio";
    const auto document = wormedit::sys::audio_project_document(
        wormedit::song_id_t{112}, music, "Title", "Artist", 7);
    REQUIRE(document);
    for (const auto& group : document->output_groups) {
        const auto copied = wormedit::copy_resource_file_into_export(
            {.project_directory = temporary.path,
             .source_path = music,
             .relative_output_path = group.relative_output_path,
             .asset = document->audio_assets.front().id});
        REQUIRE(copied);
        const auto staged =
            wormedit::discover_staged_export_assets(*document, group, temporary.path);
        REQUIRE(staged);
        REQUIRE(staged.mappings.size() == 1);
        wormedit::conversion_request_t request;
        request.target = group.family;
        request.charts = group.charts;
        request.staged_assets = staged.mappings;
        if (group.family == wormedit::format_family_t::stepmania) {
            request.stepmania = wormedit::stepmania_export_policy_t{
                .file_profile = group.relative_output_path == "sm"
                                    ? wormedit::stepmania_profile_t::sm
                                    : wormedit::stepmania_profile_t::ssc};
        }
        const auto converted = wormedit::convert_document(*document, request);
        std::string diagnostics;
        for (const auto& diagnostic : converted.diagnostics)
            diagnostics += diagnostic.message + "\n";
        INFO(group.name << ": " << diagnostics);
        REQUIRE(converted);
        const auto published = wormedit::publish_conversion_group_atomic(
            temporary.path, group, request, converted);
        REQUIRE(published.published);
        CHECK(std::filesystem::exists(
            temporary.path / group.relative_output_path / converted.outputs.front().suggested_filename));
        CHECK(std::filesystem::exists(temporary.path / copied.relative_path));
    }
}

TEST_CASE("conversion publication rejects collisions and unsafe destinations",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports");
    std::ofstream(temporary.path / "exports/song.bmson") << "old";
    wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                   .relative_output_path = "exports"};
    wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson};
    CHECK_FALSE(wormedit::plan_conversion_publication(temporary.path, group, request, converted()));
    request.overwrite = true;
    CHECK(wormedit::plan_conversion_publication(temporary.path, group, request, converted()));
    CHECK_FALSE(wormedit::plan_conversion_publication(temporary.path, group, request,
                                                      converted("../escape.bmson")));
    group.relative_output_path = "exports/chart.bmson";
    auto multiple = converted();
    multiple.outputs.push_back(multiple.outputs.front());
    CHECK_FALSE(wormedit::plan_conversion_publication(temporary.path, group, request, multiple));
}

TEST_CASE("conversion publication rejects missing assets and symlink paths",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports");
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bmson,
        .staged_assets = {{wormedit::asset_id_t{5}, "assets/5-tone.wav"}}};
    CHECK_FALSE(wormedit::plan_conversion_publication(temporary.path, group, request, converted()));
    request.staged_assets.clear();
    std::error_code error;
    std::filesystem::create_directory_symlink(temporary.path, temporary.path / "exports/link",
                                              error);
    if (!error) {
        auto linked = group;
        linked.relative_output_path = "exports/link";
        CHECK_FALSE(
            wormedit::plan_conversion_publication(temporary.path, linked, request, converted()));
    }
}

TEST_CASE("conversion publication returns no partial plan on a late failure",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bmson,
        .staged_assets = {{wormedit::asset_id_t{5}, "assets/missing.wav"}}};
    const auto plan =
        wormedit::plan_conversion_publication(temporary.path, group, request, converted());
    CHECK_FALSE(plan);
    CHECK(plan.files.empty());
    CHECK(plan.assets.empty());
}

TEST_CASE("conversion publication installs a new complete output directory",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson};
    const auto result =
        wormedit::publish_conversion_group_atomic(temporary.path, group, request, converted());
    INFO((result.errors.empty() ? "" : result.errors.front()));
    REQUIRE(result.published);
    CHECK(read(temporary.path / "exports/song.bmson") == "{}");
}

TEST_CASE("conversion publication replaces a set and retains staged assets and unrelated files",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports/assets");
    std::ofstream(temporary.path / "exports/song.bmson") << "old";
    std::ofstream(temporary.path / "exports/other.txt") << "keep";
    std::ofstream(temporary.path / "exports/assets/5-tone.wav") << "audio";
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{
        .target = wormedit::format_family_t::bmson,
        .overwrite = true,
        .staged_assets = {{wormedit::asset_id_t{5}, "assets/5-tone.wav"}}};
    auto outputs = converted();
    outputs.outputs.push_back({.target = wormedit::format_family_t::bmson,
                               .chart = wormedit::chart_id_t{2},
                               .suggested_filename = "second.bmson",
                               .bytes = "second",
                               .reimport_validated = true});
    const auto result =
        wormedit::publish_conversion_group_atomic(temporary.path, group, request, outputs);
    INFO((result.errors.empty() ? "" : result.errors.front()));
    REQUIRE(result.published);
    CHECK(read(temporary.path / "exports/song.bmson") == "{}");
    CHECK(read(temporary.path / "exports/second.bmson") == "second");
    CHECK(read(temporary.path / "exports/other.txt") == "keep");
    CHECK(read(temporary.path / "exports/assets/5-tone.wav") == "audio");
}

TEST_CASE("conversion publication fault stages preserve the previous output set",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports");
    std::ofstream(temporary.path / "exports/song.bmson") << "old";
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson,
                                                 .overwrite = true};
    for (const auto stage : {wormedit::conversion_publication_stage_t::copy_existing,
                             wormedit::conversion_publication_stage_t::write_output,
                             wormedit::conversion_publication_stage_t::validate_staging,
                             wormedit::conversion_publication_stage_t::update_manifest,
                             wormedit::conversion_publication_stage_t::exchange_directory}) {
        wormedit::conversion_publication_options_t options;
        options.allow_stage = [stage](auto candidate) { return candidate != stage; };
        const auto result = wormedit::publish_conversion_group_atomic(
            temporary.path, group, request, converted(), options);
        CHECK_FALSE(result.published);
        CHECK(read(temporary.path / "exports/song.bmson") == "old");
    }
}

TEST_CASE("conversion publication leaves old charts intact after a later chart write fails",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports");
    std::ofstream(temporary.path / "exports/song.bmson") << "old";
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson,
                                                 .overwrite = true};
    auto outputs = converted();
    outputs.outputs.push_back({.target = wormedit::format_family_t::bmson,
                               .chart = wormedit::chart_id_t{2},
                               .suggested_filename = "second.bmson",
                               .bytes = "second",
                               .reimport_validated = true});
    int writes{};
    wormedit::conversion_publication_options_t options;
    options.allow_stage = [&](auto stage) {
        return stage != wormedit::conversion_publication_stage_t::write_output || ++writes < 2;
    };
    const auto result =
        wormedit::publish_conversion_group_atomic(temporary.path, group, request, outputs, options);
    CHECK_FALSE(result.published);
    CHECK(read(temporary.path / "exports/song.bmson") == "old");
    CHECK_FALSE(std::filesystem::exists(temporary.path / "exports/second.bmson"));
}

TEST_CASE("conversion publication rejects symlinks in the existing output tree",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports");
    std::ofstream(temporary.path / "exports/song.bmson") << "old";
    std::error_code error;
    std::filesystem::create_symlink(temporary.path / "exports/song.bmson",
                                    temporary.path / "exports/link", error);
    if (error)
        return;
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson,
                                                 .overwrite = true};
    const auto result =
        wormedit::publish_conversion_group_atomic(temporary.path, group, request, converted());
    CHECK_FALSE(result.published);
    CHECK(read(temporary.path / "exports/song.bmson") == "old");
}

TEST_CASE("conversion publication prunes only earlier charts owned by its group",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports/assets");
    std::ofstream(temporary.path / "exports/manual.txt") << "keep";
    std::ofstream(temporary.path / "exports/assets/5-tone.wav") << "audio";
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t first_request{
        .target = wormedit::format_family_t::bmson,
        .staged_assets = {{wormedit::asset_id_t{5}, "assets/5-tone.wav"}}};
    REQUIRE(wormedit::publish_conversion_group_atomic(temporary.path, group, first_request,
                                                      converted("old.bmson"))
                .published);
    REQUIRE(std::filesystem::exists(temporary.path / "exports/old.bmson"));
    const auto previous_manifest = read(temporary.path / "exports/.wormedit-output-set");
    wormedit::conversion_publication_options_t fault;
    fault.allow_stage = [](auto stage) {
        return stage != wormedit::conversion_publication_stage_t::update_manifest;
    };
    const auto interrupted = wormedit::publish_conversion_group_atomic(
        temporary.path, group, first_request, converted("new.bmson"), fault);
    CHECK_FALSE(interrupted.published);
    CHECK(read(temporary.path / "exports/old.bmson") == "{}");
    CHECK_FALSE(std::filesystem::exists(temporary.path / "exports/new.bmson"));
    CHECK(read(temporary.path / "exports/.wormedit-output-set") == previous_manifest);
    const auto second = wormedit::publish_conversion_group_atomic(
        temporary.path, group, first_request, converted("new.bmson"));
    INFO((second.errors.empty() ? "" : second.errors.front()));
    REQUIRE(second.published);
    CHECK(second.pruned_files == 1);
    CHECK_FALSE(std::filesystem::exists(temporary.path / "exports/old.bmson"));
    CHECK(read(temporary.path / "exports/new.bmson") == "{}");
    CHECK(read(temporary.path / "exports/manual.txt") == "keep");
    CHECK(read(temporary.path / "exports/assets/5-tone.wav") == "audio");
    CHECK(read(temporary.path / "exports/.wormedit-output-set").find("exports\tnew.bmson\t") !=
          std::string::npos);
}

TEST_CASE("conversion publication protects another group's generated filename",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    const wormedit::output_group_t first{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t create{.target = wormedit::format_family_t::bmson};
    REQUIRE(wormedit::publish_conversion_group_atomic(temporary.path, first, create, converted())
                .published);
    const wormedit::output_group_t second{.family = wormedit::format_family_t::bmson,
                                          .relative_output_path = "exports/song.bmson"};
    const wormedit::conversion_request_t replace{.target = wormedit::format_family_t::bmson,
                                                 .overwrite = true};
    const auto rejected = wormedit::publish_conversion_group_atomic(temporary.path, second, replace,
                                                                    converted("different.bmson"));
    CHECK_FALSE(rejected.published);
    CHECK(read(temporary.path / "exports/song.bmson") == "{}");
}

TEST_CASE("conversion publication rejects a tampered ownership manifest",
          "[unit][format][publication]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports");
    std::ofstream(temporary.path / "exports/song.bmson") << "old";
    std::ofstream(temporary.path / "exports/.wormedit-output-set")
        << "wormedit-output-set-v2\nexports\t../outside.bmson\t0\tcbf29ce484222325\n";
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson,
                                                 .overwrite = true};
    const auto rejected =
        wormedit::publish_conversion_group_atomic(temporary.path, group, request, converted());
    CHECK_FALSE(rejected.published);
    CHECK(read(temporary.path / "exports/song.bmson") == "old");
}

TEST_CASE(
    "conversion publication retains edited generated charts and rejects same-name replacement",
    "[unit][format][publication]") {
    temporary_directory_t temporary;
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                         .relative_output_path = "exports"};
    const wormedit::conversion_request_t create{.target = wormedit::format_family_t::bmson};
    REQUIRE(wormedit::publish_conversion_group_atomic(temporary.path, group, create,
                                                      converted("old.bmson"))
                .published);
    std::ofstream(temporary.path / "exports/old.bmson") << "hand edited";
    const wormedit::conversion_request_t replace{.target = wormedit::format_family_t::bmson,
                                                 .overwrite = true};
    const auto rejected = wormedit::publish_conversion_group_atomic(temporary.path, group, replace,
                                                                    converted("old.bmson"));
    CHECK_FALSE(rejected.published);
    CHECK(read(temporary.path / "exports/old.bmson") == "hand edited");
    const auto renamed = wormedit::publish_conversion_group_atomic(temporary.path, group, replace,
                                                                   converted("new.bmson"));
    INFO((renamed.errors.empty() ? "" : renamed.errors.front()));
    REQUIRE(renamed.published);
    CHECK(renamed.pruned_files == 0);
    CHECK(read(temporary.path / "exports/old.bmson") == "hand edited");
    CHECK(read(temporary.path / "exports/new.bmson") == "{}");
}

namespace {
void check_publication(bool value) { CHECK(value); }
void require_publication(bool value) { REQUIRE(value); }
auto sliced_publication() -> wormedit::conversion_result_t {
    auto result = converted();
    wormedit::slice_span_plan_t plan;
    plan.spans.push_back({wormedit::asset_id_t{5}, 0, 100, "slice.wav"});
    result.slice_plans.push_back(std::move(plan));
    return result;
}
auto generated_audio_options(bool fail = false) -> wormedit::conversion_publication_options_t {
    wormedit::conversion_publication_options_t options;
    options.stage_generated_files = [fail](const auto& staging) -> std::optional<std::string> {
        std::ofstream(staging / "slice.wav", std::ios::binary) << "generated pcm";
        if (fail) return "Injected render failure";
        return std::nullopt;
    };
    return options;
}
} // namespace

TEST_CASE("generated conversion audio is staged atomically with chart ownership",
          "[unit][format][publication][sliced-conversion]") {
    temporary_directory_t temporary;
    std::filesystem::create_directories(temporary.path / "exports");
    std::ofstream(temporary.path / "exports/manual.txt") << "keep";
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                       .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson};
    const auto failed = wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        sliced_publication(), generated_audio_options(true));
    CHECK_FALSE(failed.published);
    CHECK_FALSE(std::filesystem::exists(temporary.path / "exports/song.bmson"));
    CHECK_FALSE(std::filesystem::exists(temporary.path / "exports/slice.wav"));
    check_publication(read(temporary.path / "exports/manual.txt") == "keep");
    require_publication(wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        sliced_publication(), generated_audio_options()).published);
    check_publication(read(temporary.path / "exports/slice.wav") == "generated pcm");
    check_publication(read(temporary.path / "exports/.wormedit-output-set").find("slice.wav") != std::string::npos);
}

TEST_CASE("generated audio replacement protects edits and preserves unowned files",
          "[unit][format][publication][sliced-conversion]") {
    temporary_directory_t temporary;
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                       .relative_output_path = "exports"};
    wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson};
    require_publication(wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        sliced_publication(), generated_audio_options()).published);
    std::ofstream(temporary.path / "exports/manual.txt") << "keep";
    request.overwrite = true;
    require_publication(wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        sliced_publication(), generated_audio_options()).published);
    check_publication(read(temporary.path / "exports/manual.txt") == "keep");
    std::ofstream(temporary.path / "exports/slice.wav") << "edited";
    const auto rejected = wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        sliced_publication(), generated_audio_options());
    CHECK_FALSE(rejected.published);
    check_publication(read(temporary.path / "exports/slice.wav") == "edited");
    check_publication(read(temporary.path / "exports/song.bmson") == "{}");
}

TEST_CASE("generated audio rejects missing staging and another group's ownership",
          "[unit][format][publication][sliced-conversion]") {
    temporary_directory_t temporary;
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                       .relative_output_path = "exports"};
    wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson};
    CHECK_FALSE(wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        sliced_publication()).published);
    CHECK_FALSE(std::filesystem::exists(temporary.path / "exports"));
    require_publication(wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        sliced_publication(), generated_audio_options()).published);
    request.overwrite = true;
    const wormedit::output_group_t other{.family = wormedit::format_family_t::bmson,
                                       .relative_output_path = "exports/other.bmson"};
    CHECK_FALSE(wormedit::publish_conversion_group_atomic(temporary.path, other, request,
        sliced_publication(), generated_audio_options()).published);
    check_publication(read(temporary.path / "exports/slice.wav") == "generated pcm");
}

TEST_CASE("retained assets cannot collide with generated span filenames",
          "[unit][format][publication][sliced-conversion]") {
    temporary_directory_t temporary;
    auto result = sliced_publication();
    result.asset_copies.push_back({wormedit::asset_id_t{8}, "slice.wav"});
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                       .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson};
    bool invoked{};
    auto options = generated_audio_options();
    options.stage_generated_files = [&](const auto&) -> std::optional<std::string> {
        invoked = true;
        return std::nullopt;
    };
    const auto rejected = wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        result, options);
    CHECK_FALSE(rejected.published);
    CHECK_FALSE(invoked);
    CHECK_FALSE(std::filesystem::exists(temporary.path / "exports"));
}

TEST_CASE("generated audio staging rejects symlinks without touching the source",
          "[unit][format][publication][sliced-conversion]") {
    temporary_directory_t temporary;
    std::ofstream(temporary.path / "source.wav") << "source";
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
                                       .relative_output_path = "exports"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson};
    wormedit::conversion_publication_options_t options;
    options.stage_generated_files = [&](const auto& staging) -> std::optional<std::string> {
        std::filesystem::create_symlink(temporary.path / "source.wav", staging / "slice.wav");
        return std::nullopt;
    };
    const auto rejected = wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        sliced_publication(), options);
    CHECK_FALSE(rejected.published);
    CHECK_FALSE(std::filesystem::exists(temporary.path / "exports"));
    check_publication(read(temporary.path / "source.wav") == "source");
}

TEST_CASE("conversion publication distinguishes dotted directories from chart filenames",
          "[unit][format][publication][conversion]") {
    temporary_directory_t temporary;
    const wormedit::output_group_t group{.family = wormedit::format_family_t::bmson,
        .relative_output_path = "kick-v1.2"};
    const wormedit::conversion_request_t request{.target = wormedit::format_family_t::bmson};
    auto result = converted("first.bmson");
    result.outputs.push_back(result.outputs.front());
    result.outputs.back().suggested_filename = "second.bmson";
    const auto file_plan = wormedit::plan_conversion_publication(temporary.path, group, request, result);
    check_publication(!file_plan);
    const auto directory_plan = wormedit::plan_conversion_publication(temporary.path, group, request, result, true);
    require_publication(static_cast<bool>(directory_plan));
    check_publication(directory_plan.files.front().project_relative_path == "kick-v1.2/first.bmson");
    const auto published = wormedit::publish_conversion_group_atomic(temporary.path, group, request,
        result, {.destination_is_directory = true});
    require_publication(published.published);
    check_publication(read(temporary.path / "kick-v1.2/first.bmson") == "{}");
    check_publication(read(temporary.path / "kick-v1.2/second.bmson") == "{}");
    check_publication(!std::filesystem::exists(temporary.path / "first.bmson"));
}
