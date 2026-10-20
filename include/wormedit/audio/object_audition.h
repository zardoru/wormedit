#pragma once

#include <wormedit/audio/audition_resolver.h>
#include <wormedit/core/operation_state.h>
#include <wormedit/core/result.h>

namespace wormedit::audio {

// Click previews of stem resources are capped in output-frame space. Shorter
// clips and their source offsets remain unchanged; deliberate transport is separate.
[[nodiscard]] auto is_bound_stem_audition(const song_document_t& song,
    std::optional<resource_id_t> resource, std::span<const audition_source_binding_t> bindings,
    std::span<audition_clip_t> clips, std::span<const hitsound_binding_t> hitsounds = {}) -> bool;

struct object_audition_request_t {
    std::uint64_t document_revision{};
    chart_id_t chart;
    object_id_t object;
    auto operator==(const object_audition_request_t&) const -> bool = default;
};

enum class object_audition_phase_t : std::uint8_t { prepared, started };

struct prepared_object_audition_t {
    object_audition_request_t request;
    std::vector<audition_clip_t> clips;
    auto operator==(const prepared_object_audition_t&) const -> bool = default;
};

struct object_audition_effect_t {
    operation_state_t<object_audition_phase_t> status{failure_code_t::resolve_failed};
    std::optional<prepared_object_audition_t> prepared;
    std::optional<audition_start_outcome_t> transport;
    auto operator==(const object_audition_effect_t&) const -> bool = default;
};
// Rejected starts retain the prepared clips and transport failure details.
using object_audition_outcome_t =
    result_t<object_audition_effect_t,
             diagnosed_t<object_audition_effect_t, std::vector<audition_resolve_diagnostic_t>>>;

// Control-thread coordinator. prepare_object() resolves every layer without touching
// the current transport generation; callers may therefore validate an object
// before an edit removes it, then call start_prepared() only after that edit succeeds.
class object_audition_controller_t {
  public:
    explicit object_audition_controller_t(audition_transport_bridge_t& bridge) : bridge_(bridge) {}

    [[nodiscard]] auto prepare_object(const song_document_t& song,
                                      object_audition_request_t request,
                                      std::span<const audition_source_binding_t> bindings,
                                      audition_resolve_options_t options = {})
        -> object_audition_outcome_t;
    [[nodiscard]] auto start_prepared(prepared_object_audition_t prepared)
        -> object_audition_outcome_t;
    [[nodiscard]] auto start_object(const song_document_t& song, object_audition_request_t request,
                                    std::span<const audition_source_binding_t> bindings,
                                    audition_resolve_options_t options = {})
        -> object_audition_outcome_t;

    [[nodiscard]] auto stop() noexcept -> transport_stop_result_t;

  private:
    audition_transport_bridge_t& bridge_;
};

} // namespace wormedit::audio
