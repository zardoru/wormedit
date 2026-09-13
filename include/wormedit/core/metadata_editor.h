#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
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
