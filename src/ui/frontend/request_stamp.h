#pragma once
#include "targeted_action.h"
#include <wormedit/core/document_command_types.h>
#include <wormedit/core/vocabulary.h>
#include <cstdint>

namespace wormedit::ui {
/* The one chart, project and document revision a request was built against. Every request that
   must not act on a different document state carries this stamp (workspace_action_t::stamp) and is
   validated through it, so a dispatcher never rebuilds the project or revision from live state. */
struct request_stamp_t {
    chart_id_t chart_id;
    song_id_t project_id;
    uint64_t document_revision;

    [[nodiscard]] static auto from(const worm_ui_compose_target_t& target) -> request_stamp_t {
        return {chart_id_t{target.chart}, song_id_t{target.project}, target.revision};
    }
    /* Any view that carries project_id, chart_id and document_revision. */
    template <class View> [[nodiscard]] static auto of(const View& view) -> request_stamp_t {
        return {chart_id_t{view.chart_id.value}, song_id_t{view.project_id.value}, view.document_revision};
    }
    [[nodiscard]] auto chart_stamp() const -> chart_stamp_t {
        return {.document = {.project = project_id, .revision = document_revision}, .chart = chart_id};
    }
    /* Project, chart and revision all still describe `document` with `selected` open. */
    [[nodiscard]] auto is_current(const song_document_t& document, chart_id_t selected) const -> bool {
        return chart_stamp().is_current(document, selected);
    }
    /* Project and revision only, for requests that are not about one chart. */
    [[nodiscard]] auto is_document_current(const song_document_t& document) const -> bool {
        return chart_stamp().document.is_current(document);
    }
};
} // namespace wormedit::ui
