#pragma once

#include <wormedit/core/result.h>
#include <wormedit/core/storyboard_vocabulary.h>
#include <wormedit/core/lua_source.h>
#include <wormedit/core/storyboard.h>

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <span>
#include <vector>

namespace wormedit {

// Numeric values retain their authored arity so malformed descriptors can be
// diagnosed before any evaluator consumes them. Times are signed integer ms.
template <class Value = std::vector<double>> struct osbx_key_t {
    std::string id;
    std::int64_t time_ms{};
    Value value{};
    osbx_easing_t easing{osbx_linear_easing_t{}};
    auto operator==(const osbx_key_t&) const -> bool = default;
};

template<class Value = std::vector<double>> struct osbx_loop_t {
    std::string id;
    std::int64_t start_ms{};
    std::uint32_t count{1};
    std::vector<osbx_key_t<Value>> keys;
    [[nodiscard]] auto iteration_start(std::uint32_t iteration) const -> result_t<std::int64_t, failure_code_t>;
    [[nodiscard]] auto end_time() const -> result_t<std::int64_t, failure_code_t> { return iteration_start(count); }
    auto operator==(const osbx_loop_t&) const -> bool = default;
};
template<class Value = std::vector<double>> struct osbx_trigger_t {
    std::string id;
    storyboard_trigger_t trigger;
    std::vector<osbx_key_t<Value>> keys;
    auto operator==(const osbx_trigger_t&) const -> bool = default;
};
template<class Value = std::vector<double>> struct osbx_key_program_t {
    std::variant<osbx_loop_t<Value>, osbx_trigger_t<Value>> data;
    [[nodiscard]] auto id() const -> const std::string& { return std::visit([](const auto& v) -> const std::string& { return v.id; }, data); }
    [[nodiscard]] auto id() -> std::string& { return std::visit([](auto& v) -> std::string& { return v.id; }, data); }
    [[nodiscard]] auto keys() const -> std::span<const osbx_key_t<Value>> {
        return std::visit([](const auto& v) -> std::span<const osbx_key_t<Value>> { return v.keys; }, data);
    }
    [[nodiscard]] auto keys() -> std::span<osbx_key_t<Value>> {
        return std::visit([](auto& v) -> std::span<osbx_key_t<Value>> { return v.keys; }, data);
    }
    auto operator==(const osbx_key_program_t&) const -> bool = default;
};

enum class osbx_lua_sampling_mode_t { adaptive, fixed };
struct osbx_lua_sampling_t {
    osbx_lua_sampling_mode_t mode{osbx_lua_sampling_mode_t::adaptive};
    double tolerance{0.002};
    std::uint32_t max_points{256};
    std::uint32_t fixed_point_count{2};
    auto operator==(const osbx_lua_sampling_t&) const -> bool = default;
};
struct osbx_lua_link_t {
    std::string id;
    lua_source_descriptor_t source;
    auto operator==(const osbx_lua_link_t&) const -> bool = default;
};
struct osbx_lua_track_link_t {
    osbx_lua_link_t link;
    // Inclusive integer-ms endpoints. Outside this range authored keys apply.
    std::int64_t begin_ms{}, end_ms{1000};
    osbx_lua_sampling_t sampling;
    [[nodiscard]] auto contains(std::int64_t time_ms) const -> bool {
        return time_ms >= begin_ms && time_ms <= end_ms;
    }
    auto operator==(const osbx_lua_track_link_t&) const -> bool = default;
};

template <class Value = std::vector<double>> struct osbx_track_t {
    Value default_value{};
    std::vector<osbx_key_t<Value>> keys;
    std::optional<osbx_lua_track_link_t> lua;
    // Clip-template numeric tracks only. Replaces the default, never authored keys.
    std::optional<std::string> parameter;
    // Root keys are fallback before any relative key starts. Contributions
    // compete by most recent absolute segment/key start; programs win root
    // ties and later authored programs win program ties. A final key is held.
    // Within one trigger block the latest already-started direct firing restarts
    // its relative keys; there is no implicit gameplay/name-family inference.
    std::vector<osbx_key_program_t<Value>> program;
    template<class Visit> void visit_authored_ids(Visit visit) { visit_ids(*this, visit); }
    template<class Visit> void visit_authored_ids(Visit visit) const { visit_ids(*this, visit); }
    [[nodiscard]] auto can_edit_key_at(std::int64_t time_ms) const -> bool {
        return !lua || !lua->contains(time_ms);
    }
    auto operator==(const osbx_track_t&) const -> bool = default;
  private:
    // Self preserves only the cv qualification of this same track type.
    template<class Self, class Visit> static void visit_ids(Self& self, Visit& visit) {
        for (auto& key : self.keys) visit(key.id);
        for (auto& block : self.program) {
            visit(block.id());
            for (auto& key : block.keys()) visit(key.id);
        }
    }

};

struct osbx_transform_op_t {
    std::string id;
    osbx_transform_kind_t kind{osbx_transform_kind_t::translate};
    // translate/scale/skew/pivot: 2; rotate: 1; matrix: 6 [a,b,c,d,tx,ty].
    // Rotation and skew use clockwise degrees in the Y-down canvas.
    osbx_track_t<> track{.default_value = {0, 0}};
    std::vector<osbx_transform_op_t> children; // Only pivot owns nested ops.
    auto operator==(const osbx_transform_op_t&) const -> bool = default;
};

struct osbx_tracks_t {
    osbx_track_t<> opacity{.default_value = {1}};
    osbx_track_t<> color{.default_value = {1, 1, 1, 1}}; // Linear RGBA multipliers.
    osbx_track_t<bool> visibility{.default_value = true};
    osbx_track_t<osbx_blend_t> blend{.default_value = osbx_blend_t::normal};
    auto operator==(const osbx_tracks_t&) const -> bool = default;
};

struct osbx_provenance_t {
    std::string file;
    std::size_t line{};
    std::string generator_key;
    auto operator==(const osbx_provenance_t&) const -> bool = default;
};

struct osbx_clip_parameter_t {
    std::string name;
    std::vector<double> default_value;
    auto operator==(const osbx_clip_parameter_t&) const -> bool = default;
};
struct osbx_clip_binding_t {
    std::string name;
    std::vector<double> value;
    auto operator==(const osbx_clip_binding_t&) const -> bool = default;
};
struct osbx_clip_instance_t {
    std::string id, clip;
    std::int64_t start_ms{};
    double time_scale{1}; // Exact stored binary64 ratio, not an accumulated delta.
    std::vector<osbx_clip_binding_t> parameters;
    auto operator==(const osbx_clip_instance_t&) const -> bool = default;
};
struct osbx_clip_tracks_t {
    osbx_track_t<> opacity{.default_value = {1}};
    osbx_track_t<> color{.default_value = {1, 1, 1, 1}};
    osbx_track_t<bool> visibility{.default_value = true};
    // Absence is distinct from an explicit normal override.
    std::optional<osbx_track_t<osbx_blend_t>> blend;
    auto operator==(const osbx_clip_tracks_t&) const -> bool = default;
};
struct osbx_clip_t {
    std::string id;
    std::vector<osbx_transform_op_t> ops;
    osbx_clip_tracks_t tracks;
    std::vector<osbx_clip_parameter_t> parameters;
    std::vector<osbx_clip_instance_t> uses;
    auto operator==(const osbx_clip_t&) const -> bool = default;
};

struct osbx_sample_t {
    std::int64_t time_ms{};
    std::string path;
    std::optional<std::uint8_t> volume;
    [[nodiscard]] auto is_valid(std::size_t maximum = storyboard_default_maximum_path_bytes) const noexcept -> bool {
        return storyboard_sample_t::is_valid_payload(path, volume, maximum);
    }
    [[nodiscard]] auto effective_volume() const noexcept -> std::uint8_t {
        return storyboard_sample_t::effective_volume(volume);
    }
    auto operator==(const osbx_sample_t&) const -> bool = default;
};

struct osbx_node_t {
    std::string id;
    osbx_node_kind_t kind{osbx_node_kind_t::group};
    std::optional<std::string> parent;
    osbx_layer_t layer{osbx_layer_t::background};
    int order{};
    std::optional<std::string> image; // Sprite or Animation base-template image.
    std::string origin{"Centre"}; // Existing OSB origin spellings.
    std::vector<osbx_transform_op_t> ops;
    osbx_tracks_t tracks;
    osbx_provenance_t provenance;
    std::optional<osbx_lua_link_t> children_link;
    std::optional<osbx_lifetime_t> lifetime; // Absent is unbounded; inherited through parents.
    std::vector<osbx_clip_instance_t> uses;
    // Absence preserves incomplete legacy placeholders; enum alone never activates media.
    std::optional<storyboard_animation_t> animation;
    std::optional<osbx_sample_t> sample;
    auto operator==(const osbx_node_t&) const -> bool = default;
};

struct osbx_image_t {
    std::string id;
    std::string file; // Project-relative, no filesystem access during validation.
    auto operator==(const osbx_image_t&) const -> bool = default;
};
struct osbx_canvas_t {
    double width{640};
    double height{480};
    auto operator==(const osbx_canvas_t&) const -> bool = default;
};
struct osbx_document_t {
    std::string api_version{"0.1"};
    osbx_canvas_t canvas;
    std::vector<osbx_image_t> images;
    std::vector<osbx_node_t> nodes;
    std::vector<osbx_clip_t> clips;
    [[nodiscard]] static auto is_valid_project_path(std::string_view) -> bool;
    auto operator==(const osbx_document_t&) const -> bool = default;
};

struct osbx_validation_options_t {
    std::size_t maximum_nodes{250'000};
    std::size_t maximum_images{250'000};
    std::size_t maximum_ops{250'000};
    std::size_t maximum_keys{500'000};
    std::size_t maximum_nesting{128}; // Root ops have depth 1; every nested pivot adds 1.
    std::size_t maximum_id_bytes{1024};
    std::size_t maximum_path_bytes{4 * 1024};
    std::size_t maximum_expanded_layers{250'000};
    std::size_t maximum_expanded_storage_bytes{64 * 1024 * 1024};
};

enum class osbx_diagnostic_code_t {
    invalid_version, invalid_canvas, invalid_id, duplicate_id, invalid_path,
    missing_parent, missing_image, parent_cycle, cross_layer_parent,
    invalid_image_reference, invalid_value_shape, non_finite_value,
    invalid_value_range, invalid_easing, unordered_keys, invalid_children,
    invalid_enum, invalid_origin, nesting_limit, node_limit, image_limit,
    op_limit, key_limit, invalid_lua_link, unprepared_lua_link,
    invalid_clip, clip_cycle, clip_expansion_limit,
};
struct osbx_diagnostic_location_t {
    std::string id;
    osbx_provenance_t provenance;
    std::string link_id; // Worker link identity, independent of diagnostic text/node.
    auto operator==(const osbx_diagnostic_location_t&) const -> bool = default;
};
using osbx_diagnostic_t = format_diagnostic_t<osbx_diagnostic_code_t, osbx_diagnostic_location_t>;

// Derived compile data only; never serialized. Exact maps which cannot fit the
// bounded shared fractional clock are refused, never rounded into integer keys.
struct osbx_clip_clock_t {
    storyboard_time_t origin;
    std::uint64_t numerator{1}, denominator{1};
    [[nodiscard]] static auto is_before(storyboard_time_t a, storyboard_time_t b) -> bool {
        return storyboard_time_t::is_before(a, b);
    }
    [[nodiscard]] auto map(std::int64_t) const -> result_t<storyboard_time_t, failure_code_t>;
    [[nodiscard]] auto unmap(storyboard_time_t) const -> result_t<storyboard_time_t, failure_code_t>;
    [[nodiscard]] auto compose(const osbx_clip_instance_t&) const
        -> result_t<osbx_clip_clock_t, failure_code_t>;
};
struct osbx_compiled_clip_t {
    std::size_t node_index{};
    storyboard_time_t begin;
    osbx_clip_clock_t clock;
    std::vector<osbx_transform_op_t> ops;
    osbx_clip_tracks_t tracks;
};
struct osbx_clip_expansion_cost_t {
    std::size_t ops{}, keys{}, layers{}, storage{};
    // Pure bounded preflight for prepared joins and optional subtree copies.
    // additional_nodes are charged through the SAME clip planner before cloning.
    [[nodiscard]] static auto from(const osbx_document_t&, osbx_validation_options_t = {},
        std::span<const std::string_view> additional_nodes = {})
        -> attempt_t<osbx_clip_expansion_cost_t, std::vector<osbx_diagnostic_t>>;
};

// The same bounded planner is used by validation and index construction. The
// source must already have passed structural validation before compiling.
[[nodiscard]] auto compile_osbx_clips(const osbx_document_t&, osbx_validation_options_t = {})
    -> attempt_t<std::vector<osbx_compiled_clip_t>, std::vector<osbx_diagnostic_t>>;

// Validation is structural, deterministic and read-only. Singular finite affine
// matrices are valid; consumers must handle noninvertibility safely.
[[nodiscard]] auto validate_osbx_document(const osbx_document_t& document,
                                         osbx_validation_options_t options = {})
    -> diagnosed_t<bool, std::vector<osbx_diagnostic_t>>;

} // namespace wormedit
