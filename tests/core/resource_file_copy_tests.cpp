#include <wormedit/core/resource_file_copy.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
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
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence.fetch_add(1)));
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

namespace {
void require_copy_status(const wormedit::project_asset_copy_outcome_t& copy,
                         wormedit::project_asset_copy_phase_t status) {
    REQUIRE(copy);
    REQUIRE(copy->status == status);
}

void require_copy_failure(const wormedit::project_asset_copy_outcome_t& copy,
                          wormedit::failure_code_t code) {
    REQUIRE_FALSE(copy);
    REQUIRE(copy.error().code == code);
}

} // namespace

TEST_CASE("project asset copy publishes bytes once and reuses an identical destination",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "kick.wav";
    write_text(source, "first audio bytes");
    const wormedit::project_asset_copy_request_t request{.project_directory = directory.path,
                                                         .source_path = source,
                                                         .asset = wormedit::asset_id_t{12}};
    const auto copied = wormedit::create_project_asset_copy(request);
    REQUIRE(copied);
    require_copy_status(copied, wormedit::project_asset_copy_phase_t::copied);
    CHECK(copied->relative_path == std::filesystem::path{"assets/kick.wav"});
    CHECK(read_text(copied->absolute_path) == "first audio bytes");
    const auto reused = wormedit::create_project_asset_copy(request);
    require_copy_status(reused, wormedit::project_asset_copy_phase_t::reused);
    CHECK(reused->relative_path == copied->relative_path);
    CHECK(std::distance(std::filesystem::directory_iterator(directory.path / "assets"),
                        std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("project asset copy leaves the project unchanged when its source is missing",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const wormedit::project_asset_copy_request_t request{
        .project_directory = directory.path,
        .source_path = directory.path / "missing.wav",
        .asset = wormedit::asset_id_t{12}};
    const auto result = wormedit::create_project_asset_copy(request);
    require_copy_failure(result, wormedit::failure_code_t::unavailable);
    CHECK_FALSE(std::filesystem::exists(directory.path / "assets"));
}

TEST_CASE("automatic project copies reuse identical bytes under a different asset filename",
          "[unit][core][resources][project-save]") {
    temporary_directory_t directory;
    const auto first = directory.path / "first.wav";
    const auto second = directory.path / "second.wav";
    write_text(first, "identical audio bytes");
    write_text(second, "identical audio bytes");
    const auto copied = wormedit::create_project_asset_copy({.project_directory = directory.path,
        .source_path = first, .asset = wormedit::asset_id_t{12}});
    require_copy_status(copied, wormedit::project_asset_copy_phase_t::copied);
    const auto reused = wormedit::create_project_asset_copy({.project_directory = directory.path,
        .source_path = second, .asset = wormedit::asset_id_t{13}, .reuse_identical_files = true});
    require_copy_status(reused, wormedit::project_asset_copy_phase_t::reused);
    CHECK(reused->relative_path == copied->relative_path);
    CHECK_FALSE(std::filesystem::exists(directory.path / "assets/13-second.wav"));
}

TEST_CASE("automatic project copies never reuse an unpublished copy temporary",
          "[unit][core][resources][project-save]") {
    temporary_directory_t directory;
    std::filesystem::create_directory(directory.path / "assets");
    write_text(directory.path / "source.wav", "audio bytes");
    write_text(directory.path / "assets/.wormedit-copy-test", "audio bytes");
    const auto copied = wormedit::create_project_asset_copy({.project_directory = directory.path,
        .source_path = directory.path / "source.wav", .asset = wormedit::asset_id_t{12},
        .reuse_identical_files = true});
    require_copy_status(copied, wormedit::project_asset_copy_phase_t::copied);
    CHECK(copied->relative_path == "assets/source.wav");
}

TEST_CASE("unlinked copy cleanup removes only its own intact publication",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "music.wav";
    write_text(source, "audio bytes");
    const wormedit::project_asset_copy_request_t request{.project_directory = directory.path,
                                                         .source_path = source,
                                                         .asset = wormedit::asset_id_t{21}};
    const auto copied = wormedit::create_project_asset_copy(request);
    require_copy_status(copied, wormedit::project_asset_copy_phase_t::copied);
    REQUIRE(copied->published_identity);
    CHECK(wormedit::discard_unlinked_project_asset_copy(request, *copied));
    CHECK_FALSE(std::filesystem::exists(copied->absolute_path));
    CHECK_FALSE(wormedit::discard_unlinked_project_asset_copy(request, *copied));

    const auto again = wormedit::create_project_asset_copy(request);
    require_copy_status(again, wormedit::project_asset_copy_phase_t::copied);
    const auto reused = wormedit::create_project_asset_copy(request);
    require_copy_status(reused, wormedit::project_asset_copy_phase_t::reused);
    CHECK_FALSE(wormedit::discard_unlinked_project_asset_copy(request, *reused));
    CHECK(std::filesystem::exists(again->absolute_path));
    write_text(again->absolute_path, "modified audio");
    CHECK_FALSE(wormedit::discard_unlinked_project_asset_copy(request, *again));
    CHECK(read_text(again->absolute_path) == "modified audio");
    std::filesystem::remove(again->absolute_path);
    write_text(again->absolute_path, "audio bytes");
    CHECK_FALSE(wormedit::discard_unlinked_project_asset_copy(request, *again));
    CHECK(read_text(again->absolute_path) == "audio bytes");
}

TEST_CASE("unlinked export copy cleanup preserves a symlinked destination directory",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "music.ogg";
    write_text(source, "audio bytes");
    const wormedit::export_asset_copy_request_t request{.project_directory = directory.path,
                                                        .source_path = source,
                                                        .relative_output_path = "charts/song.bmson",
                                                        .asset = wormedit::asset_id_t{22}};
    const auto copied = wormedit::create_export_asset_copy(request);
    require_copy_status(copied, wormedit::project_asset_copy_phase_t::copied);
    CHECK(wormedit::discard_unlinked_export_asset_copy(request, *copied));
    const auto again = wormedit::create_export_asset_copy(request);
    require_copy_status(again, wormedit::project_asset_copy_phase_t::copied);
    const auto assets = directory.path / "charts/assets";
    const auto moved = directory.path / "elsewhere";
    std::filesystem::rename(assets, moved);
    std::error_code error;
    std::filesystem::create_directory_symlink(moved, assets, error);
    if (!error) {
        CHECK_FALSE(wormedit::discard_unlinked_export_asset_copy(request, *again));
        CHECK(std::filesystem::exists(moved / again->relative_path.filename()));
    }
}

TEST_CASE("project asset copy falls back around a conflicting basename without replacing it",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "kick.wav";
    write_text(source, "old bytes");
    const wormedit::project_asset_copy_request_t request{.project_directory = directory.path,
                                                         .source_path = source,
                                                         .asset = wormedit::asset_id_t{12}};
    std::filesystem::create_directory(directory.path / "assets");
    write_text(directory.path / "assets/kick.wav", "old bytes");
    write_text(source, "new bytes");
    const auto collision = wormedit::create_project_asset_copy(request);
    REQUIRE(collision);
    CHECK(collision->relative_path == "assets/12-kick.wav");
    CHECK(read_text(directory.path / "assets/kick.wav") == "old bytes");
    CHECK(read_text(directory.path / "assets/12-kick.wav") == "new bytes");
}

TEST_CASE("project asset copy reuses a byte-identical source basename",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    std::filesystem::create_directories(directory.path / "assets");
    write_text(directory.path / "kick.wav", "same bytes");
    write_text(directory.path / "assets/kick.wav", "same bytes");
    const wormedit::project_asset_copy_request_t request{.project_directory = directory.path,
        .source_path = "kick.wav", .asset = wormedit::asset_id_t{12}};
    const auto inspected = wormedit::inspect_project_asset_file(request);
    require_copy_status(inspected, wormedit::project_asset_copy_phase_t::reused);
    CHECK(inspected->relative_path == "assets/kick.wav");
    const auto copied = wormedit::create_project_asset_copy(request);
    require_copy_status(copied, wormedit::project_asset_copy_phase_t::reused);
    CHECK(copied->relative_path == inspected->relative_path);
}

TEST_CASE("project asset inspection reports reuse and safe fallback availability without writing",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "kick.wav";
    write_text(source, "audio bytes");
    const wormedit::project_asset_copy_request_t request{.project_directory = directory.path,
                                                         .source_path = source,
                                                         .asset = wormedit::asset_id_t{12}};
    require_copy_failure(wormedit::inspect_project_asset_file(request), wormedit::failure_code_t::not_found);
    CHECK_FALSE(std::filesystem::exists(directory.path / "assets"));
    const auto copied = wormedit::create_project_asset_copy(request);
    REQUIRE(copied);
    require_copy_status(wormedit::inspect_project_asset_file(request), wormedit::project_asset_copy_phase_t::reused);
    write_text(source, "different bytes");
    const auto fallback = wormedit::inspect_project_asset_file(request);
    require_copy_failure(fallback, wormedit::failure_code_t::not_found);
    CHECK(read_text(copied->absolute_path) == "audio bytes");
    std::error_code error;
    std::filesystem::remove(copied->absolute_path, error);
    REQUIRE_FALSE(error);
    std::filesystem::create_symlink(source, copied->absolute_path, error);
    if (!error)
        require_copy_failure(wormedit::inspect_project_asset_file(request), wormedit::failure_code_t::not_found);
}

auto verify_planned_same_name_batch(const std::filesystem::path& root) -> bool {
    std::filesystem::create_directories(root / "first");
    std::filesystem::create_directories(root / "second");
    write_text(root / "first/kick.wav", "first bytes");
    write_text(root / "second/kick.wav", "second bytes");
    const std::array requests{
        wormedit::project_asset_copy_request_t{.project_directory = root,
            .source_path = "first/kick.wav", .asset = wormedit::asset_id_t{11}},
        wormedit::project_asset_copy_request_t{.project_directory = root,
            .source_path = "second/kick.wav", .asset = wormedit::asset_id_t{12}}};
    const auto planned = wormedit::plan_project_asset_copies(requests, "assets");
    if (!planned || planned->size() != 2 ||
        (*planned)[0].destination_filename() != "kick.wav" ||
        (*planned)[1].destination_filename() != "12-kick.wav")
        return false;
    const auto first = wormedit::create_project_asset_copy((*planned)[0]);
    const auto second = wormedit::create_project_asset_copy((*planned)[1]);
    return first && second && first->relative_path == "assets/kick.wav" &&
           second->relative_path == "assets/12-kick.wav" &&
           read_text(first->absolute_path) == "first bytes" &&
           read_text(second->absolute_path) == "second bytes";
}

TEST_CASE("asset copy planning reserves distinct deterministic names for a same-plan collision",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    CHECK(verify_planned_same_name_batch(directory.path));
}

TEST_CASE("asset copy planning refuses when both deterministic names contain different bytes",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    std::filesystem::create_directories(directory.path / "assets");
    write_text(directory.path / "kick.wav", "source bytes");
    write_text(directory.path / "assets/kick.wav", "first conflicting bytes");
    write_text(directory.path / "assets/12-kick.wav", "second conflicting bytes");
    const wormedit::project_asset_copy_request_t request{.project_directory = directory.path,
        .source_path = "kick.wav", .asset = wormedit::asset_id_t{12}};
    const auto planned = wormedit::plan_project_asset_copies(
        std::span{&request, std::size_t{1}}, "assets");
    REQUIRE_FALSE(planned);
    CHECK(planned.error().code == wormedit::failure_code_t::conflict);
    CHECK(read_text(directory.path / "assets/kick.wav") == "first conflicting bytes");
    CHECK(read_text(directory.path / "assets/12-kick.wav") == "second conflicting bytes");
}

TEST_CASE("project asset validation rejects a temporary before publishing its name",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "music.wav";
    write_text(source, "audio bytes");
    bool examined{};
    const wormedit::project_asset_copy_request_t request{
        .project_directory = directory.path,
        .source_path = source,
        .asset = wormedit::asset_id_t{17},
        .validate_temporary = [&](const std::filesystem::path& temporary) {
            examined = true;
            return read_text(temporary) == "different bytes";
        }};
    const auto rejected = wormedit::create_project_asset_copy(request);
    CHECK(examined);
    require_copy_failure(rejected, wormedit::failure_code_t::invalid_data);
    CHECK_FALSE(std::filesystem::exists(directory.path / "assets/17-music.wav"));
    CHECK(std::filesystem::is_empty(directory.path / "assets"));
    std::stop_source cancelled;
    auto interrupted = request;
    interrupted.validate_temporary = [&](const auto&) {
        cancelled.request_stop();
        return true;
    };
    require_copy_failure(wormedit::create_project_asset_copy(interrupted, cancelled.get_token()), wormedit::failure_code_t::cancelled);
    CHECK(std::filesystem::is_empty(directory.path / "assets"));
    auto accepted = request;
    accepted.validate_temporary = [](const auto& temporary) {
        return read_text(temporary) == "audio bytes";
    };
    require_copy_status(wormedit::create_project_asset_copy(accepted), wormedit::project_asset_copy_phase_t::copied);
}

TEST_CASE("project asset copy validates relative sources, limits, and cancellation",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "kick.wav";
    write_text(source, "source data");
    const wormedit::project_asset_copy_request_t request{.project_directory = directory.path,
                                                         .source_path = "kick.wav",
                                                         .asset = wormedit::asset_id_t{5}};
    require_copy_status(wormedit::create_project_asset_copy(request), wormedit::project_asset_copy_phase_t::copied);
    auto invalid = request;
    invalid.source_path = "../kick.wav";
    require_copy_failure(wormedit::create_project_asset_copy(invalid), wormedit::failure_code_t::invalid_request);
    invalid = request;
    invalid.asset = wormedit::asset_id_t{6};
    invalid.maximum_bytes = 2;
    require_copy_failure(wormedit::create_project_asset_copy(invalid), wormedit::failure_code_t::invalid_request);
    std::stop_source cancelled;
    cancelled.request_stop();
    require_copy_failure(wormedit::create_project_asset_copy(invalid, cancelled.get_token()), wormedit::failure_code_t::invalid_request);
    invalid.maximum_bytes = 100;
    require_copy_failure(wormedit::create_project_asset_copy(invalid, cancelled.get_token()), wormedit::failure_code_t::cancelled);
    CHECK_FALSE(std::filesystem::exists(directory.path / "assets/6-kick.wav"));
}

auto verify_export_asset_copy(const wormedit::export_asset_copy_request_t& request) -> bool {
    const auto copied = wormedit::create_export_asset_copy(request);
    if (!copied || copied->relative_path != "exports/bms/assets/kick.wav" ||
        wormedit::export_asset_reference_path(request.relative_output_path, copied->relative_path) !=
            std::filesystem::path{"assets/kick.wav"} ||
        wormedit::export_asset_reference_path(request.relative_output_path, "outside/12-kick.wav") ||
        read_text(copied->absolute_path) != "first audio bytes" ||
        std::filesystem::exists(request.project_directory / "assets/12-kick.wav"))
        return false;
    const auto reused = wormedit::create_export_asset_copy(request);
    if (!reused || reused->status != wormedit::project_asset_copy_phase_t::reused)
        return false;
    write_text(request.project_directory / "kick.wav", "changed audio bytes");
    const auto changed = wormedit::create_export_asset_copy(request);
    return changed && changed->relative_path == "exports/bms/assets/12-kick.wav" &&
           read_text(changed->absolute_path) == "changed audio bytes" &&
           read_text(copied->absolute_path) == "first audio bytes";
}

TEST_CASE("export asset copy stages beside an output group without changing project assets",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    write_text(directory.path / "kick.wav", "first audio bytes");
    const wormedit::export_asset_copy_request_t request{.project_directory = directory.path,
                                                        .source_path = "kick.wav",
                                                        .relative_output_path =
                                                            "exports/bms/chart.bms",
                                                        .asset = wormedit::asset_id_t{12}};
    CHECK(verify_export_asset_copy(request));
}

TEST_CASE("export asset inspection recovers identical staged copies without writing",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    write_text(directory.path / "kick.wav", "audio bytes");
    const wormedit::export_asset_copy_request_t request{.project_directory = directory.path,
                                                        .source_path = "kick.wav",
                                                        .relative_output_path =
                                                            "exports/chart.bmson",
                                                        .asset = wormedit::asset_id_t{12}};
    const auto absent = wormedit::inspect_export_asset_file(request);
    require_copy_failure(absent, wormedit::failure_code_t::not_found);
    CHECK_FALSE(std::filesystem::exists(directory.path / "exports"));
    const auto copied = wormedit::create_export_asset_copy(request);
    REQUIRE(copied);
    const auto recovered = wormedit::inspect_export_asset_file(request);
    require_copy_status(recovered, wormedit::project_asset_copy_phase_t::reused);
    CHECK(recovered->relative_path == copied->relative_path);
    write_text(directory.path / "kick.wav", "different bytes");
    require_copy_failure(wormedit::inspect_export_asset_file(request), wormedit::failure_code_t::not_found);
    CHECK(read_text(copied->absolute_path) == "audio bytes");
    std::error_code error;
    std::filesystem::remove(copied->absolute_path, error);
    REQUIRE_FALSE(error);
    std::filesystem::create_symlink(directory.path / "kick.wav", copied->absolute_path, error);
    if (!error)
        require_copy_failure(wormedit::inspect_export_asset_file(request), wormedit::failure_code_t::not_found);
}

TEST_CASE("export asset validation rejects a temporary before publication",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    const auto source = directory.path / "music.ogg";
    write_text(source, "audio bytes");
    const wormedit::export_asset_copy_request_t request{
        .project_directory = directory.path,
        .source_path = source,
        .relative_output_path = "exports/chart.bmson",
        .asset = wormedit::asset_id_t{19},
        .validate_temporary = [](const auto&) { return false; }};
    require_copy_failure(wormedit::create_export_asset_copy(request), wormedit::failure_code_t::invalid_data);
    CHECK_FALSE(std::filesystem::exists(directory.path / "exports/assets/music.ogg"));
    CHECK(std::filesystem::is_empty(directory.path / "exports/assets"));
}

TEST_CASE("export asset copy rejects traversal and symlinked output directories",
          "[unit][core][resources]") {
    temporary_directory_t directory;
    write_text(directory.path / "kick.wav", "source data");
    wormedit::export_asset_copy_request_t request{.project_directory = directory.path,
                                                  .source_path = "kick.wav",
                                                  .relative_output_path = "exports/chart.bms",
                                                  .asset = wormedit::asset_id_t{5}};
    request.relative_output_path = "../outside/chart.bms";
    require_copy_failure(wormedit::create_export_asset_copy(request), wormedit::failure_code_t::invalid_request);
    request.relative_output_path = directory.path / "outside/chart.bms";
    require_copy_failure(wormedit::create_export_asset_copy(request), wormedit::failure_code_t::invalid_request);
    request.relative_output_path = "exports/chart.bms";
    std::filesystem::create_directory(directory.path / "actual");
    std::error_code error;
    std::filesystem::create_directory_symlink(directory.path / "actual", directory.path / "exports",
                                              error);
    if (!error) {
        require_copy_failure(wormedit::create_export_asset_copy(request), wormedit::failure_code_t::invalid_request);
        CHECK_FALSE(std::filesystem::exists(directory.path / "actual/assets/5-kick.wav"));
    }
}

TEST_CASE("export asset copy accepts an output group directory path", "[unit][core][resources]") {
    temporary_directory_t directory;
    write_text(directory.path / "kick.wav", "source data");
    const wormedit::export_asset_copy_request_t request{.project_directory = directory.path,
                                                        .source_path = "kick.wav",
                                                        .relative_output_path = "charts",
                                                        .asset = wormedit::asset_id_t{5}};
    CHECK(wormedit::export_asset_directory(request.relative_output_path) ==
          std::filesystem::path{"charts/assets"});
    const auto copied = wormedit::create_export_asset_copy(request);
    REQUIRE(copied);
    CHECK(copied->relative_path == std::filesystem::path{"charts/assets/kick.wav"});
    CHECK(
        wormedit::export_asset_reference_path(request.relative_output_path, copied->relative_path) ==
        std::filesystem::path{"assets/kick.wav"});
    CHECK(read_text(copied->absolute_path) == "source data");
}
