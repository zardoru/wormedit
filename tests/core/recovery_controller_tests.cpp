#include <wormedit/core/recovery_controller.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace {

struct temporary_directory_t {
    std::filesystem::path path;

    temporary_directory_t() {
        static std::atomic<std::uint64_t> sequence{1};
        path = std::filesystem::temp_directory_path() /
               ("wormedit-recovery-controller-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::create_directories(path);
    }

    ~temporary_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

auto read_text(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto valid_song(std::uint64_t revision) -> wormedit::song_document_t {
    return {.schema_version = 2, .id = wormedit::song_id_t{417}, .revision = revision};
}

auto scan_project(const std::filesystem::path& primary)
    -> wormedit::native_project_recovery_scan_outcome_t {
    return wormedit::scan_native_project_recovery_candidates(
        {.primary_path = primary, .discover_stale_temporaries = false});
}

} // namespace

TEST_CASE("recovery controller exposes backend-neutral bounded rows and typed actions",
          "[unit][core][recovery-controller]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto backup = native_project_adjacent_backup_path(primary);
    const auto save_as = directory.path / "recovered-copy.wormedit";
    REQUIRE(create_native_project_file(primary, valid_song(5)).value);
    REQUIRE(create_native_project_recovery_file(backup, valid_song(6), {.session_sequence = 2})
                .value);
    const auto primary_before = read_text(primary);
    const auto backup_before = read_text(backup);
    auto scan = scan_project(primary);

    native_project_recovery_controller_t controller;
    const auto& view = controller.replace_scan(std::move(scan));
    REQUIRE(view.banner_visible);
    REQUIRE(view.rows.size() == 2);
    REQUIRE(view.preferred_recovery_row == 1);
    const auto primary_row = view.rows[0];
    const auto backup_row = view.rows[1];
    CHECK(primary_row.kind == native_project_recovery_candidate_kind_t::primary);
    CHECK(primary_row.available_actions.empty());
    CHECK(backup_row.status == native_project_recovery_candidate_status_t::valid);
    CHECK(backup_row.recommended_action == native_project_recovery_action_t::inspect_read_only);
    CHECK(std::ranges::find(backup_row.available_actions,
                            native_project_recovery_action_t::save_as_new_project) !=
          backup_row.available_actions.end());

    const auto inspected = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = backup_row.target,
        .kind = wormedit::native_project_recovery_target_kind_t::inspect});
    REQUIRE(inspected.value.accepted);
    REQUIRE(inspected.value.action_result.has_value());
    REQUIRE(inspected.value.action_result->value.applied);
    REQUIRE(inspected.value.action_result->value.document.has_value());
    CHECK(inspected.value.action_result->value.document->revision == 6);
    CHECK(read_text(primary) == primary_before);

