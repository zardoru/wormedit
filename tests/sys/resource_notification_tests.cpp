#include "../support/stale_action_assertions.h"
#include <catch2/generators/catch_generators.hpp>
#include "sys/metadata/metadata_asset_runtime.h"
#include <wormedit/formats/osu_mania.h>
#include <span>
#include "sys/resource_job_state.h"
#include "ui/frontend.h"

#include <wormedit/core/resource_file_copy.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct resource_notification_fixture_t {
    notification_center_t center;
    notification_sink_t sink{center};
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t resource_copy{scheduler, sink};
    document_history_t history;
    workspace_controller_t workspace;
    std::filesystem::path root = unique_directory();
    std::filesystem::path source = root / "source.wav";
    std::filesystem::path project = root / "project";
    runtime::metadata_asset_t assets{sink};

    static auto unique_directory() -> std::filesystem::path {
        static std::atomic<std::uint64_t> sequence{};
        return std::filesystem::temp_directory_path() /
            ("wormedit-resource-notification-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(sequence.fetch_add(1)));
    }

    resource_notification_fixture_t() {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(project);
        std::ofstream{source} << "test audio bytes";
        history.document.id = song_id_t{1};
        history.document.charts.push_back({.id = chart_id_t{1}});
        workspace.active_tab = workspace_tab_t::metadata;
        workspace.selected_chart = chart_id_t{1};
    }
    ~resource_notification_fixture_t() { std::filesystem::remove_all(root); }

    auto entries() -> std::vector<notification_entry_t> {
        center.drain_inbox(notification_time_t{0});
        return center.snapshot(notification_capacity);
    }
    auto inspect_action() const -> wormedit::ui::workspace_action_t {
        wormedit::ui::workspace_action_t action{};
        action.compose_input_chart_id.value = 1;
        action.compose_input_document_revision = history.document.revision;
        action.compose_resource_asset_id.value = 1;
        action.metadata_inspect_import = true;
        action.compose_import_path = source.c_str();
        return action;
    }
    auto wait_for_import() -> std::optional<std::string> {
        std::optional<std::string> completed;
        for (int attempt = 0; attempt < 1000 && !completed; ++attempt) {
            completed = assets.poll(history, workspace, project);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return completed;
    }
    void wait_for_preview() {
        for (int attempt = 0; attempt < 200 && assets.copy_preview_pending(); ++attempt) {
            (void)assets.poll(history, workspace, project);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        (void)assets.poll(history, workspace, project);
    }
};

void expect_notification(const notification_entry_t& entry, notification_severity_t severity,
                         notification_source_t source, std::string_view message) {
    CHECK(entry.severity == severity);
    CHECK(entry.source == source);
    CHECK(entry.message == message);
}

void check_resource_notification(bool value) { CHECK(value); }
void require_resource_notification(bool value) { REQUIRE(value); }

auto wait_for_resource_result(resource_notification_fixture_t& fixture)
    -> std::optional<std::string> {
    std::optional<std::string> result;
    for (int attempt = 0; attempt < 200 && !result; ++attempt) {
        result = fixture.resource_copy.poll(fixture.history, fixture.workspace);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return result;
}

auto wait_for_export_result(runtime::output_group_export_t& exports,
                            const document_history_t& history) -> std::optional<std::string> {
    std::optional<std::string> result;
    for (int attempt = 0; attempt < 200 && !result; ++attempt) {
        result = exports.poll(history);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return result;
}

auto failed_copy_worker(const resource_copy_request_t&, std::stop_token)
    -> resource_copy_job_t::work_outcome_t {
    return std::unexpected(wormedit::error_t<failure_code_t>{failure_code_t::task_failed,
                                                                "Injected copy worker failure."});
}
auto cancelled_copy_worker(const resource_copy_request_t&, std::stop_token)
    -> resource_copy_job_t::work_outcome_t {
    return resource_copy_job_t::work_outcome_t{
        std::in_place, std::unexpected(wormedit::error_t<failure_code_t>{
                           failure_code_t::cancelled, {}})};
}

struct export_worker_control_t {
    std::atomic<bool> entered{};
    std::atomic<int> publications{};
    std::atomic<std::uint64_t> observed_revision{};
};
export_worker_control_t export_worker_control;

auto export_test_worker(const output_group_export_request_t& request, std::stop_token stop)
    -> output_group_export_task_result_t {
    const auto& mode = request.plan.group.name;
    if (mode == "throw")
        throw std::runtime_error("Injected export worker exception.");
    if (mode == "failure")
        return {.message = "Injected publication failure."};
    if (mode == "confirm" && request.plan.existing_files == existing_files_policy_t::ask)
        return {.confirmation_required = true,
                .message = "Replace existing files?",
                .severity = notification_severity_t::warning,
                .detail = "confirm: exports/chart.bms\n"};
    const bool before = mode == "cancel-before";
    const bool after = mode == "cancel-after";
    if (!before)
        ++export_worker_control.publications;
    request.progress->total.store(2);
    request.progress->completed.store(1);
    export_worker_control.entered = true;
    if (before || after) {
        while (!stop.stop_requested())
            std::this_thread::yield();
    }
    export_worker_control.observed_revision = request.document.revision;
    if (before)
        return {.message = "Export cancelled before publication.",
                .severity = notification_severity_t::info};
    return {.published = true,
            .message = "Output group published from the selected revision.",
            .severity = notification_severity_t::success};
}

auto start_test_export(runtime::output_group_export_t& runtime,
                       const document_history_t& history, std::string mode = {})
    -> runtime::output_group_export_t::start_outcome_t {
    return runtime.start(history.document,
        output_group_export_plan_t::from(history.document, {.name = std::move(mode)}),
        {}, export_test_worker);
}

auto wait_for_export_worker() -> bool {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (export_worker_control.entered)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}
} // namespace

TEST_CASE("metadata copy inspection posts start and completion once", "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    REQUIRE(fixture.assets.start_copy_preview(action, fixture.history, fixture.workspace,
                                              fixture.project) ==
            "Copy destination inspection started.");
    auto entries = fixture.entries();
    REQUIRE(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::info,
                        notification_source_t::import_, "Copy destination inspection started.");

    fixture.wait_for_preview();
    entries = fixture.entries();
    REQUIRE(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::info,
                        notification_source_t::import_,
                        "Destination is free; copy will create it.");
    (void)fixture.assets.poll(fixture.history, fixture.workspace, fixture.project);
    CHECK(fixture.entries().size() == 2);
}

TEST_CASE("metadata inspection retains advisory feedback after invalidation and cancel",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    require_resource_notification(fixture.assets.start_copy_preview(
                                      action, fixture.history, fixture.workspace,
                                      fixture.project) == "Copy destination inspection started.");
    fixture.assets.clear_copy_preview_key();
    ++fixture.history.document.revision;
    fixture.assets.cancel();
    fixture.wait_for_preview();
    check_resource_notification(!fixture.assets.copy_preview_pending());
    check_resource_notification(!fixture.assets.copy_preview_key());
    const auto entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    check_resource_notification(entries[0].source == notification_source_t::import_);
    check_resource_notification(entries[0].severity == notification_severity_t::info);
    check_resource_notification(!entries[0].message.empty());
    (void)fixture.assets.poll(fixture.history, fixture.workspace, fixture.project);
    check_resource_notification(fixture.entries().size() == 2);
}

TEST_CASE("metadata copy inspection rejection is explicit and does not mutate document",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    action.compose_input_document_revision++;
    const auto revision = fixture.history.document.revision;
    const auto message = fixture.assets.start_copy_preview(action, fixture.history,
                                                           fixture.workspace, fixture.project);
    test::check_stale_action_message(message, "Metadata copy inspection");
    const auto entries = fixture.entries();
    REQUIRE(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::warning,
                        notification_source_t::import_, message);
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.empty());
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("metadata import completion rejects its captured project and revision",
          "[unit][sys][resource-notification][metadata-completion-stamp]") {
    const auto replace_project = GENERATE(false, true);
    resource_notification_fixture_t fixture;
    fixture.history.document.settings.audio_asset_directory = "media/audio";
    fixture.source = fixture.root / "source.ogg";
    std::filesystem::copy_file(std::filesystem::path{__FILE__}.parent_path().parent_path() / "fixtures" / "audio" / "keysound-c4.ogg",
                               fixture.source);
    auto action = fixture.inspect_action();
    action.metadata_import_copy_project = true;
    (void)fixture.assets.start_import_copy(action, fixture.history, fixture.workspace,
        fixture.project, fixture.scheduler, false, false);
    require_resource_notification(fixture.assets.import_pending());
    if (replace_project) fixture.history.document.id = song_id_t{2};
    else {
        ++fixture.history.document.revision;
        fixture.history.document.settings.audio_asset_directory = "later/audio";
    }
    const auto message = fixture.wait_for_import();
    require_resource_notification(message.has_value());
    test::check_stale_action_message(*message, "Metadata audio copy linking");
    check_resource_notification(!std::filesystem::exists(fixture.project / "media/audio/source.ogg"));
    check_resource_notification(std::filesystem::exists(fixture.source));
    check_resource_notification(fixture.history.document.audio_assets.empty());
    check_resource_notification(fixture.history.undo.empty());
}

TEST_CASE("metadata import-copy rejects stale start before inspecting audio",
          "[unit][sys][resource-notification][metadata-copy-stamp]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    action.metadata_import_copy_project = true;
    ++action.compose_input_document_revision;
    const auto message = fixture.assets.start_import_copy(action, fixture.history, fixture.workspace,
        fixture.project, fixture.scheduler, false, false);
    test::check_stale_action_message(message, "Metadata import-copy");
    check_resource_notification(!fixture.assets.import_pending());
    check_resource_notification(fixture.history.undo.empty());
}
#endif

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("metadata import decode failure posts once and never links invalid audio",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    auto action = fixture.inspect_action();
    action.metadata_import_copy_project = true;
    const auto revision = fixture.history.document.revision;
    const auto started = fixture.assets.start_import_copy(action, fixture.history, fixture.workspace,
        fixture.project, fixture.scheduler, false, false);
    require_resource_notification(fixture.assets.import_pending());
    auto entries = fixture.entries();
    require_resource_notification(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::info, notification_source_t::import_, started);
    const auto completed = fixture.wait_for_import();
    require_resource_notification(static_cast<bool>(completed));
    entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::error, notification_source_t::import_, *completed);
    check_resource_notification(!fixture.assets.import_pending());
    check_resource_notification(!fixture.assets.poll(fixture.history, fixture.workspace, fixture.project));
    check_resource_notification(fixture.entries().size() == 2);
    check_resource_notification(fixture.history.document.audio_assets.empty());
    check_resource_notification(fixture.history.document.revision == revision);
    check_resource_notification(fixture.history.undo.empty());
}
#endif

