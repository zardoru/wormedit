#include "sys/project_open.h"

#include <wormedit/formats/bms.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using wormedit::conversion_diagnostic_code_t;
using wormedit::diagnostic_severity_t;
using wormedit::format_family_t;
using wormedit::loss_policy_t;

auto matrix_fixtures() -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(WORMEDIT_FIXTURE_DIR)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() &&
            wormedit::sys::is_supported_project_file_extension(entry.path()) &&
            !name.starts_with("negative-") && !name.starts_with("invalid-"))
            paths.push_back(entry.path());
    }
    std::ranges::sort(paths);
    return paths;
}

auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, {}};
}

auto bms_encoding(const std::filesystem::path& path) -> std::optional<wormedit::text_encoding_t> {
    using wormedit::text_encoding_t;
    static const std::map<std::string, text_encoding_t> choices{
        {"cp932-shift-jis.bms", text_encoding_t::cp932},
        {"euc-kr.bms", text_encoding_t::euc_kr},
        {"cp949-extension.bms", text_encoding_t::cp949},
        {"windows-1252.bms", text_encoding_t::windows_1252}};
    const auto found = choices.find(path.filename().string());
    return found == choices.end() ? std::nullopt : std::optional{found->second};
}

// Mirrors the fixture inventory: BMS-family files need an explicit lane
// profile and encoding choice, legacy-negative StepMania files an explicit
// timing syntax; everything else opens through the application path.
auto load_fixture(const std::filesystem::path& path) -> std::optional<wormedit::song_document_t> {
    const auto extension = path.extension().string();
    const auto source = read_text(path);
    const auto bytes = std::as_bytes(std::span{source.data(), source.size()});
    if (extension == ".bms" || extension == ".bme" || extension == ".bml" || extension == ".pms") {
        wormedit::bms_parse_options_t options;
        options.file_extension = extension;
        options.encoding_override = bms_encoding(path);
        options.lane_profile_id = extension == ".pms"   ? "pms-standard-9button"
                                  : extension == ".bme" ? "bme-7k-outside-scratch"
                                                        : "bms-5k-outside-scratch-free-zone";
        auto parsed = wormedit::parse_bms_bytes(bytes, options);
        if (parsed.has_errors())
            return std::nullopt;
        return std::move(parsed.song);
    }
    if (path.filename().string().starts_with("legacy-negative-")) {
        wormedit::stepmania_parse_options_t options;
        options.source_timing_syntax = path.filename() == "legacy-negative-stop.sm"
            ? wormedit::stepmania_timing_syntax_t::legacy_negative_stop
            : wormedit::stepmania_timing_syntax_t::legacy_negative_bpm;
        auto parsed = wormedit::parse_stepmania_bytes(bytes, options);
        if (parsed.has_errors())
            return std::nullopt;
        return std::move(parsed.song);
    }
    return wormedit::sys::open_project_file(path).document;
}

struct matrix_target_t {
    const char* name;
    wormedit::conversion_request_t request;
};

auto matrix_targets() -> std::vector<matrix_target_t> {
    const auto stepmania = [](wormedit::stepmania_profile_t profile) {
        return wormedit::stepmania_export_policy_t{
            .file_profile = profile,
            .timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps};
    };
    return {
        {"native", {.target = format_family_t::native_project}},
        {"bms", {.target = format_family_t::bms}},
        {"ssc", {.target = format_family_t::stepmania,
                 .stepmania = stepmania(wormedit::stepmania_profile_t::ssc)}},
        {"sm", {.target = format_family_t::stepmania,
                .stepmania = stepmania(wormedit::stepmania_profile_t::sm)}},
        {"osu", {.target = format_family_t::osu_mania}},
        {"bmson", {.target = format_family_t::bmson}},
    };
}

