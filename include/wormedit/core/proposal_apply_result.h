#pragma once
#include <wormedit/core/command.h>
namespace wormedit {
template <class Issue> struct proposal_apply_payload_t {
    Issue issue{Issue::none};
    std::string message;
    [[nodiscard]] auto has_errors() const noexcept -> bool { return !is_success(); }
    [[nodiscard]] auto is_success() const noexcept -> bool { return issue == Issue::none; }
};
template <class Issue>
using proposal_apply_result_t = command_result_with_t<proposal_apply_payload_t<Issue>>;
} // namespace wormedit
