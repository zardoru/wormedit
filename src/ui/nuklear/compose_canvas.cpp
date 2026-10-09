#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/tooltip.h"
#include "ui/nuklear/arrow_glyph.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/compose_bookmark_label.h"
#include "ui/nuklear/compose_bookmark_rename.h"
#include "ui/nuklear/compose_input_internal.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/sdl_backend.h"

#include "wormedit/nuklear_config.h"
#include <wormedit/core/document.h>
#include <wormedit/core/interaction.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>

namespace {
wormedit::ui::field::renderer_t* tile_renderer_override{};
std::optional<WormUiTestingRect> last_compose_viewport_for_testing;
std::optional<WormUiTestingRect> quantize_overlay_viewport;
bool quantize_ghosts_drawn_this_frame{};
WormUiTestingComposeGhostDrawState quantize_ghost_draw_state{};
auto paint_field(nk_context* context) -> wormedit::ui::field::renderer_t& {
    return tile_renderer_override ? *tile_renderer_override :
        wormedit::ui::nuklear::compose_field(context);
}
struct tile_renderer_scope_t {
    wormedit::ui::field::renderer_t* previous{};
    explicit tile_renderer_scope_t(wormedit::ui::field::renderer_t& renderer)
        : previous(tile_renderer_override) { tile_renderer_override=&renderer; }
    ~tile_renderer_scope_t() { tile_renderer_override=previous; }
};
struct static_hover_records_t {
    std::vector<std::size_t> notes, holds;
};
struct static_paint_indices_t {
    std::array<wormedit::ui::field::static_interval_index_t,11> passes;
    std::uint64_t style{}, generation{};
    std::size_t bytes{}, scanned{}, extracted{};
    double glyph_guard{32};
    std::vector<std::size_t> query_scratch;
    std::unordered_map<std::uint64_t,static_hover_records_t> notes_by_id;
    template<class T, class Bounds>
    void rebuild_pass(unsigned pass, const T* records, std::size_t count, Bounds bounds) {
        std::vector<wormedit::ui::field::static_interval_t> intervals;
        intervals.reserve(count);
        for (std::size_t index=0; index<count; ++index) {
            const auto [begin,end]=bounds(records[index]);
            intervals.push_back({begin,end,index});
        }
        passes[pass].rebuild(intervals);
        bytes+=passes[pass].accounted_bytes();
    }
    void refresh(const WormUiComposeView& view) {
        scanned=0; extracted=0;
        if (!view.static_scene || (style==view.static_scene_style_generation &&
            generation==view.static_scene_source_generation)) return;
        style=view.static_scene_style_generation;
        generation=view.static_scene_source_generation;
        bytes=0;
        const auto& source=*view.static_scene;
        query_scratch.resize(std::max({source.band_count,source.lane_count,source.bgm_lane_count,
            source.grid_line_count,source.timing_marker_count,source.hold_body_count,
            source.bgm_event_count,source.bga_event_count,source.note_endpoint_count}));
        bytes+=query_scratch.capacity()*sizeof(std::size_t);
        glyph_guard=32;
        for (std::size_t index=0; index<source.lane_count; ++index)
            glyph_guard=std::max(glyph_guard,
                (source.lanes[index].bounds.right-source.lanes[index].bounds.left)*.5+20);
        const auto rect=[](const auto& value) { return std::pair{value.bounds.top,value.bounds.bottom}; };
        rebuild_pass(2,source.bands,source.band_count,rect);
        rebuild_pass(3,source.lanes,source.lane_count,rect);
        rebuild_pass(4,source.bgm_lanes,source.bgm_lane_count,rect);
        rebuild_pass(5,source.grid_lines,source.grid_line_count,
            [](const auto& line) { return std::pair{line.y_pixels-16,line.y_pixels+16}; });
        rebuild_pass(6,source.timing_markers,source.timing_marker_count,
            [](const auto& marker) { return std::pair{marker.marker_y_pixels-16,marker.marker_y_pixels+16}; });
        rebuild_pass(7,source.hold_bodies,source.hold_body_count,rect);
        rebuild_pass(8,source.bgm_events,source.bgm_event_count,rect);
        rebuild_pass(9,source.note_endpoints,source.note_endpoint_count,rect);
        rebuild_pass(10,source.bga_events,source.bga_event_count,rect);
        notes_by_id.clear();
        notes_by_id.reserve(source.note_endpoint_count+source.hold_body_count);
        for (std::size_t index=0; index<source.note_endpoint_count; ++index)
            notes_by_id[source.note_endpoints[index].note_id].notes.push_back(index);
        for (std::size_t index=0; index<source.hold_body_count; ++index)
            notes_by_id[source.hold_bodies[index].note_id].holds.push_back(index);
        bytes+=notes_by_id.bucket_count()*sizeof(void*);
        for (const auto& [id,records]:notes_by_id)
            bytes+=sizeof(id)+sizeof(records)+6*sizeof(void*)+
                (records.notes.capacity()+records.holds.capacity())*sizeof(std::size_t);
    }
};
static_paint_indices_t static_paint_indices;
// Source publication caps the total record count at 32768. Together these
// shallow scalar copies stay below 3 MiB; bounded geometry stays below 4 MiB.
static_assert(sizeof(WormUiComposeNoteView)<=96 && sizeof(WormUiComposeHoldView)<=96 &&
    sizeof(WormUiTimingMarkerView)<=96 && sizeof(WormUiComposeBgmEventView)<=96 &&
    sizeof(WormUiComposeLaneView)<=96 && sizeof(WormUiComposeBandView)<=96 &&
    sizeof(WormUiComposeBgmLaneView)<=96 && sizeof(WormUiComposeBgaEventView)<=96 &&
    sizeof(WormUiComposeGridLineView)<=96);
struct static_paint_storage_t {
    WormUiComposeView view;
    std::vector<WormUiComposeBandView> bands;
    std::vector<WormUiComposeLaneView> lanes;
    std::vector<WormUiComposeGridLineView> grid;
    std::vector<WormUiTimingMarkerView> timing;
    std::vector<WormUiComposeHoldView> holds;
    std::vector<WormUiComposeBgmLaneView> bgm_lanes;
    std::vector<WormUiComposeBgmEventView> bgm;
    std::vector<WormUiComposeBgaEventView> bga;
    std::vector<WormUiComposeNoteView> notes;
    template<class T>
    void select(unsigned pass, const T* source, std::vector<T>& target,
                const T*& pointer, std::size_t& count, double begin, double end,
                double offset) {
        const auto result=static_paint_indices.passes[pass].query_into(
            begin,end,static_paint_indices.query_scratch);
        static_paint_indices.scanned+=result.scanned;
        // Scratch holds at least the source record count for every pass.
        // Overflow nevertheless preserves output/order with the compatible query.
        const auto fallback=result.overflow ? static_paint_indices.passes[pass].query(begin,end)
                                            : wormedit::common::interval_query_t{};
        const auto indices=result.overflow ? std::span<const std::size_t>(fallback.indices)
            : std::span<const std::size_t>(static_paint_indices.query_scratch).first(result.count);
        static_paint_indices.extracted+=indices.size();
        target.reserve(indices.size());
        for (const auto index:indices) {
            auto value=source[index];
            if constexpr (requires { value.bounds; }) {
                value.bounds.top+=offset; value.bounds.bottom+=offset;
                if constexpr (std::is_same_v<T, WormUiComposeBgmEventView>) {
                    value.head_top_pixels += static_cast<float>(offset);
                    value.head_bottom_pixels += static_cast<float>(offset);
                }
            } else if constexpr (requires { value.y_pixels; }) value.y_pixels+=offset;
            else value.marker_y_pixels+=offset;
            target.push_back(value);
        }
        pointer=target.data(); count=target.size();
    }
    static_paint_storage_t(const WormUiComposeView& current, unsigned pass,
                           double origin, double height, double scale) : view(current) {
        const auto& source=*current.static_scene;
        const auto offset=current.static_scene_world_origin_pixels-origin/scale;
        const auto margin=std::max(32.0,static_paint_indices.glyph_guard);
        const auto begin=-offset-margin, end=-offset+height/scale+margin;
        view.viewport_height_pixels=height/scale;
        switch (pass) {
        case 2:
            select(2,source.bands,bands,view.bands,view.band_count,begin,end,offset);
            select(3,source.lanes,lanes,view.lanes,view.lane_count,begin,end,offset);
            select(4,source.bgm_lanes,bgm_lanes,view.bgm_lanes,view.bgm_lane_count,begin,end,offset);
            break;
        case 3: select(pass,source.lanes,lanes,view.lanes,view.lane_count,begin,end,offset); break;
        case 4: select(pass,source.bgm_lanes,bgm_lanes,view.bgm_lanes,view.bgm_lane_count,begin,end,offset); break;
        case 5: select(pass,source.grid_lines,grid,view.grid_lines,view.grid_line_count,begin,end,offset); break;
        case 6: select(pass,source.timing_markers,timing,view.timing_markers,view.timing_marker_count,begin,end,offset); break;
        case 7:
            select(7,source.hold_bodies,holds,view.hold_bodies,view.hold_body_count,begin,end,offset);
            select(8,source.bgm_events,bgm,view.bgm_events,view.bgm_event_count,begin,end,offset);
            break;
        case 8: select(pass,source.bgm_events,bgm,view.bgm_events,view.bgm_event_count,begin,end,offset); break;
        case 9: select(pass,source.note_endpoints,notes,view.note_endpoints,view.note_endpoint_count,begin,end,offset); break;
        case 10: select(pass,source.bga_events,bga,view.bga_events,view.bga_event_count,begin,end,offset); break;
        default: break;
        }
    }
};
}

void reset_compose_canvas_viewport_for_testing() {
    last_compose_viewport_for_testing.reset();
    quantize_overlay_viewport.reset();
    quantize_ghosts_drawn_this_frame = false;
    quantize_ghost_draw_state = {};
    reset_compose_quantize_section_visibility();
}

static auto field_color(nk_color) -> wormedit::ui::field::color_t;
static void field_line(const ComposePaintContext&, float, float, float, float, float, nk_color,
                       wormedit::ui::field::layer_t);

static auto clip_quantize_line(float& x0, float& y0, float& x1, float& y1,
                               struct nk_rect clip) -> bool {
    const auto dx = x1 - x0;
    const auto dy = y1 - y0;
    float low = 0.0f, high = 1.0f;
    const std::array<float, 4> p{-dx, dx, -dy, dy};
    const std::array<float, 4> q{x0 - clip.x, clip.x + clip.w - x0,
                                 y0 - clip.y, clip.y + clip.h - y0};
    for (std::size_t index = 0; index < p.size(); ++index) {
        if (std::abs(p[index]) < 0.0001f) {
            if (q[index] < 0.0f) return false;
            continue;
        }
        const auto ratio = q[index] / p[index];
        if (p[index] < 0.0f) low = std::max(low, ratio);
        else high = std::min(high, ratio);
        if (low > high) return false;
    }
    const auto start_x = x0, start_y = y0;
    x0 = start_x + low * dx; y0 = start_y + low * dy;
    x1 = start_x + high * dx; y1 = start_y + high * dy;
    return true;
}

static auto draw_quantize_dashed_connector(const ComposePaintContext& paint, float x0, float y0,
                                           float x1, float y1, nk_color color) -> bool {
    if (!clip_quantize_line(x0, y0, x1, y1, paint.viewport)) return false;
    const auto dx = x1 - x0, dy = y1 - y0;
    const auto length = std::hypot(dx, dy);
    if (!(length > 0.0f)) return false;
    constexpr float dash = 5.0f, gap = 4.0f;
    bool emitted{};
    for (float offset = 0.0f; offset < length; offset += dash + gap) {
        const auto end = std::min(length, offset + dash);
        field_line(paint, x0 + dx * offset / length, y0 + dy * offset / length,
                   x0 + dx * end / length, y0 + dy * end / length, 1.5f, color, 10);
        emitted = true;
        ++quantize_ghost_draw_state.emitted_segments;
        quantize_ghost_draw_state.red = color.r;
        quantize_ghost_draw_state.green = color.g;
        quantize_ghost_draw_state.blue = color.b;
        quantize_ghost_draw_state.alpha = color.a;
    }
    return emitted;
}

static auto quantize_ghost_color(const WormUiComposeQuantizeGhostView& ghost, bool tail)
    -> nk_color {
    if (ghost.collision) {
        auto color = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict);
        color.a = 153;
        return color;
    }
    if ((tail && (ghost.tail_halfway || ghost.stationary_tail_annotation)) ||
        (!tail && ghost.head_halfway)) return nk_rgba(255, 190, 56, 190);
    return nk_rgba(112, 220, 255, 170);
}

static auto draw_quantize_endpoint(const ComposePaintContext& paint, const WormUiComposeRect& source,
                                   std::uint32_t glyph, nk_color color) -> bool {
    const auto scale_x = paint.viewport.w / static_cast<float>(paint.view->viewport_width_pixels);
    const auto scale_y = paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    const auto rect = nk_rect(paint.viewport.x + static_cast<float>(source.left) * scale_x,
        paint.viewport.y + static_cast<float>(source.top) * scale_y,
        static_cast<float>(source.right - source.left) * scale_x,
        static_cast<float>(source.bottom - source.top) * scale_y);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return false;
    bool emitted{};
    if (wormedit::ui::is_arrow_glyph(static_cast<std::uint8_t>(glyph)) && rect.w >= 10.0f) {
        const auto tile_size = rect.w;
        const auto tile_x = rect.x;
        const auto tile_y = rect.y + (rect.h - tile_size) * 0.5f;
        const auto authored = wormedit::ui::arrow_polygon(static_cast<std::uint8_t>(glyph),
                                                           tile_x, tile_y, tile_size);
        for (std::size_t point = 0; point < authored.size(); ++point) {
            const auto& from = authored[point];
            const auto& to = authored[(point + 1) % authored.size()];
            emitted = draw_quantize_dashed_connector(paint, from.x, from.y, to.x, to.y, color) || emitted;
        }
    } else {
        emitted = draw_quantize_dashed_connector(paint, rect.x, rect.y, rect.x + rect.w, rect.y, color) || emitted;
        emitted = draw_quantize_dashed_connector(paint, rect.x + rect.w, rect.y,
            rect.x + rect.w, rect.y + rect.h, color) || emitted;
        emitted = draw_quantize_dashed_connector(paint, rect.x + rect.w, rect.y + rect.h,
            rect.x, rect.y + rect.h, color) || emitted;
        emitted = draw_quantize_dashed_connector(paint, rect.x, rect.y + rect.h,
            rect.x, rect.y, color) || emitted;
    }
    return emitted;
}

static auto quantize_screen_point(struct nk_rect viewport, const WormUiComposeView& view,
                                 double x, double y) -> std::pair<float, float> {
    return {viewport.x + static_cast<float>(x * viewport.w / view.viewport_width_pixels),
            viewport.y + static_cast<float>(y * viewport.h / view.viewport_height_pixels)};
}

static auto draw_quantize_head_ghost(const ComposePaintContext& paint,
    const WormUiComposeQuantizeGhostView& ghost, struct nk_rect viewport) -> bool {
    const auto color = quantize_ghost_color(ghost, false);
    bool emitted{};
    if (ghost.moved_head) {
        const auto [x0, y0] = quantize_screen_point(viewport, *paint.view,
            ghost.before_head_anchor_x, ghost.before_head_anchor_y);
        const auto [x1, y1] = quantize_screen_point(viewport, *paint.view,
            ghost.after_head_anchor_x, ghost.after_head_anchor_y);
        emitted = draw_quantize_dashed_connector(paint, x0, y0, x1, y1, color) || emitted;
    }
    emitted = draw_quantize_endpoint(paint, ghost.before_head, ghost.before_head_glyph, color) || emitted;
    emitted = draw_quantize_endpoint(paint, ghost.after_head, ghost.after_head_glyph, color) || emitted;
    return emitted;
}

static auto draw_quantize_tail_ghost(const ComposePaintContext& paint,
    const WormUiComposeQuantizeGhostView& ghost, struct nk_rect viewport) -> bool {
    const auto color = quantize_ghost_color(ghost, true);
    bool emitted{};
    if (ghost.moved_tail) {
        const auto [x0, y0] = quantize_screen_point(viewport, *paint.view,
            ghost.before_tail_anchor_x, ghost.before_tail_anchor_y);
        const auto [x1, y1] = quantize_screen_point(viewport, *paint.view,
            ghost.after_tail_anchor_x, ghost.after_tail_anchor_y);
        emitted = draw_quantize_dashed_connector(paint, x0, y0, x1, y1, color) || emitted;
    }
    const auto [head_x, head_y] = quantize_screen_point(viewport, *paint.view,
        ghost.after_head_anchor_x, ghost.after_head_anchor_y);
    const auto [tail_x, tail_y] = quantize_screen_point(viewport, *paint.view,
        ghost.after_tail_anchor_x, ghost.after_tail_anchor_y);
    auto x0 = head_x, y0 = head_y, x1 = tail_x, y1 = tail_y;
    if (clip_quantize_line(x0, y0, x1, y1, viewport)) {
        field_line(paint, x0, y0, x1, y1, 3.0f,
                   nk_rgba(color.r, color.g, color.b, 90), 9);
        emitted = true;
    }
    emitted = draw_quantize_endpoint(paint, ghost.before_tail, ghost.before_tail_glyph, color) || emitted;
    emitted = draw_quantize_endpoint(paint, ghost.after_tail, ghost.after_tail_glyph, color) || emitted;
    return emitted;
}

static auto draw_quantize_ghost_rows(const ComposePaintContext& paint,
                                    const WormUiComposeView& view, struct nk_rect viewport) -> bool {
    bool emitted{};
    for (std::size_t index = 0; index < view.quantize_ghost_count; ++index) {
        const auto& ghost = view.quantize_ghosts[index];
        emitted = draw_quantize_head_ghost(paint, ghost, viewport) || emitted;
        if (ghost.has_tail) emitted = draw_quantize_tail_ghost(paint, ghost, viewport) || emitted;
    }
    return emitted;
}

void draw_compose_quantize_ghosts_after_docks(nk_context* context,
                                              const WormUiComposeView* view,
                                              bool section_visible) {
    if (!quantize_overlay_viewport) return;
    const auto saved = *quantize_overlay_viewport;
    quantize_overlay_viewport.reset();
    if (!context || !view || !section_visible || !view->quantize_ghosts_drawable ||
        !view->scene_available || !view->quantize_ghosts || view->quantize_ghost_count == 0)
        return;
    const auto viewport = nk_rect(saved.x, saved.y, saved.width, saved.height);
    auto* canvas = nk_window_get_canvas(context);
    if (!canvas) return;
    wormedit::ui::nuklear::begin_compose_field(context, {viewport.x, viewport.y, viewport.w, viewport.h});
    const ComposePaintContext paint{context, view, canvas, viewport, viewport};
    quantize_ghosts_drawn_this_frame = draw_quantize_ghost_rows(paint, *view, viewport);
    wormedit::ui::nuklear::insert_compose_field(context, canvas, viewport);
}

auto compose_canvas_viewport_for_testing() -> std::optional<WormUiTestingRect> {
    return last_compose_viewport_for_testing;
}

auto compose_quantize_ghosts_drawn_for_testing() -> bool {
    return quantize_ghosts_drawn_this_frame;
}

auto compose_quantize_ghost_draw_state_for_testing() -> WormUiTestingComposeGhostDrawState {
    return quantize_ghost_draw_state;
}

static struct nk_rect compose_screen_rect(const WormUiComposeRect* source,
                                          const WormUiComposeView* view, struct nk_rect viewport) {
    const auto x_scale = viewport.w / static_cast<float>(view->viewport_width_pixels);
    const auto y_scale = viewport.h / static_cast<float>(view->viewport_height_pixels);
    const auto left = viewport.x + static_cast<float>(source->left) * x_scale;
    const auto top = viewport.y + static_cast<float>(source->top) * y_scale;
    const auto right = viewport.x + static_cast<float>(source->right) * x_scale;
    const auto bottom = viewport.y + static_cast<float>(source->bottom) * y_scale;
    const auto clipped_left = std::max(viewport.x, left);
    const auto clipped_top = std::max(viewport.y, top);
    const auto clipped_right = std::min(viewport.x + viewport.w, right);
    const auto clipped_bottom = std::min(viewport.y + viewport.h, bottom);
    return nk_rect(clipped_left, clipped_top, std::max(0.0f, clipped_right - clipped_left),
                   std::max(0.0f, clipped_bottom - clipped_top));
}

