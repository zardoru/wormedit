#pragma once
#include <string_view>
namespace wormedit {
inline constexpr std::string_view bms36_digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
enum class bms36_case_policy_t { uppercase_only, allow_lowercase };
[[nodiscard]] constexpr auto bms36_value(unsigned char byte,
    bms36_case_policy_t policy = bms36_case_policy_t::allow_lowercase) noexcept -> int {
    if (policy == bms36_case_policy_t::allow_lowercase && byte >= 'a' && byte <= 'z')
        byte = static_cast<unsigned char>(byte - ('a' - 'A'));
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'A' && byte <= 'Z') return byte - 'A' + 10;
    return -1;
}
} // namespace wormedit
