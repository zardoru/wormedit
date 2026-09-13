#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {

struct semantic_layer_edit_limits_t {
    std::size_t maximum_layers{100'000};
    std::size_t maximum_note_selection{100'000};
    std::size_t maximum_chart_objects{1'000'000};
    std::size_t maximum_name_bytes{4U * 1024U};
    std::size_t maximum_total_name_bytes{1U * 1024U * 1024U};
};

// Layer colors use normalized floating-point components at the editor boundary
// and are rounded to the document's 8-bit RGBA representation on commit.
struct semantic_layer_rgba_t {
    double red{};
    double green{};
    double blue{};
    double alpha{1.0};
};

struct insert_semantic_layer_action_t {
    chart_id_t chart;
    std::string name;
    semantic_layer_rgba_t color;
    bool visible{true};
    bool locked{false};
};

struct update_semantic_layer_action_t {
    chart_id_t chart;
    layer_id_t layer;
    std::string name;
    semantic_layer_rgba_t color;
    bool visible{true};
    bool locked{false};
};

struct delete_semantic_layer_action_t {
    chart_id_t chart;
    layer_id_t layer;
    bool remove_from_notes{false};
};

struct reorder_semantic_layers_action_t {
    chart_id_t chart;
    // Must contain every current layer exactly once in the requested order.
    std::vector<layer_id_t> ordered_layers;
};

struct assign_notes_to_semantic_layer_action_t {
    chart_id_t chart;
    layer_id_t layer;
    std::vector<object_id_t> notes;
};

struct remove_notes_from_semantic_layer_action_t {
    chart_id_t chart;
    layer_id_t layer;
    std::vector<object_id_t> notes;
};

struct toggle_semantic_layer_membership_action_t {
    chart_id_t chart;
    layer_id_t layer;
    std::vector<object_id_t> notes;
};

using semantic_layer_edit_action_t =
    std::variant<insert_semantic_layer_action_t, update_semantic_layer_action_t,
                 delete_semantic_layer_action_t, reorder_semantic_layers_action_t,
                 assign_notes_to_semantic_layer_action_t, remove_notes_from_semantic_layer_action_t,
                 toggle_semantic_layer_membership_action_t>;

// Applies one layer edit through shared document history. Layer IDs are stable,
// nonzero, chart-local values. Membership actions affect only the explicitly
// listed notes and preserve all gameplay fields. Locked layers cannot have
// memberships changed or be deleted; their metadata and order remain editable
// so a layer can be renamed or unlocked. Deleting a populated layer requires
// remove_from_notes=true and removes its memberships in the same transaction.
[[nodiscard]] auto apply_semantic_layer_edit(document_history_t& history,
                                             std::uint64_t expected_revision,
                                             const semantic_layer_edit_action_t& action,
                                             semantic_layer_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