static auto field_color(nk_color color) -> wormedit::ui::field::color_t {
    return {color.r, color.g, color.b, color.a};
}
static void field_rect(const ComposePaintContext& paint, struct nk_rect bounds, float rounding,
                       nk_color color, wormedit::ui::field::layer_t layer = 0) {
    auto& field = paint_field(paint.context);
    if (rounding >= 1) {
        const auto points = wormedit::ui::field::rounded_rectangle_points(
            {std::trunc(bounds.x), std::trunc(bounds.y), std::trunc(bounds.w), std::trunc(bounds.h)},
            std::trunc(rounding));
        field.polygon(layer, points, field_color(color));
    } else {
        field.rect(layer, {bounds.x, bounds.y, bounds.w, bounds.h}, field_color(color));
    }
}
static void field_line(const ComposePaintContext& paint, float x0, float y0, float x1, float y1,
                       float width, nk_color color, wormedit::ui::field::layer_t layer = 0) {
    paint_field(paint.context)
        .line(layer, {x0, y0}, {x1, y1}, width, field_color(color));
}
static void field_stroke_rect(const ComposePaintContext& paint, struct nk_rect bounds, float rounding,
                              float width, nk_color color,
                              wormedit::ui::field::layer_t layer = 0) {
    if (rounding >= 1) {
        const auto rounded = wormedit::ui::field::rounded_rectangle_points(
            {std::trunc(bounds.x), std::trunc(bounds.y), std::trunc(bounds.w), std::trunc(bounds.h)},
            std::trunc(rounding));
        paint_field(paint.context).stroke_polygon(
            layer, rounded, width, field_color(color));
        return;
    }
    const std::array<SDL_FPoint, 4> points{{{bounds.x, bounds.y},
                                            {bounds.x + bounds.w, bounds.y},
                                            {bounds.x + bounds.w, bounds.y + bounds.h},
                                            {bounds.x, bounds.y + bounds.h}}};
    paint_field(paint.context).stroke_polygon(layer, points, width,
                                                                     field_color(color));
}
static void field_text(const ComposePaintContext& paint, struct nk_rect bounds, const char* text,
                       int length, const nk_user_font* font, nk_color, nk_color foreground,
                       wormedit::ui::field::layer_t layer = 0) {
    if (!tile_renderer_override) {
        wormedit::ui::nuklear::draw_compose_field_text(paint.context,layer,bounds,text,length,font,foreground);
        return;
    }
    if (!font || !text || length<=0 || bounds.w<=0 || bounds.h<=0) return;
    auto width=font->width(font->userdata,font->height,text,length);
    if (width>bounds.w) {
        int accepted{};
        while (accepted<length) {
            const auto decoded=wormedit::ui::field::decode_utf8(
                {text+accepted,static_cast<std::size_t>(length-accepted)});
            if (!decoded.bytes) break;
            const auto next=accepted+static_cast<int>(decoded.bytes);
            if (font->width(font->userdata,font->height,text,next)>bounds.w) break;
            accepted=next;
        }
        length=accepted;
    }
    paint_field(paint.context).text(layer,{std::trunc(bounds.x),std::trunc(bounds.y)},
        {text,static_cast<std::size_t>(length)},field_color(foreground),
        wormedit::ui::nuklear::compose_field_font(font));
}

// White-on-black-outline text stays legible on any lane color: the glyph run is drawn in black
// at the eight 1 px offsets, then in the text color.
static void field_text_outlined(const ComposePaintContext& paint, struct nk_rect bounds,
                                const char* text, int length, const nk_user_font* font,
                                nk_color foreground,
                                wormedit::ui::field::layer_t layer = 0) {
    static constexpr std::array<std::array<float, 2>, 8> offsets{{
        {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 0.0f},
        {1.0f, 0.0f}, {-1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}};
    for (const auto& offset : offsets)
        field_text(paint, nk_rect(bounds.x + offset[0], bounds.y + offset[1], bounds.w, bounds.h),
                   text, length, font, nk_rgba(0, 0, 0, 0), nk_rgb(0, 0, 0), layer);
    field_text(paint, bounds, text, length, font, nk_rgba(0, 0, 0, 0), foreground, layer);
}

// Stem lane resizing moves lane and event geometry without changing the revision or any other
// key field, so the retained tiles must be keyed by the visible lane widths too.
static auto compose_bgm_lane_width_digest(const WormUiComposeView& view) -> std::uint64_t {
    std::uint64_t digest = 1469598103934665603ULL;
    for (const auto& lane : std::span(view.bgm_lanes, view.bgm_lane_count)) {
        digest = (digest ^ std::bit_cast<std::uint64_t>(lane.width_pixels)) * 1099511628211ULL;
        digest = (digest ^ std::bit_cast<std::uint64_t>(lane.bounds.left)) * 1099511628211ULL;
    }
    return digest;
}
static auto compose_tile_request(const ComposePaintContext& paint, unsigned pass, bool scrolling)
    -> wormedit::ui::field::tile_request_t {
    const auto& view=*paint.view;
    wormedit::ui::field::tile_request_t request;
    request.viewport={paint.viewport.x,paint.viewport.y,paint.viewport.w,paint.viewport.h};
    const auto scale=paint.viewport.h/static_cast<float>(view.viewport_height_pixels);
    request.origin=scrolling ? (view.downscroll ? -view.area_scroll_pixels*scale-paint.viewport.h
                                               : view.area_scroll_pixels*scale) : 0;
    request.key.pass=pass;
    request.key.style={view.static_scene_style_generation,
        static_cast<std::uint64_t>(view.bms_measure_labels) |
        (static_cast<std::uint64_t>(view.waveform_row_count!=0)<<1) |
        (static_cast<std::uint64_t>(view.waveform_timing_locked)<<2) |
        (static_cast<std::uint64_t>(view.lane_key_legend_visible)<<3) |
        (static_cast<std::uint64_t>(view.timing_focus)<<4) |
        (static_cast<std::uint64_t>(view.edit_snap_enabled)<<5) |
        // Snap changes the grid lines without bumping the document revision.
        (view.area_snap_denominator<<8)};
    // Color views affect raster artwork, not the retained core scene.
    if (pass == 7 || pass == 9)
        request.key.style[1] |= static_cast<std::uint64_t>(
            worm_ui_nuklear_compose_state().color_view) << 6;
    request.key.identity={view.project_id,view.chart_id,view.document_revision,
        std::bit_cast<std::uint64_t>(view.pixels_per_standard_measure),
        std::bit_cast<std::uint64_t>(view.viewport_width_pixels),
        std::bit_cast<std::uint64_t>(view.viewport_height_pixels),
        std::bit_cast<std::uint64_t>(view.bgm_horizontal_scroll_pixels),
        std::bit_cast<std::uint64_t>(view.playable_field_left_pixels),
        std::bit_cast<std::uint64_t>(view.playable_field_right_pixels),
        std::bit_cast<std::uint32_t>(paint.viewport.x),std::bit_cast<std::uint32_t>(paint.viewport.y),
        std::bit_cast<std::uint32_t>(paint.viewport.w),std::bit_cast<std::uint32_t>(paint.viewport.h),
        view.timing_lane_visibility_mask,view.downscroll,
        reinterpret_cast<std::uintptr_t>(paint.context->style.font) ^
            (compose_bgm_lane_width_digest(view) * 0x9E3779B97F4A7C15ULL)};
    return request;
}
template<class T>
static void shift_static_records(std::vector<T>& values, double offset) {
    for (auto& value:values) {
        if constexpr (requires { value.bounds; }) {
            value.bounds.top+=offset; value.bounds.bottom+=offset;
        } else if constexpr (requires { value.y_pixels; }) value.y_pixels+=offset;
        else value.marker_y_pixels+=offset;
    }
}
template<class Draw>
static void draw_indexed_region(const ComposePaintContext& paint,unsigned pass,
                                SDL_FRect region,double viewport_origin,Draw draw) {
    if (!paint.view->static_scene) { draw(paint); return; }
    const auto scale=paint.viewport.h/paint.view->viewport_height_pixels;
    const auto delta=region.y-paint.viewport.y;
    static_paint_storage_t storage(*paint.view,pass,viewport_origin+delta,region.h,scale);
    storage.view.viewport_height_pixels=paint.view->viewport_height_pixels;
    const auto offset=delta/scale;
    shift_static_records(storage.bands,offset); shift_static_records(storage.lanes,offset);
    shift_static_records(storage.grid,offset); shift_static_records(storage.timing,offset);
    shift_static_records(storage.holds,offset); shift_static_records(storage.bgm_lanes,offset);
    shift_static_records(storage.bgm,offset); shift_static_records(storage.bga,offset);
    shift_static_records(storage.notes,offset);
    if (pass == 6) {
        // The interval halo includes neighboring bodies, but direct projection
        // publishes timing markers only when their centers are in the viewport.
        std::erase_if(storage.timing, [&](const auto& marker) {
            return marker.marker_y_pixels < 0 ||
                   marker.marker_y_pixels > storage.view.viewport_height_pixels;
        });
        storage.view.timing_markers = storage.timing.data();
        storage.view.timing_marker_count = storage.timing.size();
    }
    auto indexed=paint; indexed.view=&storage.view;
    draw(indexed);
}
template<class Draw>
static void prepare_compose_tiles(const ComposePaintContext& paint,unsigned pass,
                                  const wormedit::ui::field::tile_request_t& request,Draw draw) {
    if (!paint.view->static_scene) return;
    const auto scale=paint.viewport.h/paint.view->viewport_height_pixels;
    paint_field(paint.context).prepare_tiles(request,
        paint.view->static_scene_covered_begin_pixels*scale,
        paint.view->static_scene_covered_end_pixels*scale,
        paint.view->static_scene_accounted_bytes+static_paint_indices.bytes+8*1024*1024,
        [&](auto& renderer,double origin) {
            tile_renderer_scope_t scoped(renderer);
            // A geometry halo prevents tile-edge clipping from changing authored
            // arrow centers or clamped timing marker bodies before raster cropping.
            const auto halo=std::max(96.0f,paint.viewport.w*.5f);
            static_paint_storage_t storage(*paint.view,pass,origin-halo,512+2*halo,scale);
            auto prepared=paint;
            storage.view.hover_note_visible=false;
            prepared.view=&storage.view;
            prepared.viewport=nk_rect(0,-halo,paint.viewport.w,512+2*halo);
            prepared.rail=nk_rect(0,-halo,paint.rail.w,512+2*halo);
            draw(prepared);
        });
}
static void append_hover_hole(std::vector<SDL_FRect>& holes,SDL_FRect bounds,
                               const ComposePaintContext& paint) {
    const auto left=std::max(paint.viewport.x,std::floor(bounds.x));
    const auto top=std::max(paint.viewport.y,std::floor(bounds.y));
    const auto right=std::min(paint.viewport.x+paint.viewport.w,std::ceil(bounds.x+bounds.w));
    const auto bottom=std::min(paint.viewport.y+paint.viewport.h,std::ceil(bounds.y+bounds.h));
    if (right<=left || bottom<=top) return;
    const auto unique=wormedit::ui::field::subtract_rectangles({left,top,right-left,bottom-top},holes);
    holes.insert(holes.end(),unique.begin(),unique.end());
}
static auto hover_source_rect(const ComposePaintContext& paint,WormUiComposeRect bounds)
    -> struct nk_rect {
    const auto scale=paint.viewport.h/paint.view->viewport_height_pixels;
    const auto origin=compose_tile_request(paint,9,true).origin/scale;
    const auto offset=paint.view->static_scene_world_origin_pixels-origin;
    bounds.top+=offset; bounds.bottom+=offset;
    return compose_screen_rect(&bounds,paint.view,paint.viewport);
}
static void append_hover_note_holes(const ComposePaintContext& paint,
                                   const static_hover_records_t& records,
                                   std::vector<SDL_FRect>& holes) {
    for (const auto index:records.notes) {
        const auto& note=paint.view->static_scene->note_endpoints[index];
        auto rect=hover_source_rect(paint,note.bounds);
        if (rect.w<=0 || rect.h<=0) continue;
        if (wormedit::ui::is_arrow_glyph(note.glyph)) {
            const auto width=std::max(1.0f,rect.w-2);
            const auto top=rect.y+(std::max(2.0f,rect.h)-width)*.5f;
            const auto bottom=std::max(rect.y+rect.h,top+width);
            rect.y=std::min(rect.y,top); rect.h=bottom-rect.y;
        }
        append_hover_hole(holes,{rect.x-6,rect.y-6,rect.w+12,rect.h+12},paint);
        if (holes.size()>32) return;
    }
}
static void append_hover_hold_holes(const ComposePaintContext& paint,
                                   const static_hover_records_t& records,
                                   std::vector<SDL_FRect>& holes) {
    for (const auto index:records.holds) {
        const auto rect=hover_source_rect(paint,paint.view->static_scene->hold_bodies[index].bounds);
        if (rect.w<=0 || rect.h<=0) continue;
        for (const auto border:std::array<SDL_FRect,4>{{
            {rect.x-3,rect.y-3,rect.w+6,6},{rect.x-3,rect.y+rect.h-3,rect.w+6,6},
            {rect.x-3,rect.y-3,6,rect.h+6},{rect.x+rect.w-3,rect.y-3,6,rect.h+6}}})
            append_hover_hole(holes,border,paint);
        if (holes.size()>32) return;
    }
}
static auto compose_hover_holes(const ComposePaintContext& paint,unsigned pass)
    -> std::optional<std::vector<SDL_FRect>> {
    if (!paint.view->hover_note_visible) return std::vector<SDL_FRect>{};
    if (!paint.view->static_scene) return std::nullopt;
    const auto found=static_paint_indices.notes_by_id.find(paint.view->hover_note_id);
    if (found==static_paint_indices.notes_by_id.end()) return std::nullopt;
    std::vector<SDL_FRect> holes;
    if (pass==9) append_hover_note_holes(paint,found->second,holes);
    else append_hover_hold_holes(paint,found->second,holes);
    if (holes.size()>32) return std::nullopt;
    return holes;
}
template<class Draw>
static void draw_static_tile_region(const ComposePaintContext& paint,unsigned pass,
                                    SDL_FRect region,
                                    const wormedit::ui::field::tile_request_t& base,
                                    bool indexed,bool capture,Draw draw) {
    auto& field=paint_field(paint.context);
    const auto previous=field.push_clip(region);
    const auto first=field.indices().size();
    if (indexed) draw_indexed_region(paint,pass,region,base.origin,draw);
    else draw(paint);
    if (capture) {
        auto strip=base;
        strip.origin+=region.y-base.viewport.y;
        strip.viewport=region;
        field.capture_tiles(strip,first);
    }
    field.pop_clip(previous);
}
// Ready intersections and missing strips occupy disjoint clips in the same pass.
// Dynamic selection/hover paths keep their original per-object draw order.
template<class Draw>
static void draw_compose_cached_pass(const ComposePaintContext& paint, unsigned pass,
                                    bool scrolling, bool dynamic, Draw draw,
                                    std::span<const SDL_FRect> holes={}) {
    if (dynamic) { draw(paint); return; }
    auto& field=paint_field(paint.context);
    auto request=compose_tile_request(paint,pass,scrolling);
    const auto viewport_origin=request.origin;
    const auto base_request=request;
    const bool retained=scrolling && paint.view->static_scene;
    const auto scale=paint.viewport.h/paint.view->viewport_height_pixels;
    const auto guard_size=pass==9 ? static_paint_indices.glyph_guard*scale : 32.0;
    if (scrolling && pass!=2 && guard_size>=request.viewport.h*.5) {
        draw(paint); return;
    }
    if (retained) prepare_compose_tiles(paint,pass,request,draw);
    if (scrolling && pass!=2) {
        const auto guard=static_cast<float>(guard_size);
        const auto top=std::ceil(request.viewport.y+guard);
        const auto bottom=std::floor(request.viewport.y+request.viewport.h-guard);
        for (const auto region:std::array<SDL_FRect,2>{{
            {request.viewport.x,request.viewport.y,request.viewport.w,top-request.viewport.y},
            {request.viewport.x,bottom,request.viewport.w,
                request.viewport.y+request.viewport.h-bottom}}}) {
            for (const auto outside:wormedit::ui::field::subtract_rectangles(region,holes)) {
                draw_static_tile_region(paint,pass,outside,base_request,retained,false,draw);
            }
        }
        request.origin+=top-request.viewport.y;
        request.viewport.y=top;
        request.viewport.h=bottom-top;
    }
    if (!field.composite_tiles(request,holes)) {
        for (const auto missing:field.missing_tiles(request))
            for (const auto region:wormedit::ui::field::subtract_rectangles(missing,holes))
                draw_static_tile_region(paint,pass,region,base_request,retained,!retained,draw);
    }
    for (const auto hole:holes) {
        const auto previous=field.push_clip(hole);
        draw_indexed_region(paint,pass,hole,viewport_origin,draw);
        field.pop_clip(previous);
    }
}

static void draw_compose_grid_tick(const ComposePaintContext& paint,
                                   const WormUiComposeGridLineView& line, float y) {
    const auto& view = *paint.view;
    auto& field = paint_field(paint.context);
    if (view.waveform_row_count != 0 && !view.waveform_timing_locked && line.chart_row != 0 &&
        (line.kind == 1 || line.kind == 3)) {
        const auto color = line.chart_row == view.beat_fit_chart_row && view.beat_fit_drag_active
                               ? wormedit::ui::field::color_t{255, 207, 104}
                               : wormedit::ui::field::color_t{165, 177, 195};
        field.rect(1, {paint.rail.x + paint.rail.w - 13.0f, y - 3.0f, 7.0f, 6.0f}, color);
    }
    if (line.measure_number == 0 || y + 15.0f > paint.viewport.y + paint.viewport.h) {
        return;
    }
    char label[20]{};
    const auto length = view.bms_measure_labels
                            ? snprintf(label, sizeof(label), "#%03u", line.measure_number - 1)
                            : snprintf(label, sizeof(label), "M%u", line.measure_number);
    if (length <= 0) {
        return;
    }
    const auto label_bounds = nk_rect(paint.rail.x + 3.0f, y + 2.0f,
                                      std::max(1.0f, paint.rail.w - 6.0f), 14.0f);
    if (view.selected_bms_measure_number_plus_one == line.measure_number) {
        field.rect(1, {label_bounds.x, label_bounds.y, label_bounds.w, label_bounds.h},
                   {67, 93, 135});
    }
    // Retain the serialized text origin used by the pinned canvas adapter.
    field.text(1, {std::trunc(label_bounds.x), std::trunc(label_bounds.y)}, label,
               {174, 185, 203},
               wormedit::ui::nuklear::compose_field_font(paint.context->style.font));
}

static void draw_compose_grid_line(const ComposePaintContext& paint,
                                   const WormUiComposeGridLineView& line) {
    const auto& view = *paint.view;
    auto& field = paint_field(paint.context);
    if (!std::isfinite(line.y_pixels) || line.y_pixels < 0.0 ||
        line.y_pixels > view.viewport_height_pixels)
        return;
    const auto y = paint.viewport.y + static_cast<float>(line.y_pixels) * paint.viewport.h /
                                          static_cast<float>(view.viewport_height_pixels);
    struct nk_color color = nk_rgb(42, 49, 63);
    float thickness = 0.7f;
    if (line.kind == 3) {
        color = nk_rgb(95, 107, 128);
        thickness = 1.2f;
    } else if (line.kind == 1 || line.kind == 2) {
        color = nk_rgb(60, 70, 88);
        thickness = line.emphasis >= 2 ? 1.1f : 0.9f;
    } else if (line.emphasis >= 2) {
        color = nk_rgb(52, 61, 78);
    }
    field.hline(1, paint.viewport.x, paint.viewport.x + paint.viewport.w, y, thickness,
                {color.r, color.g, color.b, color.a});
    draw_compose_grid_tick(paint, line, y);
}

static void draw_compose_grid_lines(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->grid_line_count; ++index) {
        draw_compose_grid_line(paint, paint.view->grid_lines[index]);
    }
}

static void draw_compose_lane_layout_segments(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->lane_layout_segment_count; ++index) {
        const auto& segment = paint.view->lane_layout_segments[index];
        const auto y = paint.viewport.y + static_cast<float>(segment.y_pixels) * paint.viewport.h /
                                               static_cast<float>(paint.view->viewport_height_pixels);
        if (std::isfinite(y) && y >= paint.viewport.y && y <= paint.viewport.y + paint.viewport.h)
            paint_field(paint.context).rect(
                1, {paint.rail.x + paint.rail.w - 7.0f, y - 3.0f, 5.0f, 6.0f}, {126, 190, 255});
    }
}

static float compose_projected_y(const ComposePaintContext& paint, double y_pixels) {
    return paint.viewport.y + static_cast<float>(y_pixels) * paint.viewport.h /
                                    static_cast<float>(paint.view->viewport_height_pixels);
}

static void draw_compose_beat_sections(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->beat_section_count; ++index) {
        const auto& section = paint.view->beat_sections[index];
        if (section.proposed_span_projection_visible) {
            const auto top = compose_projected_y(paint, section.proposed_span_top_y_pixels);
            const auto bottom = compose_projected_y(paint, section.proposed_span_bottom_y_pixels);
            field_rect(paint,
                         nk_rect(paint.viewport.x, top, paint.viewport.w,
                                 std::max(1.0f, bottom - top)),
                         0.0f, nk_rgba(255, 207, 104, 25), 4);
        }
        if (section.span_projection_visible) {
            const auto top = compose_projected_y(paint, section.projected_span_top_y_pixels);
            const auto bottom = compose_projected_y(paint, section.projected_span_bottom_y_pixels);
            field_rect(paint,
                         nk_rect(paint.viewport.x, top, paint.viewport.w,
                                 std::max(1.0f, bottom - top)),
                         0.0f, nk_rgba(87, 198, 177, 20), 4);
        }
        if (section.begin_projection_visible) {
            const auto y = compose_projected_y(paint, section.projected_begin_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.0f, nk_rgba(98, 206, 185, 160), 4);
        }
        if (section.end_projection_visible) {
            const auto y = compose_projected_y(paint, section.projected_end_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.0f, nk_rgba(98, 206, 185, 160), 4);
        }
        if (section.proposed_begin_projection_visible) {
            const auto y = compose_projected_y(paint, section.proposed_begin_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.4f, nk_rgba(255, 207, 104, 185), 4);
        }
        if (section.proposed_end_projection_visible) {
            const auto y = compose_projected_y(paint, section.proposed_end_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.4f, nk_rgba(255, 207, 104, 185), 4);
        }
    }
}

