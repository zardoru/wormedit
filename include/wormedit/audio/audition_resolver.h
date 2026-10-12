#pragma once

#include <wormedit/audio/audition_bridge.h>
#include <wormedit/audio/preview_bridge.h>
#include <wormedit/audio/source_registry.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/sound_slice.h>

#include <span>
#include <vector>

namespace wormedit::audio {

using audition_source_binding_t = decoded_source_binding_t;

struct audition_resolve_options_t {
    std::size_t maximum_channel_events{200'000};
    std::size_t maximum_layers{64};
    std::size_t maximum_objects{200'000};
    std::size_t maximum_total_clips{200'000};
    sample_rounding_t rounding{sample_rounding_t::nearest_ties_to_even};
    bool skip_unavailable_preview_sources{};
};

enum class audition_resolve_code_t {
    missing_chart,
    missing_object,
    invalid_link,
    missing_resource,
    missing_source_binding,
    duplicate_source_binding,
    invalid_timing,
    invalid_source_geometry,
    empty_range,
    capacity_exceeded,
};

using audition_resolve_diagnostic_t =
    object_diagnostic_t<audition_resolve_code_t, audition_resolve_code_t::missing_object>;

struct preview_bindings_resolve_result_t {
    std::uint64_t document_revision{};
    chart_id_t chart;
    std::vector<preview_object_source_binding_t> bindings;
    std::vector<audition_resolve_diagnostic_t> diagnostics;
    std::size_t skipped_unavailable_objects{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

struct audition_resolve_result_t {
    std::uint64_t document_revision{};
    chart_id_t chart;
    object_id_t object;
    std::vector<audition_clip_t> clips;
    std::vector<audition_resolve_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !clips.empty() && diagnostics.empty();
    }
};

// Resolves a semantic note or BGM object into transport-ready one-shot clips.
// Linked BMSON playable notes expand identical fused note geometry across
// distinct source channels; a BGM object resolves only its own retained event.
[[nodiscard]] auto resolve_object_audition(const song_document_t& song, chart_id_t chart,
                                           object_id_t object,
                                           std::span<const audition_source_binding_t> bindings,
                                           audition_resolve_options_t options = {})
    -> audition_resolve_result_t;

// Auditions one uncommitted BMSON marker region against a transient chart
// containing the complete pending batch. The caller's document is unchanged.
struct bmson_draft_audition_request_t {
    std::uint64_t expected_revision{};
    chart_id_t chart;
    object_id_t reference_bgm_event;
    std::span<const bmson_sound_marker_insertion_t> markers;
    std::size_t marker_index{};
};

[[nodiscard]] auto resolve_bmson_draft_audition(
    const song_document_t& song, bmson_draft_audition_request_t request,
    std::span<const audition_source_binding_t> bindings,
    audition_resolve_options_t options = {}) -> audition_resolve_result_t;

// Resolves all playable/BGM objects referenced by an immutable preview
// schedule. Document, event, channel-slice, asset, and runtime-source indexes
// are built once for the whole batch.
[[nodiscard]] auto resolve_preview_bindings(const song_document_t& song, chart_id_t chart,
                                            const preview_schedule_t& schedule,
                                            std::span<const audition_source_binding_t> bindings,
                                            audition_resolve_options_t options = {})
    -> preview_bindings_resolve_result_t;

} // namespace wormedit::audio
