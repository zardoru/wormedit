#include "sys/compose_kiai_spans.h"

namespace wormedit::sys {

auto kiai_spans_view_t::from(const chart_document_t& chart, const measure_index_t& measures,
                        std::size_t limit) -> kiai_spans_view_t {
    kiai_spans_view_t result;
    result.spans = kiai_spans_of(chart, measures);
    if (result.spans.size() > limit) {
        result.spans.resize(limit);
        result.truncated = true;
    }
    return result;
}

} // namespace wormedit::sys