// Draws a bookmark name above its line; `previous` keeps earlier labels from being covered.
static void draw_compose_bookmark_label(const ComposePaintContext& paint, const char* label,
                                        float line_y, nk_color color,
                                        std::optional<wormedit::ui::bookmark_label_box_t>& previous) {
    const auto* font = paint.context->style.font;
    if (!label || !label[0] || !font) return;
    const auto width_of = [font](std::string_view text) {
        return font->width(font->userdata, font->height, text.data(), static_cast<int>(text.size()));
    };
    const auto scale = paint.viewport.w / static_cast<float>(paint.view->viewport_width_pixels);
    const auto left = paint.viewport.x +
        static_cast<float>(paint.view->playable_field_left_pixels) * scale;
    const auto width = static_cast<float>(paint.view->playable_field_right_pixels -
                                         paint.view->playable_field_left_pixels) * scale;
    const auto box = wormedit::ui::layout_bookmark_label(
        label, width_of, font->height, line_y,
        {left, paint.viewport.y, width, paint.viewport.h}, previous);
    if (!box) return;
    previous = box;
    field_text(paint, nk_rect(box->x, box->y, box->w, box->h), box->text.c_str(),
               static_cast<int>(box->text.size()), font, nk_rgba(0, 0, 0, 0), color, 4);
}

// The bookmark under the pointer, using the same full-width band that double-click rename
// uses. A note under the pointer wins, as it does for the click.
static auto compose_hovered_bookmark_id(const ComposePaintContext& paint) -> std::uint64_t {
    const auto& view = *paint.view;
    const auto& mouse = paint.context->input.mouse.pos;
    if (!view.visible || view.input_occluded || view.viewport_width_pixels <= 0.0 ||
        view.viewport_height_pixels <= 0.0 || !nk_input_is_mouse_hovering_rect(
            &paint.context->input, paint.viewport)) return 0;
    const auto x = (static_cast<double>(mouse.x) - paint.viewport.x) *
                   view.viewport_width_pixels / paint.viewport.w;
    const auto y = (static_cast<double>(mouse.y) - paint.viewport.y) *
                   view.viewport_height_pixels / paint.viewport.h;
    if (compose_point_hits_note(&view, x, y)) return 0;
    return compose_bookmark_at(view, x, y).value_or(0);
}

// A faint band over the hit area, so the double-click target is visible before clicking.
static void draw_compose_bookmark_hover(const ComposePaintContext& paint, float y,
                                        bool labelled, nk_color color) {
    const auto scale = paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    const auto above = labelled ? static_cast<float>(compose_bookmark_label_zone_height) * scale
                                : 6.0f * scale;
    color.a = 36;
    field_rect(paint, nk_rect(paint.viewport.x, y - above, paint.viewport.w, above + 6.0f * scale),
               0.0f, color, 4);
}

// While a bookmark is dragged, a dashed-looking ghost line follows the pointer; the bookmark
// itself moves to the snapped row on release.
static void draw_compose_bookmark_move_ghost(const ComposePaintContext& paint) {
    if (!state.bookmark_move_active || state.bookmark_move_chart_id != paint.view->chart_id) return;
    const auto scale = paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    const auto y = paint.viewport.y + static_cast<float>(state.bookmark_move_current_y_pixels) * scale;
    field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 1.6f,
               nk_rgb(255, 218, 139), 4);
}

static void draw_compose_beat_bookmarks(const ComposePaintContext& paint,
    std::optional<wormedit::ui::bookmark_label_box_t>& previous) {
    draw_compose_bookmark_move_ghost(paint);
    const auto hovered = state.bookmark_move_active ? state.bookmark_move_id
                                                    : compose_hovered_bookmark_id(paint);
    for (size_t index = 0; index < paint.view->beat_bookmark_count; ++index) {
        const auto& bookmark = paint.view->beat_bookmarks[index];
        if (bookmark.projection_visible) {
            const auto y = compose_projected_y(paint, bookmark.projected_y_pixels);
            const auto hover = bookmark.bookmark_id == hovered;
            if (hover) draw_compose_bookmark_hover(paint, y, bookmark.label && bookmark.label[0],
                                                   nk_rgb(111, 224, 201));
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, hover ? 2.2f : 1.3f, nk_rgb(111, 224, 201), 4);
            draw_compose_bookmark_label(paint, bookmark.label, y, nk_rgb(111, 224, 201), previous);
        }
        if (bookmark.proposed_projection_visible) {
            const auto y = compose_projected_y(paint, bookmark.proposed_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.6f, nk_rgb(255, 218, 139), 4);
        }
    }
}

static void draw_compose_audio_sections(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->audio_section_count; ++index) {
        const auto& section = paint.view->audio_sections[index];
        if (section.span_projection_visible) {
            const auto top = compose_projected_y(paint, section.projected_span_top_y_pixels);
            const auto bottom = compose_projected_y(paint, section.projected_span_bottom_y_pixels);
            field_rect(paint,
                         nk_rect(paint.viewport.x, top, paint.viewport.w,
                                 std::max(1.0f, bottom - top)),
                         0.0f, nk_rgba(188, 111, 235, 24), 4);
        }
        if (section.begin_projection_visible) {
            const auto y = compose_projected_y(paint, section.projected_begin_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.1f, nk_rgb(205, 139, 242), 4);
        }
        if (section.end_projection_visible) {
            const auto y = compose_projected_y(paint, section.projected_end_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.1f, nk_rgb(205, 139, 242), 4);
        }
    }
}

static void draw_compose_audio_bookmarks(const ComposePaintContext& paint,
    std::optional<wormedit::ui::bookmark_label_box_t>& previous) {
    const auto hovered = compose_hovered_bookmark_id(paint);
    for (size_t index = 0; index < paint.view->audio_bookmark_count; ++index) {
        const auto& bookmark = paint.view->audio_bookmarks[index];
        if (!bookmark.projection_visible) {
            continue;
        }
        const auto y = compose_projected_y(paint, bookmark.projected_y_pixels);
        const auto hover = bookmark.bookmark_id == hovered;
        if (hover) draw_compose_bookmark_hover(paint, y, bookmark.label && bookmark.label[0],
                                               nk_rgb(232, 166, 255));
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       hover ? 2.3f : 1.4f, nk_rgb(232, 166, 255), 4);
        draw_compose_bookmark_label(paint, bookmark.label, y, nk_rgb(232, 166, 255), previous);
    }
}

// Generic lanes without a key color take their color from the lane role.
static auto compose_lane_key_color(uint8_t key_color, uint8_t role) -> uint8_t {
    if (key_color != 0) return key_color;
    switch (role) {
    case 1: return 3; // scratch
    case 5: return 5; // free zone
    case 6: return 4; // foot pedal
    default: return 0;
    }
}

struct ComposeTimingLaneLayout {
    std::array<struct nk_rect, WORM_UI_TIMING_LANE_COUNT> lanes{};
    std::array<struct nk_color, WORM_UI_TIMING_LANE_COUNT> colors{};
    float field_left{};
};

static auto compose_timing_marker_document_rect(const WormUiComposeView& view,
                                                const WormUiTimingMarkerView& marker)
    -> WormUiComposeRect {
    const auto marker_lane_ordinal = static_cast<unsigned>(marker.lane);
    if (marker_lane_ordinal >= WORM_UI_TIMING_LANE_COUNT) return {};
    const auto bit = static_cast<std::uint8_t>(1U << marker_lane_ordinal);
    const auto visible_count = std::popcount(view.timing_lane_visibility_mask);
    if (visible_count == 0 || (view.timing_lane_visibility_mask & bit) == 0) return {};
    const auto preceding = static_cast<std::uint8_t>(view.timing_lane_visibility_mask & (bit - 1));
    const auto visible_marker_lane_slot = std::popcount(preceding);
    const auto timing_right = view.bga_lane_visible ? view.bga_lane_bounds.left
                                                    : view.playable_field_left_pixels;
    const auto width = (timing_right - compose_measure_rail_width) / visible_count;
    const auto inner_left = compose_measure_rail_width + width * visible_marker_lane_slot + 2.0;
    const auto inner_width = std::max(0.0, width - 4.0);
    const auto left = inner_left + inner_width * marker.marker_left_fraction;
    const auto right = inner_left + inner_width * marker.marker_right_fraction;
    constexpr double height = 16.0;
    const auto top = std::clamp(marker.marker_y_pixels - height * 0.5, 0.0,
                                std::max(0.0, view.viewport_height_pixels - height));
    return {left, top, std::max(left + 1.0, right), top + height};
}

static auto compose_timing_marker_screen_rect(const ComposePaintContext& paint,
                                               const WormUiTimingMarkerView& marker)
    -> struct nk_rect {
    const auto bounds = compose_timing_marker_document_rect(*paint.view, marker);
    return compose_screen_rect(&bounds, paint.view, paint.viewport);
}

static auto compose_hovered_timing_marker(const ComposePaintContext& paint)
    -> const WormUiTimingMarkerView* {
    const WormUiTimingMarkerView* hovered = nullptr;
    for (size_t index = 0; index < paint.view->timing_marker_count; ++index) {
        const auto& marker = paint.view->timing_markers[index];
        const auto bounds = compose_timing_marker_screen_rect(paint, marker);
        if (bounds.w > 0.0f && bounds.h > 0.0f &&
            nk_input_is_mouse_hovering_rect(&paint.context->input, bounds)) hovered = &marker;
    }
    return hovered;
}

auto compose_timing_marker_at(const WormUiComposeView& view, double x, double y)
    -> const WormUiTimingMarkerView* {
    const WormUiTimingMarkerView* hit = nullptr;
    for (size_t index = 0; index < view.timing_marker_count; ++index) {
        const auto& marker = view.timing_markers[index];
        const auto bounds = compose_timing_marker_document_rect(view, marker);
        if (x >= bounds.left && x <= bounds.right && y >= bounds.top && y <= bounds.bottom)
            hit = &marker;
    }
    return hit;
}

static void compose_timing_lane_layout(const ComposePaintContext& paint, ComposeTimingLaneLayout& layout) {
    for (size_t index = 0; index < layout.colors.size(); ++index) {
        layout.colors[index] = wormedit::ui::theme::compose_note_accent(index);
    }
    const auto timing_left = paint.rail.x + paint.rail.w;
    const auto timing_width = std::max(0.0f, layout.field_left - timing_left);
    const auto visible_count = static_cast<std::size_t>(
        std::popcount(paint.view->timing_lane_visibility_mask));
    const auto lane_width = visible_count == 0 ? 0.0f : timing_width / visible_count;
    std::size_t visible_lane = 0;
    for (size_t index = 0; index < layout.lanes.size(); ++index) {
        if ((paint.view->timing_lane_visibility_mask & (1U << index)) == 0) {
            continue;
        }
        auto& lane = layout.lanes[index];
        lane = nk_rect(timing_left + lane_width * static_cast<float>(visible_lane++),
                       paint.viewport.y, lane_width, paint.viewport.h);
    }
}
static void draw_compose_timing_lanes(const ComposePaintContext& paint,
                                      ComposeTimingLaneLayout& layout) {
    constexpr std::array<const char*, WORM_UI_TIMING_LANE_COUNT> labels{
        "BPM", "SV", "SPD", "STP", "DLY", "WRP", "KIA", "ANC"};
    for (size_t index=0; index<layout.lanes.size(); ++index) {
        const auto& lane=layout.lanes[index];
        if (lane.w<=0) continue;
        const auto lane_width=lane.w;
        field_rect(paint, lane, 0.0f,
                     (index & 1U) != 0 ? nk_rgb(25, 29, 38) : nk_rgb(28, 32, 42));
        field_line(paint, lane.x, paint.viewport.y, lane.x,
                       paint.viewport.y + paint.viewport.h, 1.0f,
                       wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::grid));
        if (lane_width >= 17.0f) {
            const auto title = nk_rect(lane.x + 2.0f, paint.viewport.y + 2.0f,
                                       std::max(1.0f, lane_width - 4.0f), 14.0f);
            field_text(paint, title, labels[index], static_cast<int>(strlen(labels[index])),
                         paint.context->style.font, nk_rgba(18, 21, 29, 215), layout.colors[index]);
        }
    }
}

static void draw_compose_bands(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->band_count; ++index) {
        const auto& band = paint.view->bands[index];
        field_rect(paint, compose_screen_rect(&band.bounds, paint.view, paint.viewport), 0,
                   band.split_stage ? nk_rgb(27, 31, 43) : nk_rgb(25, 29, 39));
        const auto top = compose_projected_y(paint, band.bounds.top);
        const auto bottom = compose_projected_y(paint, band.bounds.bottom);
        field_line(paint, paint.viewport.x, top, paint.viewport.x + paint.viewport.w,
                       top, 1.0f, nk_rgb(80, 91, 112));
        field_line(paint, paint.viewport.x, bottom, paint.viewport.x + paint.viewport.w,
                       bottom, 1.0f, nk_rgb(61, 70, 87));
    }
}

static void draw_compose_play_lanes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->lane_count; ++index) {
        const auto& lane = paint.view->lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto key = wormedit::ui::theme::compose_key_color(
            compose_lane_key_color(lane.key_color, lane.role));
        field_rect(paint, rect, 0.0f, key.lane);
        field_line(paint, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                       nk_rgb(62, 70, 86));

    }
}

static void draw_compose_play_lane_titles(const ComposePaintContext& paint) {
    for (size_t index=0; index<paint.view->lane_count; ++index) {
        const auto& lane=paint.view->lanes[index];
        const auto rect=compose_screen_rect(&lane.bounds,paint.view,paint.viewport);
        if (paint.view->lane_key_legend_visible && rect.w >= 28.0f && rect.h >= 18.0f) {
            char label[16]{};
            const auto label_length = snprintf(label, sizeof(label), "%u", lane.display_order + 1U);
            if (label_length > 0) {
                const auto label_bounds = nk_rect(rect.x + 2.0f, rect.y + 2.0f,
                                                  std::min(28.0f, rect.w - 4.0f), 15.0f);
                field_text_outlined(paint, label_bounds, label, static_cast<int>(strlen(label)),
                                    paint.context->style.font, nk_rgb(240, 244, 250));
            }
        }
    }
}

static void draw_compose_bgm_lanes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bgm_lane_count; ++index) {
        const auto& lane = paint.view->bgm_lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto alternate = (lane.source_order & 1U) != 0;
        field_rect(paint, rect, 0.0f,
                     alternate ? nk_rgb(43, 38, 52) : nk_rgb(38, 34, 47));
        field_line(paint, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                       nk_rgb(79, 67, 91));
    }
}

static void draw_compose_scene_background(const ComposePaintContext& paint,
                                          ComposeTimingLaneLayout& layout) {
    field_rect(paint, paint.rail, 0.0f,
                 wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::rail));
    field_line(paint, paint.rail.x + paint.rail.w, paint.rail.y,
                   paint.rail.x + paint.rail.w, paint.rail.y + paint.rail.h, 1.0f,
                   nk_rgb(80, 91, 112));
    compose_timing_lane_layout(paint,layout);
    draw_compose_cached_pass(paint,1,false,false,[&](const ComposePaintContext& cached) { draw_compose_timing_lanes(cached,layout); });
    // The original bodies keep their order. Titles stay live inside their
    // own lane margins, disjoint from the subsequent BGM background bodies.
    draw_compose_cached_pass(paint,2,true,false,[&](const ComposePaintContext& cached) {
        draw_compose_bands(cached); draw_compose_play_lanes(cached); draw_compose_bgm_lanes(cached);
    });
    draw_compose_play_lane_titles(paint);
}

static auto finite_envelope_row(const WormUiComposeWaveformRowView& row) -> bool {
    return std::isfinite(row.y_pixels) && std::isfinite(row.rms[0]) && std::isfinite(row.rms[1]) &&
           std::isfinite(row.absolute_peak[0]) && std::isfinite(row.absolute_peak[1]);
}

static void draw_compose_retained_gaps(const ComposePaintContext& paint,
        const WormUiComposeWaveformRowView* source, size_t count, float center,
        float half_width, nk_color color, SDL_FRect coverage) {
    for (size_t index = 0; index < count; ++index) {
        const auto& row = source[index];
        if (!finite_envelope_row(row)) continue;
        const auto y = compose_projected_y(paint, row.y_pixels);
        if (y >= coverage.y && y < coverage.y + coverage.h) continue;
        field_line(paint, center - std::clamp(row.absolute_peak[0], 0.0f, 1.0f) * half_width, y,
            center + std::clamp(row.absolute_peak[1], 0.0f, 1.0f) * half_width, y, 1.0f, color, 6);
    }
}

static auto draw_compose_envelope_texture(const ComposePaintContext& paint,
        const WormUiComposeWaveformRowView* source, size_t count, float center, float half_width,
        size_t slot, nk_color rms, nk_color peak, bool joined, uint64_t lane_key = 0,
        std::optional<wormedit::ui::field::waveform_piece_key_t> stem_key = std::nullopt) -> bool {
    const auto& view = *paint.view;
    const auto y_scale = paint.viewport.h / static_cast<float>(view.viewport_height_pixels);
    if (!std::isfinite(y_scale) || y_scale <= 0) return false;
    // Envelope texels describe whole world pixels. PCM points keep their exact
    // projected coordinates in draw_compose_waveform_samples.
    const auto scroll = std::floor(view.area_scroll_pixels);
    const auto origin = view.downscroll
        ? -scroll - view.viewport_height_pixels : scroll;
    auto key = view.document_revision ^ (view.project_id * 1099511628211ULL);
    key ^= view.chart_id * 1469598103934665603ULL;
    if (!stem_key) {
        key ^= view.selected_audio_asset_id * 1099511628211ULL;
        key ^= view.channel_waveform_source_lane_id * static_cast<uint64_t>(slot);
    }
    key ^= lane_key * 0x9E3779B97F4A7C15ULL;
    if (!stem_key) key ^= std::bit_cast<uint64_t>(view.pixels_per_standard_measure);
    key ^= std::bit_cast<uint64_t>(view.waveform_audio_offset_numerator);
    key ^= view.waveform_audio_offset_denominator;
    const SDL_FRect bounds{center - half_width, paint.viewport.y, half_width * 2, paint.viewport.h};
    auto& renderer = paint_field(paint.context);
    const wormedit::ui::field::envelope_style_t style{field_color(rms), field_color(peak), joined};
    SDL_FRect coverage{};
    if (stem_key && renderer.retain_piece_envelope(*stem_key, 6, bounds,
            origin, key, style, y_scale, coverage)) {
        if (coverage.y > paint.viewport.y || coverage.y + coverage.h < paint.viewport.y + paint.viewport.h)
            draw_compose_retained_gaps(paint, source, count, center, half_width, peak, coverage);
        return true;
    }
    static std::vector<wormedit::ui::field::envelope_row_t> rows;
    rows.clear();
    rows.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const auto& row = source[index];
        rows.push_back({row.y_pixels + origin, {row.rms[0], row.rms[1]},
                                               {row.absolute_peak[0], row.absolute_peak[1]}});
    }
    if (stem_key)
        return renderer.piece_envelope(*stem_key, 6, rows, bounds, origin, key, style, y_scale);
    return renderer.envelope(slot, 6, rows, bounds, origin, key, style, y_scale);
}

static void draw_compose_waveform_samples(const ComposePaintContext& paint,
                                          float center, float half_width) {
    const auto& view = *paint.view;
    for (size_t index = 1; index < view.waveform_row_count; ++index) {
        const auto& before = view.waveform_rows[index - 1U];
        const auto& after = view.waveform_rows[index];
        if (!before.sample_point || !after.sample_point || !std::isfinite(before.y_pixels) ||
            !std::isfinite(after.y_pixels)) continue;
        for (size_t channel = 0; channel < 2; ++channel) {
            if (!std::isfinite(before.samples[channel]) || !std::isfinite(after.samples[channel]))
                continue;
            const auto color = channel == 0 ? nk_rgba(114, 222, 239, 190)
                                            : nk_rgba(238, 160, 114, 190);
            field_line(paint,
                center + std::clamp(before.samples[channel], -1.0f, 1.0f) * half_width,
                compose_projected_y(paint, before.y_pixels),
                center + std::clamp(after.samples[channel], -1.0f, 1.0f) * half_width,
                compose_projected_y(paint, after.y_pixels), 1.0f, color, 6);
        }
    }
}
static void draw_compose_waveform(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (view.waveform_row_count == 0) {
        return;
    }
    const auto scale = paint.viewport.w / static_cast<float>(view.viewport_width_pixels);
    const auto left = paint.viewport.x + static_cast<float>(view.playable_field_left_pixels) * scale;
    const auto right = paint.viewport.x + static_cast<float>(view.playable_field_right_pixels) * scale;
    const auto center = (left + right) * 0.5f;
    const auto half_width = std::max(0.0f, (right - left) * 0.47f);
    if (view.waveform_rows[0].sample_point) {
        draw_compose_waveform_samples(paint, center, half_width);
        return;
    }
    if (draw_compose_envelope_texture(paint, view.waveform_rows, view.waveform_row_count,
            center, half_width, 0, nk_rgba(57, 137, 167, 96), nk_rgba(114, 222, 239, 142), false))
        return;
    for (size_t index = 0; index < view.waveform_row_count; ++index) {
        const auto& row = view.waveform_rows[index];
        if (!finite_envelope_row(row)) continue;
        const auto y = compose_projected_y(paint, row.y_pixels);
        field_line(paint, center - row.rms[0] * half_width, y, center, y, 2.0f,
                       nk_rgba(57, 137, 167, 96), 6);
        field_line(paint, center, y, center + row.rms[1] * half_width, y, 2.0f,
                       nk_rgba(57, 137, 167, 96), 6);
        field_line(paint, center - row.absolute_peak[0] * half_width, y, center, y,
                       1.0f, nk_rgba(114, 222, 239, 142), 6);
        field_line(paint, center, y, center + row.absolute_peak[1] * half_width, y,
                       1.0f, nk_rgba(114, 222, 239, 142), 6);
    }
}