TEST_CASE("resource copy worker failure posts once and leaves the document unchanged",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    require_resource_notification(static_cast<bool>(fixture.resource_copy.start(
        {.target = {.asset = asset_id_t{1},
                    .document = document_stamp_t::from(fixture.history.document),
                    .source_path = fixture.source,
                    .export_copy = false}},
        failed_copy_worker)));
    auto entries = fixture.entries();
    require_resource_notification(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::info, notification_source_t::import_,
                        "Resource copy started in the background.");

    const auto completed = wait_for_resource_result(fixture);
    require_resource_notification(completed == "Injected copy worker failure.");
    entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::error,
                        notification_source_t::import_, *completed);
    check_resource_notification(!fixture.resource_copy.poll(fixture.history, fixture.workspace));
    check_resource_notification(fixture.entries().size() == 2);
    check_resource_notification(fixture.history.document.revision == revision);
    check_resource_notification(fixture.history.undo.empty());
}

TEST_CASE("resource copy stale completion warns once without changing document",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    const auto revision = fixture.history.document.revision;
    require_resource_notification(static_cast<bool>(fixture.resource_copy.start(
        {.target = {.asset = asset_id_t{1},
                    .document = document_stamp_t{.project = fixture.history.document.id,
                                                 .revision = revision - 1},
                    .source_path = fixture.source,
                    .export_copy = false},
         .project_directory = fixture.project})));
    const auto completed = wait_for_resource_result(fixture);
    require_resource_notification(completed.has_value());
    test::check_stale_action_message(*completed, "Resource copy relink");
    const auto entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::warning,
                        notification_source_t::import_, *completed);
    check_resource_notification(fixture.history.document.revision == revision);
    check_resource_notification(fixture.history.document.audio_assets.empty());
    check_resource_notification(fixture.history.undo.empty());
}

