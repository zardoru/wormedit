#include <wormedit/core/result.h>
#include <wormedit/core/async_job.h>
#include <wormedit/core/sound_slice.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>
#include <utility>

using namespace wormedit;

static_assert(std::is_same_v<result_t<int, int>, std::expected<int, int>>);
static_assert(std::is_aggregate_v<wormedit::error_t<diagnostic_code_t>>);
static_assert(std::is_aggregate_v<diagnosed_t<int>>);
static_assert(std::is_same_v<decltype(diagnosed_t<int>{}.diagnostics), diagnostic_list_t>);
static_assert(std::is_same_v<attempt_t<int>, diagnosed_t<std::optional<int>>>);

TEST_CASE("attempts keep payload presence independent of severity", "[unit][core][result]") {
    attempt_t<std::unique_ptr<int>> attempt{};
    CHECK_FALSE(attempt.value);
    CHECK(attempt.ok());
    attempt.diagnostics.push_back({.severity = diagnostic_severity_t::warning});
    CHECK(attempt.ok());
    attempt.value = std::make_unique<int>(42);
    attempt.diagnostics.push_back({.severity = diagnostic_severity_t::error});
    CHECK_FALSE(attempt.ok());
    REQUIRE(attempt.value);
    CHECK(**attempt.value == 42);
}

TEST_CASE("format diagnostics retain typed locations and codes", "[unit][core][result]") {
    enum class code_t { malformed, unsupported };
    struct location_t {
        std::size_t line{};
        std::size_t column{};
        auto operator==(const location_t&) const -> bool = default;
    };
    using diagnostic = format_diagnostic_t<code_t, location_t>;
    static_assert(std::is_aggregate_v<diagnostic>);
    CHECK(diagnostic{}.severity == diagnostic_severity_t::error);
    CHECK(diagnostic{}.code == code_t::malformed);
    using unsupported_diagnostic = format_diagnostic_t<code_t, location_t, code_t::unsupported>;
    CHECK(unsupported_diagnostic{}.code == code_t::unsupported);
    const diagnostic warning{diagnostic_severity_t::warning, code_t::unsupported,
                             "retained warning", {7, 11}};
    attempt_t<int, std::vector<diagnostic>> attempt{.diagnostics = {warning}};
    CHECK(attempt.ok());
    CHECK(attempt.diagnostics.front() == warning);
}

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

TEST_CASE("async start outcomes preserve generation and failure feedback",
          "[unit][core][result][async]") {
    const async_job_start_outcome_t ready{17};
    REQUIRE(ready);
    CHECK(*ready == 17);
    const async_job_start_outcome_t failed{std::unexpected(wormedit::error_t<async_job_start_status_t>{
        async_job_start_status_t::capacity_exceeded, "full"})};
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == async_job_start_status_t::capacity_exceeded);
    CHECK(failed.error().message == "full");
}

namespace {

auto typed_diagnostics_keep_codes_and_strict_compatibility() -> bool {
    sound_slice_outcome_t outcome{
        .value = {},
        .diagnostics = {{.severity = diagnostic_severity_t::warning,
                         .code = sound_slice_error_code_t::timing_failure,
                         .message = "bounded timing", .object = object_id_t{7}}}};
    const auto accepts_warning = outcome.ok();
    auto legacy = sound_slice_result_t::from(std::move(outcome));
    if (legacy || legacy.diagnostics.size() != 1)
        return false;
    const auto& warning = legacy.diagnostics.front();
    sound_slice_outcome_t error_outcome{
        .value = {},
        .diagnostics = {{.severity = diagnostic_severity_t::error,
                         .code = sound_slice_error_code_t::invalid_source_range,
                         .message = "empty source", .object = object_id_t{8}}}};
    return accepts_warning && !error_outcome.ok() &&
           warning.severity == diagnostic_severity_t::warning &&
           warning.code == sound_slice_error_code_t::timing_failure &&
           warning.message == "bounded timing" && warning.object == object_id_t{7};
}

} // namespace

TEST_CASE("typed diagnosed results retain metadata and legacy warning rejection",
          "[unit][core][result][sound-slice]") {
    CHECK(typed_diagnostics_keep_codes_and_strict_compatibility());
}
