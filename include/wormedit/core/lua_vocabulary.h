#pragma once

#include <wormedit/core/vocabulary.h>
#include <string>

namespace wormedit {
struct lua_parameter_t {
    std::string name;
    signed_rational_t value;
    auto operator==(const lua_parameter_t&) const -> bool = default;
};

} // namespace wormedit