TEST_CASE("output export publishes typed success and terminal failure once",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    runtime::output_group_export_t exports{fixture.sink};
    require_resource_notification(static_cast<bool>(start_test_export(exports, fixture.history)));
    auto entries = fixture.entries();
    require_resource_notification(entries.size() == 1);
    expect_notification(entries[0], notification_severity_t::info, notification_source_t::export_,
                        "Export target preparation started in the background.");
    const auto completed = wait_for_export_result(exports, fixture.history);
    require_resource_notification(completed == "Output group published from the selected revision.");
    entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::success,
                        notification_source_t::export_, *completed);
    check_resource_notification(!exports.poll(fixture.history));
    check_resource_notification(fixture.entries().size() == 2);

    require_resource_notification(static_cast<bool>(
        start_test_export(exports, fixture.history, "failure")));
    (void)wait_for_export_result(exports, fixture.history);
    entries = fixture.entries();
    require_resource_notification(entries.size() == 4);
    expect_notification(entries[0], notification_severity_t::error,
                        notification_source_t::export_, "Injected publication failure.");
}

TEST_CASE("resource copy links a real successful copy and canceled fallback remains visible", "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    fixture.workspace.song = fixture.history.document.id;
    fixture.history.document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = fixture.source});
    require_resource_notification(static_cast<bool>(fixture.resource_copy.start(
        {.target = {.asset = asset_id_t{1},
                    .document = document_stamp_t::from(fixture.history.document),
                    .source_path = fixture.source},
         .project_directory = fixture.project})));
    const auto completed = wait_for_resource_result(fixture);
    require_resource_notification(completed == "Resource copied into the project and linked.");
    auto entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], notification_severity_t::success, notification_source_t::import_, *completed);
    check_resource_notification(fixture.history.document.audio_assets[0].source_path ==
                                "assets/source.wav");
    check_resource_notification(fixture.history.undo.size() == 1);
    require_resource_notification(static_cast<bool>(fixture.resource_copy.start(
        {.target = {.asset = asset_id_t{1},
                    .document = document_stamp_t::from(fixture.history.document)}},
        cancelled_copy_worker)));
    require_resource_notification(wait_for_resource_result(fixture) == "Resource copy failed.");
    entries = fixture.entries();
    expect_notification(entries[0], notification_severity_t::info, notification_source_t::import_, "Resource copy failed.");
    check_resource_notification(fixture.history.undo.size() == 1);
}