static auto compose_waveform_lane_rect(const ComposePaintContext& paint,
                                      const WormUiComposeBgmLaneView& lane) -> struct nk_rect {
    auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
    if (lane.stem && lane.width_pixels > 36.0) {
        const auto scale = paint.viewport.w / static_cast<float>(paint.view->viewport_width_pixels);
        rect.x = paint.viewport.x + static_cast<float>(lane.resize_edge_pixels - lane.width_pixels) * scale;
        rect.w = static_cast<float>(lane.width_pixels) * scale;
    }
    return rect;
}

class compose_waveform_lane_clip_t {
  public:
    compose_waveform_lane_clip_t(const ComposePaintContext& paint, const WormUiComposeBgmLaneView& lane)
        : field_(paint_field(paint.context)) {
        if (lane.stem && lane.width_pixels > 36.0) {
            const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
            previous_ = field_.push_clip({rect.x, rect.y, rect.w, rect.h});
        }
    }
    ~compose_waveform_lane_clip_t() { if (previous_) field_.pop_clip(*previous_); }
  private:
    wormedit::ui::field::renderer_t& field_;
    std::optional<SDL_FRect> previous_;
};

static void draw_compose_channel_waveform(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (view.channel_waveform_row_count == 0) {
        return;
    }
    const auto selected_lane = std::find_if(
        view.bgm_lanes, view.bgm_lanes + view.bgm_lane_count, [&](const auto& lane) {
            return lane.source_lane_id == view.channel_waveform_source_lane_id;
        });
    if (selected_lane == view.bgm_lanes + view.bgm_lane_count) {
        return;
    }
    const auto rect = compose_waveform_lane_rect(paint, *selected_lane);
    const compose_waveform_lane_clip_t clip(paint, *selected_lane);
    if (rect.w <= 1.0f || rect.h <= 0.0f) {
        return;
    }
    const auto center = rect.x + rect.w * 0.5f;
    const auto half_width = rect.w * 0.43f;
    if (draw_compose_envelope_texture(paint, view.channel_waveform_rows,
            view.channel_waveform_row_count, center, half_width, 1,
            nk_rgba(87, 157, 196, 126), nk_rgba(146, 230, 251, 224), true)) return;
    for (size_t index = 0; index < view.channel_waveform_row_count; ++index) {
        const auto& row = view.channel_waveform_rows[index];
        if (!finite_envelope_row(row)) continue;
        const auto y = compose_projected_y(paint, row.y_pixels);
        field_line(paint, center - row.rms[0] * half_width, y,
                       center + row.rms[1] * half_width, y, 2.0f, nk_rgba(87, 157, 196, 126), 6);
        field_line(paint, center - row.absolute_peak[0] * half_width, y,
                       center + row.absolute_peak[1] * half_width, y, 1.0f,
                       nk_rgba(146, 230, 251, 224), 6);
    }
}

// The BGM lane column a stem piece or cut sits in, in screen coordinates; nullopt when that lane
// is not on screen (scrolled out of the BGM pane).
static auto compose_stem_lane_rect(const ComposePaintContext& paint, uint64_t lane_id)
    -> std::optional<struct nk_rect> {
    const auto& view = *paint.view;
    const auto lanes = std::span(view.bgm_lanes, view.bgm_lane_count);
    const auto found = std::find_if(lanes.begin(), lanes.end(),
        [&](const auto& lane) { return lane.source_lane_id == lane_id; });
    if (found == lanes.end()) return std::nullopt;
    const WormUiComposeRect* bounds = &found->bounds;
    const auto rect = compose_screen_rect(bounds, &view, paint.viewport);
    if (rect.w <= 1.0f || rect.h <= 0.0f) return std::nullopt;
    return rect;
}

static auto compose_pending_piece_rect(const ComposePaintContext& paint,
        const WormUiComposeStemWaveformView& piece, struct nk_rect rect) -> struct nk_rect {
    if (!piece.bounded) return rect;
    const auto first = compose_projected_y(paint, piece.begin_y_pixels);
    const auto last = compose_projected_y(paint, piece.end_y_pixels);
    const auto low = std::clamp(std::min(first, last), rect.y, rect.y + rect.h);
    const auto high = std::clamp(std::max(first, last), rect.y, rect.y + rect.h);
    return nk_rect(rect.x, low, rect.w, high - low);
}

// One stem piece: the waveform of the slices that sit in one lane.
static void draw_compose_stem_piece(const ComposePaintContext& paint, const WormUiComposeStemWaveformView& stem) {
    const auto rect = compose_stem_lane_rect(paint, stem.source_lane_id);
    if (!rect) return;
    if (stem.pending || stem.row_count == 0) {
        field_rect(paint, compose_pending_piece_rect(paint, stem, *rect), 0.0f,
            nk_rgba(146, 230, 251, stem.pending ? 22 : 0), 6);
        return;
    }
    const auto lanes = std::span(paint.view->bgm_lanes, paint.view->bgm_lane_count);
    const auto lane = std::find_if(lanes.begin(), lanes.end(),
        [&](const auto& value) { return value.source_lane_id == stem.source_lane_id; });
    const auto waveform_rect = compose_waveform_lane_rect(paint, *lane);
    const compose_waveform_lane_clip_t clip(paint, *lane);
    const auto center = waveform_rect.x + waveform_rect.w * 0.5f;
    const auto half_width = waveform_rect.w * 0.43f;
    if (draw_compose_envelope_texture(paint, stem.rows, stem.row_count, center, half_width,
            0, nk_rgba(87, 157, 196, 96), nk_rgba(146, 230, 251, 190), true,
            stem.source_lane_id, wormedit::ui::field::waveform_piece_key_t{
                static_cast<wormedit::ui::field::waveform_piece_kind_t>(stem.piece_kind), stem.stem_key})) return;
    for (size_t row_index = 0; row_index < stem.row_count; ++row_index) {
        const auto& row = stem.rows[row_index];
        if (!finite_envelope_row(row)) continue;
        const auto y = compose_projected_y(paint, row.y_pixels);
        field_line(paint, center - row.absolute_peak[0] * half_width, y,
                   center + row.absolute_peak[1] * half_width, y, 1.0f, nk_rgba(146, 230, 251, 190), 6);
    }
}

// The tile of the note or BGM marker that starts a slice, if it is on screen (it may be a note
// in the field).
static auto compose_bgm_head_bounds(const WormUiComposeBgmEventView& event) -> WormUiComposeRect {
    auto bounds = event.bounds;
    if (event.has_tail) {
        bounds.top = event.head_top_pixels;
        bounds.bottom = event.head_bottom_pixels;
    }
    return bounds;
}

static auto compose_stem_point_tile(const ComposePaintContext& paint, uint64_t object_id, bool playable)
    -> std::optional<std::pair<struct nk_rect, bool>> {
    const auto& view = *paint.view;
    if (playable) {
        for (size_t index = 0; index < view.note_endpoint_count; ++index) {
            const auto& note = view.note_endpoints[index];
            if (note.note_id != object_id || note.proposal_ghost) continue;
            return std::pair{compose_screen_rect(&note.bounds, &view, paint.viewport), note.selected};
        }
        return std::nullopt;
    }
    for (size_t index = 0; index < view.bgm_event_count; ++index) {
        const auto& event = view.bgm_events[index];
        if (event.event_id == object_id) {
            const auto head = compose_bgm_head_bounds(event);
            return std::pair{compose_screen_rect(&head, &view, paint.viewport), event.selected};
        }
    }
    return std::nullopt;
}

// Cut lines at the slice points of every stem: green for a continue point, orange for a restart,
// bright while the pointer is on the point's tile or the tile is selected.
static void draw_compose_stem_cuts(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    const auto& mouse = paint.context->input.mouse.pos;
    for (size_t index = 0; index < view.stem_cut_count; ++index) {
        const auto& cut = view.stem_cuts[index];
        const auto rect = compose_stem_lane_rect(paint, cut.bgm_lane_id);
        if (!rect) continue;
        const auto tile = compose_stem_point_tile(paint, cut.object_id, cut.object_is_note);
        const auto hovered = tile && !view.input_occluded && mouse.x >= tile->first.x && mouse.x < tile->first.x + tile->first.w &&
                             mouse.y >= tile->first.y && mouse.y < tile->first.y + tile->first.h;
        const auto lit = hovered || (tile && tile->second);
        auto color = cut.restart ? nk_rgb(255, 164, 64) : nk_rgb(96, 214, 120);
        color.a = lit ? 255 : 120;
        const auto y = compose_projected_y(paint, cut.y_pixels);
        field_line(paint, rect->x, y, rect->x + rect->w, y, lit ? 3.0f : 2.0f, color, 9);
    }
}

// Every stem, in the lanes of its slices, with its cut lines on top.
static void draw_compose_stem_waveforms(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    for (size_t index = 0; index < view.stem_waveform_count; ++index)
        draw_compose_stem_piece(paint, view.stem_waveforms[index]);
    draw_compose_stem_cuts(paint);
}

static void draw_compose_beat_fit_region(const ComposePaintContext& paint,
                                         const ComposeTimingLaneLayout& layout) {
    const auto& view = *paint.view;
    if (!view.beat_fit_drag_active) {
        return;
    }
    const auto anchor_y = std::clamp(compose_projected_y(paint, view.beat_fit_anchor_y_pixels),
                                     paint.viewport.y, paint.viewport.y + paint.viewport.h);
    const auto beat_y = std::clamp(compose_projected_y(paint, view.beat_fit_beat_y_pixels),
                                   paint.viewport.y, paint.viewport.y + paint.viewport.h);
    const auto top = std::min(anchor_y, beat_y);
    const auto bottom = std::max(anchor_y, beat_y);
    if (bottom <= top) {
        return;
    }
    const auto right = paint.viewport.x +
                       static_cast<float>(view.playable_field_right_pixels) * paint.viewport.w /
                           static_cast<float>(view.viewport_width_pixels);
    field_rect(paint,
                 nk_rect(layout.field_left, top, right - layout.field_left, bottom - top), 0.0f,
                 nk_rgba(255, 207, 104, 22), 6);
}

static void draw_compose_differences(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->difference_count; ++index) {
        field_rect(paint, compose_screen_rect(&paint.view->differences[index].bounds,
                                              paint.view, paint.viewport),
                   0.0f, nk_rgba(229, 190, 58, 74), 6);
    }
}

static void reserve_compose_song_envelope(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (view.waveform_row_count == 0 || view.waveform_rows[0].sample_point) return;
    const auto scale = paint.viewport.w / static_cast<float>(view.viewport_width_pixels);
    const auto left = paint.viewport.x + static_cast<float>(view.playable_field_left_pixels) * scale;
    const auto right = paint.viewport.x + static_cast<float>(view.playable_field_right_pixels) * scale;
    const auto center = (left + right) * 0.5f;
    const auto half_width = std::max(0.0f, (right - left) * 0.47f);
    const auto y_scale = paint.viewport.h / static_cast<float>(view.viewport_height_pixels);
    paint_field(paint.context).reserve_envelope(0,
        {center - half_width, paint.viewport.y, half_width * 2, paint.viewport.h}, y_scale);
}

static void reserve_compose_channel_envelope(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (view.channel_waveform_row_count == 0) return;
    const auto rect = compose_stem_lane_rect(paint, view.channel_waveform_source_lane_id);
    if (!rect) return;
    const auto y_scale = paint.viewport.h / static_cast<float>(view.viewport_height_pixels);
    const auto center = rect->x + rect->w * 0.5f;
    const auto half_width = rect->w * 0.43f;
    paint_field(paint.context).reserve_envelope(1,
        {center - half_width, paint.viewport.y, half_width * 2, paint.viewport.h}, y_scale);
}

static void pin_compose_stem_envelopes(const ComposePaintContext& paint) {
    using piece_key_t = wormedit::ui::field::waveform_piece_key_t;
    std::vector<std::pair<float, piece_key_t>> ordered;
    const auto& view = *paint.view;
    ordered.reserve(view.stem_waveform_count);
    for (size_t index = 0; index < view.stem_waveform_count; ++index) {
        const auto& stem = view.stem_waveforms[index];
        if (stem.pending || stem.row_count == 0) continue;
        if (const auto rect = compose_stem_lane_rect(paint, stem.source_lane_id))
            ordered.emplace_back(rect->x, piece_key_t{
                static_cast<wormedit::ui::field::waveform_piece_kind_t>(stem.piece_kind), stem.stem_key});
    }
    // Schedule left-to-right without changing painter order for overlapping stems.
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    std::vector<piece_key_t> keys;
    keys.reserve(ordered.size());
    for (const auto& entry : ordered) keys.push_back(entry.second);
    auto& renderer = paint_field(paint.context);
    renderer.pin_piece_envelopes(keys);
    renderer.prepare_piece_zoom(view.pixels_per_standard_measure, keys);
}

static void draw_compose_waveform_layer(const ComposePaintContext& paint,
                                        const ComposeTimingLaneLayout& layout) {
    pin_compose_stem_envelopes(paint);
    reserve_compose_song_envelope(paint);
    reserve_compose_channel_envelope(paint);
    draw_compose_waveform(paint);
    draw_compose_stem_waveforms(paint);
    draw_compose_channel_waveform(paint);
    draw_compose_beat_fit_region(paint, layout);
    draw_compose_differences(paint);
}

static void draw_compose_proposed_grid(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->proposed_grid_line_count; ++index) {
        const auto& line = paint.view->proposed_grid_lines[index];
        const auto y = compose_projected_y(paint, line.y_pixels);
        const auto color = line.kind == 3                     ? nk_rgba(255, 207, 104, 230)
                           : line.kind == 1 || line.kind == 2 ? nk_rgba(255, 207, 104, 155)
                                                              : nk_rgba(255, 207, 104, 75);
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       line.kind == 3 ? 1.6f : 1.0f, color, 4);
    }
}

static void format_beat_fit_bpm(double bpm, char* text, std::size_t capacity) {
    const auto length = snprintf(text, capacity, "%.3f", bpm);
    if (length <= 0) return;
    auto end = static_cast<std::size_t>(std::min<int>(length, static_cast<int>(capacity - 1)));
    while (end > 0 && text[end - 1] == '0') text[--end] = '\0';
    if (end > 0 && text[end - 1] == '.') text[--end] = '\0';
}

static void draw_compose_beat_fit_overlay(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.beat_fit_drag_active) {
        return;
    }
    const auto y = compose_projected_y(paint, view.beat_fit_target_y_pixels);
    if (view.beat_fit_onset_snap_active && y >= paint.viewport.y &&
        y <= paint.viewport.y + paint.viewport.h) {
        field_line(paint, paint.viewport.x + paint.rail.w - 7.0f, y - 6.0f,
                   paint.viewport.x + paint.rail.w - 7.0f, y + 6.0f,
                   3.0f, nk_rgb(110, 222, 236), 4);
    }
    if (y >= paint.viewport.y && y <= paint.viewport.y + paint.viewport.h) {
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       1.3f, nk_rgba(110, 222, 236, 215), 4);
    }
    char label[140]{};
    const auto original = static_cast<double>(view.beat_fit_original_bpm_numerator) /
                          static_cast<double>(view.beat_fit_original_bpm_denominator);
    const auto proposed = static_cast<double>(view.beat_fit_proposed_bpm_numerator) /
                          static_cast<double>(view.beat_fit_proposed_bpm_denominator);
    char original_text[32]{};
    char proposed_text[32]{};
    format_beat_fit_bpm(original, original_text, sizeof(original_text));
    format_beat_fit_bpm(proposed, proposed_text, sizeof(proposed_text));
    auto length = snprintf(label, sizeof(label), "Fit %s -> %s BPM", original_text, proposed_text);
    if (view.beat_fit_snap_active && view.beat_fit_snap_step_denominator != 0) {
        const auto step = static_cast<double>(view.beat_fit_snap_step_numerator) /
                          static_cast<double>(view.beat_fit_snap_step_denominator);
        char step_text[24]{};
        format_beat_fit_bpm(step, step_text, sizeof(step_text));
        length += snprintf(label + length, sizeof(label) - static_cast<std::size_t>(length),
                           " · snap %s", step_text);
    }
    if (length > 0) {
        const auto bounds = nk_rect(paint.viewport.x + paint.rail.w + 4.0f,
                                    paint.viewport.y + 18.0f,
                                    std::min(310.0f, paint.viewport.w - paint.rail.w - 8.0f), 18.0f);
        field_text(paint, bounds, label, static_cast<int>(strlen(label)),
                     paint.context->style.font, nk_rgba(18, 21, 29, 235), nk_rgb(255, 219, 148), 4);
    }
}

// Holding Alt (Shift+Alt moves only the line) shows every measure end line that can be dragged as a band, like a bookmark's hit
// area; the one under the pointer is stronger and named.
static void draw_compose_measure_resize_affordance(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (state.measure_resize_active || view.input_occluded || view.measure_row_count == 0 ||
        view.viewport_height_pixels <= 0.0 || (SDL_GetModState() & SDL_KMOD_ALT) == 0) return;
    const auto scale = paint.viewport.h / static_cast<float>(view.viewport_height_pixels);
    const auto band = 6.0f;
    const auto inside = nk_input_is_mouse_hovering_rect(&paint.context->input, paint.viewport);
    const auto mouse_y = paint.context->input.mouse.pos.y;
    for (std::size_t index = 0; index < view.grid_line_count; ++index) {
        const auto& line = view.grid_lines[index];
        if (line.measure_number < 2 || line.measure_number - 2 >= view.measure_row_count) continue;
        const auto y = compose_projected_y(paint, line.y_pixels);
        if (y < paint.viewport.y || y > paint.viewport.y + paint.viewport.h) continue;
        const auto hovered = inside && std::abs(mouse_y - y) <= band;
        auto color = nk_rgb(110, 222, 236);
        color.a = hovered ? 70 : 26;
        field_rect(paint, nk_rect(paint.viewport.x, y - band, paint.viewport.w, band * 2.0f), 0.0f, color, 4);
        if (!hovered) continue;
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 2.2f,
                   nk_rgba(110, 222, 236, 230), 4);
        char label[48]{};
        if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0)
            snprintf(label, sizeof(label), "Drag to move only this bar line");
        else
            snprintf(label, sizeof(label), "Drag to resize measure %u", line.measure_number - 1);
        field_text(paint, nk_rect(paint.viewport.x + paint.rail.w + 4.0f, std::max(paint.viewport.y, y - 22.0f),
                                  std::min(220.0f, paint.viewport.w - paint.rail.w - 8.0f), 18.0f),
                   label, static_cast<int>(strlen(label)), paint.context->style.font,
                   nk_rgba(18, 21, 29, 235), nk_rgb(148, 229, 238), 4);
    }
}

// Alt+drag of a bar line: the proposed end line and the meter it would set.
static void draw_compose_measure_resize_overlay(const ComposePaintContext& paint) {
    if (!state.measure_resize_active) return;
    const auto y = compose_projected_y(paint, state.measure_resize_ghost_y_pixels);
    const auto color = state.measure_resize_valid ? nk_rgba(110, 222, 236, 215) : nk_rgba(232, 112, 112, 215);
    if (y >= paint.viewport.y && y <= paint.viewport.y + paint.viewport.h)
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 1.6f, color, 4);
    const auto* label = state.measure_resize_label.data();
    if (label[0] == '\0') return;
    const auto bounds = nk_rect(paint.viewport.x + paint.rail.w + 4.0f,
                                std::clamp(y - 20.0f, paint.viewport.y, paint.viewport.y + paint.viewport.h - 18.0f),
                                std::min(310.0f, paint.viewport.w - paint.rail.w - 8.0f), 18.0f);
    field_text(paint, bounds, label, static_cast<int>(strlen(label)), paint.context->style.font,
               nk_rgba(18, 21, 29, 235),
               state.measure_resize_valid ? nk_rgb(148, 229, 238) : nk_rgb(240, 150, 150), 4);
}

static void draw_compose_offset_fit_overlay(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.offset_fit_drag_active) return;
    const auto y = compose_projected_y(paint, view.offset_fit_target_y_pixels);
    if (y >= paint.viewport.y && y <= paint.viewport.y + paint.viewport.h) {
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       1.3f, nk_rgba(110, 222, 236, 215), 4);
    }
    const auto denominator = std::max<std::uint64_t>(1, view.waveform_audio_offset_denominator);
    const auto proposed_denominator =
        std::max<std::uint64_t>(1, view.waveform_proposed_audio_offset_denominator);
    const auto original = static_cast<double>(view.waveform_audio_offset_numerator) /
                          static_cast<double>(denominator);
    const auto proposed = static_cast<double>(view.waveform_proposed_audio_offset_numerator) /
                          static_cast<double>(proposed_denominator);
    char label[96]{};
    const auto length = snprintf(label, sizeof(label), "Offset %+.3f -> %+.3f s", original, proposed);
    if (length > 0) {
        const auto bounds = nk_rect(paint.viewport.x + paint.rail.w + 4.0f,
                                    paint.viewport.y + 18.0f,
                                    std::min(310.0f, paint.viewport.w - paint.rail.w - 8.0f), 18.0f);
        field_text(paint, bounds, label, static_cast<int>(strlen(label)),
                     paint.context->style.font, nk_rgba(18, 21, 29, 235), nk_rgb(148, 229, 238), 4);
    }
}

