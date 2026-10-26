#pragma once
#include <wormedit/core/chart_clipboard.h>

namespace wormedit {
[[nodiscard]] auto append_clipboard_recipe(chart_clipboard_t&, const chart_document_t&,
    const measure_index_t&, recipe_id_t) -> bool;
[[nodiscard]] auto place_clipboard_recipes(const song_document_t&, const chart_document_t&,
    const measure_index_t&, const chart_clipboard_t&, std::uint64_t anchor,
    chart_paste_proposal_t&) -> result_t<void, std::string>;
void stage_clipboard_recipes(song_document_t&, chart_id_t, const chart_paste_proposal_t&);
// Shared exact arithmetic for ordinary objects and recipe placements.
[[nodiscard]] auto exact_clipboard_rows(rational_t, std::uint64_t resolution)
    -> std::optional<std::uint64_t>;
} // namespace wormedit
