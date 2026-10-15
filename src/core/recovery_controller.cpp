#include "text_util.h"
#include <wormedit/core/recovery_controller.h>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>

namespace wormedit {
namespace {

constexpr std::size_t maximum_storage_diagnostic_message_bytes = 240;
constexpr std::size_t maximum_storage_diagnostic_path_bytes = 4096;

struct encoded_path_t {
    std::string text;
    bool truncated{};
};

auto encode_path(const std::filesystem::path& path, std::size_t maximum_bytes) -> encoded_path_t {
    const auto encoded = path.generic_u8string();
    std::string text;
    text.reserve(encoded.size());
    for (const auto byte : encoded) {
        text.push_back(static_cast<char>(byte));
    }
    if (text.size() <= maximum_bytes) {
        return {.text = std::move(text)};
    }

    constexpr std::string_view suffix{"..."};
    auto keep = maximum_bytes > suffix.size() ? maximum_bytes - suffix.size() : std::size_t{};
    // Do not leave a partial UTF-8 code point at the end of a clipped label.
    keep = text_util::truncate_utf8(text, keep).size();
    text.resize(keep);
    text.append(suffix.substr(0, maximum_bytes - keep));
    return {.text = std::move(text), .truncated = true};
}

auto bounded_diagnostic(const native_project_storage_diagnostic_t& diagnostic,
                        std::size_t maximum_path_bytes, bool& truncated)
    -> native_project_storage_diagnostic_t {
    auto result = diagnostic;
    if (result.message.size() > maximum_storage_diagnostic_message_bytes) {
        truncated = true;
        result.message.resize(maximum_storage_diagnostic_message_bytes - 3);
        result.message += "...";
    }
    const auto path = encode_path(result.path, maximum_path_bytes);
    if (path.truncated) {
        truncated = true;
        result.path.clear();
    }
    return result;
}

auto controller_diagnostic(native_project_storage_diagnostic_code_t code, std::string message)
    -> native_project_storage_diagnostic_t {
    if (message.size() > maximum_storage_diagnostic_message_bytes) {
        message.resize(maximum_storage_diagnostic_message_bytes - 3);
        message += "...";
    }
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .path = {}};
}

void append_bounded_diagnostic(std::vector<native_project_storage_diagnostic_t>& diagnostics,
                               bool& truncated, std::size_t maximum_diagnostics,
                               const native_project_storage_diagnostic_t& diagnostic,
                               std::size_t maximum_path_bytes) {
    if (diagnostics.size() >= maximum_diagnostics) {
        truncated = true;
        return;
    }
    auto diagnostic_truncated = false;
    diagnostics.push_back(bounded_diagnostic(diagnostic, maximum_path_bytes, diagnostic_truncated));
    truncated = truncated || diagnostic_truncated;
}

auto recovery_action_kind(const native_project_recovery_target_action_t& request)
    -> native_project_recovery_action_t {
    switch (request.kind) {
    case native_project_recovery_target_kind_t::inspect:
        return native_project_recovery_action_t::inspect_read_only;
    case native_project_recovery_target_kind_t::discard:
        return native_project_recovery_action_t::discard;
    case native_project_recovery_target_kind_t::quarantine:
        return native_project_recovery_action_t::quarantine;
    }
    return native_project_recovery_action_t::quarantine;
}
auto recovery_action_kind(const recover_native_project_recovery_action_t&)
    -> native_project_recovery_action_t {
    return native_project_recovery_action_t::recover_into_session;
}
auto recovery_action_kind(const save_as_native_project_recovery_action_t&)
    -> native_project_recovery_action_t {
    return native_project_recovery_action_t::save_as_new_project;
}
struct recovery_action_kind_visitor_t {
    template <class Action>
    auto operator()(const Action& action) const -> native_project_recovery_action_t {
        return recovery_action_kind(action);
    }
};
auto action_kind(const native_project_recovery_controller_action_t& action)
    -> native_project_recovery_action_t {
    return std::visit(recovery_action_kind_visitor_t{}, action);
}

auto action_target(const native_project_recovery_controller_action_t& action)
    -> native_project_recovery_action_target_t {
    return std::visit([](const auto& request) { return request.target; }, action);
}

auto bounded_actions(std::span<const native_project_recovery_action_t> actions)
    -> std::vector<native_project_recovery_action_t> {
    constexpr std::array known_actions{
        native_project_recovery_action_t::inspect_read_only,
        native_project_recovery_action_t::recover_into_session,
        native_project_recovery_action_t::save_as_new_project,
        native_project_recovery_action_t::discard,
        native_project_recovery_action_t::quarantine,
    };
    std::vector<native_project_recovery_action_t> result;
    result.reserve(std::min(actions.size(), known_actions.size()));
    if (actions.size() > known_actions.size()) {
        return result;
    }
    for (const auto action : known_actions) {
        if (std::ranges::find(actions, action) != actions.end()) {
            result.push_back(action);
        }
    }
    return result;
}

} // namespace