    auto mismatched_target = backup_row.target;
    mismatched_target.candidate_index = primary_row.target.candidate_index;
    const auto mismatched = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = mismatched_target,
        .kind = wormedit::native_project_recovery_target_kind_t::inspect});
    CHECK_FALSE(mismatched.value.accepted);
    CHECK_FALSE(mismatched.value.action_result.has_value());
    CHECK(!mismatched.ok());
    CHECK(read_text(backup) == backup_before);

    auto out_of_range_target = backup_row.target;
    out_of_range_target.row_index = view.rows.size();
    const auto out_of_range = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = out_of_range_target,
        .kind = wormedit::native_project_recovery_target_kind_t::discard});
    CHECK_FALSE(out_of_range.value.accepted);
    CHECK_FALSE(out_of_range.value.action_result.has_value());
    CHECK(read_text(backup) == backup_before);

    const auto unavailable = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = primary_row.target,
        .kind = wormedit::native_project_recovery_target_kind_t::discard});
    CHECK_FALSE(unavailable.value.accepted);
    CHECK_FALSE(unavailable.value.action_result.has_value());
    CHECK(read_text(primary) == primary_before);

    const auto wrong_session = controller.dispatch_action(recover_native_project_recovery_action_t{
        .target = backup_row.target,
        .expected_session = {.is_set = true,
                             .version = native_project_document_version_t{
                                 .project_id = song_id_t{417}, .revision = 4}}});
    REQUIRE(wrong_session.value.accepted);
    REQUIRE(wrong_session.value.action_result.has_value());
    CHECK_FALSE(wrong_session.value.action_result->value.applied);
    CHECK(read_text(primary) == primary_before);
    CHECK(read_text(backup) == backup_before);

    const auto recovered = controller.dispatch_action(recover_native_project_recovery_action_t{
        .target = backup_row.target,
        .expected_session = {.is_set = true,
                             .version = native_project_document_version_t{
                                 .project_id = song_id_t{417}, .revision = 5}}});
    REQUIRE(recovered.value.accepted);
    REQUIRE(recovered.value.action_result.has_value());
    REQUIRE(recovered.value.action_result->value.applied);
    CHECK(recovered.value.action_result->value.document->revision == 6);
    CHECK(read_text(primary) == primary_before);

    const auto saved_as = controller.dispatch_action(save_as_native_project_recovery_action_t{
        .target = backup_row.target, .destination = save_as});
    REQUIRE(saved_as.value.accepted);
    REQUIRE(saved_as.value.action_result.has_value());
    REQUIRE(saved_as.value.action_result->value.applied);
    const auto saved_document = parse_native_project(read_text(save_as));
    REQUIRE(saved_document.value.has_value());
    CHECK(saved_document.value->revision == 6);
    CHECK(read_text(primary) == primary_before);

    const auto attempted_primary_overwrite =
        controller.dispatch_action(save_as_native_project_recovery_action_t{.target = backup_row.target,
                                                                     .destination = primary});
    REQUIRE(attempted_primary_overwrite.value.accepted);
    REQUIRE(attempted_primary_overwrite.value.action_result.has_value());
    CHECK_FALSE(attempted_primary_overwrite.value.action_result->value.applied);
    CHECK(read_text(primary) == primary_before);

    const auto old_generation = view.scan_generation;
    auto new_scan = scan_project(primary);
    const auto& refreshed = controller.replace_scan(std::move(new_scan));
    CHECK(refreshed.scan_generation == old_generation + 1);
    const auto stale = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = backup_row.target,
        .kind = wormedit::native_project_recovery_target_kind_t::discard});
    CHECK_FALSE(stale.value.accepted);
    CHECK_FALSE(stale.value.action_result.has_value());
    CHECK(read_text(backup) == backup_before);

    const auto refreshed_backup = refreshed.rows[1];
    const auto discarded = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = refreshed_backup.target,
        .kind = wormedit::native_project_recovery_target_kind_t::discard});
    REQUIRE(discarded.value.accepted);
    REQUIRE(discarded.value.action_result.has_value());
    REQUIRE(discarded.value.action_result->value.applied);
    REQUIRE(discarded.value.action_result->value.quarantined_path.has_value());
    CHECK_FALSE(std::filesystem::exists(backup));
    CHECK(read_text(*discarded.value.action_result->value.quarantined_path) == backup_before);
    CHECK(read_text(primary) == primary_before);
    CHECK_FALSE(controller.view().banner_visible);
    CHECK(controller.view().rows.empty());
    const auto repeated = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = refreshed_backup.target,
        .kind = wormedit::native_project_recovery_target_kind_t::discard});
    CHECK_FALSE(repeated.value.accepted);
    CHECK_FALSE(repeated.value.action_result.has_value());
}

