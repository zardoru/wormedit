#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <optional>
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

struct semantic_layer_palette_window_t {
    std::size_t first_row{};
    std::size_t page{};
    std::size_t page_count{};
    std::size_t total_count{};
    std::vector<std::size_t> layer_indices;
};

// Projects one bounded palette page from visible layers plus an explicitly
// selected hidden layer. reveal_selected moves the window to that stable ID;
// otherwise requested_page is clamped after document edits.
[[nodiscard]] auto make_semantic_layer_palette_window(const chart_document_t& chart,
                                                      std::optional<layer_id_t> selected_layer,
                                                      std::size_t requested_page,
                                                      bool reveal_selected, std::size_t page_size,
                                                      std::size_t maximum_layers = 100'000)
    -> std::optional<semantic_layer_palette_window_t>;

// Projects a bounded scrolling window over every layer, including hidden
// layers, in chart order. The first row clamps after catalog edits. A zero
// row count or a chart exceeding maximum_layers rejects the request.
[[nodiscard]] auto make_semantic_layer_palette_window(const chart_document_t& chart,
                                                      std::size_t first_row,
                                                      std::size_t row_count,
                                                      std::size_t maximum_layers = 100'000)
    -> std::optional<semantic_layer_palette_window_t>;

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

enum class membership_operation_t { assign, remove, toggle };

struct semantic_layer_membership_edit_t {
    chart_id_t chart;
    layer_id_t layer;
    std::vector<object_id_t> notes;
    std::vector<object_id_t> bgm_events;
    membership_operation_t operation{membership_operation_t::assign};
};

struct clear_semantic_layer_membership_action_t {
    chart_id_t chart;
    std::vector<object_id_t> notes;
    std::vector<object_id_t> bgm_events;
};

// Compose assignment replaces previous memberships. Toggle clears notes already
// in the target, and replaces every other note with that one target.
struct replace_notes_semantic_layer_action_t {
    chart_id_t chart;
    layer_id_t layer;
    std::vector<object_id_t> notes;
    bool toggle{};
    std::vector<object_id_t> bgm_events;
};

using semantic_layer_edit_action_t =
    std::variant<insert_semantic_layer_action_t, update_semantic_layer_action_t,
                 delete_semantic_layer_action_t, reorder_semantic_layers_action_t,
                 semantic_layer_membership_edit_t, clear_semantic_layer_membership_action_t,
                 replace_notes_semantic_layer_action_t>;

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

struct sound_layer_defaults_action_t {
    chart_id_t chart;
    // 1..maximum_sound_layer_default_resources catalog sounds.
    std::vector<resource_id_t> resources;
    // The layers every listed sound receives when painted; empty clears them.
    std::vector<layer_id_t> layers;
};

inline constexpr std::size_t maximum_sound_layer_default_resources = 100'000;

// Sets or clears the default layers of a group of sounds on one chart in one
// history step, so painting those sounds tags the new note. Layers must exist
// on the chart, be unlocked and unique; resources must be catalog sounds. Any
// invalid entry rejects the whole batch. A no-op creates no history entry.
[[nodiscard]] auto apply_sound_layer_defaults(document_history_t& history,
                                              std::uint64_t expected_revision,
                                              const sound_layer_defaults_action_t& action)
    -> document_command_result_t;

struct sound_layer_membership_action_t {
    chart_id_t chart;
    std::vector<resource_id_t> resources;
    layer_id_t layer;
    // true adds the layer to every listed sound, false removes it.
    bool member{true};
    // Adding also tags existing notes. Preserve assigned layers unless requested.
    bool overwrite_note_layers{};
    // Optional chart-wide fill for unassigned BGM; BGM supports one layer only.
    bool assign_unlayered_bgm{};
};

// Adds or removes one layer from the defaults of a group of sounds while the
// other layers of each sound stay as they are. Adding needs an unlocked layer;
// removing works on locked ones. Same atomic rules as apply_sound_layer_defaults.
// Adding tags matching unlayered notes in the same history step. Overwrite also
// replaces assigned memberships; locked memberships reject the whole request.
[[nodiscard]] auto apply_sound_layer_membership(document_history_t& history,
                                                std::uint64_t expected_revision,
                                                const sound_layer_membership_action_t& action)
    -> document_command_result_t;

struct add_selected_sounds_to_semantic_layer_action_t {
    chart_id_t chart;
    layer_id_t layer;
    std::vector<object_id_t> notes;
    std::vector<object_id_t> bgm_events;
};

// Deduplicates the sounds of selected notes/BGM, adds the target to their defaults,
// and fills matching unassigned notes/BGM across this chart in one Undo step.
// Explicit memberships are retained. Silent selected objects are ignored; an all-
// silent selection is a no-op. Invalid or repeated selected IDs reject the batch.
[[nodiscard]] auto add_selected_sounds_to_semantic_layer(document_history_t& history,
    std::uint64_t expected_revision, const add_selected_sounds_to_semantic_layer_action_t& action,
    semantic_layer_edit_limits_t limits = {}) -> document_command_result_t;

// Layers a sound brings to a freshly placed note: its defaults that still exist
// on the chart and are unlocked, in the stored order.
[[nodiscard]] auto sound_default_layers_for_placement(const chart_document_t& chart,
                                                      resource_id_t resource)
    -> std::vector<layer_id_t>;

// The one rule for the layers a freshly placed object gets, whatever path placed it
// (lane key, note paint, pointer, cursor, virtual BMS canvas, BGM/BGA): an explicit
// palette target wins (an empty vector means No layer); otherwise the sound's defaults.
[[nodiscard]] auto resolve_placement_layers(
    const chart_document_t& chart,
    const std::optional<std::vector<layer_id_t>>& armed_layers,
    std::optional<resource_id_t> sound) -> std::vector<layer_id_t>;

} // namespace wormedit
