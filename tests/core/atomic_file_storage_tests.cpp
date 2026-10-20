#include <wormedit/core/atomic_file_storage.h>
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>

namespace {
auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void check_unpublished(const std::filesystem::path& destination, const std::filesystem::path& temporary) {
    CHECK(read_text(temporary) == "new");
    CHECK(read_text(destination) == "old");
}
void check_publication(wormedit::prepared_atomic_file_t& prepared,
    const std::filesystem::path& destination, const std::filesystem::path& temporary) {
    CHECK(read_text(destination) == "old");
    CHECK_FALSE(prepared.publish(false));
    CHECK(read_text(destination) == "old");
    REQUIRE(prepared.publish(true));
    CHECK(read_text(destination) == "new");
    CHECK_FALSE(std::filesystem::exists(temporary));
}
void check_cleanup(const std::filesystem::path& directory, const std::filesystem::path& temporary) {
    CHECK_FALSE(std::filesystem::exists(temporary));
    CHECK(std::filesystem::is_empty(directory));
}
}
TEST_CASE("Prepared atomic files validate privately and publish only on explicit acceptance", "[unit][atomic-file]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto destination = directory->path() / "preview.ogg";
    REQUIRE(wormedit::create_file_atomic(destination, "old", 1024));
    std::filesystem::path temporary;
    auto prepared = wormedit::prepared_atomic_file_t::from(destination, "new", 1024,
        [&](const std::filesystem::path& path) -> wormedit::atomic_file_save_outcome_t {
            temporary = path;
            check_unpublished(destination, path);
            return {};
        });
    REQUIRE(prepared);
    check_publication(*prepared, destination, temporary);
}

TEST_CASE("Prepared atomic files clean up rejection cancellation and abandoned results", "[unit][atomic-file]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto destination = directory->path() / "preview.ogg";
    std::filesystem::path temporary;
    std::stop_source stop;
    bool reject{}, cancel{};
    SECTION("reject") { reject = true; }
    SECTION("cancel") { cancel = true; }
    SECTION("abandon") {}
    {
        auto prepared = wormedit::prepared_atomic_file_t::from(destination, "new", 1024,
            [&](const std::filesystem::path& path) -> wormedit::atomic_file_save_outcome_t {
                temporary = path;
                if (cancel) stop.request_stop();
                if (reject) return std::unexpected(std::string{"Invalid encoded file"});
                return {};
            }, stop.get_token());
        CHECK(prepared.has_value() == (!reject && !cancel));
        CHECK_FALSE(std::filesystem::exists(destination));
    }
    check_cleanup(directory->path(), temporary);
}
