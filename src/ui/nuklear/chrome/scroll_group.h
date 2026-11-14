#pragma once

#include "wormedit/nuklear_config.h"

#include <string>
#include <unordered_map>
#include <vector>

// Nuklear reserves the vertical scrollbar's width (and the horizontal one's height) inside every
// group and list view that does not set NK_WINDOW_NO_SCROLLBAR, whether or not the content
// overflows. A short list therefore shows an empty gutter on its right and bottom. These wrappers
// measure the content when the group ends and, from the next frame on, drop the scrollbar while
// the content fits. Prefer them to raw nk_group_begin / nk_list_view_begin for anything that can
// scroll; tests/scripts/check_raw_scroll_groups.py keeps the raw calls from spreading.
//
// The decision lags by one frame (the first frame of a group still reserves the gutter), and the
// content is judged against the full height, so removing the gutter cannot flip it back.
namespace wormedit::ui::chrome {

namespace scroll_group_detail {

struct state_t {
    std::unordered_map<std::string, bool> fits;
    std::vector<std::string> open;
};

inline auto state() -> state_t& {
    static state_t value;
    return value;
}

inline auto flags_for(const char* id, nk_flags flags) -> nk_flags {
    const auto found = state().fits.find(id);
    if (found != state().fits.end() && found->second) flags |= NK_WINDOW_NO_SCROLLBAR;
    return flags;
}

// content_height is measured from the group's top; the visible height ignores the footer strip
// that Nuklear takes for the horizontal scrollbar, so the verdict is the same with or without it.
inline void record(nk_context* context, const std::string& id, float content_height) {
    const auto* layout = context->current->layout;
    const float visible = layout->bounds.h + layout->footer_height;
    state().fits[id] = content_height <= visible + 0.5f;
}

}  // namespace scroll_group_detail

// content_height, when the caller knows the height of the rows it is about to add (without the
// group's own padding and border), settles the scrollbar on the first frame instead of the next.
inline auto scroll_group_begin(nk_context* context, const char* id, nk_flags flags = 0,
                               float content_height = -1.0f) -> bool {
    if (content_height >= 0.0f) {
        const auto& style = context->style.window;
        const float frame = 2.0f * (style.group_padding.y + ((flags & NK_WINDOW_BORDER) ? style.group_border : 0.0f));
        scroll_group_detail::state().fits[id] = content_height + frame <= nk_widget_bounds(context).h + 0.5f;
    }
    const bool open = nk_group_begin(context, id, scroll_group_detail::flags_for(id, flags)) != 0;
    if (open) scroll_group_detail::state().open.emplace_back(id);
    return open;
}

inline void scroll_group_end(nk_context* context) {
    auto& open = scroll_group_detail::state().open;
    if (!open.empty()) {
        const auto* layout = context->current->layout;
        scroll_group_detail::record(context, open.back(),
                                    layout->at_y + layout->row.height - layout->bounds.y);
        open.pop_back();
    }
    nk_group_end(context);
}

inline auto scroll_list_view_begin(nk_context* context, nk_list_view* view, const char* id,
                                   nk_flags flags, int row_height, int row_count) -> bool {
    const bool open = nk_list_view_begin(context, view, id, scroll_group_detail::flags_for(id, flags),
                                         row_height, row_count) != 0;
    if (open) scroll_group_detail::state().open.emplace_back(id);
    return open;
}

inline void scroll_list_view_end(nk_list_view* view) {
    auto& open = scroll_group_detail::state().open;
    if (!open.empty() && view && view->ctx) {
        scroll_group_detail::record(view->ctx, open.back(), static_cast<float>(view->total_height));
        open.pop_back();
    }
    nk_list_view_end(view);
}

}  // namespace wormedit::ui::chrome