native_project_recovery_controller_t::native_project_recovery_controller_t(
    native_project_recovery_controller_options_t options)
    : options_(options) {
    options_.maximum_candidates =
        std::min(options_.maximum_candidates, maximum_native_project_recovery_view_rows);
    options_.maximum_rows =
        std::min(options_.maximum_rows, maximum_native_project_recovery_view_rows);
    options_.maximum_diagnostics = std::clamp(options_.maximum_diagnostics, std::size_t{1},
                                              maximum_native_project_recovery_view_diagnostics);
    options_.maximum_path_label_bytes = std::clamp(
        options_.maximum_path_label_bytes, std::size_t{3}, maximum_storage_diagnostic_path_bytes);
}

auto native_project_recovery_controller_t::replace_scan(native_project_recovery_scan_result_t scan)
    -> const native_project_recovery_controller_view_t& {
    view_ = {};
    scan_.reset();
    if (generation_exhausted_ || generation_ == std::numeric_limits<std::uint64_t>::max()) {
        generation_exhausted_ = true;
        view_.scan_generation = generation_;
        const auto diagnostic = controller_diagnostic(
            native_project_storage_diagnostic_code_t::resource_limit,
            "recovery scan generation is exhausted; no scan can be installed");
        append_bounded_diagnostic(view_.diagnostics, view_.diagnostics_truncated,
                                  options_.maximum_diagnostics, diagnostic,
                                  options_.maximum_path_label_bytes);
        return view_;
    }

    ++generation_;
    view_.scan_generation = generation_;
    if (scan.candidates.size() > options_.maximum_candidates) {
        const auto diagnostic =
            controller_diagnostic(native_project_storage_diagnostic_code_t::resource_limit,
                                  "recovery scan exceeds the controller candidate limit");
        append_bounded_diagnostic(view_.diagnostics, view_.diagnostics_truncated,
                                  options_.maximum_diagnostics, diagnostic,
                                  options_.maximum_path_label_bytes);
        return view_;
    }

    const auto diagnostic_count = std::min(scan.diagnostics.size(), options_.maximum_diagnostics);
    for (std::size_t index = 0; index < diagnostic_count; ++index) {
        append_bounded_diagnostic(view_.diagnostics, view_.diagnostics_truncated,
                                  options_.maximum_diagnostics, scan.diagnostics[index],
                                  options_.maximum_path_label_bytes);
    }
    view_.diagnostics_truncated =
        view_.diagnostics_truncated || scan.diagnostics.size() > diagnostic_count;
    view_.banner_visible = std::ranges::any_of(scan.candidates, [](const auto& candidate) {
        return candidate.kind != native_project_recovery_candidate_kind_t::primary &&
               !bounded_actions(candidate.available_actions).empty();
    });
    const auto row_count = std::min(scan.candidates.size(), options_.maximum_rows);
    view_.rows.reserve(row_count);
    for (std::size_t candidate_index = 0; candidate_index < row_count; ++candidate_index) {
        const auto& candidate = scan.candidates[candidate_index];
        const auto row_index = view_.rows.size();
        const auto path_label = encode_path(candidate.path, options_.maximum_path_label_bytes);
        native_project_recovery_banner_row_t row{
            .target = {.scan_generation = generation_,
                       .row_index = row_index,
                       .candidate_index = candidate_index},
            .path_label = path_label.text,
            .path_label_truncated = path_label.truncated,
            .kind = candidate.kind,
            .status = candidate.status,
            .comparison = candidate.comparison,
            .version = candidate.version,
            .available_actions = bounded_actions(candidate.available_actions),
            .recommended_action = candidate.recommended_action,
            .quarantine = candidate.quarantine,
        };
        if (candidate.recovery_metadata.has_value()) {
            row.recovery_write_time = candidate.recovery_metadata->utc_write_time;
            row.recovery_session_sequence = candidate.recovery_metadata->session_sequence;
            row.last_explicit_save_revision =
                candidate.recovery_metadata->last_explicit_save_revision;
        }
        view_.rows.push_back(std::move(row));
        if (scan.preferred_recovery_candidate == candidate_index) {
            view_.preferred_recovery_row = row_index;
        }
    }
    if (row_count < scan.candidates.size()) {
        view_.rows_truncated = true;
        const auto diagnostic = controller_diagnostic(
            native_project_storage_diagnostic_code_t::resource_limit,
            "recovery banner rows were truncated at the configured view limit");
        auto warning = diagnostic;
        warning.severity = diagnostic_severity_t::warning;
        append_bounded_diagnostic(view_.diagnostics, view_.diagnostics_truncated,
                                  options_.maximum_diagnostics, warning,
                                  options_.maximum_path_label_bytes);
    }

    std::vector<native_project_storage_diagnostic_t>{}.swap(scan.diagnostics);
    for (auto& candidate : scan.candidates) {
        auto actions = bounded_actions(candidate.available_actions);
        candidate.available_actions.swap(actions);
        candidate.document.reset();
        candidate.recovery_metadata.reset();
        if (candidate.scanned_sha256.size() != 64) {
            std::string{}.swap(candidate.scanned_sha256);
        }
    }
    scan_ = std::move(scan);
    return view_;
}

