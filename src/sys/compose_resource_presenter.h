#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/interaction.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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
    bool missing{};
    std::string target_index;
    std::string name;
    std::string source_name;
};

struct compose_resource_catalog_t {
    std::vector<compose_resource_entry_t> entries;
    std::size_t total_count{};
    bool truncated{};
};

[[nodiscard]] auto make_compose_resource_catalog(const song_document_t& document,
                                                 std::optional<asset_id_t> selected_audio_asset,
                                                 std::size_t maximum_entries)
    -> compose_resource_catalog_t;

[[nodiscard]] auto find_compose_resource_uses(const chart_document_t& chart, resource_id_t resource)
    -> std::vector<editor_selection_object_t>;

} // namespace wormedit::sys
