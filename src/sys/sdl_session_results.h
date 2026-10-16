#pragma once
#include <wormedit/core/operation_state.h>

#include <atomic>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace wormedit::sys {

using sdl_callback_failure_t = std::optional<failure_code_t>;
static_assert(std::is_trivially_copyable_v<sdl_callback_failure_t>);
static_assert(std::atomic<sdl_callback_failure_t>::is_always_lock_free);

template <class Session> struct sdl_session_start_result_t {
    std::optional<error_t<failure_code_t>> error{error_t{failure_code_t::unavailable, {}}};
    std::optional<Session> session;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error;
    }
    [[nodiscard]] auto has_failure(failure_code_t code) const noexcept -> bool {
        return error && error->code == code;
    }
    [[nodiscard]] auto message() const noexcept -> std::string_view {
        return error ? std::string_view{error->message} : std::string_view{};
    }
};

static_assert(std::is_trivially_copyable_v<operation_state_t<std::monostate>>);
static_assert(sizeof(operation_state_t<std::monostate>) <= sizeof(sdl_callback_failure_t));

template <class Pump, class Telemetry = std::monostate> struct sdl_session_service_result_t {
    operation_state_t<std::monostate> status;
    std::optional<Pump> pump;
    [[no_unique_address]] Telemetry telemetry{};
};

// Temporary: removed by T-633.
template <class Pump, class Telemetry = std::monostate>
using sdl_session_service_outcome_t = result_t<sdl_session_service_result_t<Pump, Telemetry>,
                                               sdl_session_service_result_t<Pump, Telemetry>>;

} // namespace wormedit::sys
