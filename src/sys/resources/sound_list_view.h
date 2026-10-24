#pragma once

#include <memory>
#include <wormedit/core/resource_vocabulary.h>

#include "sys/audio/audio_fallback.h"

#include <wormedit/core/document.h>
#include <wormedit/core/command.h>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/natural_order.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <span>
#include <unordered_set>
#include <vector>

namespace wormedit::sys {

enum class dock_sort_t : std::uint8_t { natural_name, bms_id };
[[nodiscard]] inline auto matches_resource_use(resource_use_filter_t filter, std::uint64_t uses, bool missing) -> bool {
    switch (filter) {
    case resource_use_filter_t::used: return uses != 0;
    case resource_use_filter_t::unused: return uses == 0;
    case resource_use_filter_t::missing: return missing;
    default: return true;
    }
}

// Sounds tab data: the audio catalog as a searchable, windowed list plus the
// properties of one selected sound. Pure projection of the document.

struct sound_list_row_t {
    resource_id_t resource;
    std::string name;
    std::string source_path;
    sound_kind_t kind{sound_kind_t::unspecified};
    std::uint32_t tag_count{};
    // Tags joined with ", " for display.
    std::string tags_text;
    std::uint64_t duration_frames{};
    std::uint32_t sample_rate{};
    std::uint64_t use_count{};
    bool missing{};
};

struct sound_list_request_t {
    // Case-insensitive substring of the name or of any tag. Empty matches all.
    std::string search;
    // Case-insensitive exact tag. Empty matches all.
    std::string tag;
    std::size_t first_row{};
    std::size_t row_limit{64};
    auto operator==(const sound_list_request_t&) const -> bool = default;
};

struct sound_list_view_t {
    // The requested window of matching rows, in catalog order.
    std::vector<sound_list_row_t> rows;
    std::size_t total_matching{};
    // first_row clamped to the matches.
    std::size_t first_row{};
    // Every tag in the catalog (first spelling), sorted case-insensitively.
    std::vector<std::string> all_tags;

    [[nodiscard]] static auto from(const song_document_t& document,
                                   const sound_list_request_t& request) -> sound_list_view_t;
};

class sound_list_cache_t {
  public:
    [[nodiscard]] auto view_for(const song_document_t& document, const sound_list_request_t& request,
                                std::uint64_t session_generation = 0)
        -> const sound_list_view_t&;
  private:
    struct key_t {
        document_stamp_t stamp;
        sound_list_request_t request;
        std::uint64_t session_generation;
        auto operator==(const key_t&) const -> bool = default;
    };
    keyed_cache_t<key_t, sound_list_view_t> projection_;
};

// The kind shown for a resource: the explicit one, else stem for BGM/music-only
// resources and keysound otherwise.
[[nodiscard]] auto effective_sound_kind(const resource_entry_t& resource) -> sound_kind_t;

struct sound_properties_view_t {
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
    std::uint64_t use_count{};
    bool missing{};