static void draw_compose_timing_preview_legend(const ComposePaintContext& paint) {
    if (paint.view->timing_preview_rect_count == 0) {
        return;
    }
    constexpr const char* legend = "Outlines: proposed notes/holds in cyan, BGM in purple";
    const bool fit_label = paint.view->beat_fit_drag_active || paint.view->offset_fit_drag_active;
    const auto top = paint.viewport.y + (fit_label ? 38.0f : 18.0f);
    const auto bounds = nk_rect(paint.viewport.x + paint.rail.w + 4.0f, top,
                                std::min(380.0f, paint.viewport.w - paint.rail.w - 8.0f), 17.0f);
    field_text(paint, bounds, legend, static_cast<int>(strlen(legend)),
                 paint.context->style.font, nk_rgba(18, 21, 29, 220), nk_rgb(194, 227, 238), 4);
}

static void draw_compose_timing_marker_drag_ghosts(const ComposePaintContext& paint) {
    if (!state.timing_marker_drag_active ||
        state.timing_marker_chart_id != paint.view->chart_id ||
        state.timing_marker_revision != paint.view->document_revision) return;
    bool selected_fixed = false;
    for (size_t index = 0; index < paint.view->timing_marker_count; ++index)
        selected_fixed |= paint.view->timing_markers[index].selected &&
                          paint.view->timing_markers[index].fixed_initial;
    const auto ghost_color = selected_fixed ? nk_rgb(242, 111, 104) : nk_rgb(255, 207, 99);
    const auto delta = static_cast<float>(state.timing_marker_current_y_pixels -
                                           state.timing_marker_anchor_y_pixels) *
                       paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    for (size_t index = 0; index < paint.view->timing_marker_count; ++index) {
        const auto& marker = paint.view->timing_markers[index];
        if (!marker.selected) continue;
        auto ghost = compose_timing_marker_screen_rect(paint, marker);
        ghost.y += delta;
        field_rect(paint, ghost, 1.0f,
                     selected_fixed ? nk_rgba(242, 111, 104, 45) : nk_rgba(255, 207, 99, 40), 4);
        field_stroke_rect(paint, ghost, 1.0f, 2.0f, ghost_color, 4);
    }
}

static void draw_compose_timing_markers(const ComposePaintContext& paint,
                                        const ComposeTimingLaneLayout& layout) {
    const auto* hovered = tile_renderer_override ? nullptr : compose_hovered_timing_marker(paint);
    for (size_t index = 0; index < paint.view->timing_marker_count; ++index) {
        const auto& marker = paint.view->timing_markers[index];
        const auto marker_lane_ordinal = static_cast<size_t>(marker.lane);
        if (marker_lane_ordinal >= layout.lanes.size() ||
            layout.lanes[marker_lane_ordinal].w <= 0.0f) {
            continue;
        }
        const auto bounds = compose_timing_marker_screen_rect(paint, marker);
        field_rect(paint, bounds, 1.0f, nk_rgb(18, 22, 31), 4);
        const auto selected = marker.selected ||
                              (marker.fixed_initial && marker.object_id == 0 &&
                               state.timing_missing_initial_selected);
        const auto outline = hovered == &marker ? nk_rgb(255, 220, 112)
                             : selected ? nk_rgb(116, 92, 49)
                                        : nk_rgb(55, 64, 79);
        field_stroke_rect(paint, bounds, 1.0f, hovered == &marker ? 2.0f : 1.5f,
                       outline, 4);
        if (!marker.short_label || !marker.short_label[0]) continue;
        const auto* font = paint.context->style.font;
        const auto length = static_cast<int>(strlen(marker.short_label));
        const auto text_width = font->width(font->userdata, font->height, marker.short_label,
                                            length);
        const auto text_bounds = nk_rect(bounds.x + 2.0f, bounds.y + (bounds.h - font->height) * 0.5f,
                                         std::max(bounds.w - 4.0f, text_width + 2.0f), font->height);
        field_text(paint, text_bounds, marker.short_label, length, font,
                     nk_rgba(18, 22, 31, 0), layout.colors[marker_lane_ordinal], 4);
    }
    draw_compose_timing_marker_drag_ghosts(paint);
}

static void draw_compose_timing_preview_markers(const ComposePaintContext& paint,
                                                const ComposeTimingLaneLayout& layout) {
    for (size_t index = 0; index < paint.view->timing_preview_marker_count; ++index) {
        const auto& marker = paint.view->timing_preview_markers[index];
        const auto marker_lane_ordinal = static_cast<size_t>(marker.lane);
        if (marker_lane_ordinal >= layout.lanes.size() ||
            layout.lanes[marker_lane_ordinal].w <= 0.0f) {
            continue;
        }
        const auto& lane = layout.lanes[marker_lane_ordinal];
        const auto y = compose_projected_y(paint, marker.marker_y_pixels);
        field_line(paint, lane.x + 2.0f, y, lane.x + lane.w - 2.0f, y, 2.0f,
                       nk_rgba(255, 207, 104, 215), 4);
    }
}

static void draw_compose_timing_stage_marker(const ComposePaintContext& paint,
                                             const ComposeTimingLaneLayout& layout) {
    if (!paint.view->timing_stage_active) {
        return;
    }
    const auto staged_timing_lane_ordinal = static_cast<size_t>(paint.view->timing_stage_lane);
    if (staged_timing_lane_ordinal >= layout.lanes.size()) {
        return;
    }
    const auto& lane = layout.lanes[staged_timing_lane_ordinal];
    const auto y = compose_projected_y(paint, paint.view->timing_stage_y_pixels);
    const auto bounds = nk_rect(lane.x + 2.0f, y - 5.0f,
                                std::max(1.0f, lane.w - 4.0f), 10.0f);
    const auto color = paint.view->timing_stage_valid ? nk_rgb(255, 221, 108)
                                                      : nk_rgb(238, 96, 88);
    field_rect(paint, bounds, 1.0f, nk_rgba(color.r, color.g, color.b, 105), 4);
    field_stroke_rect(paint, bounds, 1.0f, 2.0f, color, 4);
}

static void draw_compose_unbound_lane_keys_hint(const ComposePaintContext& paint) {
    if (!paint.view->lane_keys_unbound) return;
    constexpr const char* label = "No lane keys are bound for this chart";
    const auto bounds = nk_rect(paint.viewport.x + paint.rail.w + 4.0f,
        paint.viewport.y + 22.0f, std::min(380.0f, paint.viewport.w - paint.rail.w - 8.0f), 17.0f);
    field_text(paint, bounds, label, static_cast<int>(strlen(label)),
        paint.context->style.font, nk_rgba(18, 21, 29, 255), nk_rgb(255, 221, 108), 4);
}

// The inner horizontal extent of a visible timing lane in document pixels; empty when the lane is
// hidden. Same slot arithmetic as the marker rectangles.
auto compose_timing_lane_inner_extent(const WormUiComposeView& view, WormUiTimingLaneKind lane)
    -> std::optional<std::pair<double, double>> {
    const auto ordinal = static_cast<unsigned>(lane);
    const auto bit = static_cast<std::uint8_t>(1U << ordinal);
    const auto visible_count = std::popcount(view.timing_lane_visibility_mask);
    if (ordinal >= WORM_UI_TIMING_LANE_COUNT || visible_count == 0 ||
        (view.timing_lane_visibility_mask & bit) == 0) return std::nullopt;
    const auto slot = std::popcount(static_cast<std::uint8_t>(view.timing_lane_visibility_mask & (bit - 1)));
    const auto timing_right = view.bga_lane_visible ? view.bga_lane_bounds.left
                                                    : view.playable_field_left_pixels;
    const auto width = (timing_right - compose_measure_rail_width) / visible_count;
    const auto left = compose_measure_rail_width + width * slot + 2.0;
    return std::pair{left, left + std::max(0.0, width - 4.0)};
}

// While an edge or a new area is dragged, the area it would become follows the pointer.
static void draw_compose_kiai_drag_preview(const ComposePaintContext& paint,
                                           const std::pair<double, double>& extent) {
    if (!state.kiai_drag_active || !state.kiai_drag_moved || state.kiai_drag_speed) return;
    double fixed_y = state.kiai_drag_anchor_y;
    if (state.kiai_drag_edge == WORM_UI_SPAN_HANDLE_BODY) {
        // The whole area follows the pointer's travel.
        const std::span spans(paint.view->kiai_spans, paint.view->kiai_span_count);
        const auto found = std::ranges::find_if(
            spans, [](const auto& value) { return value.head_object_id == state.kiai_drag_head_id; });
        if (found == spans.end()) return;
        const auto travel = state.kiai_drag_current_y - state.kiai_drag_anchor_y;
        const WormUiComposeRect moved{extent.first, found->bounds.top + travel, extent.second,
                                      found->bounds.bottom + travel};
        const auto rect = compose_screen_rect(&moved, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) return;
        field_rect(paint, rect, 0.0f, nk_rgba(255, 236, 170, 70), 10);
        field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(255, 236, 170), 10);
        return;
    }
    if (state.kiai_drag_edge != WORM_UI_SPAN_HANDLE_CREATE) {
        const std::span spans(paint.view->kiai_spans, paint.view->kiai_span_count);
        const auto found = std::ranges::find_if(
            spans, [](const auto& value) { return value.head_object_id == state.kiai_drag_head_id; });
        if (found == spans.end()) return;
        const auto* span = &*found;
        const bool head_is_top = !paint.view->downscroll;
        const bool fixed_is_top = state.kiai_drag_edge == WORM_UI_SPAN_HANDLE_BEGIN ? !head_is_top : head_is_top;
        fixed_y = fixed_is_top ? span->bounds.top : span->bounds.bottom;
    }
    const WormUiComposeRect bounds{extent.first, std::min(fixed_y, state.kiai_drag_current_y),
                                   extent.second, std::max(fixed_y, state.kiai_drag_current_y)};
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 0.0f, nk_rgba(255, 236, 170, 70), 10);
    field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(255, 236, 170), 10);
}

static void draw_compose_kiai_spans(const ComposePaintContext& paint) {
    const auto extent = compose_timing_lane_inner_extent(*paint.view, WORM_UI_TIMING_LANE_KIAI);
    if (!extent) return;
    draw_compose_kiai_drag_preview(paint, *extent);
    for (size_t index = 0; index < paint.view->kiai_span_count; ++index) {
        const auto& span = paint.view->kiai_spans[index];
        const WormUiComposeRect bounds{extent->first, span.bounds.top, extent->second, span.bounds.bottom};
        const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) continue;
        field_rect(paint, rect, 0.0f, nk_rgba(250, 204, 92, span.selected ? 96 : 60), 10);
        field_stroke_rect(paint, rect, 0.0f, span.selected ? 2.0f : 1.0f,
                          span.selected ? nk_rgb(255, 236, 170) : nk_rgba(250, 204, 92, 170), 10);
        // Head (opening edge) and tail (closing edge) handles, solid so they read as grabbable.
        const auto head_y = paint.view->downscroll ? rect.y + rect.h : rect.y;
        const auto tail_y = paint.view->downscroll ? rect.y : rect.y + rect.h;
        if (span.head_visible)
            field_line(paint, rect.x, head_y, rect.x + rect.w, head_y, 4.0f, nk_rgb(250, 204, 92), 10);
        if (span.tail_visible)
            field_line(paint, rect.x, tail_y, rect.x + rect.w, tail_y, 4.0f, nk_rgb(214, 160, 46), 10);
    }
}

// Stored SV recipes: a translucent area over the recipe's rows with its relative value as a
// continuous line, grabbable begin and end edges, and a muted look when disabled.
// While a recipe edge or body is dragged, the span it would become follows the pointer.
static void draw_compose_recipe_drag_preview(const ComposePaintContext& paint) {
    if (!state.recipe_drag_active || !state.recipe_drag_moved) return;
    const std::span areas(paint.view->recipe_areas, paint.view->recipe_area_count);
    const auto found = std::ranges::find_if(
        areas, [](const auto& area) { return area.recipe_id == state.recipe_drag_id; });
    if (found == areas.end()) return;
    const auto extent = compose_timing_lane_inner_extent(
        *paint.view, static_cast<WormUiTimingLaneKind>(found->lane));
    if (!extent) return;
    double top = found->bounds.top, bottom = found->bounds.bottom;
    const bool begin_is_top = !paint.view->downscroll;
    if (state.recipe_drag_edge == 2) {
        const auto travel = state.recipe_drag_current_y - state.recipe_drag_anchor_y;
        top += travel;
        bottom += travel;
    } else {
        const bool moving_top = (state.recipe_drag_edge == 0) == begin_is_top;
        const auto fixed = moving_top ? bottom : top;
        top = std::min(fixed, state.recipe_drag_current_y);
        bottom = std::max(fixed, state.recipe_drag_current_y);
    }
    const WormUiComposeRect bounds{extent->first, top, extent->second, bottom};
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 0.0f, nk_rgba(214, 226, 255, 60), 10);
    field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(214, 226, 255), 10);
}

// The line of one recipe's relative value inside its area rectangle.
static void draw_compose_recipe_graph(const ComposePaintContext& paint,
                                      const WormUiComposeRecipeAreaView& area, struct nk_rect rect) {
    const auto x_of = [&](double fraction) {
        return rect.x + 2.0f + static_cast<float>(fraction) * std::max(0.0f, rect.w - 4.0f);
    };
    const auto color = area.previewing ? nk_rgb(255, 196, 98)
                       : area.enabled ? nk_rgb(214, 226, 255) : nk_rgb(150, 156, 170);
    for (size_t vertex = 1; vertex < area.graph_count; ++vertex) {
        const auto& from = paint.view->recipe_graph_points[area.graph_first + vertex - 1];
        const auto& to = paint.view->recipe_graph_points[area.graph_first + vertex];
        field_line(paint, x_of(from.x_fraction), compose_projected_y(paint, from.y_pixels),
                   x_of(to.x_fraction), compose_projected_y(paint, to.y_pixels), 1.5f, color, 10);
    }
}

// Area fill, outline and the grabbable begin and end edges of one recipe.
static void draw_compose_recipe_frame(const ComposePaintContext& paint,
                                      const WormUiComposeRecipeAreaView& area, struct nk_rect rect) {
    const auto base = area.enabled ? nk_rgb(150, 170, 255) : nk_rgb(120, 126, 142);
    field_rect(paint, rect, 0.0f, nk_rgba(base.r, base.g, base.b, area.selected ? 70 : 38), 10);
    field_stroke_rect(paint, rect, 0.0f, area.selected ? 2.0f : 1.0f,
                      nk_rgba(base.r, base.g, base.b, area.enabled ? 210 : 140), 10);
    const auto begin_y = paint.view->downscroll ? rect.y + rect.h : rect.y;
    const auto end_y = paint.view->downscroll ? rect.y : rect.y + rect.h;
    if (area.begin_visible) field_line(paint, rect.x, begin_y, rect.x + rect.w, begin_y, 3.0f, base, 10);
    if (area.end_visible) field_line(paint, rect.x, end_y, rect.x + rect.w, end_y, 3.0f, base, 10);
    if (!area.evaluable) {
        // No graph to draw (a Lua expression, for now): a diagonal says "not evaluated".
        field_line(paint, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, 1.0f,
                   nk_rgba(base.r, base.g, base.b, 120), 10);
    }
}

static void draw_compose_recipe_areas(const ComposePaintContext& paint) {
    draw_compose_recipe_drag_preview(paint);
    for (size_t index = 0; index < paint.view->recipe_area_count; ++index) {
        const auto& area = paint.view->recipe_areas[index];
        const auto extent = compose_timing_lane_inner_extent(
            *paint.view, static_cast<WormUiTimingLaneKind>(area.lane));
        if (!extent) continue;
        const WormUiComposeRect bounds{extent->first, area.bounds.top, extent->second, area.bounds.bottom};
        const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) continue;
        draw_compose_recipe_frame(paint, area, rect);
        if (area.evaluable) draw_compose_recipe_graph(paint, area, rect);
    }
}

// SPEEDS transitions: a faint band over the span a SPEEDS event interpolates across, stronger
// under the pointer, with its target and length written next to it when there is room.
// While a Speed edge is dragged, the span it would become follows the pointer.
static void draw_compose_speed_drag_preview(const ComposePaintContext& paint,
                                            const std::pair<double, double>& extent) {
    if (!state.kiai_drag_active || !state.kiai_drag_moved || !state.kiai_drag_speed) return;
    const std::span spans(paint.view->speed_spans, paint.view->speed_span_count);
    const auto found = std::ranges::find_if(
        spans, [](const auto& value) { return value.object_id == state.kiai_drag_head_id; });
    if (found == spans.end()) return;
    if (state.kiai_drag_edge == WORM_UI_SPAN_HANDLE_BODY) {
        // The whole span follows the pointer's travel.
        const auto travel = state.kiai_drag_current_y - state.kiai_drag_anchor_y;
        const WormUiComposeRect moved{extent.first, found->bounds.top + travel, extent.second,
                                      found->bounds.bottom + travel};
        const auto rect = compose_screen_rect(&moved, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) return;
        field_rect(paint, rect, 0.0f, nk_rgba(136, 203, 238, 70), 10);
        field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(137, 207, 239), 10);
        return;
    }
    // The edge that is not dragged stays; an instant Speed grows from its single edge.
    const bool head_is_top = !paint.view->downscroll;
    const bool fixed_is_top = state.kiai_drag_edge == WORM_UI_SPAN_HANDLE_BEGIN ? !head_is_top : head_is_top;
    const auto fixed_y = fixed_is_top ? found->bounds.top : found->bounds.bottom;
    const WormUiComposeRect bounds{extent.first, std::min(fixed_y, state.kiai_drag_current_y),
                                   extent.second, std::max(fixed_y, state.kiai_drag_current_y)};
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 0.0f, nk_rgba(136, 203, 238, 70), 10);
    field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(137, 207, 239), 10);
}

static void draw_compose_speed_spans(const ComposePaintContext& paint) {
    const auto extent = compose_timing_lane_inner_extent(*paint.view, WORM_UI_TIMING_LANE_SPEED);
    if (!extent) return;
    draw_compose_speed_drag_preview(paint, *extent);
    for (size_t index = 0; index < paint.view->speed_span_count; ++index) {
        const auto& span = paint.view->speed_spans[index];
        const WormUiComposeRect bounds{extent->first, span.bounds.top, extent->second, span.bounds.bottom};
        auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f) continue;
        if (rect.h < 3.0f) rect.h = 3.0f;
        const bool hovered = nk_input_is_mouse_hovering_rect(&paint.context->input, rect);
        field_rect(paint, rect, 0.0f, nk_rgba(136, 203, 238, hovered ? 84 : 34), 10);
        field_line(paint, rect.x, rect.y, rect.x + rect.w, rect.y, 1.0f,
                   hovered ? nk_rgb(137, 207, 239) : nk_rgba(137, 207, 239, 120), 10);
        field_line(paint, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0f,
                   hovered ? nk_rgb(91, 145, 177) : nk_rgba(91, 145, 177, 120), 10);
    }
}

// A value curve inside one timing lane: a faint 1.0x reference line, then the polyline.
static void draw_compose_value_graph(const ComposePaintContext& paint, WormUiTimingLaneKind lane,
                                     const WormUiComposeSvGraphPointView* points, size_t count,
                                     double one_fraction, struct nk_color color) {
    const auto extent = compose_timing_lane_inner_extent(*paint.view, lane);
    if (!extent || count < 2) return;
    const auto x_of = [&](double fraction) {
        const WormUiComposeRect probe{extent->first + (extent->second - extent->first) * fraction, 0.0,
                                      extent->first + (extent->second - extent->first) * fraction, 1.0};
        return compose_screen_rect(&probe, paint.view, paint.viewport).x;
    };
    if (one_fraction >= 0.0) {
        const auto x = x_of(one_fraction);
        field_line(paint, x, paint.viewport.y, x, paint.viewport.y + paint.viewport.h, 1.0f,
                   nk_rgba(160, 170, 190, 70), 10);
    }
    for (size_t index = 1; index < count; ++index) {
        const auto& from = points[index - 1];
        const auto& to = points[index];
        field_line(paint, x_of(from.x_fraction), compose_projected_y(paint, from.y_pixels),
                   x_of(to.x_fraction), compose_projected_y(paint, to.y_pixels), 1.5f, color, 10);
    }
}

static void draw_compose_sv_graph(const ComposePaintContext& paint) {
    draw_compose_value_graph(paint, WORM_UI_TIMING_LANE_SCROLL, paint.view->sv_graph_points,
                             paint.view->sv_graph_point_count, paint.view->sv_graph_one_fraction,
                             nk_rgb(110, 222, 236));
}

static void draw_compose_speed_graph(const ComposePaintContext& paint) {
    draw_compose_value_graph(paint, WORM_UI_TIMING_LANE_SPEED, paint.view->speed_graph_points,
                             paint.view->speed_graph_point_count,
                             paint.view->speed_graph_one_fraction, nk_rgb(137, 207, 239));
}

static void draw_compose_timing_layer(const ComposePaintContext& paint,
                                      const ComposeTimingLaneLayout& layout) {
    draw_compose_kiai_spans(paint);
    draw_compose_speed_spans(paint);
    draw_compose_recipe_areas(paint);
    draw_compose_sv_graph(paint);
    draw_compose_speed_graph(paint);
    draw_compose_proposed_grid(paint);
    draw_compose_beat_fit_overlay(paint);
    draw_compose_measure_resize_affordance(paint);
    draw_compose_measure_resize_overlay(paint);
    draw_compose_offset_fit_overlay(paint);
    draw_compose_timing_preview_legend(paint);
    draw_compose_unbound_lane_keys_hint(paint);
    const bool active_marker=state.timing_marker_drag_active || state.timing_missing_initial_selected ||
        compose_hovered_timing_marker(paint)!=nullptr ||
        std::ranges::any_of(std::span(paint.view->timing_markers,paint.view->timing_marker_count),
                           [](const auto& marker) { return marker.selected; });
    draw_compose_cached_pass(paint,6,true,active_marker,[&](const ComposePaintContext& cached) { draw_compose_timing_markers(cached,layout); });
    draw_compose_timing_preview_markers(paint, layout);
    draw_compose_timing_stage_marker(paint, layout);
    draw_compose_beat_sections(paint);
    std::optional<wormedit::ui::bookmark_label_box_t> previous_bookmark_label;
    draw_compose_beat_bookmarks(paint, previous_bookmark_label);
    draw_compose_audio_sections(paint);
    draw_compose_audio_bookmarks(paint, previous_bookmark_label);
}

