#pragma once

#include <wormedit/formats/native_project_storage.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {

inline constexpr std::size_t maximum_native_project_recovery_view_rows = 64;
inline constexpr std::size_t maximum_native_project_recovery_view_diagnostics = 64;

struct native_project_recovery_action_target_t {
    std::uint64_t scan_generation{};
    std::size_t row_index{};
    std::size_t candidate_index{};

    auto operator==(const native_project_recovery_action_target_t&) const -> bool = default;
};

struct inspect_native_project_recovery_action_t {
    native_project_recovery_action_target_t target;
};

struct recover_native_project_recovery_action_t {
    native_project_recovery_action_target_t target;
    native_project_recovery_session_expectation_t expected_session;
};

struct save_as_native_project_recovery_action_t {
    native_project_recovery_action_target_t target;
    std::filesystem::path destination;
};

struct discard_native_project_recovery_action_t {
    native_project_recovery_action_target_t target;
};

struct quarantine_native_project_recovery_action_t {
    native_project_recovery_action_target_t target;
};

using native_project_recovery_controller_action_t =
    std::variant<inspect_native_project_recovery_action_t, recover_native_project_recovery_action_t,
                 save_as_native_project_recovery_action_t, discard_native_project_recovery_action_t,
                 quarantine_native_project_recovery_action_t>;

struct native_project_recovery_banner_row_t {
    native_project_recovery_action_target_t target;
    std::string path_label;
    bool path_label_truncated{};
    native_project_recovery_candidate_kind_t kind{
        native_project_recovery_candidate_kind_t::caller_supplied_entry};
    native_project_recovery_candidate_status_t status{
        native_project_recovery_candidate_status_t::missing};
    native_project_recovery_comparison_t comparison{
        native_project_recovery_comparison_t::not_comparable};
    std::optional<native_project_document_version_t> version;
    std::optional<std::chrono::system_clock::time_point> recovery_write_time;
    std::optional<std::uint64_t> recovery_session_sequence;
    std::vector<native_project_recovery_action_t> available_actions;
    std::optional<native_project_recovery_action_t> recommended_action;
    native_project_recovery_quarantine_t quarantine{native_project_recovery_quarantine_t::retain};
};

struct native_project_recovery_controller_options_t {
    std::size_t maximum_candidates{maximum_native_project_recovery_view_rows};
    std::size_t maximum_rows{maximum_native_project_recovery_view_rows};
    std::size_t maximum_diagnostics{32};
    std::size_t maximum_path_label_bytes{512};
};

struct native_project_recovery_controller_view_t {
    std::uint64_t scan_generation{};
    bool banner_visible{};
    bool rows_truncated{};
    bool diagnostics_truncated{};
    std::vector<native_project_recovery_banner_row_t> rows;
    std::optional<std::size_t> preferred_recovery_row;
    std::vector<native_project_storage_diagnostic_t> diagnostics;
};

struct native_project_recovery_controller_dispatch_result_t {
    bool accepted{};
    bool diagnostics_truncated{};
    std::optional<native_project_recovery_action_result_t> action_result;
    std::vector<native_project_storage_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Headless state adapter for recovery UI. It owns one bounded scanner result at
// a time; every row action carries the generation and indices from that view.
class native_project_recovery_controller_t {
  public:
    explicit native_project_recovery_controller_t(
        native_project_recovery_controller_options_t options = {});

    // Replaces the active scan and invalidates every action token issued for
    // the previous scan. The scanner result is moved into controller ownership.
    [[nodiscard]] auto replace_scan(native_project_recovery_scan_result_t scan)
        -> const native_project_recovery_controller_view_t&;

    [[nodiscard]] auto view() const noexcept -> const native_project_recovery_controller_view_t&;

    [[nodiscard]] auto dispatch(const native_project_recovery_controller_action_t& action)
        -> native_project_recovery_controller_dispatch_result_t;

  private:
    native_project_recovery_controller_options_t options_;
    native_project_recovery_controller_view_t view_;
    std::optional<native_project_recovery_scan_result_t> scan_;
    std::uint64_t generation_{};
    bool generation_exhausted_{};
};

} // namespace wormedit
