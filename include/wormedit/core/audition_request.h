#pragma once

#include <wormedit/core/interaction_types.h>
#include <wormedit/core/bmson_sound_channel_editor.h>

#include <variant>

namespace wormedit {

// A validated, revision-bound intent to audition one semantic sound object.
// Runtime audio resolves resources, BMSON slices, and fused layers from this
// identity rather than relying on a lossy resource-only hint.
struct editor_object_audition_request_t {
    std::uint64_t document_revision{};
    chart_id_t chart;
    editor_selection_object_t object;

    auto operator==(const editor_object_audition_request_t&) const -> bool = default;
};

struct bmson_draft_audition_request_t {
    chart_id_t chart;
    std::uint64_t revision{};
    object_id_t reference;
    std::vector<bmson_sound_marker_insertion_t> markers;
    std::size_t marker_index{};
    auto operator==(const bmson_draft_audition_request_t&) const -> bool = default;
};

// Alternative order is also the audition phase order: object, draft, catalog.
using audition_request_t = std::variant<editor_object_audition_request_t,
    bmson_draft_audition_request_t, resource_id_t>;

} // namespace wormedit
