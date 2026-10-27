#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/bms_native_content.h>
#include <wormedit/core/result.h>
#include <wormedit/core/lane_profiles.h>
#include <array>
#include <span>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace wormedit {

// Authored positions refer to the difficulty's shared measures. A rational measure
// fraction preserves inactive source slots before a projection chooses its row grid.
struct bms_note_attributes_t {
    std::vector<layer_id_t> semantic_layers;
    std::optional<std::string> label;
    std::optional<osu_hitsample_t> osu_hitsample;
    [[nodiscard]] static auto from(const note_t& note) -> bms_note_attributes_t {
        return {.semantic_layers = note.semantic_layers, .label = note.label, .osu_hitsample = note.osu_hitsample};
    }
    [[nodiscard]] auto is_empty() const -> bool {
        return semantic_layers.empty() && !label && !osu_hitsample;
    }
    auto operator==(const bms_note_attributes_t&) const -> bool = default;
};
struct bms_note_content_t {
    bms_program_position_t position;
    lane_id_t lane;
    note_kind_t kind{note_kind_t::tap};
    std::optional<std::string> wav;
    std::optional<unsigned int> mine_value;
    bms_note_attributes_t attributes{};
    auto operator==(const bms_note_content_t&) const -> bool = default;
};
// Endpoints remain separate authored contributions, even if evaluation pairs
// endpoints from different scopes into one playable hold.
struct bms_hold_endpoint_t {
    bms_program_position_t position;
    lane_id_t lane;
    std::string wav;
    bms_note_attributes_t attributes{};
    auto operator==(const bms_hold_endpoint_t&) const -> bool = default;
};
struct bms_bgm_content_t {
    bms_program_position_t position;
    object_id_t column;
    std::optional<std::string> wav;
    std::size_t structural_slot{};
    std::vector<layer_id_t> semantic_layers{};
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
    std::optional<meter_t> meter{};
    auto operator==(const bms_measure_length_t&) const -> bool = default;
};
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
struct bms_metadata_definition_t {
    bms_metadata_field_t field;
    std::string_view command;
    auto operator==(const bms_metadata_definition_t&) const -> bool = default;
    [[nodiscard]] static auto from(std::string_view command) -> const bms_metadata_definition_t*;
};
inline constexpr std::array bms_metadata_definitions{
    bms_metadata_definition_t{bms_metadata_field_t::player, "PLAYER"},
    bms_metadata_definition_t{bms_metadata_field_t::octave_fp, "OCT/FP"},
    bms_metadata_definition_t{bms_metadata_field_t::title, "TITLE"},
    bms_metadata_definition_t{bms_metadata_field_t::subtitle, "SUBTITLE"},
    bms_metadata_definition_t{bms_metadata_field_t::artist, "ARTIST"},
    bms_metadata_definition_t{bms_metadata_field_t::subartist, "SUBARTIST"},
    bms_metadata_definition_t{bms_metadata_field_t::genre, "GENRE"},
    bms_metadata_definition_t{bms_metadata_field_t::playlevel, "PLAYLEVEL"},
    bms_metadata_definition_t{bms_metadata_field_t::rank, "RANK"},
    bms_metadata_definition_t{bms_metadata_field_t::defexrank, "DEFEXRANK"},
    bms_metadata_definition_t{bms_metadata_field_t::total, "TOTAL"},
    bms_metadata_definition_t{bms_metadata_field_t::volwav, "VOLWAV"},
    bms_metadata_definition_t{bms_metadata_field_t::difficulty, "DIFFICULTY"},
    bms_metadata_definition_t{bms_metadata_field_t::comment, "COMMENT"},
    bms_metadata_definition_t{bms_metadata_field_t::url, "URL"},
    bms_metadata_definition_t{bms_metadata_field_t::email, "EMAIL"},
    bms_metadata_definition_t{bms_metadata_field_t::offset, "OFFSET"},
    bms_metadata_definition_t{bms_metadata_field_t::stagefile, "STAGEFILE"},
    bms_metadata_definition_t{bms_metadata_field_t::banner, "BANNER"},
    bms_metadata_definition_t{bms_metadata_field_t::backbmp, "BACKBMP"},
    bms_metadata_definition_t{bms_metadata_field_t::preview, "PREVIEW"}};
