#pragma once

#include "sys/sound_list_view.h"
#include <wormedit/core/command.h>
#include <wormedit/core/keyed_cache.h>
#include <tuple>

namespace wormedit::sys {
struct sound_picker_row_t {
    resource_id_t resource;
    std::string slot, name;
    bool missing{};
};
struct sound_picker_cache_t;

struct sound_picker_view_result_t {
    std::vector<sound_picker_row_t> rows;
    std::size_t total{};

    // Full dock catalog, irrespective of its filters/sections/window. ASCII case
    // folding preserves other UTF-8 bytes. Exact/prefix/substring precede subsequences.
    [[nodiscard]] static auto from(sound_picker_cache_t& cache,
                                   const song_document_t& document, const chart_document_t& chart, std::string_view query)
        -> const sound_picker_view_result_t&;
};
struct sound_picker_cache_t {
    struct query_t {
        std::size_t catalog_generation;
        std::string_view text;
        std::string& normalized;
    };
    struct query_key_t {
        std::size_t catalog_generation{};
        std::string normalized;
        auto operator==(const query_key_t&) const -> bool = default;
        auto operator==(const query_t& query) const -> bool;
        static auto from(const query_t& query) -> query_key_t;
        void assign(const query_t& query);
    };
    keyed_cache_t<std::tuple<const song_document_t*, const chart_document_t*, chart_stamp_t>,
                  std::vector<sound_picker_row_t>> catalog;
    keyed_cache_t<query_key_t, sound_picker_view_result_t> matches;
};

} // namespace wormedit::sys
