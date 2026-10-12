#pragma once

#include "sys/compose_view.h"
#include <algorithm>
#include <cmath>
#include <variant>
#include <wormedit/core/sound_selection.h>

namespace wormedit::sys {
inline auto compose_sound_selection_keys(const wormedit::song_document_t& song,
                                         const wormedit::chart_document_t& chart,
                                         const wormedit::editor_interaction_state_t& state,
                                         const application_command_state_t& application)
    -> std::vector<wormedit::sound_selection_key_t> {
    using namespace wormedit;
    std::vector<sound_selection_key_t> keys;
    const auto add = [&keys](std::optional<sound_selection_key_t> key) {
        if (key && std::ranges::find(keys, *key) == keys.end())
            keys.push_back(*key);
    };
    if (application.sound_selection_chart == chart.id &&
        application.sound_selection_revision == song.revision &&
        application.sound_selection_hover) {
        add(sound_selection_object_key(song, chart, *application.sound_selection_hover));
        return keys;
    }
    std::vector<object_id_t> sources = state.selected_notes;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::note ||
            object.kind == editor_selection_object_kind_t::bgm_event)
            sources.push_back(object.id);
    keys = sound_selection_object_keys(song, chart, sources);
    if (!keys.empty())
        return keys;
    if (application.sound_selection_chart == chart.id &&
        application.sound_selection_revision == song.revision)
        for (const auto resource : application.sound_selection_dock)
            add(sound_selection_resource_key(song, chart, resource));
    return keys;
}
inline auto compose_sound_selection_available(const wormedit::song_document_t& song,
                                              const wormedit::chart_document_t& chart,
                                              const wormedit::editor_interaction_state_t& state,
                                              const application_command_state_t& application)
    -> bool {
    return !editor_interaction_has_pending_gesture(state) && !application.paste_session &&
           !compose_sound_selection_keys(song, chart, state, application).empty();
}
inline auto compose_same_sound_objects(const wormedit::song_document_t& song,
                                       const wormedit::chart_document_t& chart,
                                       const wormedit::editor_interaction_state_t& state,
                                       const application_command_state_t& application,
                                       std::span<const wormedit::sound_selection_key_t> keys,
                                       bool visible)
    -> std::vector<wormedit::editor_selection_object_t> {
    using namespace wormedit;
    std::optional<sound_selection_range_t> range;
    if (visible && state.pixels_per_standard_measure > 0) {
        const auto rows_per_pixel = static_cast<double>(chart.divisions_per_standard_measure) /
                                    state.pixels_per_standard_measure;
        range = sound_selection_range_t{
            state.viewport_scroll_pixels * rows_per_pixel,
            (state.viewport_scroll_pixels + state.viewport_height_pixels) * rows_per_pixel};
    }
    return select_same_sound(song, chart,
                             {.keys = keys,
                              .visible = range,
                              .hidden_layers = application.excluded_semantic_layer_filters,
                              .required_layers = application.required_semantic_layer_filters,
                              .solo_layer = application.semantic_layer_solo_active
                                                ? application.semantic_layer_target
                                                : std::nullopt});
}
// The menu supplies its clicked source; shortcuts use hover/selection/dock order.
// Scope changes the query range, never the guards or empty-source behavior.
inline auto compose_same_sound_request(const song_document_t& song, const chart_document_t& chart,
    const editor_interaction_state_t& state, const application_command_state_t& application,
    bool visible, editor_area_selection_mode_t mode, std::optional<object_id_t> object = std::nullopt,
    std::optional<resource_id_t> resource = std::nullopt)
    -> std::variant<select_editor_objects_action_t, std::string> {
    if (editor_interaction_has_pending_gesture(state) || application.paste_session)
        return "Finish or cancel the active Compose gesture before selecting sounds.";
    std::vector<sound_selection_key_t> keys;
    if (object || resource) {
        const auto key = resource ? sound_selection_resource_key(song, chart, *resource)
                                  : sound_selection_object_key(song, chart, *object);
        if (key) keys.push_back(*key);
    } else {
        keys = compose_sound_selection_keys(song, chart, state, application);
    }
    if (keys.empty()) return "Hover a note or select a sound first";
    return select_editor_objects_action_t{
        compose_same_sound_objects(song, chart, state, application, keys, visible), mode, false};
}

} // namespace wormedit::sys