// A diagnostic is classified when it is a loss (warning, or info once the
// policy allows it) or a typed error that names a real conversion failure
// rather than a malformed request.
auto classification_failure(const wormedit::conversion_diagnostic_t& diagnostic,
                            loss_policy_t policy) -> std::optional<std::string> {
    if (diagnostic.message.empty())
        return "empty message";
    if (diagnostic.severity == diagnostic_severity_t::error) {
        if (diagnostic.code == conversion_diagnostic_code_t::invalid_request ||
            diagnostic.code == conversion_diagnostic_code_t::unknown_target_profile)
            return "error is a request mistake, not a conversion outcome";
        if (diagnostic.loss && diagnostic.code != conversion_diagnostic_code_t::target_diagnostic)
            return "loss flag on a non-target error";
        return std::nullopt;
    }
    const auto expected = policy == loss_policy_t::allow ? diagnostic_severity_t::info
                                                         : diagnostic_severity_t::warning;
    if (diagnostic.severity == diagnostic_severity_t::warning && !diagnostic.loss)
        return "warning without a loss classification";
    if (diagnostic.loss && diagnostic.severity != expected)
        return "loss severity does not match the loss policy";
    return std::nullopt;
}

struct matrix_tally_t {
    std::size_t pairs{};
    std::size_t converted{};
    std::size_t lossy{};
};

auto request_for(const matrix_target_t& target, loss_policy_t policy)
    -> wormedit::conversion_request_t {
    auto request = target.request;
    request.loss_policy = policy;
    // Only the native project and BMS writers keep a branch tree.
    if (request.target != format_family_t::native_project && request.target != format_family_t::bms)
        request.branches = wormedit::branch_projection_t::active_projection;
    return request;
}

void check_diagnostics(const wormedit::conversion_result_t& result, loss_policy_t policy) {
    for (const auto& diagnostic : result.diagnostics) {
        INFO(diagnostic.message);
        const auto failure = classification_failure(diagnostic, policy);
        INFO(failure.value_or("classified"));
        CHECK_FALSE(failure.has_value());
    }
}

void check_outputs(const wormedit::conversion_result_t& result) {
    if (!result)
        return;
    CHECK_FALSE(result.outputs.empty());
    for (const auto& output : result.outputs)
        CHECK(output.reimport_validated);
}

void check_rejected_loss(const wormedit::conversion_result_t& result) {
    CHECK_FALSE(result);
    CHECK(result.outputs.empty());
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == conversion_diagnostic_code_t::loss_rejected;
    }));
}

void check_pair(const std::filesystem::path& fixture, const wormedit::song_document_t& song,
                const matrix_target_t& target, matrix_tally_t& tally) {
    INFO(fixture.filename().string() << " -> " << target.name);
    const auto warned = wormedit::convert_document(song, request_for(target, loss_policy_t::warn));
    const auto allowed = wormedit::convert_document(song, request_for(target, loss_policy_t::allow));
    const auto rejected =
        wormedit::convert_document(song, request_for(target, loss_policy_t::reject));
    check_diagnostics(warned, loss_policy_t::warn);
    check_diagnostics(allowed, loss_policy_t::allow);
    check_diagnostics(rejected, loss_policy_t::reject);
    for (const auto* result : {&warned, &allowed, &rejected})
        check_outputs(*result);
    const bool lossy = std::ranges::any_of(warned.diagnostics, &wormedit::conversion_diagnostic_t::loss);
    // A pair that already fails for another reason never reaches the loss gate.
    if (static_cast<bool>(warned) && lossy)
        check_rejected_loss(rejected);
    ++tally.pairs;
    tally.converted += static_cast<bool>(warned) ? 1U : 0U;
    tally.lossy += static_cast<bool>(warned) && lossy ? 1U : 0U;
}

} // namespace

TEST_CASE("loss matrix classifies every diagnostic for every fixture and writer",
          "[unit][format][conversion][loss]") {
    const auto fixtures = matrix_fixtures();
    REQUIRE_FALSE(fixtures.empty());
    const auto targets = matrix_targets();
    matrix_tally_t tally;
    for (const auto& fixture : fixtures) {
        INFO(fixture.string());
        const auto song = load_fixture(fixture);
        REQUIRE(song.has_value());
        for (const auto& target : targets)
            check_pair(fixture, *song, target, tally);
    }
    // Guard against a vacuous matrix: some pairs must convert, some with loss.
    CHECK(tally.pairs == fixtures.size() * targets.size());
    CHECK(tally.converted > 0);
    CHECK(tally.lossy > 0);
}
