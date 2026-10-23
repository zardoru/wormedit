#pragma once
#include <wormedit/core/document.h>
#include <wormedit/core/catalogs.h>
#include <wormedit/core/command.h>

namespace wormedit {
struct timing_category_info_t {
    timing_category_t kind;
    std::string_view label;
    [[nodiscard]] static constexpr auto for_recipe(sv_recipe_output_t output) -> timing_category_t {
        switch (output) {
        case sv_recipe_output_t::sv:
        case sv_recipe_output_t::scrolls: return timing_category_t::scroll;
        case sv_recipe_output_t::speeds: return timing_category_t::speed;
        }
        return timing_category_t::count;
    }
};
inline constexpr std::array timing_categories{
    timing_category_info_t{timing_category_t::bpm, "BPM"},
    timing_category_info_t{timing_category_t::scroll, "SV / scroll"},
    timing_category_info_t{timing_category_t::speed, "SPEEDS"},
    timing_category_info_t{timing_category_t::kiai, "Kiai"},
    timing_category_info_t{timing_category_t::stop, "Stops"},
    timing_category_info_t{timing_category_t::delay, "Delays"},
    timing_category_info_t{timing_category_t::warp, "Warps"},
    timing_category_info_t{timing_category_t::anchor, "Timing anchors"},
    timing_category_info_t{timing_category_t::offset, "Audio offset"},
    timing_category_info_t{timing_category_t::measures, "Measure layout"},
};
static_assert(is_catalog_exhaustive(timing_categories, timing_category_count));

// Exact cumulative musical-beat mapping. Measure identity and bar boundaries may differ.
struct timing_position_map_t {
    measure_index_t source;
    measure_index_t target;
    std::uint64_t source_resolution{};
    std::uint64_t target_resolution{};
    [[nodiscard]] static auto from(const chart_document_t& source, const chart_document_t& target)
        -> result_t<timing_position_map_t, diagnostic_list_t>;
    [[nodiscard]] auto rows(std::uint64_t source_rows) const -> std::optional<std::uint64_t>;
    [[nodiscard]] auto position(local_position_t source_position,
        measure_chart_end_policy_t end = measure_chart_end_policy_t::reject) const -> std::optional<local_position_t>;
    [[nodiscard]] auto span(position_span_t source_span) const -> std::optional<position_span_t>;
};
struct timing_link_limits_t {
    std::size_t maximum_materialized_records{default_maximum_chart_objects};
};
// The publication runner calls this once on its private working document.
[[nodiscard]] auto reconcile_timing_links(const song_document_t& before, song_document_t& proposed,
    timing_link_limits_t limits = {}) -> diagnostic_list_t;
[[nodiscard]] auto stage_timing_link_edit(document_command_transaction_t& transaction, chart_id_t chart,
    const timing_link_request_t& request) -> attempt_t<document_edit_effect_t>;
[[nodiscard]] auto apply_timing_link_edit(document_history_t& history, const chart_stamp_t& stamp,
    const timing_link_request_t& request) -> document_command_result_t;
[[nodiscard]] auto validate_timing_links(const song_document_t& song) -> diagnostic_list_t;
} // namespace wormedit