TEST_CASE("export cancellation retains publication receipts and busy admission",
          "[unit][sys][resource-notification]") {
    const auto mode = GENERATE(std::string{"cancel-before"}, std::string{"cancel-after"});
    resource_notification_fixture_t fixture;
    runtime::output_group_export_t exports{fixture.sink};
    export_worker_control.entered = false;
    export_worker_control.publications = 0;
    require_resource_notification(static_cast<bool>(start_test_export(exports, fixture.history, mode)));
    const bool entered = wait_for_export_worker();
    // Always unblock the worker before any fatal assertion.
    exports.cancel();
    require_resource_notification(entered);
    check_resource_notification(exports.is_busy());
    check_resource_notification(exports.view().active);
    check_resource_notification(exports.view().cancelling);
    check_resource_notification(exports.view().completed == 1);
    check_resource_notification(exports.view().total == 2);
    check_resource_notification(!start_test_export(exports, fixture.history));
    const auto completed = wait_for_export_result(exports, fixture.history);
    require_resource_notification(completed.has_value());
    check_resource_notification(!exports.is_busy());
    check_resource_notification(!exports.poll(fixture.history));
    const bool published = mode == "cancel-after";
    check_resource_notification(export_worker_control.publications == (published ? 1 : 0));
    check_resource_notification(*completed == (published ? "Output group published from the selected revision."
                                  : "Export cancelled before publication."));
    const auto entries = fixture.entries();
    require_resource_notification(entries.size() == 2);
    expect_notification(entries[0], published ? notification_severity_t::success
                                              : notification_severity_t::info,
                        notification_source_t::export_, *completed);
}

