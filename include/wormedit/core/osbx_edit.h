#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/storyboard_vocabulary.h>
#include <wormedit/core/osbx_document.h>
#include <wormedit/core/interaction_types.h>
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/resource_assignment.h>
#include <set>
#include <tuple>
#include <variant>

namespace wormedit {

struct osbx_create_storyboard_t {
    std::string name;
    auto operator==(const osbx_create_storyboard_t&) const -> bool = default;
};
struct osbx_delete_storyboard_t {
    auto operator==(const osbx_delete_storyboard_t&) const -> bool = default;
};
struct osbx_rename_storyboard_t {
    std::string name;
    auto operator==(const osbx_rename_storyboard_t&) const -> bool = default;
};
struct osbx_add_node_t {
    osbx_node_kind_t kind{osbx_node_kind_t::group};
    std::optional<std::string> parent;
    osbx_layer_t layer{osbx_layer_t::background};
    std::optional<std::string> image;
    auto operator==(const osbx_add_node_t&) const -> bool = default;
};
// Deletion always removes the complete subtree, including its transform/key IDs.
struct osbx_delete_node_t {
    std::string node;
    auto operator==(const osbx_delete_node_t&) const -> bool = default;
};
struct osbx_rename_node_t {
    std::string node, new_id;
    auto operator==(const osbx_rename_node_t&) const -> bool = default;
};
struct osbx_reparent_node_t {
    std::string node;
    std::optional<std::string> parent;
    auto operator==(const osbx_reparent_node_t&) const -> bool = default;
};
// Destination is an index among all nodes in the same layer, ordered by order
// then document position. Reordering normalizes only that layer's order values.
struct osbx_reorder_node_t {
    std::string node;
    std::size_t index{};
    auto operator==(const osbx_reorder_node_t&) const -> bool = default;
};
// Moves the subtree; detaches its root when its old parent is in another layer.
struct osbx_set_layer_t {
    std::string node;
    osbx_layer_t layer{osbx_layer_t::background};
    auto operator==(const osbx_set_layer_t&) const -> bool = default;
};
struct osbx_set_image_t {
    std::string node;
    std::optional<std::string> image;
    auto operator==(const osbx_set_image_t&) const -> bool = default;
};
struct osbx_set_origin_t {
    std::string node, origin;
    auto operator==(const osbx_set_origin_t&) const -> bool = default;
};
struct osbx_add_op_t {
    std::string node;
    std::optional<std::string> parent_op; // A pivot, or the node's root stack.
    osbx_transform_kind_t kind{osbx_transform_kind_t::translate};
    auto operator==(const osbx_add_op_t&) const -> bool = default;
};
struct osbx_remove_op_t {
    std::string node, op;
    auto operator==(const osbx_remove_op_t&) const -> bool = default;
};
struct osbx_reorder_op_t {
    std::string node, op;
    std::size_t index{}; // Within the op's existing stack.
    auto operator==(const osbx_reorder_op_t&) const -> bool = default;
};

using osbx_edit_value_t = std::variant<std::vector<double>, bool, osbx_blend_t>;
struct osbx_set_default_t {
    osbx_track_target_t track;
    osbx_edit_value_t value;
    auto operator==(const osbx_set_default_t&) const -> bool = default;
};
struct osbx_add_key_t {
    osbx_track_target_t track;
    std::int64_t time_ms{};
    osbx_edit_value_t value;
    osbx_easing_t easing{osbx_linear_easing_t{}};
    auto operator==(const osbx_add_key_t&) const -> bool = default;
};
struct osbx_move_key_t {
    osbx_track_target_t track;
    std::string key;
    std::int64_t time_ms{};
    auto operator==(const osbx_move_key_t&) const -> bool = default;
};
struct osbx_set_key_value_t {
    osbx_track_target_t track;
    std::string key;
    osbx_edit_value_t value;
    auto operator==(const osbx_set_key_value_t&) const -> bool = default;
};
struct osbx_set_easing_t {
    osbx_track_target_t track;
    std::string key;
    osbx_easing_t easing;
    auto operator==(const osbx_set_easing_t&) const -> bool = default;
};
struct osbx_delete_key_t {
    osbx_track_target_t track;
    std::string key;
    auto operator==(const osbx_delete_key_t&) const -> bool = default;
};

struct osbx_move_keys_t {
    std::vector<osbx_key_target_t> keys;
    std::int64_t delta_ms{};
    auto operator==(const osbx_move_keys_t&) const -> bool = default;
};
struct osbx_add_image_t {
    std::string file;
    auto operator==(const osbx_add_image_t&) const -> bool = default;
};
struct osbx_existing_image_t {
    std::string image;
    auto operator==(const osbx_existing_image_t&) const -> bool = default;
};
struct osbx_resource_image_t {
    resource_id_t resource;
    asset_id_t asset;
    std::filesystem::path source_path; // Original source captured before worker preparation.
    std::string file; // Validated project-relative local or newly copied image.
    auto operator==(const osbx_resource_image_t&) const -> bool = default;
};
struct osbx_import_image_t {
    import_resource_asset_action_t file;
    auto operator==(const osbx_import_image_t& other) const -> bool {
        return std::tie(file.source_path, file.copied_project_relative_path, file.expected_new_asset_id,
            file.role, file.name, file.allocate_bms_identifier, file.audio_stream, file.image_geometry) ==
            std::tie(other.file.source_path, other.file.copied_project_relative_path, other.file.expected_new_asset_id,
                other.file.role, other.file.name, other.file.allocate_bms_identifier, other.file.audio_stream, other.file.image_geometry);
    }
};
struct osbx_sprite_placement_t {
    osbx_layer_t layer{osbx_layer_t::background};
    double x{}, y{};
    auto operator==(const osbx_sprite_placement_t&) const -> bool = default;
};
struct osbx_place_image_t {
    std::variant<osbx_existing_image_t, osbx_resource_image_t, osbx_import_image_t> source;
    std::optional<osbx_sprite_placement_t> sprite;
    auto operator==(const osbx_place_image_t&) const -> bool = default;
};
struct osbx_remove_image_t {
    std::string image;
    auto operator==(const osbx_remove_image_t&) const -> bool = default;
};
struct osbx_set_lifetime_t {
    std::string node;
    std::optional<osbx_lifetime_t> lifetime; // Clear restores unbounded visibility.
    auto operator==(const osbx_set_lifetime_t&) const -> bool = default;
};
// Converts edit.storyboard into a new editable OSBX copy; source stays intact.
struct osbx_convert_storyboard_t {
    auto operator==(const osbx_convert_storyboard_t&) const -> bool = default;
};
struct osbx_duplicate_node_t {
    std::string node;
    auto operator==(const osbx_duplicate_node_t&) const -> bool = default;
};
// Resolves stable sibling/parent identities in the working scene; moves the
// complete source subtree as one block. Before a group means before its earliest
// remaining descendant/root draw position; parent-end follows its last descendant.
// Cross-layer drops always reject.
struct osbx_drop_node_t {
    std::string node;
    std::optional<std::string> parent, before;
    osbx_layer_t layer{osbx_layer_t::background};
    auto operator==(const osbx_drop_node_t&) const -> bool = default;
};
struct osbx_set_order_t {
    std::string node;
    int order{};
    auto operator==(const osbx_set_order_t&) const -> bool = default;
};
struct osbx_delete_keys_t {
    std::vector<osbx_key_target_t> keys;
    auto operator==(const osbx_delete_keys_t&) const -> bool = default;
};
// A missing link clears scripting while retaining every authored key/child.
// Empty expected_link means creation; existing updates retain their allocated ID.
struct osbx_set_lua_track_t {
    osbx_track_target_t track;
    std::string expected_link;
    std::optional<osbx_lua_track_link_t> link;
    auto operator==(const osbx_set_lua_track_t&) const -> bool = default;
};
struct osbx_set_lua_children_t {
    std::string node, expected_link;
    std::optional<osbx_lua_link_t> link;
    auto operator==(const osbx_set_lua_children_t&) const -> bool = default;
};
// Detaches one materialized storyboard; the recipe and all other outputs remain.
struct osbx_detach_storyboard_t {
    recipe_id_t expected_owner;
    auto operator==(const osbx_detach_storyboard_t&) const -> bool = default;
};
using osbx_edit_change_t = std::variant<osbx_create_storyboard_t, osbx_delete_storyboard_t,
    osbx_rename_storyboard_t, osbx_add_node_t, osbx_delete_node_t, osbx_rename_node_t,
    osbx_reparent_node_t, osbx_reorder_node_t, osbx_set_layer_t, osbx_set_image_t,
    osbx_set_origin_t, osbx_add_op_t, osbx_remove_op_t, osbx_reorder_op_t,
    osbx_set_default_t, osbx_add_key_t, osbx_move_key_t, osbx_set_key_value_t,
    osbx_set_easing_t, osbx_delete_key_t, osbx_move_keys_t, osbx_add_image_t, osbx_remove_image_t,
    osbx_set_lifetime_t, osbx_convert_storyboard_t, osbx_duplicate_node_t, osbx_drop_node_t, osbx_set_order_t, osbx_delete_keys_t, osbx_place_image_t, osbx_set_lua_track_t, osbx_set_lua_children_t, osbx_detach_storyboard_t>;

// Allocation reserves declarations from current, Undo and Redo snapshots. New
// strings use <parent>/<kind>/<counter>, above every reserved counter.
struct osbx_edit_ids_t {
    std::set<std::string, std::less<>> reserved;
    std::optional<storyboard_id_t> next_storyboard;
    std::optional<std::string> inserted_node, inserted_image, inserted_op;
    [[nodiscard]] static auto from(const document_history_t&) -> osbx_edit_ids_t;
    [[nodiscard]] auto next(std::string_view parent, std::string_view kind,
                            const osbx_document_t&) const -> std::string;
    auto operator==(const osbx_edit_ids_t&) const -> bool = default;
};
struct osbx_edit_t {
    storyboard_id_t storyboard; // Ignored for create; allocation belongs to history.
    osbx_edit_change_t change;
    [[nodiscard]] auto apply(document_command_transaction_t&, const osbx_edit_ids_t&) const
        -> attempt_t<document_edit_effect_t>;
    auto operator==(const osbx_edit_t&) const -> bool = default;
};
struct osbx_edit_created_ids_t {
    std::optional<storyboard_id_t> storyboard;
    std::optional<std::string> node;
    std::optional<std::string> image;
    auto operator==(const osbx_edit_created_ids_t&) const -> bool = default;
};
// One executor publishes planned identities only after a successful transaction.
[[nodiscard]] auto apply_osbx_edit_with_created_ids(document_history_t&, const osbx_edit_t&)
    -> command_result_with_t<osbx_edit_created_ids_t>;
[[nodiscard]] auto apply_osbx_edit(document_history_t&, const osbx_edit_t&)
    -> document_command_result_t;
// Intentional public storyboard-ID projection for established Create/Convert callers.
// Creation/conversion return their history-reserved identity only on success.
// Other edits return an empty payload; callers never infer identity from order.
[[nodiscard]] auto apply_osbx_edit_with_result(document_history_t&, const osbx_edit_t&)
    -> command_result_with_t<std::optional<storyboard_id_t>>;

// Drags retain their target and change only the proposed value/time. Dispatch
// stores this payload in the shared gesture proposal; no document is published
// until apply, and ordinary editor cancellation discards the proposal.
struct editor_document_context_t;
struct osbx_edit_preview_t {
    document_stamp_t capture;
    std::uint64_t prepared_generation{}; // Borrow the existing installed-source identity.
    storyboard_id_t storyboard;
    song_document_t document;
    std::vector<osbx_lua_prepared_scene_t> prepared;
};
struct pending_osbx_edit_gesture_t {
    static constexpr auto kind = editor_gesture_kind_t::storyboard_edit;
    static constexpr bool document_owned = true;
    static constexpr std::string_view revision_message = "No storyboard edit gesture is active";
    osbx_edit_t edit;
    osbx_edit_ids_t ids; // Add-key reserves one stable insertion across every preview.
    std::shared_ptr<const osbx_edit_preview_t> preview_owner;
    [[nodiscard]] auto preview_document() const -> std::shared_ptr<const song_document_t> {
        return preview_owner ? std::shared_ptr<const song_document_t>{preview_owner, &preview_owner->document} : nullptr;
    }
    [[nodiscard]] static auto begin(editor_document_context_t&, const osbx_edit_t&)
        -> diagnosed_t<std::optional<pending_osbx_edit_gesture_t>>;
    [[nodiscard]] auto revise(editor_document_context_t&, const osbx_edit_change_t&) -> diagnostic_list_t;
    [[nodiscard]] auto apply(editor_document_context_t&) const -> editor_interaction_result_t;
    auto operator==(const pending_osbx_edit_gesture_t&) const -> bool = default;
};

} // namespace wormedit
