#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

struct worm_ui_nuklear_metadata_state_t {
    static constexpr std::size_t visible_tag_count = 64;

    std::uint64_t buffer_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t buffer_chart_id{};
    bool buffer_has_chart{};
    bool song_buffers_valid{};
    bool chart_buffers_valid{};
    bool tag_buffers_valid{};
    std::array<char, WORM_UI_METADATA_TITLE_BYTES + 1> title{};
    std::array<char, WORM_UI_METADATA_TITLE_BYTES + 1> artist{};
    std::array<char, WORM_UI_METADATA_CHART_TEXT_BYTES + 1> chart_name{};
    std::array<char, WORM_UI_METADATA_CHART_TEXT_BYTES + 1> chart_author{};
    std::array<char, 64> chart_rating_numerator{};
    std::array<char, 64> chart_rating_denominator{};
    std::array<char, 64> audio_offset_numerator{};
    std::array<char, 64> audio_offset_denominator{};
    std::vector<std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1>> search_tags;
    std::vector<const char*> search_tag_pointers;
    std::array<char, WORM_UI_METADATA_SEARCH_TAG_BYTES + 1> new_search_tag{};
    std::size_t selected_format_field{std::numeric_limits<std::size_t>::max()};
    bool format_target_valid{};
    bool format_target_chart_scoped{};
    std::uint8_t format_target_family{};
    std::size_t format_target_index{};
    std::size_t format_target_field_count{};
    bool format_insert_mode{};
    bool format_buffers_valid{};
    std::vector<char> format_name;
    std::vector<char> format_value;
    static constexpr std::size_t bms_field_count = 14;
    static constexpr std::size_t bms_field_value_bytes = 1024;
    std::array<std::array<char, bms_field_value_bytes + 1>, bms_field_count> bms_fields{};
    std::array<std::size_t, bms_field_count> bms_field_indices{};
    std::array<std::size_t, bms_field_count> bms_field_occurrences{};
    std::array<bool, bms_field_count> bms_field_valid{};
    std::size_t selected_output_group_index{std::numeric_limits<std::size_t>::max()};
    std::uint64_t output_path_buffer_revision{std::numeric_limits<std::uint64_t>::max()};
    std::size_t output_path_buffer_group_index{std::numeric_limits<std::size_t>::max()};
    std::array<char, WORM_UI_METADATA_OUTPUT_PATH_BYTES + 1> output_path{};
    std::array<char, WORM_UI_METADATA_OUTPUT_PATH_BYTES + 1> output_path_source{};
    bool output_path_buffer_valid{};
    std::array<char, WORM_UI_COMPOSE_IMPORT_PATH_BYTES + 1> external_music_path{};
    std::array<char, 192> external_music_error{};
    struct drop_rectangle_t {
        float x{};
        float y{};
        float width{};
        float height{};
    } project_drop_rectangle, export_drop_rectangle, output_path_drop_rectangle;
    std::size_t export_drop_group_index{};
    std::size_t output_path_drop_group_index{};
};

[[nodiscard]] auto worm_ui_nuklear_metadata_state() noexcept -> worm_ui_nuklear_metadata_state_t&;

void worm_ui_nuklear_reset_metadata_state();
void worm_ui_nuklear_clear_metadata_drop_targets();
void worm_ui_nuklear_set_metadata_drop_target(
    WormUiMetadataDropTarget target, worm_ui_nuklear_metadata_state_t::drop_rectangle_t rect);
[[nodiscard]] auto worm_ui_nuklear_metadata_drop_target(float x, float y)
    -> WormUiMetadataDropTarget;

struct worm_ui_nuklear_metadata_file_dialog_result_t {
    std::string path;
    std::string error;
    bool canceled{};
};

[[nodiscard]] auto worm_ui_nuklear_metadata_file_dialog_begin() -> std::optional<std::uint64_t>;
void worm_ui_nuklear_metadata_file_dialog_complete(std::uint64_t generation, std::string path,
                                                   std::string error, bool canceled);
[[nodiscard]] auto worm_ui_nuklear_metadata_file_dialog_take()
    -> std::optional<worm_ui_nuklear_metadata_file_dialog_result_t>;
[[nodiscard]] auto worm_ui_nuklear_metadata_file_dialog_open() -> bool;
