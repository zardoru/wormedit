#pragma once
#include <cstddef>
#include <string_view>
namespace wormedit {
// Two base-36 digits: slots 00..ZZ. Slot 00 is reserved, so 1..bms_max_slot are usable identifiers.
inline constexpr std::size_t bms_slot_count = 36 * 36;
inline constexpr std::size_t bms_max_slot = bms_slot_count - 1;
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
