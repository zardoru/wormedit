#pragma once

#include <wormedit/core/channel_map_editor.h>
#include <wormedit/core/metadata_inheritance.h>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/lane_profiles.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace wormedit::sys {

// Source keys own their miss-time snapshot; frame lookups borrow paths and previews.
struct metadata_output_path_query_t {
    const std::filesystem::path& path;
    output_destination_kind_t destination;
};
struct metadata_output_path_key_t {
    std::filesystem::path path;
    output_destination_kind_t destination;
    [[nodiscard]] auto operator==(const metadata_output_path_query_t& query) const -> bool;
    [[nodiscard]] static auto from(const metadata_output_path_query_t& query)
        -> metadata_output_path_key_t;
    void assign(const metadata_output_path_query_t& query);
};
struct metadata_output_path_text_t {
    std::string path;
    std::string asset_directory;
};
struct metadata_channel_summary_key_t {
    std::optional<channel_map_report_t> report;
    std::optional<channel_map_diagnostic_t> diagnostic;
    [[nodiscard]] auto operator==(const channel_map_preview_view_t& preview) const -> bool;
    [[nodiscard]] static auto from(const channel_map_preview_view_t& preview)
        -> metadata_channel_summary_key_t;
    void assign(const channel_map_preview_view_t& preview);
};
struct metadata_model_header_t {
    std::size_t summary_size{}, field_count{};
    bool search_tags_editable{};
    auto operator==(const metadata_model_header_t&) const -> bool = default;
};
struct metadata_static_header_t {
    asset_id_t music_asset, next_import_asset;
    std::size_t resource_count{}, channel_count{}, lane_profile_current{};
    std::uint64_t divisions{};
    object_id_t channel_layout;
    bool channel_editable{};
    auto operator==(const metadata_static_header_t&) const -> bool = default;
};
using metadata_source_key_t = std::tuple<const song_document_t*, const chart_document_t*, document_stamp_t, std::uint64_t>;

struct metadata_projection_cache_t {
    std::size_t static_rows_refills{};
    std::size_t model_rows_refills{};
    keyed_cache_t<std::tuple<const song_document_t*, document_stamp_t, chart_id_t, std::uint64_t>, metadata_model_header_t> model_rows;
    keyed_cache_t<std::tuple<metadata_source_key_t, std::optional<asset_id_t>, bool>, metadata_static_header_t> static_rows;
    void invalidate_rows() { model_rows.invalidate(); static_rows.invalidate(); }
    void invalidate_asset_paths() { inheritance.invalidate(); static_rows.invalidate(); }
    keyed_cache_t<metadata_source_key_t, resolved_metadata_t> inheritance;
    keyed_cache_t<std::filesystem::path, std::string> project_directory;
    keyed_cache_t<std::filesystem::path, std::string> selected_music_source;
    keyed_cache_t<std::filesystem::path, std::string> copy_preview_source;
    std::vector<keyed_cache_t<metadata_output_path_key_t, metadata_output_path_text_t>> output_paths;
    keyed_cache_t<metadata_channel_summary_key_t, std::string> channel_summary;
    keyed_cache_t<std::array<std::size_t, 3>, std::string> lane_summary;
    keyed_cache_t<metadata_source_key_t,
                  std::vector<lane_input_profile_t>> lane_targets;
};

} // namespace wormedit::sys
