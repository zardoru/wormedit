#pragma once

#include <span>
#include <wormedit/core/interaction_types.h>

namespace wormedit {
enum class sound_selection_key_kind_t { resource, asset, channel };
struct sound_selection_key_t {
    sound_selection_key_kind_t kind{};
    std::uint64_t value{};
    auto operator==(const sound_selection_key_t&) const -> bool = default;
};
struct sound_selection_range_t {
    double begin_row{}, end_row{};
};
struct sound_selection_query_t {
    std::span<const sound_selection_key_t> keys;
    std::optional<sound_selection_range_t> visible;
    std::span<const lane_id_t> hidden_lanes;
    std::span<const layer_id_t> hidden_layers;
    std::span<const layer_id_t> required_layers;
    std::optional<layer_id_t> solo_layer;
};
// BMSON keys always name the retained channel, never its sliced sample resource.
[[nodiscard]] auto sound_selection_object_key(const song_document_t& song,
                                              const chart_document_t& chart, object_id_t object)
    -> std::optional<sound_selection_key_t>;
[[nodiscard]] auto sound_selection_resource_key(const song_document_t& song,
                                                const chart_document_t& chart,
                                                resource_id_t resource)
    -> std::optional<sound_selection_key_t>;
// Resolve a large selection in one chart traversal rather than one scan per object.
[[nodiscard]] auto sound_selection_object_keys(const song_document_t& song,
                                               const chart_document_t& chart,
                                               std::span<const object_id_t> objects)
    -> std::vector<sound_selection_key_t>;
[[nodiscard]] auto select_same_sound(const song_document_t& song, const chart_document_t& chart,
                                     const sound_selection_query_t& query)
    -> std::vector<editor_selection_object_t>;
} // namespace wormedit
