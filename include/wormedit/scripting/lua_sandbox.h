#pragma once

#include <wormedit/core/result.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>

namespace wormedit::scripting {
struct lua_sandbox_limits_t {
    std::size_t maximum_memory_bytes{8U * 1024U * 1024U};
    std::uint64_t maximum_instructions{1'000'000};
    std::size_t maximum_source_bytes{64U * 1024U};
    std::size_t maximum_result_bytes{64U * 1024U};
    std::uint64_t seed{};
    std::stop_token cancellation;
    auto operator==(const lua_sandbox_limits_t&) const -> bool = default;
};
struct lua_sandbox_usage_t {
    std::size_t allocated_bytes{};
    std::size_t peak_bytes{};
    std::uint64_t remaining_instructions{};
    auto operator==(const lua_sandbox_usage_t&) const -> bool = default;
};
using lua_sandbox_value_t = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

// One isolated, move-only evaluation session. Globals and closures survive
// successful calls; budgets never reset. Any failure closes the Lua state.
// Instruction/cancellation checks cover VM execution and RNG draws, not a
// wall-clock deadline for standard-library C functions. Use ordered iteration
// when reproducibility matters: Lua's next/pairs order is unspecified.
class lua_sandbox_t {
  public:
    [[nodiscard]] static auto from(lua_sandbox_limits_t limits = {})
        -> result_t<lua_sandbox_t, error_t<failure_code_t>>;
    lua_sandbox_t(lua_sandbox_t&&) noexcept;
    auto operator=(lua_sandbox_t&&) noexcept -> lua_sandbox_t&;
    ~lua_sandbox_t();
    // Evaluate bounded text and return its first scalar result (nil if absent).
    // No raw Lua state, host handles or document APIs are exposed. Loading is text-only.
    [[nodiscard]] auto evaluate(std::string_view source)
        -> result_t<lua_sandbox_value_t, error_t<failure_code_t>>;
    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto usage() const noexcept -> lua_sandbox_usage_t;
  private:
    struct implementation_t;
    explicit lua_sandbox_t(std::unique_ptr<implementation_t> implementation);
    std::unique_ptr<implementation_t> implementation_;
};
} // namespace wormedit::scripting