    // Invalid (valid == false) when the resource is unknown or not audio.
    [[nodiscard]] static auto from(const song_document_t& document,
                                   resource_id_t resource) -> sound_properties_view_t;
};

// The fallback outcome of each audio resource whose authored file is absent (up to `limit`,
// document order). Exact files and resources with no candidate do not appear. Directory scans
// are cached per document revision and asset base.
struct keysound_fallback_t {
    resource_id_t resource;
    audio_fallback_t result;
    std::vector<std::string> candidate_paths;
};
[[nodiscard]] auto build_keysound_fallbacks_view(const song_document_t& document,
                                              const std::filesystem::path& asset_base,
                                              std::size_t limit, std::uint64_t session_generation = 0)
    -> std::span<const keysound_fallback_t>;

// Compose keysound dock: every placeable sample (an audio resource with the
// sample role that is not a stem), in the requested order, with the layers the
// sound receives when painted on the given chart.
inline constexpr std::size_t keysound_layer_limit = 64;

struct keysound_layer_t {
    layer_id_t id;
    std::string name;
    color_rgba_t color;
    bool locked{};
    auto operator==(const keysound_layer_t&) const -> bool = default;
};

struct keysound_row_t {
    resource_id_t resource;
    // BMS #WAV index ("01") when the sound has one, else empty.
    std::string slot;
    std::string name;
    bool missing{};
    std::uint64_t use_count{};
    std::string source_path;
    // Bit i is set when the sound's default layers include keysound_list_view_t::layers[i].
    std::uint64_t layer_mask{};
    std::uint64_t note_layer_conflict_mask{};
    std::uint64_t sound_group_id{};
    auto operator==(const keysound_row_t&) const -> bool = default;
};

struct keysound_advance_row_t {
    resource_id_t resource;
    bool missing{};
    auto operator==(const keysound_advance_row_t&) const -> bool = default;
};

struct keysound_selection_row_t {
    resource_id_t resource;
    std::uint64_t group_id{};
    std::uint64_t layer_mask{};
    std::uint64_t note_layer_conflict_mask{};
    std::string slot;
    bool missing{};
    auto operator==(const keysound_selection_row_t&) const -> bool = default;
};

struct keysound_list_request_t;
struct keysound_list_cache_t;

struct keysound_list_view_t {
    // Sound payloads only for the requested display window and selected IDs.
    std::vector<keysound_row_t> rows;
    std::vector<keysound_row_t> selected_rows;
    std::vector<keysound_selection_row_t> selection_rows;
    struct display_row_t {
        enum class kind_t : std::uint8_t { group_header, sound } kind{kind_t::sound};
        std::uint64_t group_id{};
        std::string group_name;
        std::size_t group_sound_count{};
        bool expanded{true};
        resource_id_t resource;
        auto operator==(const display_row_t&) const -> bool = default;
    };
    std::vector<display_row_t> display_rows;
    std::vector<resource_id_t> listed_resources;
    std::vector<keysound_advance_row_t> advance_rows;
    std::vector<resource_id_t> all_resources;
    // Placeable sounds in the document, including rows cut off by the limit.
    std::size_t total{};
    bool truncated{};
    std::size_t display_total{}, display_first_row{};
    std::optional<std::size_t> primary_display_index;
    bool display_truncated{};
    // The chart's first keysound_layer_limit layers in palette order.
    std::vector<keysound_layer_t> layers;
    // Complete stable-ID choice list for the default-layer filter dropdown.
    std::vector<keysound_layer_t> filter_layers;
    std::size_t filter_layer_first{}, filter_layer_total{};
    std::string selected_filter_layer_name;
    bool selected_filter_layer_valid{};
    bms_identifier_capacity_t bms_capacity{};
    auto operator==(const keysound_list_view_t& other) const -> bool {
        return rows == other.rows && selected_rows == other.selected_rows &&
            selection_rows == other.selection_rows && display_rows == other.display_rows &&
            listed_resources == other.listed_resources && advance_rows == other.advance_rows &&
            all_resources == other.all_resources && total == other.total &&
            truncated == other.truncated && display_total == other.display_total &&
            display_first_row == other.display_first_row &&
            primary_display_index == other.primary_display_index &&
            display_truncated == other.display_truncated && layers == other.layers &&
            filter_layers == other.filter_layers && filter_layer_first == other.filter_layer_first &&
            filter_layer_total == other.filter_layer_total &&
            selected_filter_layer_name == other.selected_filter_layer_name &&
            selected_filter_layer_valid == other.selected_filter_layer_valid &&
            bms_capacity.used == other.bms_capacity.used &&
            bms_capacity.total == other.bms_capacity.total &&
            bms_capacity.largest_free_run == other.bms_capacity.largest_free_run;
    }

    [[nodiscard]] static auto from(const song_document_t& document,
                                   const chart_document_t* chart,
                                   std::size_t maximum_rows,
                                   dock_sort_t sort = dock_sort_t::natural_name,
                                   bool ids_available = true)
        -> keysound_list_view_t;

    // Filter the complete placeable catalog first, then flatten group sections and
    // return only the requested display window. ID-only lists retain full filtered order.
    [[nodiscard]] static auto from(const song_document_t& document,
                                   const chart_document_t* chart,
                                   const keysound_list_request_t& request)
        -> keysound_list_view_t;

    [[nodiscard]] static auto from(keysound_list_cache_t& cache,
                                   const song_document_t& document, const chart_document_t* chart, std::size_t maximum_rows,
                                   dock_sort_t sort = dock_sort_t::natural_name, bool ids_available = true)
        -> const keysound_list_view_t&;

