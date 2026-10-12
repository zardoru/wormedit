#pragma once
namespace wormedit {
struct rect_t {
    double left{};
    double top{};
    double right{};
    double bottom{};
    auto operator==(const rect_t&) const -> bool = default;
};
} // namespace wormedit
