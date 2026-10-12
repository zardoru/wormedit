#pragma once
#include <optional>
#include <string>
namespace wormedit::sys {
template <class Status, class Session> struct sdl_session_start_result_t {
    Status status{Status::unavailable};
    std::optional<Session> session;
    std::string message;
    [[nodiscard]] explicit operator bool() const noexcept {
        return status == Status::started;
    }
};
} // namespace wormedit::sys