auto native_project_recovery_controller_t::view() const noexcept
    -> const native_project_recovery_controller_view_t& {
    return view_;
}

void native_project_recovery_controller_t::append_dispatch_error(
    native_project_recovery_controller_dispatch_outcome_t& result,
    native_project_storage_diagnostic_code_t code, std::string message) {
    const auto diagnostic = controller_diagnostic(code, std::move(message));
    append_bounded_diagnostic(result.diagnostics, result.value.diagnostics_truncated,
                              options_.maximum_diagnostics, diagnostic,
                              options_.maximum_path_label_bytes);
    append_bounded_diagnostic(view_.diagnostics, view_.diagnostics_truncated,
                              options_.maximum_diagnostics, diagnostic,
                              options_.maximum_path_label_bytes);
}

auto native_project_recovery_controller_t::dispatch_action(
    const native_project_recovery_controller_action_t& action)
    -> native_project_recovery_controller_dispatch_outcome_t {
    native_project_recovery_controller_dispatch_outcome_t result{};

    if (!scan_.has_value() || generation_exhausted_) {
        append_dispatch_error(result,
                              native_project_storage_diagnostic_code_t::recovery_action_unavailable,
                              "there is no active recovery scan");
        return result;
    }
    const auto target = action_target(action);
    if (target.scan_generation != view_.scan_generation) {
        append_dispatch_error(result,
                              native_project_storage_diagnostic_code_t::recovery_action_unavailable,
                              "recovery action belongs to a stale scan generation");
        return result;
    }
    if (target.row_index >= view_.rows.size()) {
        append_dispatch_error(result,
                              native_project_storage_diagnostic_code_t::recovery_action_unavailable,
                              "recovery action row is outside the displayed view");
        return result;
    }
    const auto& row = view_.rows[target.row_index];
    if (row.target != target || target.candidate_index >= scan_->candidates.size()) {
        append_dispatch_error(result,
                              native_project_storage_diagnostic_code_t::recovery_identity_mismatch,
                              "recovery action target does not match its displayed row");
        return result;
    }

    const auto requested_action = action_kind(action);
    if (std::ranges::find(row.available_actions, requested_action) == row.available_actions.end()) {
        append_dispatch_error(result,
                              native_project_storage_diagnostic_code_t::recovery_action_unavailable,
                              "requested action is not offered for this recovery row");
        return result;
    }

    native_project_recovery_action_options_t options;
    if (const auto* recover = std::get_if<recover_native_project_recovery_action_t>(&action)) {
        if (!recover->expected_session.is_set) {
            append_dispatch_error(
                result, native_project_storage_diagnostic_code_t::recovery_identity_mismatch,
                "recover action must include an expected session identity or empty-session guard");
            return result;
        }
        options.expected_session = recover->expected_session;
    } else if (const auto* save_as =
                   std::get_if<save_as_native_project_recovery_action_t>(&action)) {
        options.save_as_destination = save_as->destination;
    }

    result.value.accepted = true;
    result.value.action_result = apply_native_project_recovery_action(
        *scan_, target.candidate_index, requested_action, std::move(options));
    for (auto& diagnostic : result.value.action_result->diagnostics) {
        auto diagnostic_truncated = false;
        diagnostic =
            bounded_diagnostic(diagnostic, options_.maximum_path_label_bytes, diagnostic_truncated);
        result.value.diagnostics_truncated =
            result.value.diagnostics_truncated || diagnostic_truncated;
        view_.diagnostics_truncated = view_.diagnostics_truncated || diagnostic_truncated;
        append_bounded_diagnostic(result.diagnostics, result.value.diagnostics_truncated,
                                  options_.maximum_diagnostics, diagnostic,
                                  options_.maximum_path_label_bytes);
        append_bounded_diagnostic(view_.diagnostics, view_.diagnostics_truncated,
                                  options_.maximum_diagnostics, diagnostic,
                                  options_.maximum_path_label_bytes);
    }
    if (result.value.action_result->applied &&
        (requested_action == native_project_recovery_action_t::discard ||
         requested_action == native_project_recovery_action_t::quarantine)) {
        scan_.reset();
        view_.banner_visible = false;
        view_.rows.clear();
        view_.preferred_recovery_row.reset();
        if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
            generation_exhausted_ = true;
        } else {
            ++generation_;
        }
        view_.scan_generation = generation_;
    }
    return result;
}

} // namespace wormedit
