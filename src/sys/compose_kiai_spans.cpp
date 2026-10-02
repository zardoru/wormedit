#include "sys/compose_kiai_spans.h"

namespace wormedit::sys {

auto project_kiai_spans(const chart_document_t& chart, const measure_index_t& measures,
                        std::size_t limit) -> kiai_projection_t {
    kiai_projection_t result;
    result.spans = kiai_spans_of(chart, measures);
    if (result.spans.size() > limit) {
        result.spans.resize(limit);
        result.truncated = true;
    }
    return result;
}

} // namespace wormedit::sys
