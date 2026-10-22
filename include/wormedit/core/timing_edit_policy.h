#pragma once
namespace wormedit {
enum class timing_position_policy_t { musical, audio };
struct timing_anchoring_t {
    timing_position_policy_t positions{timing_position_policy_t::musical};
    bool allow_quantization{};
    auto operator==(const timing_anchoring_t&) const -> bool = default;
};
}
