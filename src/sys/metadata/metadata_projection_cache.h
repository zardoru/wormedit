#pragma once

#include <wormedit/core/channel_map_editor.h>
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
struct metadata_projection_cache_t {
    keyed_cache_t<std::filesystem::path, std::string> project_directory;
    keyed_cache_t<std::filesystem::path, std::string> selected_music_source;
    keyed_cache_t<std::filesystem::path, std::string> copy_preview_source;
    std::vector<keyed_cache_t<metadata_output_path_key_t, metadata_output_path_text_t>> output_paths;
    keyed_cache_t<metadata_channel_summary_key_t, std::string> channel_summary;
    keyed_cache_t<std::array<std::size_t, 3>, std::string> lane_summary;
    keyed_cache_t<std::tuple<document_stamp_t, chart_id_t, std::uint64_t>,
                  std::vector<lane_input_profile_t>> lane_targets;
};

} // namespace wormedit::sys
