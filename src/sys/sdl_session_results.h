#pragma once
#include <wormedit/core/result.h>

#include <optional>
#include <string>
#include <variant>

namespace wormedit::sys {

enum class sdl_callback_failure_t {
    none,
    render,
    device,
};

enum class sdl_session_start_status_t {
    started,
    unavailable,
    session_failed,
    device_failed,
};

template <class Session> struct sdl_session_start_result_t {
    sdl_session_start_status_t status{sdl_session_start_status_t::unavailable};
    std::optional<Session> session;
    std::string message;
    [[nodiscard]] explicit operator bool() const noexcept {
        return status == sdl_session_start_status_t::started;
    }
};

enum class sdl_session_service_status_t {
    ready,
    pump_failed,
    render_failed,
    device_failed,
};

template <class Pump, class Telemetry = std::monostate> struct sdl_session_service_result_t {
    sdl_session_service_status_t status{sdl_session_service_status_t::ready};
    std::optional<Pump> pump;
    [[no_unique_address]] Telemetry telemetry{};
};

// Temporary: removed by T-633.
template <class Pump, class Telemetry = std::monostate>
using sdl_session_service_outcome_t =
    result_t<sdl_session_service_result_t<Pump, Telemetry>,
             sdl_session_service_result_t<Pump, Telemetry>>;

} // namespace wormedit::sys
