#pragma once
#include "ui/frontend.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

struct nk_context;
struct nk_command_buffer;
struct nk_rect;

inline auto compose_area_y(const WormUiComposeView& view, std::uint64_t row) -> double {
    if (view.area_rows_per_measure == 0 || !std::isfinite(view.pixels_per_standard_measure))
        return 0;
    const auto y = static_cast<double>(row) * view.pixels_per_standard_measure /
                       static_cast<double>(view.area_rows_per_measure) -
                   view.area_scroll_pixels;
    return view.downscroll ? view.viewport_height_pixels - y : y;
}

struct ComposeAreaCapture {
    bool active{};
    bool begin_edge{};
    bool virtual_range{};
    std::uint64_t chart{}, revision{}, begin{}, end{};
    auto current(const WormUiComposeView& view) const -> bool {
        return chart == view.chart_id && revision == view.document_revision;
    }
    void start(const WormUiComposeView& view, bool moving_begin) {
        *this = {true,
                 moving_begin,
                 view.area_range_virtual,
                 view.chart_id,
                 view.document_revision,
                 view.area_range_begin_row,
                 view.area_range_end_row};
    }
    void restore(wormedit::ui::workspace_action_t& action) const {
        action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE;
        action.compose_input_chart_id = wormedit::chart_id_t{chart};
        action.compose_input_document_revision = revision;
        action.compose_area_begin_row = begin;
        action.compose_area_end_row = end;
        action.compose_area_virtual = virtual_range;
    }
};

auto compose_area_band_rect(const WormUiComposeView&, struct nk_rect) -> struct nk_rect;
void process_compose_area_menu(nk_context*, const WormUiComposeView&, wormedit::ui::workspace_action_t&,
                               struct nk_rect, double input_right, bool focused);
auto compose_area_pointer_consumed() -> bool;
auto compose_area_menu_open() -> bool;
auto compose_area_menu_consumed() -> bool;
void cancel_compose_area_capture(wormedit::ui::workspace_action_t&);
void reset_compose_area_menu();
auto compose_area_capture_active() -> bool;

struct ComposeAreaShortcutContext {
    bool compose_active{}, focused{}, blocked{}, text_entry{}, capture{}, band{}, escape{};
};
inline auto compose_area_consumes_shortcut(ComposeAreaShortcutContext context) -> bool {
    return context.compose_active && context.focused && !context.blocked && !context.text_entry &&
           (context.capture || (context.band && context.escape));
}
auto compose_area_band_visible() -> bool;
