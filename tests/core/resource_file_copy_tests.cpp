#include <wormedit/core/resource_file_copy.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stop_token>
#include <string>

namespace {

struct temporary_directory_t {
    std::filesystem::path path;

    temporary_directory_t() {
        static std::atomic<std::uint64_t> sequence{0};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-resource-copy-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }

    ~temporary_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void write_text(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
}

auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("project asset copy publishes bytes once and reuses an identical destination",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "kick.wav";
    write_text(source, "first audio bytes");
    const wormedit::project_asset_copy_request_t request{
        .project_directory = directory.path, .source_path = source,
        .asset = wormedit::asset_id_t{12}};
    const auto copied = wormedit::copy_resource_file_into_project(request);
    REQUIRE(copied);
    CHECK(copied.status == wormedit::project_asset_copy_status_t::copied);
    CHECK(copied.relative_path == std::filesystem::path{"assets/12-kick.wav"});
    CHECK(read_text(copied.absolute_path) == "first audio bytes");
    const auto reused = wormedit::copy_resource_file_into_project(request);
    CHECK(reused.status == wormedit::project_asset_copy_status_t::reused);
    CHECK(reused.relative_path == copied.relative_path);
    CHECK(std::distance(std::filesystem::directory_iterator(directory.path / "assets"),
                        std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("project asset copy rejects collisions without replacing the existing bytes",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "kick.wav";
    write_text(source, "old bytes");
    const wormedit::project_asset_copy_request_t request{
        .project_directory = directory.path, .source_path = source,
        .asset = wormedit::asset_id_t{12}};
    REQUIRE(wormedit::copy_resource_file_into_project(request));
    write_text(source, "new bytes");
    const auto collision = wormedit::copy_resource_file_into_project(request);
    CHECK(collision.status == wormedit::project_asset_copy_status_t::destination_collision);
    CHECK(read_text(directory.path / "assets/12-kick.wav") == "old bytes");
}

TEST_CASE("project asset copy validates relative sources, limits, and cancellation",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "kick.wav";
    write_text(source, "source data");
    const wormedit::project_asset_copy_request_t request{
        .project_directory = directory.path, .source_path = "kick.wav",
        .asset = wormedit::asset_id_t{5}};
    CHECK(wormedit::copy_resource_file_into_project(request).status ==
          wormedit::project_asset_copy_status_t::copied);
    auto invalid = request;
    invalid.source_path = "../kick.wav";
    CHECK(wormedit::copy_resource_file_into_project(invalid).status ==
          wormedit::project_asset_copy_status_t::invalid_request);
    invalid = request;
    invalid.asset = wormedit::asset_id_t{6};
    invalid.maximum_bytes = 2;
    CHECK(wormedit::copy_resource_file_into_project(invalid).status ==
          wormedit::project_asset_copy_status_t::invalid_request);
    std::stop_source cancelled;
    cancelled.request_stop();
    CHECK(wormedit::copy_resource_file_into_project(invalid, cancelled.get_token()).status ==
          wormedit::project_asset_copy_status_t::invalid_request);
    invalid.maximum_bytes = 100;
    CHECK(wormedit::copy_resource_file_into_project(invalid, cancelled.get_token()).status ==
          wormedit::project_asset_copy_status_t::cancelled);
    CHECK_FALSE(std::filesystem::exists(directory.path / "assets/6-kick.wav"));
}
