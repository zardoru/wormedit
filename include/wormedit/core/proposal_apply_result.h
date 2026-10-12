#pragma once
#include <wormedit/core/command.h>
namespace wormedit {
template <class Issue> struct proposal_apply_result_t {
    Issue issue{Issue::none};
    std::string message;
    document_command_result_t command;
    [[nodiscard]] explicit operator bool() const noexcept {
        return issue == Issue::none && static_cast<bool>(command);
    }
};
} // namespace wormedit
