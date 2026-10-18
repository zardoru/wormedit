#pragma once

#include "id_ceiling.h"

namespace wormedit {

struct object_id_allocation_t {
    std::optional<object_id_t> id;
    bool over_limit{};
    bool exhausted{};
};

inline auto next_chart_object_id(const chart_document_t& chart,
    std::size_t limit = std::numeric_limits<std::size_t>::max()) -> object_id_allocation_t {
    const auto scan = id_ceiling_t<object_id_t>::from(chart, limit);
    if (scan.is_at_capacity()) return {.over_limit = true};
    const auto id = scan.next();
    return {.id = id, .exhausted = !id};
}

inline auto chart_object_id_count(const chart_document_t& chart, object_id_t sought,
    std::size_t limit = std::numeric_limits<std::size_t>::max()) -> std::size_t {
    return id_counter_t<object_id_t>::from(chart, sought, limit).count;
}

} // namespace wormedit
