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
        return {&document, &chart, {.document = {.project = document.id, .revision = document.revision}, .chart = chart.id}};
    }
};

} // namespace wormedit::sys