TEST_CASE("export keeps its captured identity and reports exceptions once",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    runtime::output_group_export_t exports{fixture.sink};
    export_worker_control.entered = false;
    require_resource_notification(static_cast<bool>(start_test_export(exports, fixture.history, "cancel-after")));
    const bool entered = wait_for_export_worker();
    const auto captured_revision = fixture.history.document.revision;
    ++fixture.history.document.revision;
    exports.cancel();
    require_resource_notification(entered);
    const auto completed = wait_for_export_result(exports, fixture.history);
    require_resource_notification(completed.has_value());
    check_resource_notification(export_worker_control.observed_revision == captured_revision);
    check_resource_notification(completed->find("editor session changed") != std::string::npos);
    check_resource_notification(fixture.entries()[0].severity == notification_severity_t::warning);
    require_resource_notification(static_cast<bool>(start_test_export(exports, fixture.history, "throw")));
    const auto failed = wait_for_export_result(exports, fixture.history);
    require_resource_notification(failed.has_value());
    check_resource_notification(failed->find("Injected export worker exception.") != std::string::npos);
    check_resource_notification(fixture.entries()[0].severity == notification_severity_t::error);
    check_resource_notification(!exports.poll(fixture.history));
}

TEST_CASE("Ask export confirmation is retained until one-run replace or cancel",
          "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    runtime::output_group_export_t exports{fixture.sink};
    require_resource_notification(
        static_cast<bool>(start_test_export(exports, fixture.history, "confirm")));
    require_resource_notification(wait_for_export_result(exports, fixture.history).has_value());
    const auto confirmation = exports.confirmation_view();
    require_resource_notification(confirmation.required);
    check_resource_notification(std::string_view{confirmation.detail}.find("exports/chart.bms") !=
                                std::string_view::npos);
    check_resource_notification(!start_test_export(exports, fixture.history));
    require_resource_notification(static_cast<bool>(exports.confirm(fixture.history)));
    const auto completed = wait_for_export_result(exports, fixture.history);
    require_resource_notification(completed.has_value());
    check_resource_notification(!exports.confirmation_view().required);

    require_resource_notification(
        static_cast<bool>(start_test_export(exports, fixture.history, "confirm")));
    require_resource_notification(wait_for_export_result(exports, fixture.history).has_value());
    require_resource_notification(exports.confirmation_view().required);
    exports.cancel_confirmation();
    check_resource_notification(!exports.confirmation_view().required);
}

