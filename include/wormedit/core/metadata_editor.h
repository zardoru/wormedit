#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace wormedit {

struct metadata_edit_limits_t {
    std::size_t maximum_title_bytes{16U * 1024U};
    std::size_t maximum_artist_bytes{16U * 1024U};
    std::size_t maximum_chart_text_bytes{16U * 1024U};
    std::size_t maximum_search_tags{4'096};
    std::size_t maximum_search_tag_bytes{4U * 1024U};
    std::size_t maximum_fields_per_group{16'384};
    std::size_t maximum_field_name_bytes{1U * 1024U};
    std::size_t maximum_field_value_bytes{1U * 1024U * 1024U};
    std::size_t maximum_output_path_bytes{4U * 1024U};
    std::size_t maximum_total_metadata_bytes{32U * 1024U * 1024U};
};

struct set_song_metadata_action_t {
    std::string title;
    std::string artist;
    std::vector<std::string> ordered_search_tags;
};

struct set_chart_metadata_action_t {
    chart_id_t chart;
    std::string name;
    std::string author;
    std::optional<signed_rational_t> rating;
};

struct set_output_group_path_action_t {
    std::size_t group_index{};
    std::string relative_output_path;
};

// Resolves a dropped existing file or directory inside a saved project to the
// relative path accepted by the output-group editor. Symlinks are resolved
// before containment is checked.
[[nodiscard]] auto output_path_from_drop(const std::filesystem::path& project_directory,
                                         const std::filesystem::path& dropped_path,
                                         metadata_edit_limits_t limits = {})
    -> std::optional<std::filesystem::path>;

struct chart_rating_edit_t {
    chart_id_t chart;
    std::optional<signed_rational_t> rating;
};

inline constexpr std::size_t metadata_rating_component_max_bytes = 21;

// Parses exact base-10 components without reducing or otherwise changing the
// signed rational. Numerators are signed 64-bit values; denominators are
// positive 64-bit values.
[[nodiscard]] auto parse_metadata_rating(std::string_view numerator, std::string_view denominator)
    -> std::optional<signed_rational_t>;

// Builds a rating-only edit. `unset` is the explicit clear operation; when
// false, both numeric components must parse without truncation or overflow.
[[nodiscard]] auto make_chart_rating_metadata_edit(chart_id_t chart, bool unset,
                                                   std::string_view numerator,
                                                   std::string_view denominator)
    -> std::optional<chart_rating_edit_t>;

// A null chart selects song-scoped metadata; otherwise the field belongs to
// that difficulty. Indexes address exact occurrences, preserving duplicates.
struct format_metadata_target_t {
    std::optional<chart_id_t> chart;
    format_family_t family{format_family_t::native_project};
};

struct insert_format_metadata_field_action_t {
    format_metadata_target_t target;
    std::size_t index{};
    std::string name;
    std::string value;
};

struct replace_format_metadata_field_action_t {
    format_metadata_target_t target;
    std::size_t index{};
    std::string name;
    std::string value;
};

struct erase_format_metadata_field_action_t {
    format_metadata_target_t target;
    std::size_t index{};
};

using metadata_edit_action_t =
    std::variant<set_song_metadata_action_t, set_chart_metadata_action_t,
                 set_output_group_path_action_t,
                 insert_format_metadata_field_action_t, replace_format_metadata_field_action_t,
                 erase_format_metadata_field_action_t>;

// Applies one mutation through document history. New fields may be inserted in
// the authored suffix, not ahead of retained source identities. A semantic
// no-op returns applied=false without a diagnostic or revision change.
[[nodiscard]] auto apply_metadata_edit(document_history_t& history, std::uint64_t expected_revision,
                                       const metadata_edit_action_t& action,
                                       metadata_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
