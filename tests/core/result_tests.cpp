#include <wormedit/core/result.h>
#include <wormedit/core/async_job.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>

using namespace wormedit;

static_assert(std::is_same_v<result_t<int, int>, std::expected<int, int>>);
static_assert(std::is_aggregate_v<wormedit::error_t<diagnostic_code_t>>);
static_assert(std::is_aggregate_v<diagnosed_t<int>>);

TEST_CASE("shared expected results preserve values and error messages", "[unit][core][result]") {
    result_t<std::unique_ptr<int>, wormedit::error_t<diagnostic_code_t>> value = std::make_unique<int>(42);
    REQUIRE(value.has_value());
    CHECK(**value == 42);
    result_t<void, wormedit::error_t<diagnostic_code_t>> failure =
        std::unexpected(wormedit::error_t<diagnostic_code_t>{diagnostic_code_t::invalid_id, "bad id"});
    REQUIRE_FALSE(failure.has_value());
    CHECK(failure.error().code == diagnostic_code_t::invalid_id);
    CHECK(failure.error().message == "bad id");
}

TEST_CASE("diagnosed results accept warnings and reject errors", "[unit][core][result]") {
    diagnosed_t<int> value{.value = 42, .diagnostics = {}};
    CHECK(value.ok());
    value.diagnostics.push_back({.severity = diagnostic_severity_t::info});
    value.diagnostics.push_back({.severity = diagnostic_severity_t::warning});
    CHECK(value.ok());
    CHECK(value.value == 42);
    value.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                 .code = diagnostic_code_t::invalid_id, .message = "bad id"});
    CHECK_FALSE(value.ok());
    CHECK(value.diagnostics.size() == 3);
}

TEST_CASE("async compatibility preserves generation and failure feedback",
          "[unit][core][result][async]") {
    const auto ready = async_job_start_result_t::from(async_job_start_outcome_t{17});
    REQUIRE(ready);
    CHECK(ready.generation == 17);
    const auto failed = async_job_start_result_t::from(async_job_start_outcome_t{
        std::unexpected(wormedit::error_t<async_job_start_status_t>{
            async_job_start_status_t::capacity_exceeded, "full"})});
    REQUIRE_FALSE(failed);
    CHECK(failed.status == async_job_start_status_t::capacity_exceeded);
    CHECK(failed.generation == 0);
    CHECK(failed.message == "full");
}