static struct nk_color compose_selection_outline_color() {
    return wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::selection_outline);
}

static auto compose_move_conflict_color(bool ghost = false) -> nk_color {
    auto color = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::conflict);
    color.a = ghost ? 153 : 255;
    return color;
}

static void draw_compose_hold_ghost(const ComposePaintContext& paint,
                                    const WormUiComposeHoldView& hold, const struct nk_rect& rect) {
    const auto fill =
        hold.move_conflict ? compose_move_conflict_color(true) : nk_rgba(240, 171, 71, 74);
    const auto border = hold.move_conflict ? fill : nk_rgb(255, 224, 156);
    field_rect(paint, rect, 0.0f, fill, 2);
    field_stroke_rect(paint, rect, 0.0f, 1.4f, border, 2);
}

static void draw_compose_hold_bodies(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->hold_body_count; ++index) {
        const auto& hold = paint.view->hold_bodies[index];
        const auto rect = compose_screen_rect(&hold.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        if (hold.proposal_ghost) {
            draw_compose_hold_ghost(paint, hold, rect);
            continue;
        }
        const auto color = hold.proposal_origin
                               ? nk_rgb(51, 82, 100)
                               : compose_note_base_fill(
                                     worm_ui_nuklear_compose_state().color_view, hold);
        field_rect(paint, rect, 0.0f, color, 2);
        if (hold.selected) {
            field_stroke_rect(paint, rect, 0.0f, 2.0f, compose_selection_outline_color(), 2);
        }
        if (paint.view->hover_note_visible && hold.note_id == paint.view->hover_note_id) {
            field_stroke_rect(paint, rect, 0.0f, 2.0f, nk_rgb(255, 235, 148), 2);
        }
        if (hold.move_conflict_occupant)
            field_stroke_rect(paint, rect, 0.0f, 2.0f, compose_move_conflict_color(), 2);
    }
}

struct ComposeNoteStyle {
    struct nk_color fill;
    struct nk_color border;
    float border_width;
};

// Selection keeps the active view fill and adds a green outline (iBMSC style), unless a
// ghost, hover, or paint preview already owns the border.
static bool compose_note_shows_selection_outline(const WormUiComposeView& view,
                                                 const WormUiComposeNoteView& note) {
    const bool hovered = view.hover_note_visible && note.note_id == view.hover_note_id;
    const bool preview = note.proposal_ghost || note.paint_invalid_preview ||
                         note.paint_erase_preview || note.paint_preview;
    return note.selected && !hovered && !preview;
}

static auto compose_move_note_style(const WormUiComposeNoteView& note, ComposeNoteStyle style)
    -> ComposeNoteStyle {
    if (note.proposal_ghost && note.move_conflict)
        return {compose_move_conflict_color(true), compose_move_conflict_color(true), 2.0f};
    if (note.move_conflict_occupant) {
        style.border = compose_move_conflict_color();
        style.border_width = 2.0f;
    }
    return style;
}

static ComposeNoteStyle compose_note_style(const WormUiComposeView& view,
                                           const WormUiComposeNoteView& note) {
    auto fill = note.proposal_origin
                    ? nk_rgb(73, 101, 120)
                    : compose_note_base_fill(
                          worm_ui_nuklear_compose_state().color_view, note);
    auto border = nk_rgb(17, 24, 34);
    auto border_width = 1.0f;
    if (note.proposal_ghost) {
        fill = nk_rgba(240, 171, 71, 132);
        border = nk_rgb(255, 224, 156);
        border_width = 2.0f;
    } else if (view.hover_note_visible && note.note_id == view.hover_note_id) {
        fill = nk_rgb(255, 226, 127);
        border = nk_rgb(255, 247, 190);
        border_width = 2.0f;
    } else if (note.paint_invalid_preview) {
        fill = nk_rgba(245, 178, 65, 210);
        border = nk_rgb(255, 229, 166);
        border_width = 2.0f;
    } else if (note.paint_erase_preview) {
        fill = nk_rgba(244, 94, 111, 210);
        border = nk_rgb(255, 184, 190);
        border_width = 2.0f;
    } else if (note.paint_preview) {
        fill = nk_rgba(81, 218, 145, 210);
        border = nk_rgb(185, 255, 216);
        border_width = 2.0f;
    } else if (note.kind == 4) {
        fill = nk_rgb(214, 91, 104);
    } else if (note.kind == 5) {
        fill = nk_rgb(236, 194, 112);
    } else if (note.tail) {
        fill = nk_rgb(113, 201, 216);
    }
    if (compose_note_shows_selection_outline(view, note)) {
        border = compose_selection_outline_color();
        border_width = 2.0f;
    }
    return compose_move_note_style(note, {fill, border, border_width});
}

static void draw_compose_arrow_outline(const ComposePaintContext& paint,
                                        const WormUiComposeNoteView& note,
                                        const std::array<SDL_FPoint, 7>& points) {
    if (compose_arrow_layer_outline_visible(
            worm_ui_nuklear_compose_state().color_view, *paint.view, note)) {
        paint_field(paint.context).stroke_polygon(
            2, points, 4.0f,
            {note.semantic_red, note.semantic_green, note.semantic_blue, note.semantic_alpha}, 1);
    }
    const auto style = compose_note_style(*paint.view, note);
    if (note.selected || note.proposal_ghost || note.paint_preview || note.paint_erase_preview ||
        note.paint_invalid_preview ||
        (paint.view->hover_note_visible && note.note_id == paint.view->hover_note_id) ||
        note.move_conflict_occupant) {
        auto border = note.selected && !note.move_conflict && !note.move_conflict_occupant
                          ? nk_rgb(220, 247, 255)
                          : style.border;
        if (note.proposal_ghost && !note.move_conflict)
            border.a = 170;
        paint_field(paint.context).stroke_polygon(2, points, 2.0f, field_color(border), 1);
    }
}

static bool draw_compose_note_glyph(const ComposePaintContext& paint,
                                    const WormUiComposeNoteView& note, struct nk_rect tile) {
    if (!wormedit::ui::is_arrow_glyph(note.glyph) || tile.w < 10.0f) return false;
    // Artwork alone becomes square; retained scene bounds still govern hit testing.
    const auto authored = wormedit::ui::arrow_polygon(
        note.glyph, tile.x, tile.y + (tile.h - tile.w) * .5f, tile.w);
    std::array<SDL_FPoint, 7> points{};
    for (std::size_t index = 0; index < authored.size(); ++index)
        points[index] = {std::trunc(authored[index].x), std::trunc(authored[index].y)};
    // Preserve the existing snap-colored arrow artwork for transient previews.
    const auto transient = compose_note_is_transient(*paint.view, note);
    const auto color =
        note.proposal_ghost && note.move_conflict
            ? compose_move_conflict_color(true)
            : compose_note_base_fill(transient ? worm_ui_nuklear_compose_color_view_t::snap
                                               : worm_ui_nuklear_compose_state().color_view,
                                     note);
    auto& field = paint_field(paint.context);
    // Proposal ghosts stay translucent so they do not read as materialized notes.
    const auto alpha =
        static_cast<std::uint8_t>(note.proposal_ghost ? (note.move_conflict ? 153 : 120) : 255);
    field.polygon(
        2, points,
        field_color(nk_rgba(color.r, color.g, color.b, note.proposal_ghost ? alpha : color.a)), 1);
    field.stroke_polygon(2, points, 1, field_color(nk_rgba(17, 24, 34, alpha)), 1);
    draw_compose_arrow_outline(paint, note, points);
    return true;
}

static auto note_label_is_light(struct nk_color fill) -> bool {
    return (fill.r * 299 + fill.g * 587 + fill.b * 114) / 1000 >= 150;
}

// Longest UTF-8 prefix of text that, followed by an ellipsis, fits in max_width.
// Returns -1 when not even one character plus the ellipsis fits.
static auto fit_label_prefix(const nk_user_font* font, const char* text, int length,
                             float max_width) -> int {
    constexpr const char* ellipsis = "\xE2\x80\xA6";
    const auto ellipsis_width = font->width(font->userdata, font->height, ellipsis, 3);
    int best = -1;
    for (int end = 1; end < length; ++end) {
        if ((static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) continue;
        const auto width = font->width(font->userdata, font->height, text, end);
        if (width + ellipsis_width > max_width) break;
        best = end;
    }
    return best;
}

// Marker for "this note has a sound" when no readable label fits the tile.
static void draw_compose_note_sound_dot(const ComposePaintContext& paint, struct nk_rect tile,
                                        struct nk_color fill) {
    const auto size = std::min(4.0f, std::min(tile.w, tile.h) - 2.0f);
    if (size < 2.0f) return;
    const auto light = note_label_is_light(fill);
    field_rect(paint, nk_rect(tile.x + (tile.w - size) * 0.5f, tile.y + (tile.h - size) * 0.5f,
                              size, size),
               size * 0.5f, light ? nk_rgb(16, 24, 36) : nk_rgb(248, 251, 255), 2);
}

static void draw_compose_note_sound_label(const ComposePaintContext& paint,
                                          const WormUiComposeNoteView& note,
                                          struct nk_rect tile, struct nk_color fill) {
    if (note.glyph > 1 || note.tail || !note.sound_label || !note.sound_label[0]) return;
    const auto* font = paint.context->style.font;
    if (tile.h < font->height) {
        draw_compose_note_sound_dot(paint, tile, fill);
        return;
    }
    auto text = std::string{note.sound_label};
    auto text_width = font->width(font->userdata, font->height, text.c_str(),
                                  static_cast<int>(text.size()));
    if (tile.w < text_width + 4.0f) {
        const auto keep = fit_label_prefix(font, text.c_str(), static_cast<int>(text.size()),
                                           tile.w - 4.0f);
        if (keep < 0) {
            draw_compose_note_sound_dot(paint, tile, fill);
            return;
        }
        text = text.substr(0, static_cast<std::size_t>(keep)) + "\xE2\x80\xA6";
        text_width = font->width(font->userdata, font->height, text.c_str(),
                                 static_cast<int>(text.size()));
    }
    const auto text_bounds = nk_rect(tile.x + (tile.w - text_width) * 0.5f,
                                     tile.y + (tile.h - font->height) * 0.5f,
                                     text_width, font->height);
    const auto light_fill = note_label_is_light(fill);
    field_text(paint, text_bounds, text.c_str(), static_cast<int>(text.size()), font,
                 light_fill ? nk_rgba(0, 0, 0, 0) : nk_rgba(13, 21, 31, 220),
                 light_fill ? nk_rgb(16, 24, 36) : nk_rgb(248, 251, 255), 2);
}

// Semantic layer color as a 3 px stripe inside the left edge; the Layer view already fills with it.
static void draw_compose_note_layer_stripe(const ComposePaintContext& paint,
                                           const WormUiComposeNoteView& note, struct nk_rect tile) {
    if (!note.semantic_color_visible || tile.w < 6.0f ||
        worm_ui_nuklear_compose_state().color_view == worm_ui_nuklear_compose_color_view_t::layer ||
        compose_note_is_transient(*paint.view, note)) return;
    field_rect(paint, nk_rect(tile.x + 1.0f, tile.y, 3.0f, tile.h), 0.0f,
               nk_rgba(note.semantic_red, note.semantic_green, note.semantic_blue,
                       note.semantic_alpha), 2);
}

// Selected tiles keep their fill and get a 2 px green ring just outside the tile.
static void draw_compose_note_border(const ComposePaintContext& paint,
                                     const WormUiComposeNoteView& note,
                                     const ComposeNoteStyle& style, struct nk_rect tile) {
    if (compose_note_shows_selection_outline(*paint.view, note)) {
        field_stroke_rect(paint, tile, 0.0f, 1.0f, nk_rgb(17, 24, 34), 2);
        field_stroke_rect(paint, nk_rect(tile.x - 1.0f, tile.y - 1.0f, tile.w + 2.0f, tile.h + 2.0f),
                          0.0f, 2.0f, compose_selection_outline_color(), 2);
        return;
    }
    auto border = style.border;
    if (note.proposal_ghost && !note.move_conflict) border.a = 170;
    field_stroke_rect(paint, tile, 0.0f, style.border_width, border, 2);
}

static void draw_compose_note(const ComposePaintContext& paint,
                              const WormUiComposeNoteView& note) {
    const auto style = compose_note_style(*paint.view, note);
    const auto rect = compose_screen_rect(&note.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto tile = nk_rect(rect.x + 1.0f, rect.y, std::max(1.0f, rect.w - 2.0f),
                              std::max(2.0f, rect.h));
    if (draw_compose_note_glyph(paint, note, tile)) return;
    field_rect(paint, tile, 0.0f, style.fill, 2);
    draw_compose_note_border(paint, note, style, tile);
    if (note.proposal_ghost) return;
    draw_compose_note_layer_stripe(paint, note, tile);
    field_line(paint, tile.x + 1.0f, tile.y + 1.0f,
                   tile.x + tile.w - 1.0f, tile.y + 1.0f, 1.0f,
                   nk_rgba(255, 255, 255, 105), 2);
    draw_compose_note_sound_label(paint, note, tile, style.fill);
}

static void draw_compose_paint_ghost(const ComposePaintContext& paint) {
    if (!paint.view->paint_ghost_visible) return;
    const auto& note = paint.view->paint_ghost;
    auto fill = compose_note_base_fill(worm_ui_nuklear_compose_state().color_view, note);
    if (note.paint_invalid_preview) fill = nk_rgb(245, 178, 65);
    else if (note.paint_erase_preview) fill = nk_rgb(244, 94, 111);
    fill.a = 115;
    const auto rect = compose_screen_rect(&note.bounds, paint.view, paint.viewport);
    const auto tile = nk_rect(rect.x + 1, rect.y, std::max(1.0f, rect.w - 2), std::max(2.0f, rect.h));
    if (!wormedit::ui::is_arrow_glyph(note.glyph) || tile.w < 10) {
        field_rect(paint, tile, 0, fill, 2);
        return;
    }
    const auto authored = wormedit::ui::arrow_polygon(
        note.glyph, tile.x, tile.y + (tile.h - tile.w) * .5f, tile.w);
    std::array<SDL_FPoint, 7> points{};
    for (std::size_t index = 0; index < authored.size(); ++index)
        points[index] = {std::trunc(authored[index].x), std::trunc(authored[index].y)};
    paint_field(paint.context).polygon(2, points, field_color(fill), 1);
}

static void draw_compose_notes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->note_endpoint_count; ++index) {
        draw_compose_note(paint, paint.view->note_endpoints[index]);
    }
}

// Timing focus mutes each note without touching what lies behind it, so the
// waveform stays fully opaque.
static void draw_compose_notes_veil(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->note_endpoint_count; ++index) {
        const auto rect = compose_screen_rect(&paint.view->note_endpoints[index].bounds,
                                              paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) continue;
        field_rect(paint, rect, 0.0f, nk_rgba(22, 25, 33, 150), 10);
    }
}

static void draw_compose_timing_preview_rects(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->timing_preview_rect_count; ++index) {
        const auto& preview = paint.view->timing_preview_rects[index];
        const auto rect = compose_screen_rect(&preview.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto color = preview.kind == 1 ? nk_rgba(220, 164, 244, 200)
                                             : nk_rgba(110, 222, 236, 195);
        const auto fill = preview.kind == 1 ? nk_rgba(220, 164, 244, 38)
                                            : nk_rgba(110, 222, 236, 34);
        field_rect(paint, rect, 0.0f, fill, 5);
        field_stroke_rect(paint, rect, 0.0f, 1.5f, color, 5);
    }
}

static void draw_compose_playhead(const ComposePaintContext& paint) {
    if (!paint.view->playhead_visible) {
        return;
    }
    const auto y = compose_projected_y(paint, paint.view->playhead_y_pixels);
    field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 2.0f,
                   nk_rgb(93, 231, 190), 5);
}

static void draw_compose_cursor(const ComposePaintContext& paint) {
    if (!paint.view->cursor) {
        return;
    }
    const auto rect = compose_screen_rect(&paint.view->cursor->bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    // During preview the edit position remains snapped for note placement, but
    // its full-width guide follows the continuous transport position.
    const auto head_y = paint.view->playhead_visible
        ? compose_projected_y(paint, paint.view->playhead_y_pixels)
        : rect.y + rect.h * 0.5f;
    field_line(paint, paint.viewport.x, head_y, paint.viewport.x + paint.viewport.w,
                   head_y, 1.4f, nk_rgb(255, 223, 108), 5);
    field_rect(paint, rect, 0.0f, nk_rgb(248, 226, 115), 5);
    field_line(paint, rect.x, rect.y, rect.x + rect.w, rect.y, 1.0f,
                   nk_rgb(255, 243, 174), 5);
    field_line(paint, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0f,
                   nk_rgb(255, 243, 174), 5);
}

static nk_color compose_dead_span_color(std::uint8_t kind, std::uint8_t alpha) {
    switch (kind) {
    case 0: return nk_rgba(226, 110, 84, alpha);  // stop
    case 1: return nk_rgba(232, 184, 82, alpha);  // delay
    default: return nk_rgba(168, 128, 232, alpha); // warp
    }
}

// Diagonal hatch clipped to the rectangle: a row of 45 degree strokes whose ends are cut at the
// rectangle edges, so time that cannot hold a note reads as struck out.
static void draw_compose_dead_span_hatch(const ComposePaintContext& paint, struct nk_rect rect,
                                         nk_color color) {
    constexpr float spacing = 12.0f;
    const auto reach = rect.h;
    for (float offset = -reach; offset < rect.w; offset += spacing) {
        const auto x0 = std::max(rect.x, rect.x + offset);
        const auto x1 = std::min(rect.x + rect.w, rect.x + offset + reach);
        if (x1 <= x0) continue;
        const auto y0 = rect.y + rect.h - (x0 - (rect.x + offset));
        const auto y1 = rect.y + rect.h - (x1 - (rect.x + offset));
        field_line(paint, x0, y0, x1, y1, 1.0f, color, 10);
    }
}

static void draw_compose_dead_spans(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->dead_span_count; ++index) {
        const auto& span = paint.view->dead_spans[index];
        const auto rect = compose_screen_rect(&span.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) continue;
        field_rect(paint, rect, 0.0f, nk_rgba(14, 16, 22, 150), 10);
        draw_compose_dead_span_hatch(paint, rect, compose_dead_span_color(span.kind, 70));
        field_line(paint, rect.x, rect.y, rect.x + rect.w, rect.y, 1.0f,
                   compose_dead_span_color(span.kind, 200), 10);
        field_line(paint, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0f,
                   compose_dead_span_color(span.kind, 200), 10);
        if (rect.h >= 12.0f && rect.w >= 60.0f) {
            const auto text = nk_rect(rect.x + rect.w - 120.0f, rect.y, 116.0f, std::min(rect.h, 16.0f));
            field_text(paint, text, span.label, static_cast<int>(strlen(span.label)),
                       paint.context->style.font, nk_rgba(18, 21, 29, 215),
                       compose_dead_span_color(span.kind, 255), 10);
        }
    }
}

static void draw_compose_foreground_markers(const ComposePaintContext& paint) {
    draw_compose_timing_preview_rects(paint);
    draw_compose_playhead(paint);
    draw_compose_cursor(paint);
}

struct ComposeHoveredBgm {
    const char* detail{};
    bool restart{};
    bool slice{};
};

static auto compose_bgm_event_fill(const WormUiComposeBgmEventView& event) -> nk_color {
    auto fill = event.has_sound ? nk_rgb(177, 116, 207) : nk_rgb(102, 88, 113);
    if (worm_ui_nuklear_compose_state().color_view ==
        worm_ui_nuklear_compose_color_view_t::layer) {
        fill = event.semantic_color_visible
            ? nk_rgba(event.semantic_red, event.semantic_green,
                      event.semantic_blue, event.semantic_alpha)
            : wormedit::ui::theme::compose(
                  wormedit::ui::theme::ComposeColor::semantic_layer_none);
    }
    if (event.proposal_ghost) fill = event.move_conflict ? nk_rgb(230, 70, 65) : nk_rgb(238, 176, 65);
    return fill;
}

