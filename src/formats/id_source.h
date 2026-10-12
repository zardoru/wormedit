#pragma once
#include <cstdint>
namespace wormedit::format_detail {
struct id_source_t {
    std::uint64_t next{1};
    template <typename Id> auto take() -> Id {
        return Id{next++};
    }
};
} // namespace wormedit::format_detail
