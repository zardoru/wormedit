#include <wormedit/core/lua_source.h>
#include <algorithm>

namespace wormedit {
auto lua_source_descriptor_t::is_valid_identifier(std::string_view name) -> bool {
    const auto initial = [](char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
    };
    return !name.empty() && name.size() <= 128 && initial(name.front()) &&
        std::ranges::all_of(name, [&](char byte) {
            return initial(byte) || (byte >= '0' && byte <= '9');
        });
}

} // namespace wormedit
