#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/document.h>
#include <wormedit/core/interaction_types.h>
#include <wormedit/core/keyed_cache.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wormedit::sys {

struct compose_resource_entry_t {
    resource_id_t resource;
    asset_id_t asset;
    bool sample{};
    bool bgm{};
    bool bga{};
    bool audio_asset{};
    bool selected_audio_asset{};
    bool image_asset{};
    bool selected_visual_asset{};
    bool missing{};
    std::string target_index;
    std::string name;
    std::string source_name;
    std::string source_path;
    std::uint64_t use_count{};
};

struct compose_resource_catalog_cache_t;

struct compose_resource_catalog_t {
    [[nodiscard]] static auto from(const song_document_t& document,
                                  std::optional<asset_id_t> selected_audio_asset,
                                  std::size_t maximum_entries,
                                  std::optional<asset_id_t> selected_visual_asset = {})
        -> compose_resource_catalog_t;
    // Builds entries once per document stamp; only the selection is refreshed per call.
    [[nodiscard]] static auto from(compose_resource_catalog_cache_t& cache,
                                  const song_document_t& document,
                                  std::optional<asset_id_t> selected_audio_asset,
                                  std::size_t maximum_entries,
                                  std::optional<asset_id_t> selected_visual_asset = {},
                                  std::uint64_t session_generation = 0)
        -> const compose_resource_catalog_t&;
    void select(std::optional<asset_id_t> audio_asset, std::optional<asset_id_t> visual_asset);
    std::vector<compose_resource_entry_t> entries;
    std::size_t total_count{};
    bool truncated{};
};

struct compose_resource_catalog_query_t {
    const song_document_t& document;
    std::size_t maximum_entries{};
    std::uint64_t session_generation{};
};

struct compose_resource_catalog_key_t {
    const song_document_t* document_identity{};
    document_stamp_t stamp;
    std::size_t maximum_entries{};
    std::uint64_t session_generation{};

    [[nodiscard]] static auto from(const compose_resource_catalog_query_t& query)
        -> compose_resource_catalog_key_t;
    void assign(const compose_resource_catalog_query_t& query);
    [[nodiscard]] auto operator==(const compose_resource_catalog_query_t& query) const -> bool;
    auto operator==(const compose_resource_catalog_key_t&) const -> bool = default;
};

struct compose_resource_catalog_cache_t {
    keyed_cache_t<compose_resource_catalog_key_t, compose_resource_catalog_t> catalog;
};

using compose_sound_labels_t =
    std::unordered_map<std::uint64_t, std::pair<std::string, std::string>>;

struct compose_sound_text_t {
    const char* label;
    const char* detail;
};

[[nodiscard]] auto compose_resource_sound_label(const resource_entry_t& resource)
    -> std::pair<std::string, std::string>;

[[nodiscard]] auto compose_note_sound_text(std::optional<resource_id_t> sound,
                                           const compose_sound_labels_t& labels)
    -> compose_sound_text_t;

[[nodiscard]] auto find_compose_resource_uses(const chart_document_t& chart, resource_id_t resource)
    -> std::vector<editor_selection_object_t>;


} // namespace wormedit::sys
