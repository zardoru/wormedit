#pragma once

#include <wormedit/core/id.h>
#include <vector>

namespace wormedit {
// Prepared storage never authorizes arbitrary writes to a detached chart projection.
// Each scope names the authoritative adapter that may publish the mutation.
enum class chart_edit_storage_support_t {
    legacy_only,
    objects,
    layer_settings,
    layer_settings_and_objects,
    geometry,
};

struct chart_edit_mutation_t {
    chart_edit_storage_support_t storage{chart_edit_storage_support_t::legacy_only};
    // Existing identities only. For settings+objects the domain fills these while
    // planning its settings edit; the runner then replays the edit for those owners.
    std::vector<object_id_t> objects;
};
} // namespace wormedit
