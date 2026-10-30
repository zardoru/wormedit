#pragma once
#include "ui/frontend/bga_usage.h"

namespace wormedit { struct song_document_t; }

namespace wormedit::sys {
struct bga_usage_view_t {
    [[nodiscard]] static auto from(const song_document_t& document, resource_id_t resource)
        -> ui::bga_usage_view_t;
};
} // namespace wormedit::sys