TEST_CASE("output preview and publication share format request policy",
          "[unit][sys][resource-notification]") {
    song_document_t document;
    output_group_t group{.family = format_family_t::stepmania,
                         .charts = {chart_id_t{42}},
                         .destination_kind = output_destination_kind_t::file,
                         .relative_output_path = "sm",
                         .stepmania =
                             stepmania_export_policy_t{.file_profile = stepmania_profile_t::sm}};
    auto plan = output_group_export_plan_t::from(document, group, existing_files_policy_t::replace);
    require_resource_notification(plan.request.stepmania.has_value());
    check_resource_notification(plan.request.stepmania->file_profile == stepmania_profile_t::sm);
    check_resource_notification(plan.request.charts == group.charts);
    check_resource_notification(plan.request.overwrite);
    check_resource_notification(plan.group.existing_files == existing_files_policy_t::ask);
    check_resource_notification(plan.existing_files == existing_files_policy_t::replace);
    group.existing_files = existing_files_policy_t::keep;
    plan = output_group_export_plan_t::from(document, group);
    check_resource_notification(plan.existing_files == existing_files_policy_t::keep);
    check_resource_notification(!plan.request.overwrite);
    plan = output_group_export_plan_t::from(document, group, existing_files_policy_t::replace);
    check_resource_notification(plan.existing_files == existing_files_policy_t::replace);
    check_resource_notification(plan.group.existing_files == existing_files_policy_t::keep);
    group.relative_output_path = "chart.ssc";
    group.stepmania->file_profile = stepmania_profile_t::ssc;
    group.existing_files = existing_files_policy_t::ask;
    plan = output_group_export_plan_t::from(document, group);
    require_resource_notification(plan.request.stepmania.has_value());
    check_resource_notification(plan.request.stepmania->file_profile == stepmania_profile_t::ssc);
    check_resource_notification(!plan.request.overwrite);
    group.family = format_family_t::native_project;
    plan = output_group_export_plan_t::from(document, group);
    check_resource_notification(plan.request.charts.empty());

    group.family = format_family_t::bms;
    group.destination_kind = output_destination_kind_t::directory;
    for (const auto* profile : {"bme", "bml", "pms"}) {
        group.relative_output_path = std::string{"chart."} + profile;
        plan = output_group_export_plan_t::from(document, group);
        check_resource_notification(plan.request.target_profile.empty());
        check_resource_notification(plan.request.bms_output_extension.empty());
        document.charts.push_back({.id = chart_id_t{42},
                                   .source_family = format_family_t::bmson});
        plan = output_group_export_plan_t::from(document, group);
        check_resource_notification(plan.request.target_profile.empty());
        document.charts.clear();
    }
    document.charts.push_back({.id = chart_id_t{42}, .source_family = format_family_t::bmson});
    group.relative_output_path = "foreign-extension/chart.osu";
    plan = output_group_export_plan_t::from(document, group);
    check_resource_notification(plan.group.destination_kind ==
                                output_destination_kind_t::directory);
    check_resource_notification(plan.request.bms_output_extension.empty());
}

TEST_CASE("Rejected music export creates no destination", "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    fixture.history.document.audio_assets.push_back({.id = asset_id_t{5}, .source_path = fixture.source});
    fixture.history.document.resources.push_back({.id = resource_id_t{6}, .name = "Music",
        .asset = asset_id_t{5}, .roles = {resource_role_t::music}});
    auto plan = output_group_export_plan_t::from(fixture.history.document,
        {.family = format_family_t::osu_mania, .charts = {chart_id_t{1}},
         .relative_output_path = "exports/song.osu"});
    plan.request.target_profile = "invalid-profile";
    const auto result = run_output_group_export(
        {.document = fixture.history.document, .plan = std::move(plan), .asset_base = fixture.project}, {});
    REQUIRE_FALSE(result.published);
    REQUIRE_FALSE(std::filesystem::exists(fixture.project / "exports"));
}

