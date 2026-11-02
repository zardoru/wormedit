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

namespace {
auto stream_text(wormedit::prepared_atomic_file_t::stream_t& stream, std::string_view text)
    -> wormedit::atomic_file_save_outcome_t {
    return stream.write(std::as_bytes(std::span{text.data(), text.size()}));
}
auto rewrite_header(wormedit::prepared_atomic_file_t::stream_t& stream)
    -> wormedit::atomic_file_save_outcome_t {
    REQUIRE(stream_text(stream, "bad"));
    REQUIRE(stream.seek(0, wormedit::prepared_atomic_file_t::seek_origin_t::begin));
    REQUIRE(stream_text(stream, "new"));
    CHECK(stream.size() == 3);
    return {};
}
enum class stream_failure_t { producer, ignored_write, ignored_seek, validator, cancel, publish_cancel };
auto fail_stream(wormedit::prepared_atomic_file_t::stream_t& stream,
    stream_failure_t mode, std::stop_source& stop) -> wormedit::atomic_file_save_outcome_t {
    const auto written = stream_text(stream, "new");
    if (!written) return written;
    switch (mode) {
    case stream_failure_t::producer: return std::unexpected(std::string{"producer failed"});
    case stream_failure_t::ignored_write: (void)stream_text(stream, "over budget"); break;
    case stream_failure_t::ignored_seek:
        (void)stream.seek(-4, wormedit::prepared_atomic_file_t::seek_origin_t::current); break;
    case stream_failure_t::cancel: stop.request_stop(); break;
    case stream_failure_t::validator: break;
    case stream_failure_t::publish_cancel: break;
    }
    return {};
}
void check_stream_rejection(wormedit::result_t<wormedit::prepared_atomic_file_t, std::string>& prepared,
    stream_failure_t mode, std::stop_source& stop) {
    if (mode == stream_failure_t::publish_cancel) {
        REQUIRE(prepared);
        stop.request_stop();
        CHECK_FALSE(prepared->publish());
    } else CHECK_FALSE(prepared);
}
void check_failed_stream(stream_failure_t mode) {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto destination = directory->path() / "clip.mp4";
    REQUIRE(wormedit::create_file_atomic(destination, "old", 3));
    std::stop_source stop;
    {
        auto prepared = wormedit::prepared_atomic_file_t::from_stream(destination, 3,
            [&](wormedit::prepared_atomic_file_t::stream_t& stream) { return fail_stream(stream, mode, stop); },
            [mode](const std::filesystem::path&) -> wormedit::atomic_file_save_outcome_t {
                if (mode == stream_failure_t::validator) return std::unexpected(std::string{"validator failed"});
                return {};
            }, stop.get_token());
        check_stream_rejection(prepared, mode, stop);
    }
    CHECK(read_text(destination) == "old");
    CHECK(std::distance(std::filesystem::directory_iterator(directory->path()), std::filesystem::directory_iterator{}) == 1);
}
}
TEST_CASE("Atomic stream rewrites bounded headers before validated publication", "[unit][atomic-file][stream]") {
    auto directory = wormedit::test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto destination = directory->path() / "clip.webm";
    REQUIRE(wormedit::create_file_atomic(destination, "old", 3));
    std::filesystem::path temporary;
    auto prepared = wormedit::prepared_atomic_file_t::from_stream(destination, 3, rewrite_header,
        [&](const std::filesystem::path& path) -> wormedit::atomic_file_save_outcome_t {
            temporary = path;
            check_unpublished(destination, temporary);
            return {};
        });
    REQUIRE(prepared);
    check_publication(*prepared, destination, temporary);
}
TEST_CASE("Atomic streaming failures and cancellation preserve destination and clean staging", "[unit][atomic-file][stream]") {
    for (auto mode : {stream_failure_t::producer, stream_failure_t::ignored_write, stream_failure_t::ignored_seek,
                     stream_failure_t::validator, stream_failure_t::cancel, stream_failure_t::publish_cancel})
        check_failed_stream(mode);
}