struct bms_text_t {
    static constexpr std::size_t maximum_line_bytes = 1024U * 1024U;
    std::string bytes;
    text_encoding_t encoding{text_encoding_t::unknown};
    auto operator==(const bms_text_t&) const -> bool = default;
};
using bms_content_value_t = std::variant<bms_note_content_t, bms_hold_endpoint_t,
    bms_bgm_content_t, bms_bga_content_t, bms_timing_definition_t, bms_timing_reference_t,
    bms_direct_bpm_t, bms_measure_length_t, bms_resource_declaration_t, bms_metadata_declaration_t, bms_native_content_t, bms_text_t>;
struct bms_content_item_t {
    object_id_t id;
    bms_content_value_t value;
    source_provenance_t provenance;
    [[nodiscard]] auto resource_identifier() const -> std::optional<bms_native_resource_reference_t>;
    auto operator==(const bms_content_item_t&) const -> bool = default;
};
struct bms_chart_content_t {
    std::vector<bms_content_item_t> items;
    auto operator==(const bms_chart_content_t&) const -> bool = default;
};
struct bms_bare_scope_t { auto operator==(const bms_bare_scope_t&) const -> bool = default; };
struct bms_root_scope_t { auto operator==(const bms_root_scope_t&) const -> bool = default; };
struct bms_random_scope_t {
    std::int64_t range{1};
    auto operator==(const bms_random_scope_t&) const -> bool = default;
};
struct bms_if_scope_t {
    std::int64_t value{1};
    auto operator==(const bms_if_scope_t&) const -> bool = default;
};
struct bms_else_if_scope_t {
    branch_id_t previous_arm;
    std::int64_t value{};
    auto operator==(const bms_else_if_scope_t&) const -> bool = default;
};
struct bms_else_scope_t {
    branch_id_t previous_arm;
    auto operator==(const bms_else_scope_t&) const -> bool = default;
};
struct bms_switch_scope_t {
    std::int64_t value{};
    auto operator==(const bms_switch_scope_t&) const -> bool = default;
};
struct bms_skip_point_t {
    branch_id_t scope;
    std::optional<branch_id_t> after_child;
    source_provenance_t provenance;
    auto operator==(const bms_skip_point_t&) const -> bool = default;
};
struct bms_case_scope_t {
    // An absent value represents DEF. SKIP stays on the owning case, with an
    // exact lexical anchor (including inside nested scopes), not a scope node.
    std::optional<std::int64_t> value;
    std::vector<bms_skip_point_t> skips;
    auto operator==(const bms_case_scope_t&) const -> bool = default;
};
struct bms_set_random_t {
    std::int64_t value{};
    auto operator==(const bms_set_random_t&) const -> bool = default;
};
struct bms_set_switch_t {
    std::int64_t value{};
    auto operator==(const bms_set_switch_t&) const -> bool = default;
};
using bms_program_node_value_t = std::variant<bms_root_scope_t, bms_random_scope_t,
    bms_if_scope_t, bms_else_if_scope_t, bms_else_scope_t, bms_switch_scope_t,
    bms_case_scope_t, bms_set_random_t, bms_set_switch_t, bms_chart_content_t, bms_text_t, bms_bare_scope_t>;