TEST_CASE("Ask Replace Keep and all-target preflight share one publication path",
          "[unit][sys][resource-notification][output-group-export]") {
    resource_notification_fixture_t fixture;
    std::filesystem::create_directories(fixture.project / "exports");
    output_group_t group{.name = "Native",
                         .family = format_family_t::native_project,
                         .charts = {chart_id_t{1}},
                         .destination_kind = output_destination_kind_t::file,
                         .relative_output_path = "exports/song.wormedit",
                         .existing_files = existing_files_policy_t::ask};
    const auto run = [&](std::optional<existing_files_policy_t> one_run = std::nullopt) {
        return run_output_group_export(
            {.document = fixture.history.document,
             .plan = output_group_export_plan_t::from(fixture.history.document, group, one_run),
             .asset_base = fixture.project},
            {});
    };

    const auto first = run();
    require_resource_notification(first.published);
    const auto destination = fixture.project / group.relative_output_path;
    require_resource_notification(std::filesystem::is_regular_file(destination));
    const auto original_size = std::filesystem::file_size(destination);

    const auto asked = run();
    require_resource_notification(!asked.published);
    check_resource_notification(asked.confirmation_required);
    check_resource_notification(asked.detail.find("Native: exports/song.wormedit") !=
                                std::string::npos);
    check_resource_notification(std::filesystem::file_size(destination) == original_size);

    const auto kept = run(existing_files_policy_t::keep);
    require_resource_notification(!kept.published);
    check_resource_notification(!kept.confirmation_required);
    check_resource_notification(std::filesystem::file_size(destination) == original_size);

    const auto replaced = run(existing_files_policy_t::replace);
    require_resource_notification(replaced.published);
    check_resource_notification(group.existing_files == existing_files_policy_t::ask);

    std::filesystem::create_directories(fixture.project / "batch");
    const std::array ask_groups{group,
                                output_group_t{.name = "Deferred",
                                               .family = format_family_t::native_project,
                                               .charts = {chart_id_t{1}},
                                               .destination_kind = output_destination_kind_t::file,
                                               .relative_output_path = "batch/deferred.wormedit",
                                               .existing_files = existing_files_policy_t::replace}};
    const auto ask_plan =
        output_group_export_plan_t::from_all(fixture.history.document, ask_groups);
    require_resource_notification(ask_plan.has_value());
    const auto batch_asked = run_output_group_export(
        {.document = fixture.history.document, .plan = *ask_plan, .asset_base = fixture.project},
        {});
    require_resource_notification(!batch_asked.published);
    check_resource_notification(batch_asked.confirmation_required);
    check_resource_notification(
        !std::filesystem::exists(fixture.project / "batch/deferred.wormedit"));

    const std::array groups{output_group_t{.name = "First",
                                           .family = format_family_t::native_project,
                                           .charts = {chart_id_t{1}},
                                           .destination_kind = output_destination_kind_t::file,
                                           .relative_output_path = "batch/first.wormedit",
                                           .existing_files = existing_files_policy_t::replace},
                            output_group_t{.name = "Invalid",
                                           .family = format_family_t::bms,
                                           .charts = {chart_id_t{1}},
                                           .relative_output_path = "/invalid",
                                           .existing_files = existing_files_policy_t::replace}};
    const auto batch_plan = output_group_export_plan_t::from_all(fixture.history.document, groups);
    require_resource_notification(batch_plan.has_value());
    const auto batch = run_output_group_export(
        {.document = fixture.history.document, .plan = *batch_plan, .asset_base = fixture.project},
        {});
    require_resource_notification(!batch.published);
    check_resource_notification(!std::filesystem::exists(fixture.project / "batch/first.wormedit"));
}

TEST_CASE("Music export publishes its chart and automatic asset together", "[unit][sys][resource-notification]") {
    resource_notification_fixture_t fixture;
    const std::string source = "osu file format v14\n[General]\nAudioFilename: source.wav\nMode: 3\n"
        "[Metadata]\nTitle:Export music\nArtist:Test\nVersion:Normal\n"
        "[Difficulty]\nCircleSize:4\n[TimingPoints]\n0,500,4,1,0,100,1,0\n[HitObjects]\n";
    auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{source.data(), source.size()}),
                                       {.asset_base_directory = fixture.root});
    REQUIRE(parsed.value);
    auto song = std::move(parsed.value);
    const auto chart = song->charts.front().id;
    auto plan = output_group_export_plan_t::from(*song,
        {.family = format_family_t::osu_mania, .charts = {chart},
         .relative_output_path = "exports/song.osu"});
    const auto result = run_output_group_export(
        {.document = *song, .plan = std::move(plan), .asset_base = fixture.root}, {});
    INFO(result.message);
    REQUIRE(result.published);
    const auto output_directory = fixture.root / "exports/song.osu";
    REQUIRE(std::filesystem::is_directory(output_directory));
    REQUIRE(std::ranges::any_of(std::filesystem::directory_iterator{output_directory},
                               [](const auto& entry) {
                                   return entry.path().extension() == ".osu";
                               }));
    REQUIRE(std::filesystem::is_regular_file(output_directory / "assets" /
                                             fixture.source.filename()));
}

TEST_CASE("Inactive export confirmation safely populates Compose text views",
          "[unit][sys][resource-notification]") {
    runtime::output_group_export_t exports;
    const auto confirmation = exports.confirmation_view();
    wormedit::ui::compose_view_t view;
    view.export_confirmation_message = confirmation.message;
    view.export_confirmation_detail = confirmation.detail;
    check_resource_notification(!confirmation.required);
    check_resource_notification(view.export_confirmation_message.empty());
    check_resource_notification(view.export_confirmation_detail.empty());
}
