#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/keyed_cache.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::sys {

inline constexpr std::size_t quantize_report_row_capacity = 256;

struct quantize_report_row_t {
    object_id_t object;
    lane_id_t lane;
    std::uint64_t start_before_absolute_row{};
    std::uint64_t start_after_absolute_row{};
    std::optional<std::uint64_t> end_before_absolute_row;
    std::optional<std::uint64_t> end_after_absolute_row;
    local_position_t start_before;
    local_position_t start_after;
    std::optional<local_position_t> end_before;
    std::optional<local_position_t> end_after;
    bool moved{};
    bool collision{};
    std::optional<object_id_t> collision_peer;
    bool selected{};
    bool halfway{};
    bool head_halfway{};
    bool tail_halfway{};
    bool stationary_tail_annotation{};
    bool bgm{};
};

// Dry-run result for a selected-note quantization request. Report rows contain every moved or
// colliding note up to the fixed capacity; aggregate counts always cover the full selection.
struct quantize_preview_t {
    std::size_t selected{};
    std::size_t moved{};
    std::size_t unchanged{};
    std::size_t halfway_notes{};
    std::size_t holds{};
    // Collisions or other rejections that make the real command fail; Apply stays disabled.
    std::size_t blocked{};
    std::size_t colliding_objects{};
    std::size_t report_total_rows{};
    bool report_truncated{};
    // Retained rows use original chart units before and promoted units after.
    std::uint64_t source_resolution{};
    std::uint64_t display_resolution{};
    std::uint64_t resolution_scale{1};
    bool geometry_valid{};
    bool drawable{};
    std::vector<quantize_report_row_t> report_rows;
    std::string message;
};

[[nodiscard]] auto preview_quantize_selection(const chart_document_t& chart,
                                              const std::vector<object_id_t>& selected,
                                              std::uint64_t snap_denominator) -> quantize_preview_t;

[[nodiscard]] auto preview_quantize_selection(const chart_document_t& chart,
                                              const std::vector<object_id_t>& selected,
                                              quantize_request_t request,
                                              const measure_index_t* measures = nullptr) -> quantize_preview_t;

// Recomputes when chart source identity, revision, selection or request settings change.
// The caller retains the chart and advances revision for mutations.
class quantize_preview_cache_t {
public:
    [[nodiscard]] auto get(const chart_document_t& chart, std::uint64_t revision,
                           const std::vector<object_id_t>& selected,
                           std::uint64_t snap_denominator) -> const quantize_preview_t&;
    [[nodiscard]] auto get(const chart_document_t& chart, std::uint64_t revision,
                           const std::vector<object_id_t>& selected,
                           const quantize_request_t& request,
                           const measure_index_t* measures = nullptr) -> const quantize_preview_t&;

private:
    struct query_t {
        const chart_document_t* source;
        chart_id_t chart;
        std::uint64_t revision;
        const std::vector<object_id_t>& selected;
        const quantize_request_t& request;
    };
    struct key_t {
        const chart_document_t* source{};
        chart_id_t chart;
        std::uint64_t revision{};
        std::vector<object_id_t> selected;
        quantize_request_t request;

        static auto from(const query_t& query) -> key_t;
        void assign(const query_t& query);
        auto operator==(const query_t& query) const -> bool;
        auto operator==(const key_t& key) const -> bool;
    };
    keyed_cache_t<key_t, quantize_preview_t> value_;
};

} // namespace wormedit::sys
