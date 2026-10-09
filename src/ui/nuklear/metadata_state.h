#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
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
    // Single-field exact rationals, parsed by worm_ui_nuklear_parse_metadata_rational().
    std::array<char, 64> chart_rating_text{};
    std::array<char, 64> audio_offset_text{};
    // Divisions per standard measure for the Redivide action, parsed as an exact integer.
    std::array<char, 32> chart_resolution_text{};
    // Scratch text for action payloads, which still carry numerator/denominator strings.
    std::array<char, 32> action_numerator_text{};
    std::array<char, 32> action_denominator_text{};
    // Channel-map table text, four per lane (visible, invisible, long_note, mine). Reloaded from
    // the view when the document revision or layout segment changes.
    static constexpr std::size_t channel_map_text_bytes = 64;
    std::vector<std::array<std::array<char, channel_map_text_bytes + 1>, 4>> channel_map_texts;
    std::vector<const char*> channel_map_text_pointers;
    std::vector<std::uint64_t> channel_map_lane_ids;
    std::uint64_t channel_map_buffer_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t channel_map_buffer_segment{};
    bool channel_map_buffers_valid{};
    // Metadata multi-select of difficulties; reset with the rest of the state.
    std::vector<std::uint64_t> batch_selected_charts;
    std::vector<std::uint64_t> batch_action_chart_ids;
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
    bool format_add_chart_scoped{};
    std::uint8_t format_add_family{};
    bool format_buffers_valid{};
    std::vector<char> format_name;
    std::vector<char> format_value;
    static constexpr std::size_t bms_field_count = 14;
    static constexpr std::size_t bms_field_value_bytes = 1024;
    std::array<std::array<char, bms_field_value_bytes + 1>, bms_field_count> bms_fields{};
    std::array<std::size_t, bms_field_count> bms_field_indices{};
    std::array<std::size_t, bms_field_count> bms_field_occurrences{};
    std::array<bool, bms_field_count> bms_field_valid{};
    static constexpr std::size_t bmson_field_count = 5;
    static constexpr std::size_t bmson_field_value_bytes = 1024;
    std::array<std::array<char, bmson_field_value_bytes + 1>, bmson_field_count> bmson_fields{};
    std::array<std::size_t, bmson_field_count> bmson_field_indices{};
    std::array<bool, bmson_field_count> bmson_field_chart_scoped{};
    std::array<bool, bmson_field_count> bmson_field_valid{};
    std::string bmson_action_json_value;
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

struct worm_ui_nuklear_metadata_rational_t {
    std::int64_t numerator{};
    std::uint64_t denominator{1};
    friend auto operator==(const worm_ui_nuklear_metadata_rational_t&,
                           const worm_ui_nuklear_metadata_rational_t&) -> bool = default;
};

// Parses "3", "-1/3", "+0.125", "1.50" (-> 150/100) exactly, without reducing.
// With allow_milliseconds, a trailing "s" or "ms" unit is accepted ("-125 ms" -> -125/1000,
// "12.5ms" -> 125/10000, "0.5 s" -> 5/10). Surrounding whitespace is ignored. Returns nullopt
// for empty text, a zero denominator, malformed input, or int64/uint64 overflow.
[[nodiscard]] auto worm_ui_nuklear_parse_metadata_rational(std::string_view text,
                                                           bool allow_milliseconds)
    -> std::optional<worm_ui_nuklear_metadata_rational_t>;

// Formats so that parsing the result yields the same numerator and denominator:
// denominator 1 -> "3"; a power-of-ten denominator -> decimal ("-0.125", "1.50");
// anything else -> "n/d" ("2/4").
[[nodiscard]] auto worm_ui_nuklear_format_metadata_rational(worm_ui_nuklear_metadata_rational_t value)
    -> std::string;

[[nodiscard]] auto worm_ui_nuklear_metadata_file_dialog_begin() -> std::optional<std::uint64_t>;
void worm_ui_nuklear_metadata_file_dialog_complete(std::uint64_t generation, std::string path,
                                                   std::string error, bool canceled);
[[nodiscard]] auto worm_ui_nuklear_metadata_file_dialog_take()
    -> std::optional<worm_ui_nuklear_metadata_file_dialog_result_t>;
[[nodiscard]] auto worm_ui_nuklear_metadata_file_dialog_open() -> bool;
