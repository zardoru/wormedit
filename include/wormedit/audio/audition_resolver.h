#pragma once

#include <wormedit/audio/audition_bridge.h>
#include <wormedit/core/sound_slice.h>

#include <span>
#include <vector>

namespace wormedit::audio {

struct audition_source_binding_t {
    asset_id_t asset;
    audio_source_identity_t source;
    std::uint32_t source_sample_rate{};
    std::optional<std::uint64_t> source_frame_count;
    std::uint64_t output_frame_count{};
};

struct audition_resolve_options_t {
    std::size_t maximum_channel_events{200'000};
    std::size_t maximum_layers{64};
    sample_rounding_t rounding{sample_rounding_t::nearest_ties_to_even};
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

struct audition_resolve_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    audition_resolve_code_t code{audition_resolve_code_t::missing_object};
    std::string message;
    std::optional<object_id_t> object;
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
[[nodiscard]] auto resolve_object_audition(
    const song_document_t& song, chart_id_t chart, object_id_t object,
    std::span<const audition_source_binding_t> bindings,
    audition_resolve_options_t options = {}) -> audition_resolve_result_t;

} // namespace wormedit::audio
