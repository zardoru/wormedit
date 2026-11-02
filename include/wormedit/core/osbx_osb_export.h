#pragma once

#include <wormedit/core/osbx_document.h>
#include <wormedit/core/storyboard.h>

#include <span>

namespace wormedit {

using osbx_osb_diagnostic_t = path_diagnostic_t<failure_code_t, failure_code_t::unsupported>;

enum class osbx_osb_class_t { exact, baked, unsupported };
struct osbx_osb_report_t {
    std::string node_id;
    osbx_osb_class_t classification{osbx_osb_class_t::unsupported};
    std::string reason;
    double worst_error{}; // Maximum normalized corner/opacity/color error; accepted <= 1.
    std::size_t key_count{};
    std::size_t keys_before_simplify{};
    [[nodiscard]] auto mark_unsupported(std::string message) -> osbx_osb_diagnostic_t;
};
struct osbx_osb_image_size_t {
    std::string image_id;
    std::uint32_t width{};
    std::uint32_t height{};
    // Local IDs may repeat across scenes. Prepared package geometry is scoped
    // by the authored file; an empty file retains single-scene caller support.
    std::string file;
};
struct osbx_osb_span_t {
    std::int64_t begin_time_ms{};
    std::int64_t end_time_ms{};
};
struct osbx_osb_options_t {
    // Caller supplies decoded, validated dimensions; core never accesses files.
    std::span<const osbx_osb_image_size_t> image_sizes;
    // Extend coverage to this finite playback domain as well as all authored
    // key times. Supply the song/export span for static scenes and clamped tails;
    // absent context uses the authored span, or time zero for a keyless scene.
    std::optional<osbx_osb_span_t> playback_span;
    std::size_t maximum_samples_per_sprite{65'536};
    std::size_t maximum_evaluation_work{8'000'000};
    // Borrowed sealed input; nullptr differs from an intentional empty scenario.
    const storyboard_trigger_scenario_t* scenario{};
};
struct osbx_osb_export_t {
    std::optional<storyboard_document_model_t> model;
    std::vector<osbx_osb_report_t> report;
};
using osbx_osb_outcome_t = diagnosed_t<osbx_osb_export_t, std::vector<osbx_osb_diagnostic_t>>;

// File-free, bounded authored-scene preflight only. Reports contain proven
// unsupported reasons; an omitted node is UNKNOWN, never verified exportable.
// Invalid sources, unresolved Lua chains and exhausted work remain unknown.
// This does not evaluate tracks, bake, expand Lua or inspect image files.
[[nodiscard]] auto osbx_osb_known_unsupported(const osbx_document_t&,
                                            std::size_t maximum_work = 1'000'000)
    -> std::vector<osbx_osb_report_t>;

// Deterministic, all-or-nothing, file-free conversion. Verification covers every
// integer millisecond in bounded animated spans (the evaluator's clock domain),
// including times off the reduced key grid. It is not a continuous-time proof.
// Time-independent scenes need only the playback-domain endpoints.
[[nodiscard]] auto convert_osbx_to_osb(const osbx_document_t&, osbx_osb_options_t)
    -> osbx_osb_outcome_t;

} // namespace wormedit