static void draw_compose_bgm_event(const ComposePaintContext& paint,
                                  const WormUiComposeBgmEventView& event,
                                  ComposeHoveredBgm& hovered) {
    auto head = event.bounds;
    if (event.has_tail) {
        head.top = event.head_top_pixels;
        head.bottom = event.head_bottom_pixels;
    }
    const auto rect = compose_screen_rect(&head, paint.view, paint.viewport);
    const auto fill = compose_bgm_event_fill(event);
    if (event.has_tail) {
        auto body = compose_screen_rect(&event.bounds, paint.view, paint.viewport);
        const auto inset = body.w * 0.325f;
        body.x += inset;
        body.w -= inset * 2.0f;
        field_rect(paint, body, 0.0f, fill, 3);
        if (event.selected) field_stroke_rect(paint, body, 0.0f, 1.0f, nk_rgb(248, 224, 255), 3);
    }
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 1.5f, fill, 3);
    field_stroke_rect(paint, rect, 1.5f, event.selected ? 2.0f : 1.0f,
                   event.selected ? nk_rgb(248, 224, 255) : nk_rgb(50, 35, 58), 3);
    if (event.bmson_slice) {
        const auto color = event.bmson_restart ? nk_rgb(255, 188, 94) : nk_rgb(119, 231, 226);
        field_line(paint, rect.x + 3.0f, rect.y + 2.0f, rect.x + 3.0f,
                       rect.y + rect.h - 2.0f, 2.0f, color, 3);
    }
    if (event.sound_label && event.sound_label[0] && rect.w >= 18.0f && rect.h >= 13.0f) {
        const auto text_rect = nk_rect(rect.x + (event.bmson_slice ? 6.0f : 3.0f), rect.y + 2.0f,
                                       std::max(1.0f, rect.w - (event.bmson_slice ? 8.0f : 5.0f)),
                                       std::min(15.0f, rect.h - 3.0f));
        field_text(paint, text_rect, event.sound_label,
                     static_cast<int>(strlen(event.sound_label)), paint.context->style.font, fill,
                     nk_rgb(255, 245, 255), 3);
    }
    if (!event.proposal_ghost && event.sound_detail && nk_input_is_mouse_hovering_rect(&paint.context->input, rect)) {
        hovered = {event.sound_detail, event.bmson_restart, event.bmson_slice};
    }
}

static void draw_compose_bgm_events(const ComposePaintContext& paint,
                                   ComposeHoveredBgm& hovered) {
    for (size_t index = 0; index < paint.view->bgm_event_count; ++index) {
        draw_compose_bgm_event(paint, paint.view->bgm_events[index], hovered);
    }
}

static void draw_compose_bmson_marker_drafts(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bmson_marker_draft_count; ++index) {
        const auto& marker = paint.view->bmson_marker_drafts[index];
        const auto rect = compose_screen_rect(&marker.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        field_rect(paint, rect, 1.5f,
                     marker.playable ? nk_rgba(85, 184, 245, 106) : nk_rgba(240, 171, 71, 94), 3);
        field_stroke_rect(paint, rect, 1.5f, 2.0f,
                       marker.restart ? nk_rgb(255, 188, 94) : nk_rgb(119, 231, 226), 3);
    }
}

static void draw_compose_bmson_move_preview(const ComposePaintContext& paint) {
    if (!paint.view->bmson_marker_move_preview) {
        return;
    }
    const auto& marker = *paint.view->bmson_marker_move_preview;
    const auto rect = compose_screen_rect(&marker.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    field_rect(paint, rect, 1.5f,
                 marker.valid ? nk_rgba(240, 171, 71, 112) : nk_rgba(239, 82, 96, 112), 3);
    field_stroke_rect(paint, rect, 1.5f, 2.0f,
                   marker.valid ? nk_rgb(255, 224, 156) : nk_rgb(255, 132, 144), 3);
}

static void draw_compose_stem_filename(const ComposePaintContext& paint,
    const char* filename, struct nk_rect rect) {
    const auto* font = paint.context->style.font;
    auto label = std::string{filename};
    const auto title = nk_rect(rect.x + 2, paint.viewport.y + 2,
                               std::max(1.0f, rect.w - 4), 14);
    if (font->width(font->userdata, font->height, label.c_str(), static_cast<int>(label.size())) > title.w) {
        const auto keep = fit_label_prefix(font, label.c_str(), static_cast<int>(label.size()), title.w);
        label = keep < 0 ? "\xE2\x80\xA6" : label.substr(0, static_cast<std::size_t>(keep)) + "\xE2\x80\xA6";
    }
    field_text(paint, title, label.c_str(), static_cast<int>(label.size()), font,
               nk_rgba(31, 25, 38, 215), nk_rgb(211, 175, 226), 3);
}
static auto hovered_compose_stem_header(const ComposePaintContext& paint,
    const WormUiComposeBgmLaneView& lane, struct nk_rect rect) -> const char* {
    if (!lane.stem || !lane.filename || paint.view->input_occluded || rect.w <= 0) return nullptr;
    const auto header = nk_rect(rect.x, paint.viewport.y, rect.w, 18);
    return nk_input_is_mouse_hovering_rect(&paint.context->input, header) ? lane.filename : nullptr;
}
static auto draw_compose_bgm_lane_labels(const ComposePaintContext& paint) -> const char* {
    const char* hovered_filename{};
    for (size_t index = 0; index < paint.view->bgm_lane_count; ++index) {
        const auto& lane = paint.view->bgm_lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (const auto* name = hovered_compose_stem_header(paint, lane, rect)) hovered_filename = name;
        if (rect.w < 18.0f || rect.h <= 0.0f) {
            continue;
        }
        if (lane.stem && lane.width_pixels >= 96 && lane.filename && lane.filename[0]) {
            draw_compose_stem_filename(paint, lane.filename, rect);
            continue;
        }
        char label[20]{};
        const auto length = snprintf(label, sizeof(label), "BGM %zu", lane.source_order + 1);
        if (length > 0) {
            const auto title = nk_rect(rect.x + 2.0f, paint.viewport.y + 2.0f,
                                       std::max(1.0f, rect.w - 4.0f), 14.0f);
            field_text(paint, title, label, static_cast<int>(strlen(label)),
                         paint.context->style.font, nk_rgba(31, 25, 38, 215),
                         nk_rgb(211, 175, 226), 3);
        }
    }
    return hovered_filename;
}

static void draw_compose_bga_stack(const ComposePaintContext& paint,
                                   const WormUiComposeBgaEventView& event) {
    const auto rect = compose_screen_rect(&event.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 1.0f, nk_rgb(95, 74, 132), 4);
    field_stroke_rect(paint, rect, 1.0f, 2.0f, nk_rgb(228, 206, 255), 4);
    char label[32];
    std::snprintf(label, sizeof(label), "%zu events", event.overlap_count);
    field_text(paint, rect, label, static_cast<int>(std::strlen(label)),
               paint.context->style.font, nk_rgb(95, 74, 132), nk_rgb(255, 255, 255), 4);
    if (nk_input_is_mouse_hovering_rect(&paint.context->input, rect))
        wormedit::ui::nuklear::tooltip::text(paint.context,
            "Choose a BGA event at this measure");
}

static void draw_compose_bga_move_ghost(const ComposePaintContext& paint,
                                        const WormUiComposeBgaEventView& event,
                                        const struct nk_rect& original) {
    if (!paint.view->bga_move_preview_valid ||
        paint.view->bga_move_preview_event_id != event.event_id) return;
    const auto y = compose_projected_y(paint, paint.view->bga_move_preview_y_pixels);
    const auto ghost = nk_rect(original.x, y, original.w, original.h);
    field_rect(paint, ghost, 1.0f, nk_rgba(145, 206, 255, 135), 4);
    field_stroke_rect(paint, ghost, 1.0f, 2.0f, nk_rgb(195, 234, 255), 4);
}

static bool is_first_bga_stack_event(const WormUiComposeView& view, size_t index) {
    const auto row = view.bga_events[index].canvas_row;
    const auto lane = wormedit::editor_bga_lane_index(
        static_cast<wormedit::visual_event_kind_t>(view.bga_events[index].kind));
    return !std::ranges::any_of(std::span(view.bga_events, index),
        [row, lane](const auto& previous) {
            return previous.canvas_row == row && wormedit::editor_bga_lane_index(
                static_cast<wormedit::visual_event_kind_t>(previous.kind)) == lane;
        });
}

static void draw_compose_bga_event(const ComposePaintContext& paint, size_t index) {
    const auto& event = paint.view->bga_events[index];
    if (event.overlap_count > WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD) {
        if (is_first_bga_stack_event(*paint.view, index)) draw_compose_bga_stack(paint, event);
        return;
    }
    const auto rect = compose_screen_rect(&event.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    const bool video = event.kind == static_cast<std::uint8_t>(wormedit::visual_event_kind_t::video);
    const auto fill = event.missing ? nk_rgb(123, 58, 65)
            : video ? nk_rgb(74, 126, 151) : nk_rgb(104, 133, 78);
    field_rect(paint, rect, 1.0f, fill, 4);
    field_stroke_rect(paint, rect, 1.0f, event.selected ? 2.0f : 1.0f,
                          event.selected ? nk_rgb(255, 240, 173) : nk_rgb(34, 39, 32), 4);
    const char* label = event.bmp_id && event.bmp_id[0] ? event.bmp_id : event.name;
    if (label && rect.w >= 18.0f && rect.h >= 13.0f) {
        const auto text_rect = nk_rect(rect.x + 1.0f, rect.y + 1.0f,
                                           std::max(1.0f, rect.w - 2.0f),
                                           std::min(14.0f, rect.h - 2.0f));
        field_text(paint, text_rect, label, static_cast<int>(strlen(label)),
                       paint.context->style.font, fill, nk_rgb(255, 255, 245), 4);
    }
    if (nk_input_is_mouse_hovering_rect(&paint.context->input, rect))
        wormedit::ui::nuklear::tooltip::text(paint.context,
            event.missing ? "Missing BGA resource" : event.name);
}

static void draw_compose_bga_events(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bga_event_count; ++index)
        draw_compose_bga_event(paint, index);
}

static void draw_compose_bga_move_preview(const ComposePaintContext& paint) {
    if (!state.bga_event_move_active) return;
    const auto events = std::span(paint.view->bga_events, paint.view->bga_event_count);
    const auto event = std::ranges::find(events, state.bga_event_move_object_id,
        &WormUiComposeBgaEventView::event_id);
    if (event == events.end()) return;
    auto rect = compose_screen_rect(&event->bounds, paint.view, paint.viewport);
    const auto target = compose_bga_lane_kind_at(*paint.view,
        state.bga_event_move_current_x_pixels, state.bga_event_move_current_y_pixels);
    for (size_t index = 0; target && index < std::min<size_t>(paint.view->bga_lane_count, 4); ++index) {
        if (paint.view->bga_lane_kinds[index] != *target) continue;
        const auto lane = compose_screen_rect(&paint.view->bga_lane_rects[index], paint.view, paint.viewport);
        rect.x = lane.x;
        rect.w = lane.w;
        break;
    }
    draw_compose_bga_move_ghost(paint, *event, rect);
}

static const char* compose_bga_lane_label(std::uint8_t kind) {
    switch (static_cast<wormedit::visual_event_kind_t>(kind)) {
    case wormedit::visual_event_kind_t::layer: return "Layer";
    case wormedit::visual_event_kind_t::layer2: return "Layer 2";
    case wormedit::visual_event_kind_t::poor: return "Poor";
    default: return "BGA";
    }
}

static void draw_compose_bga_lane(const ComposePaintContext& paint,
                                 const WormUiComposeRect& rect, std::uint8_t kind) {
    const auto bounds = compose_screen_rect(&rect, paint.view, paint.viewport);
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return;
    field_rect(paint, bounds, 0.0f, nk_rgba(25, 30, 39, 225), 1);
    field_stroke_rect(paint, bounds, 0.0f, 1.0f, nk_rgb(76, 86, 101), 1);
    const auto title = nk_rect(bounds.x + 1.0f, paint.viewport.y + 2.0f,
                               std::max(1.0f, bounds.w - 2.0f), 14.0f);
    const auto* label = compose_bga_lane_label(kind);
    field_text(paint, title, label, static_cast<int>(std::strlen(label)), paint.context->style.font,
               nk_rgba(25, 30, 39, 225), nk_rgb(192, 204, 221), 3);
}

static void draw_compose_bga(const ComposePaintContext& paint) {
    if (!paint.view->bga_lane_visible) return;
    if (paint.view->bga_lane_count == 0)
        draw_compose_bga_lane(paint, paint.view->bga_lane_bounds, 0);
    for (size_t index = 0; index < std::min<size_t>(paint.view->bga_lane_count, 4); ++index)
        draw_compose_bga_lane(paint, paint.view->bga_lane_rects[index], paint.view->bga_lane_kinds[index]);
    const bool selected = std::ranges::any_of(
        std::span(paint.view->bga_events, paint.view->bga_event_count),
        [](const auto& event) { return event.selected; });
    draw_compose_cached_pass(paint, 10, true, selected,
        [&](const ComposePaintContext& cached) { draw_compose_bga_events(cached); });
    draw_compose_bga_move_preview(paint);
}

// Highlights the right edge of the stem lane that the pointer would resize.
static void draw_compose_stem_resize_edge(const ComposePaintContext& paint) {
    const auto hovered = worm_ui_nuklear_compose_state().stem_lane_resize_hover_lane;
    if (!hovered) return;
    for (const auto& lane : std::span(paint.view->bgm_lanes, paint.view->bgm_lane_count)) {
        if (lane.source_lane_id != hovered || !lane.stem ||
            lane.resize_edge_pixels > lane.bounds.right + 1e-6) continue;
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        const auto scale = paint.viewport.w / static_cast<float>(paint.view->viewport_width_pixels);
        const auto x = paint.viewport.x + static_cast<float>(lane.resize_edge_pixels) * scale;
        field_line(paint, x, rect.y, x, rect.y + rect.h, 2.0f,
                   wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::accent), 4);
    }
}

static ComposeHoveredBgm draw_compose_audio_markers(const ComposePaintContext& paint,
                                                   bool draw_events=true) {
    ComposeHoveredBgm hovered{};
    const bool selected=std::ranges::any_of(
        std::span(paint.view->bgm_events,paint.view->bgm_event_count),
        [](const auto& event) { return event.selected || event.proposal_ghost; });
    if (draw_events)
        draw_compose_cached_pass(paint,8,true,selected,[&](const ComposePaintContext& cached) { draw_compose_bgm_events(cached,hovered); });
    // Hover details are independent of the retained visual pass.
    for (size_t index=0; index<paint.view->bgm_event_count; ++index) {
        const auto& event=paint.view->bgm_events[index];
        const auto head = compose_bgm_head_bounds(event);
        if (!event.proposal_ghost && event.sound_detail && nk_input_is_mouse_hovering_rect(&paint.context->input,
            compose_screen_rect(&head,paint.view,paint.viewport)))
            hovered={event.sound_detail,event.bmson_restart,event.bmson_slice};
    }
    draw_compose_bmson_marker_drafts(paint);
    draw_compose_bmson_move_preview(paint);
    draw_compose_stem_resize_edge(paint);
    if (const auto* filename = draw_compose_bgm_lane_labels(paint)) {
        hovered = {};
        wormedit::ui::nuklear::tooltip::text(paint.context, filename);
    }
    draw_compose_bga(paint);
    return hovered;
}

static void draw_compose_area_band(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.area_range_available || view.area_rows_per_measure == 0) return;
    const auto rect = compose_area_band_rect(view, paint.viewport);
    field_rect(paint, rect, 0, nk_rgba(75,149,215,static_cast<nk_byte>(std::min(255U,paint.view->area_band_alpha))), 5);
    for (const auto y : {rect.y, rect.y + rect.h}) {
        const auto edge = nk_rect(rect.x, y - 4, rect.w, 8);
        const auto hover = nk_input_is_mouse_hovering_rect(&paint.context->input, edge);
        field_line(paint, rect.x, y, rect.x + rect.w, y, hover ? 3.0f : 1.0f,
                   hover ? nk_rgb(164, 218, 255) : nk_rgba(92, 176, 242, 180), 5);
    }
}

static void draw_compose_selection_preview(const ComposePaintContext& paint,
                                           const WormUiComposeRect& bounds,
                                           WormUiComposeSelectionMode mode) {
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto fill = mode == WORM_UI_COMPOSE_SELECTION_ADD ? nk_rgba(113, 214, 164, 34)
                     : mode == WORM_UI_COMPOSE_SELECTION_TOGGLE ? nk_rgba(197, 151, 245, 34)
                                                               : nk_rgba(101, 183, 242, 34);
    const auto border = mode == WORM_UI_COMPOSE_SELECTION_ADD ? nk_rgb(113, 214, 164)
                        : mode == WORM_UI_COMPOSE_SELECTION_TOGGLE ? nk_rgb(197, 151, 245)
                                                                  : nk_rgb(101, 183, 242);
    field_rect(paint, rect, 0.0f, fill, 5);
    field_stroke_rect(paint, rect, 0.0f, 1.2f, border, 5);
}

static void draw_compose_space_range_preview(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.space_range_preview_visible) {
        return;
    }
    draw_compose_selection_preview(paint, view.space_range_preview_bounds,
                                   view.space_range_preview_mode);
    const auto anchor_bright = (SDL_GetTicks() / 350U) % 2U == 0U;
    const auto anchor_color = anchor_bright ? nk_rgb(255, 224, 122) : nk_rgb(185, 142, 72);
    const auto draw_row = [&](double y_pixels, struct nk_color color) {
        const auto y = compose_projected_y(paint, y_pixels);
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       2.4f, color, 5);
    };
    draw_row(view.space_range_anchor_y_pixels, anchor_color);
    if (std::abs(view.space_range_anchor_y_pixels - view.space_range_tail_y_pixels) >= 0.01) {
        draw_row(view.space_range_tail_y_pixels,
                 anchor_bright ? nk_rgb(97, 201, 255) : nk_rgb(63, 125, 176));
    }
}

static void draw_compose_autoscroll_preview(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.viewport_autoscroll_active ||
        !std::isfinite(view.viewport_autoscroll_anchor_x_pixels) ||
        !std::isfinite(view.viewport_autoscroll_anchor_y_pixels)) {
        return;
    }
    const auto anchor_x = paint.viewport.x + static_cast<float>(
        std::clamp(view.viewport_autoscroll_anchor_x_pixels, 0.0, view.viewport_width_pixels) *
        paint.viewport.w / view.viewport_width_pixels);
    const auto anchor_y = paint.viewport.y + static_cast<float>(
        std::clamp(view.viewport_autoscroll_anchor_y_pixels, 0.0, view.viewport_height_pixels) *
        paint.viewport.h / view.viewport_height_pixels);
    field_line(paint, anchor_x - 9.0f, anchor_y, anchor_x + 9.0f, anchor_y, 2.0f,
                   nk_rgb(255, 211, 103), 5);
    field_line(paint, anchor_x, anchor_y - 9.0f, anchor_x, anchor_y + 9.0f, 2.0f,
                   nk_rgb(255, 211, 103), 5);
    const auto indicator = nk_rect(paint.viewport.x + 8.0f, paint.viewport.y + paint.viewport.h - 26.0f,
                                   std::min(250.0f, std::max(150.0f, paint.viewport.w - 16.0f)),
                                   18.0f);
    field_rect(paint, indicator, 3.0f, nk_rgba(12, 16, 24, 235), 5);
    constexpr const char* text = "Auto-scroll latched - middle-click to cancel";
    field_text(paint, indicator, text, static_cast<int>(strlen(text)),
                 paint.context->style.font, nk_rgb(12, 16, 24), nk_rgb(255, 231, 157), 5);
}

static auto compose_pointer_tooltip_bounds(const ComposePaintContext& paint,
                                           float text_width, float height) -> struct nk_rect {
    const auto max_width = std::max(1.0f, paint.viewport.w - 16.0f);
    const auto width = std::clamp(text_width + 16.0f,
                                  std::min(120.0f, max_width), max_width);
    const auto mouse = paint.context->input.mouse.pos;
    const auto left = paint.viewport.x + 8.0f;
    const auto top = paint.viewport.y + 8.0f;
    const auto x = std::clamp(mouse.x + 12.0f, left,
                              std::max(left, paint.viewport.x + paint.viewport.w - width - 8.0f));
    const auto preferred_y = mouse.y - height - 8.0f < top
                                 ? mouse.y + 18.0f : mouse.y - height - 8.0f;
    const auto y = std::clamp(preferred_y, top,
                              std::max(top, paint.viewport.y + paint.viewport.h - height - 8.0f));
    return nk_rect(x, y, width, height);
}

static auto compose_hover_display_lane_number(const WormUiComposeView& view) -> uint32_t {
    for (size_t index = 0; index < view.note_endpoint_count; ++index) {
        const auto& note = view.note_endpoints[index];
        if (note.note_id == view.hover_note_id && note.display_lane_number != 0) {
            return note.display_lane_number;
        }
    }
    for (size_t index = 0; index < view.hold_body_count; ++index) {
        const auto& hold = view.hold_bodies[index];
        if (hold.note_id == view.hover_note_id && hold.display_lane_number != 0) {
            return hold.display_lane_number;
        }
    }
    return 0;
}

static auto compose_note_tooltip_title(const WormUiComposeView& view) -> std::string {
    std::string title = view.hover_tooltip;
    const auto display_lane_number = compose_hover_display_lane_number(view);
    const auto lane = title.find("lane ");
    if (display_lane_number != 0 && lane != std::string::npos) {
        const auto begin = lane + 5;
        const auto end = title.find_first_not_of("0123456789", begin);
        title.replace(begin, end == std::string::npos ? title.size() - begin : end - begin,
                      std::to_string(display_lane_number));
    }
    return title;
}

