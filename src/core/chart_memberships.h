#pragma once
#include <algorithm>
#include <wormedit/core/document.h>
namespace wormedit {
inline void remove_memberships(song_document_t& document, chart_id_t id) {
    for (auto& group : document.output_groups)
        std::erase(group.charts, id);
}
} // namespace wormedit
