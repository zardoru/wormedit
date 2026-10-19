#pragma once

#include <wormedit/core/result.h>

#include <concepts>
#include <type_traits>
#include <variant>

namespace wormedit {

// A domain phase or a shared failure, with no separate acceptance flag.
// Storage size follows std::variant's implementation-defined discriminator layout.
template <class Phase>
    requires((std::is_enum_v<Phase> || std::same_as<Phase, std::monostate>) &&
             !std::same_as<Phase, failure_code_t>)
class operation_state_t {
  public:
    constexpr operation_state_t() noexcept = default;
    constexpr operation_state_t(Phase phase) noexcept : value_(phase) {}
    constexpr operation_state_t(failure_code_t failure) noexcept : value_(failure) {}

    [[nodiscard]] constexpr auto has_failure() const noexcept -> bool {
        return std::holds_alternative<failure_code_t>(value_);
    }
    [[nodiscard]] constexpr auto has_failure(failure_code_t code) const noexcept -> bool {
        const auto* current = std::get_if<failure_code_t>(&value_);
        return current != nullptr && *current == code;
    }
    [[nodiscard]] constexpr auto is_in_phase(Phase phase) const noexcept -> bool {
        const auto* current = std::get_if<Phase>(&value_);
        return current != nullptr && *current == phase;
    }
    [[nodiscard]] constexpr auto value() const noexcept
        -> const std::variant<Phase, failure_code_t>& {
        return value_;
    }
    auto operator==(const operation_state_t&) const -> bool = default;

  private:
    std::variant<Phase, failure_code_t> value_{Phase{}};
};

} // namespace wormedit
