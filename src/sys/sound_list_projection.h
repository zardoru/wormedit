#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/natural_order.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wormedit::sys {

enum class dock_sort_t : std::uint8_t { natural_name, bms_id };

// Sounds tab data: the audio catalog as a searchable, windowed list plus the
// properties of one selected sound. Pure projection of the document.

struct sound_list_row_t {
    resource_id_t resource;
    std::string name;
    sound_kind_t kind{sound_kind_t::unspecified};
    std::uint32_t tag_count{};
    // Tags joined with ", " for display.
    std::string tags_text;
    std::uint64_t duration_frames{};
    std::uint32_t sample_rate{};
    std::uint32_t use_count{};
    bool missing{};
};

struct sound_list_request_t {
    // Case-insensitive substring of the name or of any tag. Empty matches all.
    std::string search;
    // Case-insensitive exact tag. Empty matches all.
    std::string tag;
    std::size_t first_row{};
    std::size_t row_limit{64};
};

struct sound_list_t {
    // The requested window of matching rows, in catalog order.
    std::vector<sound_list_row_t> rows;
    std::size_t total_matching{};
    // first_row clamped to the matches.
    std::size_t first_row{};
    // Every tag in the catalog (first spelling), sorted case-insensitively.
    std::vector<std::string> all_tags;
};

// The kind shown for a resource: the explicit one, else stem for BGM/music-only
// resources and keysound otherwise.
[[nodiscard]] auto effective_sound_kind(const resource_entry_t& resource) -> sound_kind_t;

[[nodiscard]] auto project_sound_list(const song_document_t& document,
                                      const sound_list_request_t& request) -> sound_list_t;

struct sound_properties_t {
    bool valid{};
    resource_id_t resource;
    asset_id_t asset;
    std::string name;
    std::string source_path;
    sound_kind_t kind{sound_kind_t::unspecified};
    std::vector<std::string> tags;
    std::uint64_t duration_frames{};
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::uint32_t use_count{};
    bool missing{};
};

// Invalid (valid == false) when the resource is unknown or not audio.
[[nodiscard]] auto project_sound_properties(const song_document_t& document,
                                            resource_id_t resource) -> sound_properties_t;

// Compose keysound dock: every placeable sample (an audio resource with the
// sample role that is not a stem), in the requested order, with the layers the
// sound receives when painted on the given chart.
inline constexpr std::size_t keysound_layer_limit = 64;

struct keysound_layer_t {
    layer_id_t id;
    std::string name;
    color_rgba_t color;
    bool locked{};
};

struct keysound_row_t {
    resource_id_t resource;
    // BMS #WAV index ("01") when the sound has one, else empty.
    std::string slot;
    std::string name;
    bool missing{};
    // Bit i is set when the sound's default layers include keysound_list_t::layers[i].
    std::uint64_t layer_mask{};
    std::uint64_t note_layer_conflict_mask{};
};

struct keysound_list_t {
    std::vector<keysound_row_t> rows;
    // Placeable sounds in the document, including rows cut off by the limit.
    std::size_t total{};
    bool truncated{};
    // The chart's first keysound_layer_limit layers in palette order.
    std::vector<keysound_layer_t> layers;
    bms_identifier_capacity_t bms_capacity{};
};

[[nodiscard]] auto project_keysound_list(const song_document_t& document,
                                         const chart_document_t* chart,
                                         std::size_t maximum_rows,
                                         dock_sort_t sort = dock_sort_t::natural_name,
                                         bool ids_available = true)
    -> keysound_list_t;

} // namespace wormedit::sys