static void draw_compose_note_tooltip(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.hover_note_visible || !view.hover_tooltip || view.hover_tooltip[0] == '\0') {
        return;
    }
    std::string resource_text;
    for (size_t index = 0; index < view.note_endpoint_count; ++index) {
        const auto& note = view.note_endpoints[index];
        if (note.note_id == view.hover_note_id && note.sound_detail && note.sound_detail[0]) {
            resource_text = note.sound_detail;
            if (note.sound_label && note.sound_label[0] && resource_text != note.sound_label)
                resource_text += std::string{"  (ID "} + note.sound_label + ")";
            break;
        }
    }
    const char* resource_detail = resource_text.empty() ? nullptr : resource_text.c_str();
    const auto* font = paint.context->style.font;
    const auto title = compose_note_tooltip_title(view);
    const auto title_length = static_cast<int>(title.size());
    auto text_width = font->width(font->userdata, font->height,
                                  title.c_str(), title_length);
    if (resource_detail) {
        const auto detail_length = static_cast<int>(strlen(resource_detail));
        text_width = std::max(text_width, font->width(font->userdata, font->height,
                                                      resource_detail, detail_length));
    }
    const auto bounds = compose_pointer_tooltip_bounds(
        paint, text_width, resource_detail ? 50.0f : 24.0f);
    field_rect(paint, bounds, 3.0f, nk_rgba(12, 16, 24, 235), 5);
    field_stroke_rect(paint, bounds, 3.0f, 1.0f, nk_rgb(255, 235, 148), 5);
    field_text(paint, nk_rect(bounds.x + 8.0f, bounds.y, bounds.w - 16.0f, 24.0f),
                 title.c_str(), title_length, font,
                 nk_rgb(12, 16, 24), nk_rgb(255, 244, 192), 5);
    if (resource_detail) {
        field_text(paint, nk_rect(bounds.x + 8.0f, bounds.y + 26.0f,
                                           bounds.w - 16.0f, 24.0f),
                     resource_detail, static_cast<int>(strlen(resource_detail)), font,
                     nk_rgb(12, 16, 24), nk_rgb(245, 227, 255), 5);
    }
}

static void draw_compose_timing_tooltip(const ComposePaintContext& paint) {
    const auto* marker = compose_hovered_timing_marker(paint);
    if (!marker || !marker->detail || !marker->detail[0]) return;
    const auto* font = paint.context->style.font;
    const auto length = static_cast<int>(strlen(marker->detail));
    const auto text_width = font->width(font->userdata, font->height, marker->detail, length);
    const auto width = std::max(1.0f, std::min(paint.viewport.w - 16.0f,
                                              text_width + 16.0f));
    const auto mouse = paint.context->input.mouse.pos;
    const auto x = std::clamp(mouse.x + 10.0f, paint.viewport.x + 8.0f,
                              std::max(paint.viewport.x + 8.0f,
                                       paint.viewport.x + paint.viewport.w - width - 8.0f));
    const auto y = std::clamp(mouse.y - 32.0f, paint.viewport.y + 8.0f,
                              std::max(paint.viewport.y + 8.0f,
                                       paint.viewport.y + paint.viewport.h - 28.0f));
    const auto bounds = nk_rect(x, y, width, 24.0f);
    field_rect(paint, bounds, 3.0f, nk_rgba(12, 16, 24, 245), 5);
    field_stroke_rect(paint, bounds, 3.0f, 1.0f, nk_rgb(255, 220, 112), 5);
    field_text(paint, bounds, marker->detail, length, font,
                 nk_rgb(12, 16, 24), nk_rgb(255, 244, 192), 5);
}

static void draw_compose_bgm_tooltip(const ComposePaintContext& paint,
                                     const ComposeHoveredBgm& hovered) {
    if (!hovered.detail || !hovered.detail[0]) return;
    const auto* font = paint.context->style.font;
    const auto length = static_cast<int>(strlen(hovered.detail));
    const auto detail_width = font->width(font->userdata, font->height,
                                          hovered.detail, length);
    const auto mode_width = hovered.slice ? 76.0f : 0.0f;
    const auto bounds = compose_pointer_tooltip_bounds(
        paint, detail_width + mode_width, 24.0f);
    field_rect(paint, bounds, 3.0f, nk_rgba(12, 16, 24, 235), 5);
    field_stroke_rect(paint, bounds, 3.0f, 1.0f, nk_rgb(232, 190, 247), 5);
    auto name_bounds = nk_rect(bounds.x + 8.0f, bounds.y, bounds.w - 16.0f, bounds.h);
    if (hovered.slice) {
        const auto* mode = hovered.restart ? "Restart" : "Continue";
        field_text(paint, nk_rect(name_bounds.x, bounds.y, mode_width, bounds.h), mode,
                     static_cast<int>(strlen(mode)), font, nk_rgb(12, 16, 24),
                     hovered.restart ? nk_rgb(255, 205, 138) : nk_rgb(155, 243, 237), 5);
        name_bounds.x += mode_width;
        name_bounds.w = std::max(1.0f, name_bounds.w - mode_width);
    }
    field_text(paint, name_bounds, hovered.detail, length, font,
                 nk_rgb(12, 16, 24), nk_rgb(245, 227, 255), 5);
}

static bool draw_compose_paste_arrow(const ComposePaintContext& paint,
                                     const WormUiComposePasteGhostView& ghost,
                                     struct nk_rect tile, nk_color color) {
    if (ghost.kind != 0 || !wormedit::ui::is_arrow_glyph(ghost.glyph) || tile.w < 10.0f) return false;
    const auto authored = wormedit::ui::arrow_polygon(
        ghost.glyph, tile.x, tile.y + (tile.h - tile.w) * .5f, tile.w);
    std::array<SDL_FPoint, 7> points{};
    for (std::size_t index = 0; index < authored.size(); ++index)
        points[index] = {std::trunc(authored[index].x), std::trunc(authored[index].y)};
    auto& field = wormedit::ui::nuklear::compose_field(paint.context);
    field.polygon(5, points, field_color(color), 1);
    color.a = 255;
    field.stroke_polygon(5, points, 2.0f, field_color(color), 1);
    return true;
}

// A pasted timing object fills its lane slot; a warp keeps its full span, other objects use the
// usual marker height.
static auto compose_timing_ghost_rect(const ComposePaintContext& paint,
                                      const WormUiComposePasteGhostView& ghost) -> struct nk_rect {
    WormUiTimingMarkerView marker{};
    marker.lane = static_cast<WormUiTimingLaneKind>(ghost.glyph);
    marker.marker_y_pixels = (ghost.bounds.top + ghost.bounds.bottom) * 0.5;
    marker.marker_left_fraction = 0.0;
    marker.marker_right_fraction = 1.0;
    auto bounds = compose_timing_marker_document_rect(*paint.view, marker);
    if (ghost.bounds.bottom - ghost.bounds.top > bounds.bottom - bounds.top) {
        bounds.top = ghost.bounds.top;
        bounds.bottom = ghost.bounds.bottom;
    }
    return compose_screen_rect(&bounds, paint.view, paint.viewport);
}

static void draw_compose_paste_ghosts(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    const auto color = view.paste_valid ? nk_rgba(240, 171, 71, 130)
                                       : nk_rgba(242, 70, 83, 155);
    for (size_t index = 0; index < view.paste_ghost_count; ++index) {
        const auto& ghost = view.paste_ghosts[index];
        const auto rect = ghost.kind == 3 ? compose_timing_ghost_rect(paint, ghost)
                                          : compose_screen_rect(&ghost.bounds, &view, paint.viewport);
        if (draw_compose_paste_arrow(paint, ghost, rect, color)) continue;
        field_rect(paint, rect, 0.0f, color, 5);
        field_stroke_rect(paint, rect, 0.0f, 2.0f, color, 5);
    }
}

static void draw_compose_editor_overlays(const ComposePaintContext& paint,
                                         const ComposeHoveredBgm& hovered) {
    const auto& view = *paint.view;
    draw_compose_paste_ghosts(paint);
    draw_compose_space_range_preview(paint);
    if (view.area_selection_preview_visible) {
        draw_compose_selection_preview(paint, view.area_selection_preview_bounds,
                                       view.area_selection_preview_mode);
    }
    draw_compose_autoscroll_preview(paint);
    draw_compose_note_tooltip(paint);
    draw_compose_bgm_tooltip(paint, hovered);
    draw_compose_timing_tooltip(paint);
}

static auto compose_move_status_hint(const WormUiComposeView* view) -> const char* {
    static std::string message;
    if (!view) return nullptr;
    if (view->move_conflict_count) message = std::to_string(view->move_conflict_count) + " notes would overlap";
    else if (view->move_to_bgm_count) message = std::to_string(view->move_to_bgm_count) + " notes will move to BGM";
    else return nullptr;
    return message.c_str();
}

auto compose_editor_status_hint(const WormUiComposeView* view, const WormUiTimingView* timing_view)
    -> const char* {
    if (const auto* conflict = compose_move_status_hint(view))
        return conflict;
    if (view && view->paste_active)
        return view->paste_valid ? "Paste preview · click or Enter commits · Esc cancels"
                                : (view->paste_diagnostic ? view->paste_diagnostic : "Paste collision");
    if (state.timing_stage_drag_active)
        return "Move timing event · release to keep staged position · Apply to commit";
    if (state.beat_fit_drag_active)
        return "Fit beat to waveform · Ctrl snaps BPM · Ctrl+Alt snaps onsets · release to commit "
               "· Esc cancels";
    if (state.offset_fit_drag_active)
        return "Shift offset to waveform · release to commit · Esc cancels";
    if (state.waveform_beat_zero_drag_active)
        return "Move beat zero on waveform overview · release to commit · Esc cancels";
    if (state.bmson_marker_move_active)
        return "Move BMSON BGM marker · release to commit · Esc cancels";
    if (state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place)
        return "Resource placement armed · click a chart lane · Esc disarms";
    if (state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::replace)
        return "Resource replacement armed · click an existing object · Esc disarms";
    if (state.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off)
        return "BMSON razor armed · click/drag in selected BGM lane · Esc disarms";
    if (timing_view->stage_active && timing_view->stage_existing_object_id != 0)
        return "Drag outlined timing marker to resnap · edit/apply in side inspector";
    if (view->semantic_layer_paint_mode_active)
        return "";
    if (view->semantic_layer_solo_active)
        return "Layer solo active · only selected-layer notes shown";
    if (view->note_paint_mode_active)
        return "";
    if (view->waveform_overview_row_count != 0 && !view->waveform_timing_locked)
        return "Drag beat squares in rail to fit BPM · Shift+drag to shift offset · drag beat zero in song overview";
    return "↑/↓ move · drag selects/moves · Alt-drag hold · drag BMSON BGM to move";
}

static auto compose_notes_require_direct(const WormUiComposeView& view) -> bool {
    if (view.selected_note_count || view.note_paint_mode_active || view.semantic_layer_paint_mode_active)
        return true;
    // A published reusable source cannot contain transient note proposals;
    // the core window policy rejects those previews before source publication.
    if (view.static_scene) return false;
    return std::ranges::any_of(std::span(view.note_endpoints,view.note_endpoint_count),
        [](const auto& note) { return note.selected || note.proposal_ghost || note.proposal_origin; }) ||
        std::ranges::any_of(std::span(view.hold_bodies,view.hold_body_count),
        [](const auto& hold) { return hold.selected || hold.proposal_ghost || hold.proposal_origin; });
}

void draw_compose_scene_canvas(nk_context* context, const WormUiComposeView* view,
                                      nk_command_buffer* canvas, struct nk_rect viewport) {
    last_compose_viewport_for_testing = WormUiTestingRect{
        viewport.x, viewport.y, viewport.w, viewport.h};
    quantize_overlay_viewport = last_compose_viewport_for_testing;
    static_paint_indices.refresh(*view);
    wormedit::ui::nuklear::begin_compose_field(context,
                                              {viewport.x, viewport.y, viewport.w, viewport.h});
    paint_field(context).account_tile_source(view->static_scene_accounted_bytes+
        static_paint_indices.bytes+8*1024*1024);
    const auto parent_clip = canvas->clip;
    nk_push_scissor(canvas, viewport);
    const ComposePaintContext background{context, view, canvas, viewport, viewport};
    field_rect(background, viewport, 0.0f,
                 wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::viewport));
    if (!view->scene_available) {
        const auto* status = view->status_text ? view->status_text : "Chart field is unavailable.";
        field_text(background, viewport, status, static_cast<int>(strlen(status)),
                     context->style.font,
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::viewport),
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::label));
    } else {
        const auto rail_width = static_cast<float>(42.0 * viewport.w / view->viewport_width_pixels);
        const auto rail = nk_rect(viewport.x, viewport.y, rail_width, viewport.h);
        const auto field_left =
            viewport.x + static_cast<float>(view->playable_field_left_pixels) * viewport.w /
                             static_cast<float>(view->viewport_width_pixels);
        const ComposePaintContext paint{context, view, canvas, viewport, rail};
        ComposeTimingLaneLayout timing_layout{};
        timing_layout.field_left = view->bga_lane_visible
            ? viewport.x + static_cast<float>(view->bga_lane_bounds.left) * viewport.w /
                               static_cast<float>(view->viewport_width_pixels)
            : field_left;
        draw_compose_scene_background(paint, timing_layout);
        draw_compose_waveform_layer(paint, timing_layout);
        draw_compose_cached_pass(paint,5,true,view->beat_fit_drag_active ||
            view->selected_bms_measure_number_plus_one!=0,[&](const ComposePaintContext& cached) { draw_compose_grid_lines(cached); });
        draw_compose_lane_layout_segments(paint);
        draw_compose_dead_spans(paint);
        draw_compose_area_band(paint);
        draw_compose_timing_layer(paint, timing_layout);
        const bool active_notes=compose_notes_require_direct(*view);
        const bool active_bgm=std::ranges::any_of(std::span(view->bgm_events,view->bgm_event_count),
            [](const auto& event) { return event.selected || event.proposal_ghost; });
        const auto hold_holes=compose_hover_holes(paint,7);
        const auto note_holes=compose_hover_holes(paint,9);
        const bool grouped=!active_notes && !active_bgm && hold_holes.has_value();
        draw_compose_cached_pass(paint,7,true,!grouped,[&](const ComposePaintContext& cached) {
            draw_compose_hold_bodies(cached);
            if (grouped) {
                ComposeHoveredBgm ignored{};
                draw_compose_bgm_events(cached,ignored);
            }
        },hold_holes ? std::span<const SDL_FRect>(*hold_holes) : std::span<const SDL_FRect>{});
        const auto hovered_bgm = draw_compose_audio_markers(paint,!grouped);
        draw_compose_cached_pass(paint,9,true,active_notes || !note_holes,
            [&](const ComposePaintContext& cached) { draw_compose_notes(cached); },
            note_holes ? std::span<const SDL_FRect>(*note_holes) : std::span<const SDL_FRect>{});
        if (wormedit::ui::compose_notes_dimmed(compose_mode_of(*paint.view)))
            draw_compose_notes_veil(paint);
        draw_compose_paint_ghost(paint);
        draw_compose_foreground_markers(paint);
        draw_compose_editor_overlays(paint, hovered_bgm);
    }
    wormedit::ui::nuklear::insert_compose_field(context, canvas, viewport);
    nk_push_scissor(canvas, parent_clip);
}

namespace {
struct ToolChip {
    struct nk_rect bounds{}, source_bounds{};
    std::array<char, 516> source{};
    const char* prefix{};
    const char* suffix{};
    bool layer{}, warning{};
};

auto chip_text_width(const nk_user_font* font, const char* text) -> float {
    return font->width(font->userdata, font->height, text, static_cast<int>(std::strlen(text)));
}

void fit_chip_source(ToolChip& chip, const nk_user_font* font, const char* text, float width) {
    int length{};
    while (length < 512 && text[length]) ++length;
    while (length && (static_cast<unsigned char>(text[length]) & 0xc0U) == 0x80U) --length;
    if (font->width(font->userdata, font->height, text, length) <= width) {
        std::memcpy(chip.source.data(), text, static_cast<std::size_t>(length));
        return;
    }
    const auto prefix = fit_label_prefix(font, text, length, width);
    if (prefix < 0) return;
    std::memcpy(chip.source.data(), text, static_cast<std::size_t>(prefix));
    std::memcpy(chip.source.data() + prefix, "…", 3);
}

auto tool_chip(nk_context* context, const WormUiComposeView& view, struct nk_rect field) -> ToolChip {
    ToolChip chip;
    const auto* font = context->style.font;
    const auto available = field.w - 24.0f;
    if (!font || available < 64 || field.h < 54 || (!view.note_paint_mode_active && !view.semantic_layer_paint_mode_active))
        return chip;
    chip.layer = view.semantic_layer_paint_mode_active;
    chip.prefix = chip.layer ? "Layer · " : "Draw · ";
    chip.suffix = available < 360 || (!chip.layer && !view.draw_source_kind) ? ""
        : chip.layer ? " · Alt+drag removes" : " · right-drag erases";
    chip.warning = !chip.layer && view.draw_source_missing;
    const auto* source = chip.layer ? view.semantic_layer_target.name : view.draw_source_label;
    if (!source) source = chip.layer ? "Pick a layer" : "silent note";
    const auto prefix_width = chip_text_width(font, chip.prefix);
    const auto suffix_width = chip_text_width(font, chip.suffix);
    const auto source_width = std::max(0.0f, available - 40 - prefix_width - suffix_width);
    fit_chip_source(chip, font, source, source_width);
    const auto shown = chip_text_width(font, chip.source.data());
    chip.bounds = nk_rect(field.x + 12, field.y + 12,
        std::min(available, 40 + prefix_width + shown + suffix_width), 30);
    chip.source_bounds = nk_rect(chip.bounds.x + 32 + prefix_width, chip.bounds.y,
                                shown, chip.bounds.h);
    return chip;
}

void reveal_chip_sound(const WormUiComposeView& view) {
    state.sounds_panel_visible = true;
    state.sounds_dock_bmson_tab = false;
    state.keysound_slots_mode = false;
    state.keysound_search.fill(0);
    state.keysound_filters = {};
    state.keysound_filters.include_ungrouped = true;
    state.keysound_selection.primary = view.draw_source_id;
    state.keysound_selection.selected = {view.draw_source_id};
    state.keysound_reveal_primary = true;
}

void reveal_chip_source(const WormUiComposeView& view, bool layer) {
    state.source_chip_reveal_kind = 0;
    state.source_chip_reveal_id = state.layer_chip_reveal_id = 0;
    state.keysound_reveal_primary = state.bga_reveal_primary = false;
    state.chip_reveal_project = view.project_id;
    state.chip_reveal_chart = view.chart_id;
    if (layer) {
        if (!view.semantic_layer_target.layer_id) return;
        state.layers_panel_visible = true;
        state.layer_chip_reveal_id = view.semantic_layer_target.layer_id;
        return;
    }
    if (!view.draw_source_id || view.draw_source_missing) return;
    state.source_chip_reveal_kind = view.draw_source_kind;
    state.source_chip_reveal_id = view.draw_source_id;
    if (view.draw_source_kind == 1) reveal_chip_sound(view);
    else if (view.draw_source_kind == 2) {
        state.bga_panel_visible = true;
        state.bga_search.fill(0);
        state.bga_selection.primary = view.draw_source_id;
        state.bga_selection.selected = {view.draw_source_id};
        state.bga_reveal_primary = true;
    }
}
}

void draw_compose_tool_chip(nk_context* context, const WormUiComposeView& view,
                           nk_command_buffer* canvas, struct nk_rect field) {
    const auto chip = tool_chip(context, view, field);
    if (chip.bounds.w <= 0) return;
    const auto clip = canvas->clip;
    wormedit::ui::chrome::paint_surface(canvas, chip.bounds, 1, field);
    nk_push_scissor(canvas, field);
    const auto text = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text);
    wormedit::ui::lucide::draw(context, nk_rect(chip.bounds.x + 4, chip.bounds.y + 3, 24, 24),
        chip.layer ? wormedit::ui::lucide::Glyph::layers : wormedit::ui::lucide::Glyph::paintbrush, text);
    auto bounds = nk_rect(chip.bounds.x + 32, chip.bounds.y + 7, chip.bounds.w - 36, 16);
    nk_draw_text(canvas, bounds, chip.prefix, static_cast<int>(std::strlen(chip.prefix)),
                 context->style.font, nk_rgba(0,0,0,0), text);
    bounds.x = chip.source_bounds.x;
    bounds.w = chip.source_bounds.w;
    nk_draw_text(canvas, bounds, chip.source.data(), static_cast<int>(std::strlen(chip.source.data())),
                 context->style.font, nk_rgba(0,0,0,0), chip.warning
                    ? wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_bright) : text);
    bounds.x += bounds.w;
    bounds.w = std::max(0.0f, chip.bounds.x + chip.bounds.w - bounds.x - 4);
    nk_draw_text(canvas, bounds, chip.suffix, static_cast<int>(std::strlen(chip.suffix)),
                 context->style.font, nk_rgba(0,0,0,0), text);
    nk_push_scissor(canvas, clip);
}

auto process_compose_tool_chip(nk_context* context, const WormUiComposeView& view,
                              struct nk_rect field, bool focused) -> bool {
    const auto chip = tool_chip(context, view, field);
    const auto& button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto source = chip.layer ? view.semantic_layer_target.layer_id : view.draw_source_id;
    const auto kind = static_cast<std::uint8_t>(chip.layer ? 3
        : view.note_paint_mode_active ? view.draw_source_kind : 4);
    if (state.tool_chip_pointer_active) {
        const auto& stamp = state.tool_chip_stamp;
        state.tool_chip_pointer_canceled |= !focused || view.input_occluded ||
            stamp.project != view.project_id || stamp.chart != view.chart_id ||
            stamp.revision != view.document_revision || stamp.source != source || stamp.kind != kind;
        if (!button.down) state.tool_chip_pointer_active = false;
        return true;
    }
    if (!focused || view.input_occluded) return false;
    if (chip.bounds.w <= 0 || !button.clicked ||
        !nk_input_has_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, chip.bounds)) return false;
    state.tool_chip_pointer_active = button.down != 0;
    state.tool_chip_pointer_canceled = false;
    state.tool_chip_stamp = {view.project_id, view.chart_id, view.document_revision, source, kind};
    if (nk_input_has_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, chip.source_bounds))
        reveal_chip_source(view, chip.layer);
    return true;
}
