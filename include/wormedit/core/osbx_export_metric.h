#pragma once

#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/osbx_osb_export.h>

namespace wormedit {

struct osbx_export_budget_t {
    [[nodiscard]] static auto from(const osbx_evaluation_index_t&, std::size_t image_count,
        std::size_t maximum_work) -> osbx_export_budget_t;
    [[nodiscard]] auto consume(std::size_t amount) -> bool;
    std::size_t remaining_work{};
    std::size_t frame_work{};
    std::size_t maximum_instances{250'000};
    std::size_t maximum_frame_bytes{16U * 1024U * 1024U};
};

// Numeric instances are copied out of the arena. Their text views still borrow
// the immutable source/prepared document, which must outlive this value. Resetting
// the frame arena does not invalidate the copied matrix/color/opacity values.
struct osbx_export_frame_t {
    storyboard_time_t time;
    std::vector<osbx_draw_instance_t> instances;
    [[nodiscard]] static auto from(const osbx_evaluation_index_t&, storyboard_time_t,
        frame_arena_t&, osbx_export_budget_t&, osbx_frame_options_t = {})
        -> result_t<osbx_export_frame_t, std::string>;
};

// Match the authored file before the legacy single-scene unscoped fallback.
[[nodiscard]] auto osbx_export_image_size(const osbx_document_t&, std::string_view image_id,
    std::span<const osbx_osb_image_size_t>) -> const osbx_osb_image_size_t*;

// The existing OSB bake metric is measured on its contained 640x480 canvas.
// Both OSB and native adapters share this map before normalized comparison.
[[nodiscard]] auto osbx_export_canvas_transform(const osbx_canvas_t&) -> visual_affine_t;

// Origin-relative logical image corners; works for singular and reflected maps.
// request's destination/logical canvas determine the metric's pixel space.
[[nodiscard]] auto osbx_export_corner_error(visual_image_quad_request_t,
    const visual_affine_t& expected, const visual_affine_t& actual) -> double;

// Full affine corners / 0.5px, final clamped opacity and RGB / (1/255).
// Missing identity/image/origin/blend, changed order/visibility or nonfinite
// values return infinity. No dense-sample result claims unsampled extrema.
[[nodiscard]] auto osbx_export_instance_error(const osbx_draw_instance_t& expected,
    const osbx_draw_instance_t& actual, visual_image_quad_request_t) -> double;
[[nodiscard]] auto osbx_export_frame_error(const osbx_export_frame_t& expected,
    const osbx_export_frame_t& actual, const osbx_document_t&,
    std::span<const osbx_osb_image_size_t>) -> double;

// Exporter supplies references into one detached candidate document, avoiding
// a second node/op lookup vocabulary. Source/candidate must outlive this call.
struct osbx_export_bake_request_t {
    const osbx_document_t& source;
    osbx_document_t& candidate;
    const osbx_track_t<>& source_track;
    osbx_track_t<>& candidate_track;
    std::span<const osbx_osb_image_size_t> image_sizes;
    osbx_frame_options_t frame_options;
    osbx_osb_span_t playback_span;
    std::string node_id;
};
struct osbx_export_track_bake_t {
    osbx_track_t<> track;
    osbx_osb_report_t report;
    // Integer-ms emission keys, round-to-even at 9 decimals once; shared RDP
    // density/work limits. Full scene verification probes every quarter ms.
    // This is a bounded sampled-domain certificate, not a continuous-time proof.
    // The candidate is restored on both returned success and failure.
    [[nodiscard]] static auto from(osbx_export_bake_request_t, osbx_export_budget_t&)
        -> result_t<osbx_export_track_bake_t, std::string>;
};

} // namespace wormedit