struct bms_program_node_t {
    branch_id_t id;
    bms_program_node_value_t value;
    // The sole parent/order authority; parent lookup is derived on construction.
    std::vector<branch_id_t> children;
    [[nodiscard]] auto is_scope() const -> bool;
    [[nodiscard]] auto is_if_continuation() const -> bool {
        return std::holds_alternative<bms_else_if_scope_t>(value) || std::holds_alternative<bms_else_scope_t>(value);
    }
    [[nodiscard]] auto is_if_arm() const -> bool {
        return std::holds_alternative<bms_if_scope_t>(value) || is_if_continuation();
    }
    auto operator==(const bms_program_node_t&) const -> bool = default;
};
struct bms_program_limits_t {
    std::size_t maximum_nodes{262'144};
    std::size_t maximum_depth{256};
    std::size_t maximum_content_items{1'000'000};
    std::size_t maximum_text_bytes{16U * 1024U * 1024U};
    auto operator==(const bms_program_limits_t&) const -> bool = default;
};

struct bms_program_move_t {
    branch_id_t node;
    branch_id_t parent;
    std::optional<branch_id_t> after;
    auto operator==(const bms_program_move_t&) const -> bool = default;
};

class bms_program_t;
struct bms_program_subtree_t {
    [[nodiscard]] static auto from(const bms_program_t& program, branch_id_t root)
        -> result_t<bms_program_subtree_t, error_t<failure_code_t>>;
    branch_id_t root;
    std::optional<branch_id_t> parent;
    std::vector<branch_id_t> nodes;
    std::size_t scopes{}, charts{}, texts{}, settings{}, content_items{}, text_bytes{};
    auto operator==(const bms_program_subtree_t&) const -> bool = default;
};

// The immutable program owns authored semantics for a BMS difficulty.
// Musical/resource validation belongs to the projection/edit adapters, not this
// graph constructor. No live document contains a separately writable mirror.
struct bms_program_owner_t;
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
    // Null anchor means the beginning of the parent's ordered children.
    [[nodiscard]] auto with_node(branch_id_t parent, std::optional<branch_id_t> after,
        bms_program_node_t node, std::optional<branch_id_t> first_chart = {},
        bms_program_limits_t limits = {}) const -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto with_text(branch_id_t node, bms_text_t text,
        bms_program_limits_t limits = {}) const -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto with_parameter(branch_id_t node, std::optional<std::int64_t> value,
        bms_program_limits_t limits = {}) const -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto with_random_range(branch_id_t node, std::int64_t range,
        bms_program_limits_t limits = {}) const -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto is_unreachable_random_arm(branch_id_t arm) const -> bool;
    [[nodiscard]] auto unreachable_random_arms(branch_id_t selector) const -> std::vector<branch_id_t>;
    [[nodiscard]] auto with_move(const bms_program_move_t& move, bms_program_limits_t limits = {}) const
        -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto with_unwrap(branch_id_t node, bms_program_limits_t limits = {}) const
        -> result_t<bms_program_t, error_t<failure_code_t>>;
    // Branches whose conditions must be removed as part of this unwrap.
    [[nodiscard]] auto unwrap_conversions(branch_id_t node) const -> std::vector<branch_id_t>;
    [[nodiscard]] auto without_subtree(branch_id_t node, bms_program_limits_t limits = {}) const
        -> result_t<bms_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto root() const -> branch_id_t { return nodes_[root_index_].id; }
    [[nodiscard]] auto nodes() const -> std::span<const bms_program_node_t> { return nodes_; }
    [[nodiscard]] auto find(branch_id_t id) const -> const bms_program_node_t*;
    [[nodiscard]] auto parent(branch_id_t id) const -> std::optional<branch_id_t>;
    [[nodiscard]] auto has_control_flow() const -> bool;
    [[nodiscard]] auto conditional_selector(branch_id_t id) const -> std::optional<branch_id_t>;
    [[nodiscard]] auto hold_endpoints(std::span<const bms_program_owner_t> contributors) const
        -> std::vector<bms_program_owner_t>;
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
// Shared difficulty geometry and catalog read caches accompany the scoped
// semantic statements. The project catalog owns shared resource and asset values.
struct bms_difficulty_settings_t {
    std::optional<std::string> name;
    std::optional<std::string> author;
    std::optional<signed_rational_t> rating;
    metadata_overrides_t overrides;
    std::uint64_t divisions_per_standard_measure{default_divisions_per_standard_measure};
    std::optional<object_id_t> layout_segment;
    timing_links_t timing_links;
    bool show_empty_bgm_columns{};
    auto operator==(const bms_difficulty_settings_t&) const -> bool = default;
};
// Read-only evidence for preserving source tokens after automatic filesystem resolution.
// It never supplies playback paths or authorizes a later semantic asset relink.
struct bms_asset_resolution_t {
    std::filesystem::path authored;
    std::filesystem::path resolved;
    auto operator==(const bms_asset_resolution_t&) const -> bool = default;
};
struct bms_prepared_program_t {
    bms_program_t program;
    std::vector<measure_id_t> measures;
    std::optional<lane_input_profile_t> lane_profile;
    std::vector<bgm_source_lane_t> bgm_columns;
    std::vector<resource_entry_t> resources;
    std::vector<audio_asset_t> audio_assets;
    std::vector<visual_asset_t> visual_assets;
    std::vector<semantic_layer_t> semantic_layers;
    std::vector<sound_layer_default_t> sound_layer_defaults;
    bms_difficulty_settings_t settings;
    encoding_provenance_t source;
    std::vector<bms_asset_resolution_t> resolved_asset_paths;
    // Every nonzero semantic declaration/reference requires an explicit mapping.
    // Lane IDs are layout vocabulary; symbolic BMS slots and provenance stay intact.
    [[nodiscard]] auto with_remapped_ids(const std::unordered_map<std::uint64_t, std::uint64_t>& ids) const
        -> result_t<bms_prepared_program_t, error_t<failure_code_t>>;
    [[nodiscard]] auto with_catalog(const song_document_t& song) const -> bms_prepared_program_t;
    [[nodiscard]] auto with_resolved_catalog(const song_document_t& song) const -> bms_prepared_program_t;
};

struct bms_program_evaluation_t {
    // Evaluation choices only. Scope/Chart edit targeting is separate state.
    std::vector<branch_selection_t> selections;
    [[nodiscard]] auto without_invalid_choices(const bms_program_t& program) const -> bms_program_evaluation_t;
    auto operator==(const bms_program_evaluation_t&) const -> bool = default;
};
struct bms_program_projection_limits_t {
    std::size_t maximum_nodes{262'144};
    std::size_t maximum_depth{256};
    std::size_t maximum_input_items{1'000'000};
    std::size_t maximum_active_items{1'000'000};
    std::size_t maximum_diagnostics{256};
    std::uint64_t maximum_resolution{1'000'000'000};
    auto operator==(const bms_program_projection_limits_t&) const -> bool = default;
};
struct bms_program_owner_t {
    branch_id_t scope;
    branch_id_t chart;
    object_id_t item;
    auto operator==(const bms_program_owner_t&) const -> bool = default;
};
struct bms_program_projection_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    failure_code_t code{failure_code_t::invalid_data};
    std::string message;
    branch_id_t node;
    object_id_t item;
    [[nodiscard]] auto is_error() const -> bool { return severity == diagnostic_severity_t::error; }
    auto operator==(const bms_program_projection_diagnostic_t&) const -> bool = default;
};
using bms_program_projection_diagnostics_t = std::vector<bms_program_projection_diagnostic_t>;

// Detached derived cache, with const-only access. Never a second writable
// semantic owner or an implicitly published replacement for the live chart.
class bms_program_projection_t {
  public:
    [[nodiscard]] static auto from(const bms_prepared_program_t& prepared,
        const bms_program_evaluation_t& evaluation = {}, bms_program_projection_limits_t limits = {})
        -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t>;
    [[nodiscard]] static auto for_authoring(const bms_prepared_program_t& prepared, branch_id_t chart,
        const bms_program_evaluation_t& witness = {},
        bms_program_projection_limits_t limits = {})
        -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t>;
    [[nodiscard]] auto song() const -> const song_document_t& { return song_; }
    [[nodiscard]] auto active_charts() const -> std::span<const branch_id_t> { return active_charts_; }
    [[nodiscard]] auto selections() const -> std::span<const branch_selection_t> { return selections_; }
    // Prepared geometry, assets, nodes and content have disjoint nonzero identities.
    // Objects, resources and measure geometry use their stable ID value here.
    // A hold carries both endpoint owners; referenced declarations also contribute.
    [[nodiscard]] auto owners(std::uint64_t id) const -> std::span<const bms_program_owner_t>;
    [[nodiscard]] auto resolve_owner(std::uint64_t id, bms_program_owner_t expected) const
        -> result_t<bms_program_owner_t, error_t<failure_code_t>>;
  private:
    bms_program_projection_t() = default;
    [[nodiscard]] static auto build(const bms_prepared_program_t& prepared,
        const bms_program_evaluation_t& evaluation, std::optional<branch_id_t> authoring_chart,
        bms_program_projection_limits_t limits)
        -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t>;
    song_document_t song_;
    std::vector<branch_id_t> active_charts_;
    std::vector<branch_selection_t> selections_;
    std::unordered_map<std::uint64_t, std::vector<bms_program_owner_t>> owners_;
};
} // namespace wormedit
