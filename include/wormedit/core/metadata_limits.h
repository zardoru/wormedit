#pragma once

#include <cstddef>

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

inline constexpr metadata_edit_limits_t default_metadata_edit_limits{};

} // namespace wormedit
