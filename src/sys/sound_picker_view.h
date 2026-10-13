#pragma once

#include "sys/sound_list_view.h"

namespace wormedit::sys {
struct sound_picker_row_t {
    resource_id_t resource;
    std::string slot, name;
    bool missing{};
};
struct sound_picker_result_t {
    std::vector<sound_picker_row_t> rows;
    std::size_t total{};
};
struct sound_picker_cache_t {
    std::uint64_t project{}, revision{}, chart{};
    const song_document_t* document{};
    const chart_document_t* chart_address{};
    bool valid{}, query_valid{};
    std::string query;
    std::vector<sound_picker_row_t> catalog;
    sound_picker_result_t result;
    std::size_t catalog_builds{}, query_builds{};
};
// Full dock catalog, irrespective of its filters/sections/window. ASCII case
// folding preserves other UTF-8 bytes. Exact/prefix/substring precede subsequences.
[[nodiscard]] auto build_sound_picker_view(sound_picker_cache_t& cache,
    const song_document_t& document, const chart_document_t& chart, std::string_view query)
    -> const sound_picker_result_t&;
} // namespace wormedit::sys
