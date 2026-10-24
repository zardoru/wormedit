#pragma once

#include <wormedit/core/command.h>

namespace wormedit::sys {

struct compose_scene_source_key_t {
    const void* document_identity{};
    const void* chart_identity{};
    wormedit::chart_stamp_t stamp;
    auto operator==(const compose_scene_source_key_t&) const -> bool = default;

    static auto from(const wormedit::song_document_t& document,
                     const wormedit::chart_document_t& chart) -> compose_scene_source_key_t {
        auto stamp = chart_stamp_t{.document = {.project = document.id, .revision = document.revision}, .chart = chart.id};
        if (chart.bms_authoring_chart)
            stamp.document.bms_target = {.difficulty = chart.id, .chart = *chart.bms_authoring_chart};
        return {.document_identity = &document, .chart_identity = &chart, .stamp = stamp};
    }
};

} // namespace wormedit::sys
