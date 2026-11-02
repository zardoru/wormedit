#pragma once

#include "ui/frontend/storyboard.h"
#include <wormedit/core/document.h>

namespace wormedit::test {
inline auto timeline_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 3, .storyboards = {{.id = storyboard_id_t{11},
        .osbx = osbx_document_t{.nodes = {
            {.id = "first", .ops = {{.id = "pivot", .kind = osbx_transform_kind_t::pivot,
                .children = {{.id = "rotate", .kind = osbx_transform_kind_t::rotate,
                    .track = {.default_value = {0}, .keys = {
                        {.id = "key-a", .time_ms = -10, .value = {0}, .easing = osbx_step_easing_t{}},
                        {.id = "key-b", .time_ms = 20, .value = {90}}}}}}}},
                .tracks = {.opacity = {.default_value = {1}, .keys = {
                    {.id = "fade-a", .time_ms = 0, .value = {0}, .easing = osbx_osb_easing_t{.value = 3}},
                    {.id = "fade-b", .time_ms = 100, .value = {1}}}}}},
            {.id = "second"}}}}}};
}
inline auto timeline_root() -> ui::storyboard_view_t {
    ui::storyboard_view_t root{.visible = true, .project = song_id_t{1}, .revision = 3,
        .session_generation = 7};
    root.state.selection = {.storyboard = storyboard_id_t{11}, .nodes = {"second", "first"},
        .keys = {"key-a", "fade-b"}};
    return root;
}
} // namespace wormedit::test