TEST_CASE("recovery controller exposes corrupt-candidate quarantine and scan generations",
          "[unit][core][recovery-controller][quarantine]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto primary = directory.path / "chart.wormedit";
    const auto backup = native_project_adjacent_backup_path(primary);
    REQUIRE(create_native_project_file(primary, valid_song(1)).value);
    {
        std::ofstream stream(backup, std::ios::binary);
        stream << "corrupt recovery data";
    }
    const auto backup_before = read_text(backup);
    native_project_recovery_controller_t controller;
    const auto first_view = controller.replace_scan(scan_project(primary));
    REQUIRE(first_view.rows.size() == 2);
    const auto corrupt_row = first_view.rows[1];
    const auto first_generation = first_view.scan_generation;
    CHECK(corrupt_row.status == native_project_recovery_candidate_status_t::invalid);
    CHECK(corrupt_row.quarantine ==
          native_project_recovery_quarantine_t::recommend_quarantine_corrupt);
    CHECK(std::ranges::find(corrupt_row.available_actions,
                            native_project_recovery_action_t::quarantine) !=
          corrupt_row.available_actions.end());

    auto next_view_scan = scan_project(primary);
    const auto& next_view = controller.replace_scan(std::move(next_view_scan));
    CHECK(next_view.scan_generation == first_generation + 1);
    const auto stale_quarantine = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = corrupt_row.target,
        .kind = wormedit::native_project_recovery_target_kind_t::quarantine});
    CHECK_FALSE(stale_quarantine.value.accepted);
    CHECK(std::filesystem::exists(backup));
    CHECK(read_text(backup) == backup_before);

    const auto current_corrupt_row = next_view.rows[1];
    const auto quarantined = controller.dispatch_action(native_project_recovery_target_action_t{
        .target = current_corrupt_row.target,
        .kind = wormedit::native_project_recovery_target_kind_t::quarantine});
    REQUIRE(quarantined.value.accepted);
    REQUIRE(quarantined.value.action_result.has_value());
    REQUIRE(quarantined.value.action_result->value.applied);
    REQUIRE(quarantined.value.action_result->value.quarantined_path.has_value());
    CHECK_FALSE(std::filesystem::exists(backup));
    CHECK(read_text(*quarantined.value.action_result->value.quarantined_path) == backup_before);
}

TEST_CASE("recovery controller bounds rows, labels, diagnostics, and candidate acceptance",
          "[unit][core][recovery-controller][limits]") {
    using namespace wormedit;
    temporary_directory_t directory;
    const auto primary = directory.path / "a-long-project-filename.wormedit";
    const auto backup = native_project_adjacent_backup_path(primary);
    REQUIRE(create_native_project_file(primary, valid_song(1)).value);
    REQUIRE(create_native_project_recovery_file(backup, valid_song(2), {}).value);
    auto scan = scan_project(primary);

    native_project_recovery_controller_options_t options;
    options.maximum_rows = 1;
    options.maximum_path_label_bytes = 8;
    options.maximum_diagnostics = 1;
    scan.diagnostics = {
        {.severity = diagnostic_severity_t::warning,
         .code = native_project_storage_diagnostic_code_t::read_failure,
         .message = std::string(300, 'x'),
         .path = primary},
        {.severity = diagnostic_severity_t::warning,
         .code = native_project_storage_diagnostic_code_t::read_failure,
         .message = "second diagnostic",
         .path = backup},
    };
    native_project_recovery_controller_t controller{options};
    const auto& view = controller.replace_scan(std::move(scan));
    CHECK(view.banner_visible);
    CHECK(view.rows.size() == 1);
    CHECK(view.rows_truncated);
    CHECK(view.rows.front().path_label.size() <= options.maximum_path_label_bytes);
    CHECK(view.diagnostics.size() == 1);
    CHECK(view.diagnostics_truncated);
    CHECK(view.diagnostics.front().message.size() <= 240);
    CHECK(view.diagnostics.front().path.empty());

    auto oversized_scan = scan_project(primary);
    native_project_recovery_controller_options_t restrictive_options;
    restrictive_options.maximum_candidates = 1;
    native_project_recovery_controller_t restrictive{restrictive_options};
    const auto& rejected = restrictive.replace_scan(std::move(oversized_scan));
    CHECK(rejected.rows.empty());
    CHECK_FALSE(rejected.banner_visible);
    CHECK(rejected.diagnostics.size() == 1);
    CHECK(rejected.diagnostics.front().code ==
          native_project_storage_diagnostic_code_t::resource_limit);
    const auto no_active_scan = restrictive.dispatch_action(native_project_recovery_target_action_t{
        .target = view.rows.front().target,
        .kind = wormedit::native_project_recovery_target_kind_t::inspect});
    CHECK_FALSE(no_active_scan.value.accepted);
    CHECK(!no_active_scan.ok());
}
