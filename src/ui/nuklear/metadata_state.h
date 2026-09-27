#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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
    std::size_t selected_output_group_index{std::numeric_limits<std::size_t>::max()};
};

[[nodiscard]] auto worm_ui_nuklear_metadata_state() noexcept -> worm_ui_nuklear_metadata_state_t&;

void worm_ui_nuklear_reset_metadata_state();
