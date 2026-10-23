#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>
#include <span>
#include <unordered_map>
#include <variant>

namespace wormedit {

// Authored positions refer to the difficulty's shared measures. A rational measure
// fraction preserves inactive source slots before a projection chooses its row grid.
struct bms_program_position_t {
    measure_id_t measure;
    rational_t fraction;
    auto operator==(const bms_program_position_t&) const -> bool = default;
};
struct bms_note_content_t {
    bms_program_position_t position;
    lane_id_t lane;
    note_kind_t kind{note_kind_t::tap};
    std::optional<std::string> wav;
    std::optional<unsigned int> mine_value;
    auto operator==(const bms_note_content_t&) const -> bool = default;
};
// Endpoints remain separate authored contributions, even if evaluation pairs
// endpoints from different scopes into one playable hold.
struct bms_hold_endpoint_t {
    bms_program_position_t position;
    lane_id_t lane;
    std::string wav;
    auto operator==(const bms_hold_endpoint_t&) const -> bool = default;
};
struct bms_bgm_content_t {
    bms_program_position_t position;
    object_id_t column;
    std::optional<std::string> wav;
    std::size_t structural_slot{};
    auto operator==(const bms_bgm_content_t&) const -> bool = default;
};
struct bms_bga_content_t {
    bms_program_position_t position;
    visual_event_kind_t kind{visual_event_kind_t::background};
    std::string bmp;
    auto operator==(const bms_bga_content_t&) const -> bool = default;
};
enum class bms_timing_definition_kind_t { bpm, stop, scroll };
struct bms_timing_definition_t {
    bms_timing_definition_kind_t kind{bms_timing_definition_kind_t::bpm};
    // No identifier is the initial BPM declaration, not an extended reference.
    std::optional<std::string> identifier;
    signed_rational_t value;
    auto operator==(const bms_timing_definition_t&) const -> bool = default;
};
struct bms_timing_reference_t {
    bms_program_position_t position;
    bms_timing_definition_kind_t kind{bms_timing_definition_kind_t::bpm};
    std::string identifier;
    auto operator==(const bms_timing_reference_t&) const -> bool = default;
};
struct bms_direct_bpm_t {
    bms_program_position_t position;
    signed_rational_t value;
    auto operator==(const bms_direct_bpm_t&) const -> bool = default;
};
struct bms_measure_length_t {
    measure_id_t measure;
    rational_t length;
    auto operator==(const bms_measure_length_t&) const -> bool = default;
};
enum class bms_resource_namespace_t { wav, bmp };
struct bms_resource_declaration_t {
    bms_resource_namespace_t name_space{bms_resource_namespace_t::wav};
    std::string identifier;
    resource_id_t resource;
    auto operator==(const bms_resource_declaration_t&) const -> bool = default;
};
enum class bms_metadata_field_t {
    player, octave_fp, title, subtitle, artist, subartist, genre, playlevel,
    rank, defexrank, total, volwav, difficulty, comment, url, email,
    offset, stagefile, banner, backbmp, preview
};
struct bms_metadata_declaration_t {
    bms_metadata_field_t field{bms_metadata_field_t::title};
    std::variant<std::string, signed_rational_t> value;
    auto operator==(const bms_metadata_declaration_t&) const -> bool = default;
};
using bms_content_value_t = std::variant<bms_note_content_t, bms_hold_endpoint_t,
    bms_bgm_content_t, bms_bga_content_t, bms_timing_definition_t, bms_timing_reference_t,
    bms_direct_bpm_t, bms_measure_length_t, bms_resource_declaration_t, bms_metadata_declaration_t>;
struct bms_content_item_t {
    object_id_t id;
    bms_content_value_t value;
    source_provenance_t provenance;
    auto operator==(const bms_content_item_t&) const -> bool = default;
};
struct bms_chart_content_t {
    std::vector<bms_content_item_t> items;
    auto operator==(const bms_chart_content_t&) const -> bool = default;
};
struct bms_text_t {
    std::string bytes;
    text_encoding_t encoding{text_encoding_t::unknown};
    auto operator==(const bms_text_t&) const -> bool = default;
};
struct bms_root_scope_t { auto operator==(const bms_root_scope_t&) const -> bool = default; };
struct bms_random_scope_t {
    std::int64_t range{1};
    auto operator==(const bms_random_scope_t&) const -> bool = default;
};
struct bms_if_scope_t {
    std::int64_t value{1};
    auto operator==(const bms_if_scope_t&) const -> bool = default;
};
using bms_program_node_value_t = std::variant<bms_root_scope_t, bms_random_scope_t,
    bms_if_scope_t, bms_chart_content_t, bms_text_t>;
struct bms_program_node_t {
    branch_id_t id;
    bms_program_node_value_t value;
    // The sole parent/order authority; parent lookup is derived on construction.
    std::vector<branch_id_t> children;
    [[nodiscard]] auto is_scope() const -> bool;
    auto operator==(const bms_program_node_t&) const -> bool = default;
};
struct bms_program_limits_t {
    std::size_t maximum_nodes{262'144};
    std::size_t maximum_depth{256};
    std::size_t maximum_content_items{1'000'000};
    std::size_t maximum_text_bytes{16U * 1024U * 1024U};
};

// Prepared value model only: T-744 activates it as the document's semantic owner.
// Musical/resource validation belongs to the projection/edit adapters, not this
// graph constructor. No live document contains a separately writable mirror.
class bms_program_t {
  public:
    [[nodiscard]] static auto from(std::vector<bms_program_node_t> nodes,
        bms_program_limits_t limits = {}) -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] static auto from_root(branch_id_t root, branch_id_t chart,
        bms_program_limits_t limits = {}) -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto with_random(branch_id_t parent, branch_id_t scope, branch_id_t chart,
        std::int64_t range, bms_program_limits_t limits = {}) const
        -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto with_if(branch_id_t parent, branch_id_t scope, branch_id_t chart,
        std::int64_t value, bms_program_limits_t limits = {}) const
        -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto root() const -> branch_id_t { return nodes_[root_index_].id; }
    [[nodiscard]] auto nodes() const -> std::span<const bms_program_node_t> { return nodes_; }
    [[nodiscard]] auto find(branch_id_t id) const -> const bms_program_node_t*;
    [[nodiscard]] auto parent(branch_id_t id) const -> std::optional<branch_id_t>;
    [[nodiscard]] auto target_chart(branch_id_t id) const -> std::optional<branch_id_t>;
    auto operator==(const bms_program_t&) const -> bool = default;
  private:
    bms_program_t() = default;
    [[nodiscard]] auto with_scope(branch_id_t parent, bms_program_node_t scope,
        branch_id_t chart, bms_program_limits_t limits) const
        -> result_t<bms_program_t, error_t<failure_code_t>>;
    std::vector<bms_program_node_t> nodes_;
    std::unordered_map<std::uint64_t, std::size_t> indices_;
    std::unordered_map<std::uint64_t, branch_id_t> parents_;
    std::size_t root_index_{};
};
} // namespace wormedit
