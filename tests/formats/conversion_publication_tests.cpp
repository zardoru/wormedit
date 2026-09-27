#include <wormedit/formats/conversion_publication.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
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
