#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit {

enum class chart_compare_mode_t {
    baseline_first,
    any_pair,
};

enum class chart_compare_note_part_t {
    point,
    hold_head,
    hold_tail,
};

struct chart_compare_lane_signature_t {
    lane_role_t role{lane_role_t::playable};
    stage_group_t stage{stage_group_t::none};
    std::uint32_t display_order{};

    auto operator==(const chart_compare_lane_signature_t&) const -> bool = default;
};

struct chart_compare_layer_signature_t {
    std::string name;
    color_rgba_t color;

    auto operator==(const chart_compare_layer_signature_t&) const -> bool = default;
};

struct chart_compare_note_signature_t {
    chart_compare_lane_signature_t lane;
    note_kind_t kind{note_kind_t::tap};
    chart_compare_note_part_t part{chart_compare_note_part_t::point};
    std::optional<resource_id_t> sound;
    std::vector<chart_compare_layer_signature_t> semantic_layers;
    std::optional<std::string> label;

    auto operator==(const chart_compare_note_signature_t&) const -> bool = default;
};

struct chart_compare_pane_row_t {
    chart_id_t chart;
    // Empty when the exact musical position is not representable on this
    // chart's authored row grid or lies beyond its geometry.
    std::optional<local_position_t> local_position;
    std::vector<chart_compare_lane_signature_t> active_layout;
    std::vector<chart_compare_note_signature_t> notes;
    bool differs{};
    auto operator==(const chart_compare_pane_row_t&) const -> bool = default;
};

struct chart_compare_difference_row_t {
    [[nodiscard]] static auto from(rational_t position, std::vector<chart_compare_pane_row_t> panes)
        -> chart_compare_difference_row_t;

    // Exact position in standard-measure units, reduced to lowest terms.
    rational_t musical_position;
    std::vector<chart_compare_pane_row_t> panes;
    auto operator==(const chart_compare_difference_row_t&) const -> bool = default;
};

struct chart_compare_request_t {
    std::vector<chart_id_t> charts;
    chart_compare_mode_t mode{chart_compare_mode_t::baseline_first};
    bool compare_layouts{true};
    bool include_sound_assignments{false};
    bool include_semantic_layers{false};
    bool include_labels{false};
    std::size_t maximum_charts{8};
    std::size_t maximum_input_note_endpoints{200'000};
    std::size_t maximum_semantic_layer_assignments{1'000'000};
    std::size_t maximum_signature_text_bytes{64U * 1024U * 1024U};
    std::size_t maximum_layout_segments{16'384};
    std::size_t maximum_lane_definitions{200'000};
    std::size_t maximum_difference_rows{100'000};
};

// Compares selected difficulties in exact musical space. Candidate positions
// are the union of note endpoints and lane-layout transitions. The result only
// contains differing rows and is renderer-neutral; frontends may draw each row
// as a synchronized yellow band across its panes.
struct chart_comparison_t {
    [[nodiscard]] static auto from(const song_document_t& song, const chart_compare_request_t& request)
        -> diagnosed_t<chart_comparison_t>;

    std::uint64_t document_revision{};
    std::vector<chart_id_t> charts;
    std::vector<chart_compare_difference_row_t> differing_rows;
    auto operator==(const chart_comparison_t&) const -> bool = default;
};

} // namespace wormedit
