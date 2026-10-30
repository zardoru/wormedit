#pragma once

#include <wormedit/core/id.h>
#include <vector>

namespace wormedit {
// Prepared storage never authorizes arbitrary writes to a detached chart projection.
// Each scope names the authoritative adapter that may publish the mutation.
enum class chart_edit_storage_support_t {
    legacy_only,
    objects,
    layer_settings_and_objects,
};

struct chart_edit_mutation_t {
    chart_edit_storage_support_t storage{chart_edit_storage_support_t::legacy_only};
    // Object-only callbacks read the transaction's current document; the native
    // object adapter owns target resolution. Combined callbacks borrow the exact
    // prepared owner's resolved catalog across both staging passes.
    // Existing identities only. Combined layer/object edits must be deterministic:
    // the first invocation discovers identities while staging settings; the second
    // replays the same edit to publish only those objects. Each invocation may
    // change only its supplied chart and identity output, without consuming
    // captured values or depending on external side effects.
    std::vector<object_id_t> objects;
};
} // namespace wormedit
