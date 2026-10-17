#pragma once

#include <wormedit/core/interaction_state.h>

#include "interaction_context.h"

namespace wormedit::interaction_detail {

struct cursor_navigation_context_t {
    editor_edit_context_t& edit;
    const std::uint64_t& absolute;
};
[[nodiscard]] auto keep_cursor_visible(const chart_document_t& chart, const measure_index_t& index,
                                       editor_interaction_state_t& candidate) -> bool;
[[nodiscard]] auto move_edit_cursor(cursor_navigation_context_t& context,
                                    const move_edit_cursor_action_t& action)
    -> editor_interaction_result_t;
[[nodiscard]] auto page_edit_cursor(cursor_navigation_context_t& context,
                                    const page_edit_cursor_action_t& action)
    -> editor_interaction_result_t;
[[nodiscard]] auto jump_edit_cursor(cursor_navigation_context_t& context,
                                    const jump_edit_cursor_action_t& action)
    -> editor_interaction_result_t;
[[nodiscard]] auto select_bms_canvas_measure(cursor_navigation_context_t& context,
                                             const select_bms_canvas_measure_action_t& action)
    -> editor_interaction_result_t;

} // namespace wormedit::interaction_detail
