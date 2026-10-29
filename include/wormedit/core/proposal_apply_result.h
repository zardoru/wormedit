#pragma once
#include <string>
namespace wormedit {
template <class Issue> struct proposal_apply_payload_t {
    Issue issue{Issue::none};
    std::string message;
    [[nodiscard]] auto has_errors() const noexcept -> bool { return !is_success(); }
    [[nodiscard]] auto is_success() const noexcept -> bool { return issue == Issue::none; }
};
} // namespace wormedit
