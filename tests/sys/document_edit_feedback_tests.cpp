#include "sys/document_edit_feedback.h"

#include <catch2/catch_test_macros.hpp>
#include <wormedit/formats/native_project.h>

#include <catch2/generators/catch_generators.hpp>

#include <source_location>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
// Keep Catch's assertion scaffolding in one place, with the caller's location.
void check_feedback(bool value, std::source_location where = std::source_location::current()) {
    INFO("feedback assertion at " << where.file_name() << ':' << where.line());
    CHECK(value);
}

void require_feedback(bool value, std::source_location where = std::source_location::current()) {
    INFO("feedback prerequisite at " << where.file_name() << ':' << where.line());
    REQUIRE(value);
}
} // namespace

TEST_CASE("Document edit feedback requires an accepted diagnostic-free rebind",
          "[document-edit-feedback]") {
    const document_edit_description_t description{.success = "Updated.",
                                                  .rebind_failure = "Rebind failed."};
    SECTION("success") {
        const auto feedback = document_edit_feedback_t::from({.value = true}, description);
        check_feedback(feedback.accepted);
        check_feedback(feedback.severity == notification_severity_t::success);
        check_feedback(feedback.message_text() == "Updated.");
        check_feedback(feedback.diagnostics.empty());
    }
    SECTION("rejection without diagnostics uses the fixed fallback") {
        const auto feedback = document_edit_feedback_t::from({.value = false}, description);
        check_feedback(!feedback.accepted);
        check_feedback(feedback.severity == notification_severity_t::error);
        check_feedback(feedback.message_text() == "Rebind failed.");
    }
    SECTION("even informational diagnostics reject and preserve their order and codes") {
        const diagnostic_list_t diagnostics{
            {.severity = diagnostic_severity_t::info,
             .code = diagnostic_code_t::invalid_binding,
             .message = "First diagnostic."},
            {.code = diagnostic_code_t::invalid_id, .message = "Second diagnostic."}};
        const auto feedback = document_edit_feedback_t::from(
            {.value = true, .diagnostics = diagnostics}, description);
        check_feedback(!feedback.accepted);
        check_feedback(feedback.severity == notification_severity_t::info);
        check_feedback(feedback.message_text() == "First diagnostic.");
        check_feedback(feedback.diagnostics == diagnostics);
    }
}

TEST_CASE("Document edit feedback preserves caller diagnostic policies",
          "[document-edit-feedback]") {
    const auto severity =
        GENERATE(diagnostic_severity_t::info, diagnostic_severity_t::warning,
                 diagnostic_severity_t::error, static_cast<diagnostic_severity_t>(99));
    const dispatch_outcome_t rebound{.value = false,
                                     .diagnostics = {{.severity = severity,
                                                      .code = diagnostic_code_t::invalid_binding,
                                                      .message = "Binding diagnostic."}}};
    const document_edit_description_t description{.rebind_failure = "Caller fallback."};
    SECTION("audio and default Compose mapping") {
        const auto feedback = document_edit_feedback_t::from(rebound, description);
        check_feedback(feedback.message_text() == "Binding diagnostic.");
        check_feedback(feedback.severity == to_notification_severity(severity));
    }
    SECTION("metadata unknown severity remains informational") {
        auto metadata = description;
        metadata.unknown_severity = notification_severity_t::info;
        const auto feedback = document_edit_feedback_t::from(rebound, metadata);
        check_feedback(feedback.message_text() == "Binding diagnostic.");
        check_feedback(feedback.severity ==
                       to_notification_severity(severity, notification_severity_t::info));
    }
    SECTION("resource copy and message-only Compose always use fixed error") {
        auto fixed = description;
        fixed.is_message_diagnostic = false;
        const auto feedback = document_edit_feedback_t::from(rebound, fixed);
        check_feedback(feedback.message_text() == "Caller fallback.");
        check_feedback(feedback.severity == notification_severity_t::error);
        check_feedback(feedback.diagnostics == rebound.diagnostics);
    }
    SECTION("Compose can retain diagnostic text with fixed error severity") {
        auto fixed = description;
        fixed.is_severity_diagnostic = false;
        const auto feedback = document_edit_feedback_t::from(rebound, fixed);
        check_feedback(feedback.message_text() == "Binding diagnostic.");
        check_feedback(feedback.severity == notification_severity_t::error);
    }
    SECTION("Compose explicit severity overrides every other policy") {
        auto overridden = description;
        overridden.failure_severity = notification_severity_t::warning;
        const auto feedback = document_edit_feedback_t::from(rebound, overridden);
        check_feedback(feedback.message_text() == "Binding diagnostic.");
        check_feedback(feedback.severity == notification_severity_t::warning);
    }
}

TEST_CASE("Document edit rebinding refreshes history and keeps a committed edit on failure",
          "[document-edit-feedback]") {
    auto history = document_history_t::from(song_document_t{.id = song_id_t{1}});
    const auto binding = workspace_controller_t::from(history);
    require_feedback(binding.value.has_value());
    auto workspace = *binding.value;
    ++history.document.revision;
    const auto document = write_native_project(history.document);
    require_feedback(document.value.has_value());
    const auto feedback = rebind_document_edit(workspace, history, {.success = "Updated."});
    require_feedback(feedback.accepted);
    check_feedback(workspace.document_revision == history.document.revision);
    check_feedback(workspace.history.available);

    workspace.song = song_id_t{2};
    const auto failed = rebind_document_edit(workspace, history, {.rebind_failure = "Failed."});
    check_feedback(!failed.accepted);
    require_feedback(failed.diagnostics.size() == 1);
    check_feedback(failed.diagnostics.front().code == diagnostic_code_t::invalid_id);
    check_feedback(write_native_project(history.document).value == document.value);
    check_feedback(workspace.song == song_id_t{2});
}

TEST_CASE("Document edit feedback preserves success and failure notification sources",
          "[document-edit-feedback]") {
    const auto source = GENERATE(notification_source_t::edit, notification_source_t::import_,
                                 notification_source_t::export_);
    document_edit_description_t description{
        .success = "Updated.", .rebind_failure = "Failed.", .source = source};
    check_feedback(document_edit_feedback_t::from({.value = true}, description).source == source);
    SECTION("metadata and action-only Compose retain their source") {
        check_feedback(document_edit_feedback_t::from({.value = false}, description).source ==
                       source);
    }
    SECTION("resource copy, audio import and default Compose switch to document") {
        description.failure_source = notification_source_t::document;
        check_feedback(document_edit_feedback_t::from({.value = false}, description).source ==
                       notification_source_t::document);
        check_feedback(document_edit_feedback_t::from({.value = true}, description).source ==
                       source);
    }
    SECTION("Compose explicit failure source overrides document source") {
        description.failure_source = notification_source_t::audio;
        check_feedback(document_edit_feedback_t::from({.value = false}, description).source ==
                       notification_source_t::audio);
    }
}
