#pragma once

#include <ui/frontend.h>
#include <wormedit/core/document_command_types.h>

namespace wormedit::sys {
/* Validation of the stamp a UI request was built against (see ui::request_stamp_t). */
[[nodiscard]] inline auto chart_stamp_of(const ui::request_stamp_t& stamp) -> chart_stamp_t {
    return {.document = {.project = stamp.project_id, .revision = stamp.document_revision},
            .chart = stamp.chart_id};
}
/* Project, chart and revision all still describe `document` with `selected` open. */
[[nodiscard]] inline auto request_stamp_current(const ui::request_stamp_t& stamp,
                                                const song_document_t& document, chart_id_t selected) -> bool {
    return chart_stamp_of(stamp).is_current(document, selected);
}
/* Project and revision only, for requests that are not about one chart. */
[[nodiscard]] inline auto request_stamp_document_current(const ui::request_stamp_t& stamp,
                                                         const song_document_t& document) -> bool {
    return chart_stamp_of(stamp).document.is_current(document);
}
} // namespace wormedit::sys