    [[nodiscard]] static auto from(keysound_list_cache_t& cache,
                                   const song_document_t& document, const chart_document_t* chart,
                                   const keysound_list_request_t& request) -> const keysound_list_view_t&;
};


struct keysound_list_request_t {
    std::string_view search;
    resource_use_filter_t use_filter{resource_use_filter_t::all};
    std::span<const resource_id_t> selected_resource_ids;
    bool include_selected_metadata{};
    std::span<const sound_group_id_t> group_ids;
    bool group_filter_active{};
    bool include_ungrouped{true};
    keysound_layer_filter_kind_t layer_filter{keysound_layer_filter_kind_t::any};
    std::uint64_t layer_id{};
    std::span<const sound_group_id_t> collapsed_group_ids;
    bool ungrouped_collapsed{};
    std::size_t first_row{};
    std::size_t row_limit{64};
    std::uint64_t primary_resource_id{};
    std::size_t filter_layer_first{}, filter_layer_limit{64};
    dock_sort_t sort{dock_sort_t::natural_name};
    bool ids_available{true};
};

struct sound_picker_cache_t;

struct keysound_list_cache_t {
    std::shared_ptr<sound_picker_cache_t> sound_picker;
    void bind_session(std::uint64_t generation);
    [[nodiscard]] auto projection_build_count() const noexcept -> std::size_t {
        return legacy_.build_count() + requested_.build_count();
    }
    [[nodiscard]] auto asset_index_build_count() const noexcept -> std::size_t {
        return asset_index_.build_count();
    }

public:
    friend struct keysound_list_view_t;

    struct identity_t {
        std::uint64_t project_id{};
        std::uintptr_t project_address{};
        std::uint64_t revision{};
        std::uint64_t chart_id{};
        std::uintptr_t chart_address{};
        auto operator==(const identity_t&) const -> bool = default;
        static auto from(const song_document_t& document, const chart_document_t* chart) -> identity_t;
    };
    struct legacy_key_t {
        identity_t identity;
        std::size_t maximum_rows{};
        dock_sort_t sort{dock_sort_t::natural_name};
        bool ids_available{true};
        auto operator==(const legacy_key_t&) const -> bool = default;
    };
    struct request_query_t {
        identity_t identity;
        const keysound_list_request_t& request;
    };
    struct request_key_t {
        identity_t identity;
        std::string search;
        resource_use_filter_t use_filter{resource_use_filter_t::all};
        std::vector<resource_id_t> selected_resource_ids;
        bool include_selected_metadata{};
        std::vector<sound_group_id_t> group_ids;
        bool group_filter_active{};
        bool include_ungrouped{true};
        keysound_layer_filter_kind_t layer_filter{keysound_layer_filter_kind_t::any};
        std::uint64_t layer_id{};
        std::vector<sound_group_id_t> collapsed_group_ids;
        bool ungrouped_collapsed{};
        std::size_t first_row{}, row_limit{64};
        std::uint64_t primary_resource_id{};
        std::size_t filter_layer_first{}, filter_layer_limit{64};
        dock_sort_t sort{dock_sort_t::natural_name};
        bool ids_available{true};
        auto operator==(const request_key_t&) const -> bool = default;
        auto operator==(const request_query_t& query) const -> bool;
        static auto from(const request_query_t& query) -> request_key_t;
        void assign(const request_query_t& query);
    };

private:
    std::uint64_t session_generation_{};
    [[nodiscard]] auto asset_ids_for(const song_document_t& document)
        -> const std::unordered_set<std::uint64_t>&;

    static void build_asset_ids(const song_document_t& document,
                                std::unordered_set<std::uint64_t>& ids);

    keyed_cache_t<identity_t, std::unordered_set<std::uint64_t>> asset_index_;
    keyed_cache_t<legacy_key_t, keysound_list_view_t> legacy_;
    keyed_cache_t<request_key_t, keysound_list_view_t> requested_;
};

// All supported nonzero BMS WAV slots, filtered and projected in a bounded window.
struct sound_slot_request_t {
    std::string search;
    std::uint16_t selected_slot{};
    std::size_t first_row{};
    std::size_t row_limit{64};
    resource_use_filter_t use_filter{resource_use_filter_t::all};
};
struct sound_slot_row_t {
    std::uint16_t number{};
    std::string identifier;
    resource_id_t resource;
    std::string name;
    std::uint64_t use_count{};
    bool missing{};
    bool placeable{};
    std::string source_path;
};
struct sound_slots_view_t {
    std::vector<sound_slot_row_t> rows;
    std::size_t first_row{}, total_matching{};
    // Index in the filtered virtual list, or SIZE_MAX when the selected slot is hidden.
    std::size_t selected_row{SIZE_MAX};

    [[nodiscard]] static auto from(const song_document_t& document,
                                   const sound_slot_request_t& request,
                                   const chart_document_t* chart = nullptr) -> sound_slots_view_t;
};

} // namespace wormedit::sys
