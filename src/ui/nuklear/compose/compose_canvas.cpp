#include "ui/field/rect.h"
#include "ui/hitsound_icons.h"
#include "ui/compose_draw_source_labels.h"

#include <wormedit/core/rational.h>
#include "core/text_util.h"
#include "ui/nuklear/chrome/text_fit.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/compose/compose_canvas.h"
#include "ui/nuklear/compose/compose_export_cuts.h"
#include "ui/compose_timing_geometry.h"
#include "ui/nuklear/compose/compose_timing_input.h"
#include "ui/nuklear/compose/compose_input_queries.h"
#include "ui/nuklear/compose/compose_inspector.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose/compose_waveform_gaps.h"
#include "ui/nuklear/chrome/tooltip.h"
#include "ui/nuklear/render/arrow_glyph.h"
#include "ui/nuklear/compose/compose_area_menu.h"
#include "ui/nuklear/compose/compose_bookmark_label.h"
#include "ui/nuklear/compose/compose_bookmark_rename.h"
#include "ui/nuklear/compose/compose_input_internal.h"
#include "ui/nuklear/compose/compose_panel_internal.h"
#include "ui/nuklear/render/theme.h"
#include "ui/nuklear/app/sdl_backend.h"

#include "wormedit/nuklear_config.h"
#include <wormedit/core/vocabulary.h>

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

using wormedit::ui::theme::ComposeColor;
using wormedit::ui::theme::compose;
using wormedit::ui::compose_bgm_head_bounds;
static_assert(static_cast<unsigned>(wormedit::ui::compose_waveform_piece_kind_t::stem) ==
              static_cast<unsigned>(wormedit::ui::field::waveform_piece_kind_t::stem));
static_assert(static_cast<unsigned>(wormedit::ui::compose_waveform_piece_kind_t::packed_event) ==
              static_cast<unsigned>(wormedit::ui::field::waveform_piece_kind_t::packed_event));

namespace {
wormedit::ui::field::renderer_t* tile_renderer_override{};
std::optional<wormedit::ui::testing_rect_view_t> last_compose_viewport_for_testing;
std::optional<wormedit::ui::testing_rect_view_t> quantize_overlay_viewport;
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
    void refresh(const wormedit::ui::compose_view_t& view) {
        scanned=0; extracted=0;
        if (!view.static_scene || (style==view.static_scene_style_generation &&
            generation==view.static_scene_source_generation)) return;
        style=view.static_scene_style_generation;
        generation=view.static_scene_source_generation;
        bytes=0;
        const auto& source=*view.static_scene;
        query_scratch.resize(std::max({source.bands.size(),source.lanes.size(),source.bgm_lanes.size(),
            source.grid_lines.size(),source.timing_markers.size(),source.hold_bodies.size(),
            source.bgm_events.size(),source.bga_events.size(),source.note_endpoints.size()}));
        bytes+=query_scratch.capacity()*sizeof(std::size_t);
        glyph_guard=32;
        for (std::size_t index=0; index<source.lanes.size(); ++index)
            glyph_guard=std::max(glyph_guard,
                (source.lanes[index].bounds.right-source.lanes[index].bounds.left)*.5+20);
        const auto rect=[](const auto& value) { return std::pair{value.bounds.top,value.bounds.bottom}; };
        rebuild_pass(2,source.bands.data(),source.bands.size(),rect);
        rebuild_pass(3,source.lanes.data(),source.lanes.size(),rect);
        rebuild_pass(4,source.bgm_lanes.data(),source.bgm_lanes.size(),rect);
        rebuild_pass(5,source.grid_lines.data(),source.grid_lines.size(),
            [](const auto& line) { return std::pair{line.y_pixels-16,line.y_pixels+16}; });
        rebuild_pass(6,source.timing_markers.data(),source.timing_markers.size(),
            [](const auto& marker) { return std::pair{marker.marker_y_pixels-16,marker.marker_y_pixels+16}; });
        rebuild_pass(7,source.hold_bodies.data(),source.hold_bodies.size(),rect);
        rebuild_pass(8,source.bgm_events.data(),source.bgm_events.size(),rect);
        rebuild_pass(9,source.note_endpoints.data(),source.note_endpoints.size(),rect);
        rebuild_pass(10,source.bga_events.data(),source.bga_events.size(),rect);
        notes_by_id.clear();
        notes_by_id.reserve(source.note_endpoints.size()+source.hold_bodies.size());
        for (std::size_t index=0; index<source.note_endpoints.size(); ++index)
            notes_by_id[source.note_endpoints[index].note_id.value].notes.push_back(index);
        for (std::size_t index=0; index<source.hold_bodies.size(); ++index)
            notes_by_id[source.hold_bodies[index].note_id.value].holds.push_back(index);
        bytes+=notes_by_id.bucket_count()*sizeof(void*);
        for (const auto& [id,records]:notes_by_id)
            bytes+=sizeof(id)+sizeof(records)+6*sizeof(void*)+
                (records.notes.capacity()+records.holds.capacity())*sizeof(std::size_t);
    }
};
// Source publication caps the total record count at 32768. Together these
// scalar/string-view records stay below 4.5 MiB; charge their retained capacity below.
// Hitsample payloads borrow the scene cache; filenames borrow the stamped document.
static_assert(sizeof(wormedit::ui::compose_note_view_t)<=144 && sizeof(wormedit::ui::compose_hold_view_t)<=96 &&
    sizeof(wormedit::ui::compose_timing_marker_view_t)<=112 && sizeof(wormedit::ui::compose_bgm_event_view_t)<=112 &&
    sizeof(wormedit::ui::compose_lane_view_t)<=96 && sizeof(wormedit::ui::compose_band_view_t)<=96 &&
    sizeof(wormedit::ui::compose_bgm_lane_view_t)<=104 && sizeof(wormedit::ui::compose_bga_event_view_t)<=112 &&
    sizeof(wormedit::ui::compose_grid_line_view_t)<=96);
struct static_paint_window_t { double origin{},height{}; };
struct static_paint_storage_t {
    static_paint_indices_t& indices;
    explicit static_paint_storage_t(static_paint_indices_t& source) : indices(source) {}
    wormedit::ui::compose_view_t view{};
    template<class T>
    void select_records(unsigned pass, const T* source, std::vector<T>& target,
                        double begin, double end, double offset) {
        const auto result=indices.passes[pass].query_into(
            begin,end,indices.query_scratch);
        indices.scanned+=result.scanned;
        // Scratch holds at least the source record count for every pass.
        // Overflow nevertheless preserves output/order with the compatible query.
        const auto fallback=result.overflow ? indices.passes[pass].query(begin,end)
                                            : wormedit::common::interval_query_t{};
        const auto indices=result.overflow ? std::span<const std::size_t>(fallback.indices)
            : std::span<const std::size_t>(this->indices.query_scratch).first(result.count);
        this->indices.extracted+=indices.size();
        target.clear();
        target.reserve(indices.size());
        for (const auto index:indices) {
            auto value=source[index];
            if constexpr (requires { value.bounds; }) {
                value.bounds.top+=offset; value.bounds.bottom+=offset;
                if constexpr (std::is_same_v<T, wormedit::ui::compose_bgm_event_view_t>) {
                    value.head_top_pixels += static_cast<float>(offset);
                    value.head_bottom_pixels += static_cast<float>(offset);
                }
            } else if constexpr (requires { value.y_pixels; }) value.y_pixels+=offset;
            else value.marker_y_pixels+=offset;
            target.push_back(value);
        }
    }
    template<class T>
    void select(unsigned pass, const std::vector<T>& source, std::vector<T>& target,
                double begin, double end, double offset) {
        select_records(pass, source.data(), target, begin, end, offset);
    }
    auto accounted_bytes() const -> std::size_t {
        return view.bands.capacity() * sizeof(wormedit::ui::compose_band_view_t) +
            view.lanes.capacity() * sizeof(wormedit::ui::compose_lane_view_t) +
            view.grid_lines.capacity() * sizeof(wormedit::ui::compose_grid_line_view_t) +
            view.timing_markers.capacity() * sizeof(wormedit::ui::compose_timing_marker_view_t) +
            view.hold_bodies.capacity() * sizeof(wormedit::ui::compose_hold_view_t) +
            view.note_endpoints.capacity() * sizeof(wormedit::ui::compose_note_view_t) +
            view.bgm_lanes.capacity() * sizeof(wormedit::ui::compose_bgm_lane_view_t) +
            view.bgm_events.capacity() * sizeof(wormedit::ui::compose_bgm_event_view_t) +
            view.bga_events.capacity() * sizeof(wormedit::ui::compose_bga_event_view_t);
    }
    void copy_drawing_metadata(const wormedit::ui::compose_view_t& current) {
        view.viewport_width_pixels = current.viewport_width_pixels;
        view.viewport_height_pixels = current.viewport_height_pixels;
        view.playable_field_left_pixels = current.playable_field_left_pixels;
        view.playable_field_right_pixels = current.playable_field_right_pixels;
        view.bga_lane_visible = current.bga_lane_visible;
        view.bga_lane_bounds = current.bga_lane_bounds;
        view.timing_lane_visibility_mask = current.timing_lane_visibility_mask;
        view.has_waveform_geometry = current.has_waveform_geometry || !current.waveform_rows.empty();
        view.waveform_timing_locked = current.waveform_timing_locked;
        view.beat_fit_chart_row = current.beat_fit_chart_row;
        view.beat_fit_drag_active = current.beat_fit_drag_active;
        view.bms_measure_labels = current.bms_measure_labels;
        view.selected_bms_measure_number_plus_one = current.selected_bms_measure_number_plus_one;
        view.hover_note_visible = current.hover_note_visible;
        view.hover_note_id.value = current.hover_note_id.value;
        view.chart_id.value = current.chart_id.value;
        view.document_revision = current.document_revision;
    }
    // Tile callbacks need drawing metadata and the selected primitive pass only.
    // Keep their owned rows between callbacks instead of copying the complete frame view.
    void build(const wormedit::ui::compose_view_t& current, unsigned pass,
               static_paint_window_t query,static_paint_window_t projection) {
        copy_drawing_metadata(current);
        view.bands.clear(); view.lanes.clear(); view.grid_lines.clear();
        view.timing_markers.clear(); view.hold_bodies.clear(); view.note_endpoints.clear();
        view.bgm_lanes.clear(); view.bgm_events.clear(); view.bga_events.clear();
        const auto& source=*current.static_scene;
        const auto offset=current.static_scene_world_origin_pixels-projection.origin;
        const auto margin=std::max(32.0,indices.glyph_guard);
        const auto query_begin=query.origin-current.static_scene_world_origin_pixels;
        const auto begin=query_begin-margin, end=query_begin+query.height+margin;
        view.viewport_height_pixels=projection.height;
        switch (pass) {
        case 2:
            select(2,source.bands,view.bands,begin,end,offset);
            select(3,source.lanes,view.lanes,begin,end,offset);
            select(4,source.bgm_lanes,view.bgm_lanes,begin,end,offset);
            break;
        case 3: select(pass,source.lanes,view.lanes,begin,end,offset); break;
        case 4: select(pass,source.bgm_lanes,view.bgm_lanes,begin,end,offset); break;
        case 5: select(pass,source.grid_lines,view.grid_lines,begin,end,offset); break;
        case 6: select(pass,source.timing_markers,view.timing_markers,begin,end,offset); break;
        case 7:
            select(7,source.hold_bodies,view.hold_bodies,begin,end,offset);
            select(8,source.bgm_events,view.bgm_events,begin,end,offset);
            break;
        case 8: select(pass,source.bgm_events,view.bgm_events,begin,end,offset); break;
        case 9: select(pass,source.note_endpoints,view.note_endpoints,begin,end,offset); break;
        case 10: select(pass,source.bga_events,view.bga_events,begin,end,offset); break;
        default: break;
        }
    }
};
}

namespace {
struct canvas_cache_state_t final : wormedit::ui::nuklear::compose_canvas_cache_t {
    static_paint_indices_t indices;
    static_paint_storage_t scratch{indices};
    bool measure_static_work{};
    wormedit::ui::nuklear::compose_static_work_t static_work;
};
auto canvas_cache(nk_context* context) -> canvas_cache_state_t& {
    auto& owner = wormedit::ui::nuklear::compose_canvas_cache(context);
    if (!owner) owner = std::make_unique<canvas_cache_state_t>();
    return static_cast<canvas_cache_state_t&>(*owner);
}
auto paint_indices(nk_context* context) -> static_paint_indices_t& {
    return canvas_cache(context).indices;
}
using static_draw_kind_t = wormedit::ui::nuklear::compose_static_draw_kind_t;
class static_draw_work_scope_t {
  public:
    static_draw_work_scope_t(nk_context* context, unsigned pass, static_draw_kind_t kind)
        : context_(context), field_(paint_field(context)) {
        auto& owner = wormedit::ui::nuklear::compose_canvas_cache(context);
        if (!owner) return;
        auto& cache = static_cast<canvas_cache_state_t&>(*owner);
        if (!cache.measure_static_work || pass >= cache.static_work.passes.size()) return;
        work_ = &cache.static_work.passes[pass][static_cast<std::size_t>(kind)];
        vertices_ = field_.vertices().size();
        indices_ = field_.indices().size();
        batches_ = field_.batches().size();
        scanned_ = cache.indices.scanned;
        extracted_ = cache.indices.extracted;
        tiles_ = field_.tile_work();
        started_ = SDL_GetTicksNS();
    }
    ~static_draw_work_scope_t() {
        if (!work_) return;
        const auto& cache = canvas_cache(context_);
        const auto tiles = field_.tile_work();
        ++work_->calls;
        work_->vertices += field_.vertices().size() - vertices_;
        work_->indices += field_.indices().size() - indices_;
        work_->batches += field_.batches().size() - batches_;
        work_->scanned += cache.indices.scanned - scanned_;
        work_->extracted += cache.indices.extracted - extracted_;
        work_->raster_vertices += tiles.raster_vertices - tiles_.raster_vertices;
        work_->raster_indices += tiles.raster_indices - tiles_.raster_indices;
        work_->raster_draws += tiles.raster_draws - tiles_.raster_draws;
        work_->nanoseconds += SDL_GetTicksNS() - started_;
    }
  private:
    nk_context* context_{};
    wormedit::ui::field::renderer_t& field_;
    wormedit::ui::nuklear::compose_static_draw_work_t* work_{};
    wormedit::ui::field::tile_work_t tiles_;
    std::size_t vertices_{}, indices_{}, batches_{}, scanned_{}, extracted_{};
    std::uint64_t started_{};
};
void refresh_static_work(nk_context* context, const wormedit::ui::compose_view_t& view) {
    auto& cache = canvas_cache(context);
    const auto generation = cache.indices.generation;
    const auto style = cache.indices.style;
    const auto started = cache.measure_static_work ? SDL_GetTicksNS() : 0;
    cache.indices.refresh(view);
    if (!cache.measure_static_work) return;
    cache.static_work = {};
    cache.static_work.index_nanoseconds = SDL_GetTicksNS() - started;
    cache.static_work.source_index_rebuilds = generation != cache.indices.generation || style != cache.indices.style;
    if (view.static_scene) {
        const auto& source = *view.static_scene;
        cache.static_work.source_records = source.bands.size() + source.lanes.size() + source.bgm_lanes.size() +
            source.grid_lines.size() + source.timing_markers.size() + source.hold_bodies.size() +
            source.bgm_events.size() + source.bga_events.size() + source.note_endpoints.size();
    }
}
void finish_static_work(nk_context* context) {
    auto& owner = wormedit::ui::nuklear::compose_canvas_cache(context);
    if (!owner) return;
    auto& cache = static_cast<canvas_cache_state_t&>(*owner);
    if (!cache.measure_static_work) return;
    const auto& field = paint_field(context);
    const auto total = field.workload();
    const auto tiles = field.tile_work();
    auto& work = cache.static_work;
    work.scanned = cache.indices.scanned;
    work.extracted = cache.indices.extracted;
    work.vertices = total.vertices;
    work.indices = total.indices;
    work.draw_calls = total.draw_calls;
    work.tile_hits = tiles.hits;
    work.tile_misses = tiles.misses;
    work.tiles_prepared = tiles.prepared;
    work.tile_bytes = tiles.retained_bytes;
}
// The renderer's existing bounded tile geometry scratch still reserves 4 MiB.
// Owned view scratch is charged by its actual retained capacity, including prior charts.
auto canvas_cache_bytes(nk_context* context) -> std::size_t {
    const auto& cache = canvas_cache(context);
    return cache.indices.bytes + cache.scratch.accounted_bytes() +
        wormedit::ui::field::tile_geometry_scratch_budget;
}
auto canvas_accounted_source_bytes(nk_context* context, const wormedit::ui::compose_view_t& view)
    -> std::size_t {
    return view.static_scene_accounted_bytes + canvas_cache_bytes(context) +
        wormedit::ui::field::chrome_cache_reservation;
}
}

void wormedit::ui::nuklear::measure_compose_static_work_for_testing(nk_context* context, bool enabled) {
    auto& cache = canvas_cache(context);
    cache.measure_static_work = enabled;
    cache.static_work = {};
}
auto wormedit::ui::nuklear::compose_static_work_for_testing(nk_context* context) -> compose_static_work_t {
    return canvas_cache(context).static_work;
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

static auto quantize_ghost_color(const wormedit::ui::compose_quantize_ghost_view_t& ghost, bool tail)
    -> nk_color {
    if (ghost.collision) {
        auto color = compose(ComposeColor::conflict);
        color.a = 153;
        return color;
    }
    if ((tail && (ghost.tail_halfway || ghost.stationary_tail_annotation)) ||
        (!tail && ghost.head_halfway)) return nk_rgba(255, 190, 56, 190);
    return nk_rgba(112, 220, 255, 170);
}

static auto draw_quantize_endpoint(const ComposePaintContext& paint, const wormedit::ui::compose_rect_t& source,
                                   wormedit::lane_glyph_t glyph, nk_color color) -> bool {
    const auto scale_x = paint.viewport.w / static_cast<float>(paint.view->viewport_width_pixels);
    const auto scale_y = paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    const auto rect = nk_rect(paint.viewport.x + static_cast<float>(source.left) * scale_x,
        paint.viewport.y + static_cast<float>(source.top) * scale_y,
        static_cast<float>(source.right - source.left) * scale_x,
        static_cast<float>(source.bottom - source.top) * scale_y);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return false;
    bool emitted{};
    if (wormedit::ui::is_arrow_glyph(glyph) && rect.w >= 10.0f) {
        const auto tile_size = rect.w;
        const auto tile_x = rect.x;
        const auto tile_y = rect.y + (rect.h - tile_size) * 0.5f;
        const auto authored = wormedit::ui::arrow_polygon(glyph,
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

static auto quantize_screen_point(struct nk_rect viewport, const wormedit::ui::compose_view_t& view,
                                 double x, double y) -> std::pair<float, float> {
    return {viewport.x + static_cast<float>(x * viewport.w / view.viewport_width_pixels),
            viewport.y + static_cast<float>(y * viewport.h / view.viewport_height_pixels)};
}

static auto draw_quantize_head_ghost(const ComposePaintContext& paint,
    const wormedit::ui::compose_quantize_ghost_view_t& ghost, struct nk_rect viewport) -> bool {
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
    const wormedit::ui::compose_quantize_ghost_view_t& ghost, struct nk_rect viewport) -> bool {
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
                                    const wormedit::ui::compose_view_t& view, struct nk_rect viewport) -> bool {
    bool emitted{};
    for (std::size_t index = 0; index < view.quantize_ghosts.size(); ++index) {
        const auto& ghost = view.quantize_ghosts[index];
        emitted = draw_quantize_head_ghost(paint, ghost, viewport) || emitted;
        if (ghost.has_tail) emitted = draw_quantize_tail_ghost(paint, ghost, viewport) || emitted;
    }
    return emitted;
}

void draw_compose_quantize_ghosts_after_docks(nk_context* context,
                                              const wormedit::ui::compose_view_t* view,
                                              bool /*section_visible*/) {
    if (!quantize_overlay_viewport) return;
    const auto saved = *quantize_overlay_viewport;
    quantize_overlay_viewport.reset();
    if (!context || !view || !view->quantize_preview_enabled || !view->quantize_ghosts_drawable ||
        !view->scene_available || view->quantize_ghosts.empty())
        return;
    const auto viewport = nk_rect(saved.x, saved.y, saved.width, saved.height);
    auto* canvas = nk_window_get_canvas(context);
    if (!canvas) return;
    wormedit::ui::nuklear::begin_compose_field(context, {viewport.x, viewport.y, viewport.w, viewport.h});
    const ComposePaintContext paint{context, view, canvas, viewport, viewport};
    quantize_ghosts_drawn_this_frame = draw_quantize_ghost_rows(paint, *view, viewport);
    wormedit::ui::nuklear::insert_compose_field(context, canvas, viewport);
}

auto compose_canvas_viewport_for_testing() -> std::optional<wormedit::ui::testing_rect_view_t> {
    return last_compose_viewport_for_testing;
}

auto compose_quantize_ghosts_drawn_for_testing() -> bool {
    return quantize_ghosts_drawn_this_frame;
}

auto compose_quantize_ghost_draw_state_for_testing() -> WormUiTestingComposeGhostDrawState {
    return quantize_ghost_draw_state;
}

static auto compose_document_viewport_origin(const wormedit::ui::compose_view_t& view) -> double {
    return view.downscroll ? -view.area_scroll_pixels-view.viewport_height_pixels : view.area_scroll_pixels;
}

static auto compose_projection_y_scale(const wormedit::ui::compose_view_t& view, struct nk_rect viewport)
    -> float {
    return viewport.h/static_cast<float>(view.viewport_height_pixels);
}

static struct nk_rect compose_unclipped_screen_rect(const wormedit::ui::compose_rect_t* source,
                                          const wormedit::ui::compose_view_t* view, struct nk_rect viewport) {
    const auto x_scale = viewport.w / static_cast<float>(view->viewport_width_pixels);
    const auto y_scale = compose_projection_y_scale(*view,viewport);
    const auto left = viewport.x + static_cast<float>(source->left) * x_scale;
    const auto top = viewport.y + static_cast<float>(source->top) * y_scale;
    const auto right = viewport.x + static_cast<float>(source->right) * x_scale;
    const auto bottom = viewport.y + static_cast<float>(source->bottom) * y_scale;
    return nk_rect(left, top, right-left, bottom-top);
}
static struct nk_rect compose_screen_rect(const wormedit::ui::compose_rect_t* source,
                                          const wormedit::ui::compose_view_t* view, struct nk_rect viewport) {
    const auto raw = compose_unclipped_screen_rect(source, view, viewport);
    const auto overlap = wormedit::ui::rect_intersection(
        {raw.x, raw.y, raw.w, raw.h}, {viewport.x, viewport.y, viewport.w, viewport.h});
    return nk_rect(overlap.x, overlap.y, overlap.w, overlap.h);
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
static auto compose_bgm_lane_width_digest(const wormedit::ui::compose_view_t& view) -> std::uint64_t {
    std::uint64_t digest = 1469598103934665603ULL;
    for (const auto& lane : std::span(view.bgm_lanes)) {
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
    const auto scale=compose_projection_y_scale(view,paint.viewport);
    request.origin=scrolling ? compose_document_viewport_origin(view)*scale : 0;
    request.key.pass=pass;
    request.key.style={view.static_scene_style_generation ^
        (wormedit::ui::theme::field_generation() * 0x9E3779B97F4A7C15ULL),
        static_cast<std::uint64_t>(view.bms_measure_labels) |
        (static_cast<std::uint64_t>(view.waveform_rows.size()!=0)<<1) |
        (static_cast<std::uint64_t>(view.waveform_timing_locked)<<2) |
        (static_cast<std::uint64_t>(view.lane_key_legend_visible)<<3) |
        (static_cast<std::uint64_t>(view.timing_focus)<<4) |
        (static_cast<std::uint64_t>(view.edit_snap_enabled)<<5) |
        (static_cast<std::uint64_t>(view.area_snap_space)<<6) |
        // Snap changes the grid lines without bumping the document revision.
        (view.area_snap_denominator<<8)};
    // Color views affect raster artwork, not the retained core scene.
    if (pass == 7 || pass == 9)
        request.key.style[1] |= static_cast<std::uint64_t>(
            worm_ui_nuklear_compose_state().input_queue.color_view) << 6;
    request.key.identity={view.project_id.value,view.chart_id.value,view.document_revision,
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
    request.key.identity.back()=(request.key.identity.back() ^
        wormedit::ui::nuklear::compose_field_font_generation(paint.context))*1099511628211ULL;
    return request;
}
template<class Draw>
static void draw_indexed_region(const ComposePaintContext& paint,unsigned pass,
                                SDL_FRect region,Draw draw) {
    if (!paint.view->static_scene || pass==5) { draw(paint); return; }
    const double scale=compose_projection_y_scale(*paint.view,paint.viewport);
    const auto delta=region.y-paint.viewport.y;
    auto& storage = canvas_cache(paint.context).scratch;
    const static_paint_window_t projection{compose_document_viewport_origin(*paint.view),
                                            paint.view->viewport_height_pixels};
    const static_paint_window_t query{projection.origin+delta/scale,region.h/scale};
    storage.build(*paint.view,pass,query,projection);
    wormedit::ui::nuklear::compose_field(paint.context).account_tile_source(
        canvas_accounted_source_bytes(paint.context, *paint.view));
    if (pass == 6) {
        // The interval halo includes neighboring bodies, but direct projection
        // publishes timing markers only when their centers are in the viewport.
        std::erase_if(storage.view.timing_markers, [&](const auto& marker) {
            return marker.marker_y_pixels < 0 ||
                   marker.marker_y_pixels > storage.view.viewport_height_pixels;
        });
    }
    auto indexed=paint; indexed.view=&storage.view;
    draw(indexed);
}
template<class Draw>
static void prepare_compose_tiles(const ComposePaintContext& paint,unsigned pass,
                                  const wormedit::ui::field::tile_request_t& request,Draw draw) {
    if (!paint.view->static_scene) return;
    const double scale=compose_projection_y_scale(*paint.view,paint.viewport);
    // Integer staging keeps legacy truncation in the positive screen-coordinate
    // domain. The tile rasterizer translates geometry and clips back afterward.
    constexpr SDL_FPoint raster_origin{0,4096};
    paint_field(paint.context).prepare_tiles(request,
        paint.view->static_scene_covered_begin_pixels*scale,
        paint.view->static_scene_covered_end_pixels*scale,
        canvas_accounted_source_bytes(paint.context, *paint.view),
        [&](auto& renderer,double origin) {
            tile_renderer_scope_t scoped(renderer);
            // A geometry halo prevents tile-edge clipping from changing authored
            // arrow centers or clamped timing marker bodies before raster cropping.
            const auto halo=std::max(96.0f,paint.viewport.w*.5f);
            auto& storage = canvas_cache(paint.context).scratch;
            const static_paint_window_t window{(origin-halo)/scale,(512+2*halo)/scale};
            storage.build(*paint.view,pass,window,window);
            wormedit::ui::nuklear::compose_field(paint.context).account_tile_source(
                canvas_accounted_source_bytes(paint.context, *paint.view));
            auto prepared=paint;
            storage.view.hover_note_visible=false;
            prepared.view=&storage.view;
            const auto staged=renderer.viewport();
            prepared.viewport=nk_rect(staged.x,staged.y-halo,paint.viewport.w,512+2*halo);
            prepared.rail=nk_rect(staged.x,staged.y-halo,paint.rail.w,512+2*halo);
            draw(prepared);
        },raster_origin);
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
static auto hover_source_rect(const ComposePaintContext& paint,wormedit::ui::compose_rect_t bounds)
    -> struct nk_rect {
    const auto origin=compose_document_viewport_origin(*paint.view);
    const auto offset=paint.view->static_scene_world_origin_pixels-origin;
    bounds.top+=offset; bounds.bottom+=offset;
    return compose_screen_rect(&bounds,paint.view,paint.viewport);
}
static auto compose_note_artwork_rect(struct nk_rect rect, wormedit::lane_glyph_t glyph)
    -> struct nk_rect {
    if (wormedit::ui::is_arrow_glyph(glyph)) {
        const auto width=std::max(1.0f,rect.w-2);
        const auto top=rect.y+(std::max(2.0f,rect.h)-width)*.5f;
        const auto bottom=std::max(rect.y+rect.h,top+width);
        rect.y=std::min(rect.y,top); rect.h=bottom-rect.y;
    }
    return nk_rect(rect.x-6,rect.y-6,rect.w+12,rect.h+12);
}
static void append_note_artwork_hole(std::vector<SDL_FRect>& holes, struct nk_rect rect,
                                     wormedit::lane_glyph_t glyph, const ComposePaintContext& paint) {
    if (rect.w<=0 || rect.h<=0) return;
    const auto artwork=compose_note_artwork_rect(rect,glyph);
    append_hover_hole(holes,{artwork.x,artwork.y,artwork.w,artwork.h},paint);
}
static void append_hover_note_holes(const ComposePaintContext& paint,
                                   const static_hover_records_t& records,
                                   std::vector<SDL_FRect>& holes) {
    for (const auto index:records.notes) {
        const auto& note=paint.view->static_scene->note_endpoints[index];
        append_note_artwork_hole(holes,hover_source_rect(paint,note.bounds),note.glyph,paint);
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
    if (!paint_field(paint.context).can_retain_tiles()) return std::nullopt;
    if (!paint.view->hover_note_visible) return std::vector<SDL_FRect>{};
    if (!paint.view->static_scene) return std::nullopt;
    const auto found=paint_indices(paint.context).notes_by_id.find(paint.view->hover_note_id.value);
    if (found==paint_indices(paint.context).notes_by_id.end()) return std::nullopt;
    std::vector<SDL_FRect> holes;
    if (pass==9) append_hover_note_holes(paint,found->second,holes);
    else append_hover_hold_holes(paint,found->second,holes);
    if (holes.size()>32) return std::nullopt;
    return holes;
}
static void include_note_edge_artwork(std::pair<float,float>& span, struct nk_rect rect,
                                       wormedit::lane_glyph_t glyph) {
    if (rect.w<=0 || rect.h<=0) return;
    const auto artwork=compose_note_artwork_rect(rect,glyph);
    span.first=std::min(span.first,artwork.y);
    span.second=std::max(span.second,artwork.y+artwork.h);
}
// Clipping a head changes the authored arrow center. Repair only artwork from
// heads that cross an edge; fully contained heads remain in the retained tile.
static auto append_note_edge_holes(const ComposePaintContext& paint, double edge,
                                   std::vector<SDL_FRect>& holes) -> bool {
    auto& indices=paint_indices(paint.context);
    const auto offset=paint.view->static_scene_world_origin_pixels-
        compose_document_viewport_origin(*paint.view);
    const auto query=indices.passes[9].query_into(
        edge-offset-indices.glyph_guard,edge-offset+indices.glyph_guard,indices.query_scratch);
    indices.scanned+=query.scanned;
    indices.extracted+=query.count;
    if (query.overflow) return false;
    std::pair<float,float> span{paint.viewport.y+paint.viewport.h,paint.viewport.y};
    for (const auto index:std::span<const std::size_t>(indices.query_scratch).first(query.count)) {
        const auto& note=paint.view->static_scene->note_endpoints[index];
        auto bounds=note.bounds;
        bounds.top+=offset; bounds.bottom+=offset;
        if (bounds.top>=0 && bounds.bottom<=paint.view->viewport_height_pixels) continue;
        auto raw=compose_unclipped_screen_rect(&bounds,paint.view,paint.viewport);
        const auto right=std::min(raw.x+raw.w,paint.viewport.x+paint.viewport.w);
        raw.x=std::max(raw.x,paint.viewport.x); raw.w=right-raw.x;
        include_note_edge_artwork(span,raw,note.glyph);
        include_note_edge_artwork(span,compose_screen_rect(&bounds,paint.view,paint.viewport),note.glyph);
    }
    // One coherent strip per edge avoids redrawing the same neighboring heads
    // for every lane and for overlapping canonical/clipped artwork rectangles.
    append_hover_hole(holes,{paint.viewport.x,span.first,paint.viewport.w,span.second-span.first},paint);
    return holes.size()<=32;
}
// Full tiles are admitted only inside the retained source coverage. Artwork
// crossing its boundary must be painted once, rather than split between two
// raster phases from a retained tile and the original direct path.
static auto append_note_coverage_edge_hole(const ComposePaintContext& paint,double edge,
                                            std::vector<SDL_FRect>& holes) -> bool {
    auto& indices=paint_indices(paint.context);
    const auto offset=paint.view->static_scene_world_origin_pixels-
        compose_document_viewport_origin(*paint.view);
    const auto query=indices.passes[9].query_into(
        edge-offset-indices.glyph_guard,edge-offset+indices.glyph_guard,indices.query_scratch);
    indices.scanned+=query.scanned;
    indices.extracted+=query.count;
    if (query.overflow) return false;
    const auto screen_edge=paint.viewport.y+edge*compose_projection_y_scale(*paint.view,paint.viewport);
    std::pair<float,float> span{paint.viewport.y+paint.viewport.h,paint.viewport.y};
    for (const auto index:std::span<const std::size_t>(indices.query_scratch).first(query.count)) {
        const auto& note=paint.view->static_scene->note_endpoints[index];
        auto bounds=note.bounds;
        bounds.top+=offset; bounds.bottom+=offset;
        const auto raw=compose_unclipped_screen_rect(&bounds,paint.view,paint.viewport);
        const auto artwork=compose_note_artwork_rect(raw,note.glyph);
        if (artwork.y>=screen_edge || artwork.y+artwork.h<=screen_edge) continue;
        include_note_edge_artwork(span,raw,note.glyph);
        include_note_edge_artwork(span,compose_screen_rect(&bounds,paint.view,paint.viewport),note.glyph);
    }
    append_hover_hole(holes,{paint.viewport.x,span.first,paint.viewport.w,span.second-span.first},paint);
    return holes.size()<=32;
}
template<class AppendEdge>
static auto append_retained_coverage_holes(const ComposePaintContext& paint,
                                           std::vector<SDL_FRect>& holes,AppendEdge append) -> bool {
    const double scale=compose_projection_y_scale(*paint.view,paint.viewport);
    const auto begin=std::ceil(paint.view->static_scene_covered_begin_pixels*scale/512)*512;
    const auto end=std::floor(paint.view->static_scene_covered_end_pixels*scale/512)*512;
    if (begin>=end) return true;
    const auto origin=compose_document_viewport_origin(*paint.view);
    for (const auto boundary:std::array{begin,end}) {
        const auto edge=boundary/scale-origin;
        if (edge<=0 || edge>=paint.view->viewport_height_pixels) continue;
        if (!append(paint,edge,holes)) return false;
    }
    return true;
}
static auto append_note_coverage_holes(const ComposePaintContext& paint,
                                       std::vector<SDL_FRect>& holes) -> bool {
    return append_retained_coverage_holes(paint,holes,append_note_coverage_edge_hole);
}
static auto append_timing_coverage_edge_hole(const ComposePaintContext& paint,double edge,
                                             std::vector<SDL_FRect>& holes) -> bool {
    auto& indices=paint_indices(paint.context);
    const auto offset=paint.view->static_scene_world_origin_pixels-
        compose_document_viewport_origin(*paint.view);
    const auto radius=std::max(indices.glyph_guard,double(paint.context->style.font->height+12));
    const auto query=indices.passes[6].query_into(edge-offset-radius,edge-offset+radius,indices.query_scratch);
    indices.scanned+=query.scanned;
    indices.extracted+=query.count;
    if (query.overflow) return false;
    const auto screen_edge=paint.viewport.y+edge*compose_projection_y_scale(*paint.view,paint.viewport);
    std::pair<float,float> span{paint.viewport.y+paint.viewport.h,paint.viewport.y};
    for (const auto index:std::span<const std::size_t>(indices.query_scratch).first(query.count)) {
        auto marker=paint.view->static_scene->timing_markers[index];
        marker.marker_y_pixels+=offset;
        const auto bounds=wormedit::ui::compose_timing_marker_bounds(*paint.view,marker);
        auto artwork=compose_unclipped_screen_rect(&bounds,paint.view,paint.viewport);
        if (artwork.w<=0 || artwork.h<=0) continue;
        const auto height=std::max(artwork.h,paint.context->style.font->height);
        artwork.y-=(height-artwork.h)*.5f+6; artwork.h=height+12;
        if (artwork.y>=screen_edge || artwork.y+artwork.h<=screen_edge) continue;
        span.first=std::min(span.first,artwork.y);
        span.second=std::max(span.second,artwork.y+artwork.h);
    }
    append_hover_hole(holes,{paint.viewport.x,span.first,paint.viewport.w,span.second-span.first},paint);
    return holes.size()<=32;
}
static auto compose_timing_coverage_holes(const ComposePaintContext& paint)
    -> std::optional<std::vector<SDL_FRect>> {
    if (!paint_field(paint.context).can_retain_tiles()) return std::nullopt;
    std::vector<SDL_FRect> holes;
    if (paint.view->static_scene &&
        !append_retained_coverage_holes(paint,holes,append_timing_coverage_edge_hole)) return std::nullopt;
    return holes;
}
static auto compose_note_holes(const ComposePaintContext& paint)
    -> std::optional<std::vector<SDL_FRect>> {
    auto holes=compose_hover_holes(paint,9);
    if (!holes || !paint.view->static_scene) return holes;
    if (!append_note_edge_holes(paint,0,*holes) ||
        !append_note_edge_holes(paint,paint.view->viewport_height_pixels,*holes) ||
        !append_note_coverage_holes(paint,*holes)) return std::nullopt;
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
    if (indexed) draw_indexed_region(paint,pass,region,draw);
    else draw(paint);
    if (capture) {
        auto strip=base;
        strip.origin+=region.y-base.viewport.y;
        strip.viewport=region;
        field.capture_tiles(strip,first);
    }
    field.pop_clip(previous);
}
static auto has_compose_pass_edge_guard(unsigned pass, bool retained) -> bool {
    return pass!=2 && !(pass==9 && retained);
}
// Ready intersections and missing strips occupy disjoint clips in the same pass.
// Dynamic selection/hover paths keep their original per-object draw order.
template<class Draw>
static void draw_compose_cached_pass(const ComposePaintContext& paint, unsigned pass,
                                    bool scrolling, bool dynamic, Draw draw,
                                    std::span<const SDL_FRect> holes={}) {
    if (dynamic || !paint_field(paint.context).can_retain_tiles()) {
        const static_draw_work_scope_t measured{paint.context, pass, static_draw_kind_t::direct};
        draw(paint);
        return;
    }
    auto& field=paint_field(paint.context);
    auto request=compose_tile_request(paint,pass,scrolling);
    const auto base_request=request;
    const bool retained=scrolling && paint.view->static_scene;
    const double scale=compose_projection_y_scale(*paint.view,paint.viewport);
    const auto guard_size=pass==9 ? paint_indices(paint.context).glyph_guard*scale : 32.0;
    if (scrolling && pass!=2 && guard_size>=request.viewport.h*.5) {
        const static_draw_work_scope_t measured{paint.context, pass, static_draw_kind_t::direct};
        draw(paint); return;
    }
    if (retained) {
        const static_draw_work_scope_t measured{paint.context, pass, static_draw_kind_t::preparation};
        prepare_compose_tiles(paint,pass,request,draw);
    }
    if (scrolling && has_compose_pass_edge_guard(pass,retained)) {
        const auto guard=static_cast<float>(guard_size);
        const auto top=std::ceil(request.viewport.y+guard);
        const auto bottom=std::floor(request.viewport.y+request.viewport.h-guard);
        for (const auto region:std::array<SDL_FRect,2>{{
            {request.viewport.x,request.viewport.y,request.viewport.w,top-request.viewport.y},
            {request.viewport.x,bottom,request.viewport.w,
                request.viewport.y+request.viewport.h-bottom}}}) {
            for (const auto outside:wormedit::ui::field::subtract_rectangles(region,holes)) {
                const static_draw_work_scope_t measured{paint.context, pass, static_draw_kind_t::guard};
                draw_static_tile_region(paint,pass,outside,base_request,retained,false,draw);
            }
        }
        request.origin+=top-request.viewport.y;
        request.viewport.y=top;
        request.viewport.h=bottom-top;
    }
    bool ready{};
    {
        const static_draw_work_scope_t measured{paint.context, pass, static_draw_kind_t::composite};
        ready = field.composite_tiles(request,holes);
    }
    if (!ready) {
        for (const auto missing:field.missing_tiles(request)) {
            for (const auto region:wormedit::ui::field::subtract_rectangles(missing,holes)) {
                const static_draw_work_scope_t measured{paint.context, pass, static_draw_kind_t::missing};
                draw_static_tile_region(paint,pass,region,base_request,retained,!retained,draw);
            }
        }
    }
    for (const auto hole:holes) {
        const static_draw_work_scope_t measured{paint.context, pass, static_draw_kind_t::hole};
        const auto previous=field.push_clip(hole);
        draw_indexed_region(paint,pass,hole,draw);
        field.pop_clip(previous);
    }
}

static void draw_compose_grid_tick(const ComposePaintContext& paint,
                                   const wormedit::ui::compose_grid_line_view_t& line, float y) {
    const auto& view = *paint.view;
    auto& field = paint_field(paint.context);
    if ((view.has_waveform_geometry || !view.waveform_rows.empty()) && !view.waveform_timing_locked && worm_ui_nuklear_beat_fit_line(line)) {
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
                                   const wormedit::ui::compose_grid_line_view_t& line) {
    const auto& view = *paint.view;
    auto& field = paint_field(paint.context);
    if (!std::isfinite(line.y_pixels) || line.y_pixels < 0.0 ||
        line.y_pixels > view.viewport_height_pixels)
        return;
    const auto y = paint.viewport.y + static_cast<float>(line.y_pixels) * paint.viewport.h /
                                          static_cast<float>(view.viewport_height_pixels);
    const auto style = wormedit::ui::theme::compose_grid_line_style(line.kind, line.emphasis, line.snap_class);
    const auto color = style.color;
    field.hline(1, paint.viewport.x, paint.viewport.x + paint.viewport.w, y, style.thickness,
                {color.r, color.g, color.b, color.a});
    draw_compose_grid_tick(paint, line, y);
}

static void draw_compose_grid_lines(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->grid_lines.size(); ++index) {
        draw_compose_grid_line(paint, paint.view->grid_lines[index]);
    }
}

static void draw_compose_lane_layout_segments(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->lane_layout_segments.size(); ++index) {
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

// Bar-line handles in the measure lane. Drawn every frame (not in the cached grid pass) because
// they follow the selection; the grid line itself stays the one bar-line projection.
static void draw_compose_measure_lane(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.measure_lane.visible) return;
    auto& field = paint_field(paint.context);
    for (size_t index = 0; index < view.grid_lines.size(); ++index) {
        const auto& line = view.grid_lines[index];
        const auto bounds = wormedit::ui::compose_bar_line_bounds(view, line);
        if (!bounds) continue;
        const auto y = compose_projected_y(paint, line.y_pixels);
        if (y < paint.viewport.y || y > paint.viewport.y + paint.viewport.h) continue;
        const bool selected = view.measure_lane.is_selected(line.measure_id);
        const auto height = selected ? 5.0f : 3.0f;
        const auto color = selected ? wormedit::ui::field::color_t{255, 207, 104}
                                    : wormedit::ui::field::color_t{120, 140, 170};
        field.rect(1, {paint.rail.x + 2.0f, y - height * 0.5f, paint.rail.w - 4.0f, height}, color);
    }
    if (state.bar_line_move.active) {
        const auto y = compose_projected_y(paint, state.bar_line_move.current_y);
        if (y >= paint.viewport.y && y <= paint.viewport.y + paint.viewport.h)
            field.rect(1, {paint.rail.x + 1.0f, y - 3.0f, paint.rail.w - 2.0f, 6.0f},
                       {255, 207, 104, 180});
    }
}

static void draw_compose_beat_sections(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->beat_sections.size(); ++index) {
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
    const wormedit::ui::bookmark_rename_line_t line{.labelled = labelled};
    const auto [top, bottom] = line.hit_extent(wormedit::ui::compose_bookmark_label_zone_height);
    const auto scale = paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    const auto above = -static_cast<float>(top) * scale;
    const auto below = static_cast<float>(bottom) * scale;
    color.a = 36;
    field_rect(paint, nk_rect(paint.viewport.x, y - above, paint.viewport.w, above + below),
               0.0f, color, 4);
}

// While a bookmark is dragged, a dashed-looking ghost line follows the pointer; the bookmark
// itself moves to the snapped row on release.
static void draw_compose_bookmark_move_ghost(const ComposePaintContext& paint) {
    if (!state.bookmark_ui.bookmark_move_active || state.bookmark_ui.bookmark_move_chart_id != paint.view->chart_id.value) return;
    const auto scale = paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    const auto y = paint.viewport.y + static_cast<float>(state.bookmark_ui.bookmark_move_current_y_pixels) * scale;
    field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 1.6f,
               nk_rgb(255, 218, 139), 4);
}

static void draw_compose_beat_bookmarks(const ComposePaintContext& paint,
    std::optional<wormedit::ui::bookmark_label_box_t>& previous) {
    draw_compose_bookmark_move_ghost(paint);
    const auto hovered = state.bookmark_ui.bookmark_move_active ? state.bookmark_ui.bookmark_move_id
                                                    : compose_hovered_bookmark_id(paint);
    for (size_t index = 0; index < paint.view->beat_bookmarks.size(); ++index) {
        const auto& bookmark = paint.view->beat_bookmarks[index];
        if (bookmark.projection_visible) {
            const auto y = compose_projected_y(paint, bookmark.projected_y_pixels);
            const auto hover = bookmark.bookmark_id.value == hovered;
            if (hover) draw_compose_bookmark_hover(paint, y, bookmark.label.data() && bookmark.label.data()[0],
                                                   nk_rgb(111, 224, 201));
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, hover ? 2.2f : 1.3f, nk_rgb(111, 224, 201), 4);
            draw_compose_bookmark_label(paint, bookmark.label.data(), y, nk_rgb(111, 224, 201), previous);
        }
        if (bookmark.proposed_projection_visible) {
            const auto y = compose_projected_y(paint, bookmark.proposed_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.6f, nk_rgb(255, 218, 139), 4);
        }
    }
}

static void draw_compose_audio_sections(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->audio_sections.size(); ++index) {
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

// The song preview band. A point-only preview ends at the audition default, drawn fainter
// because no length is stored.
// While a preview edge or label is dragged, a ghost line follows the pointer; the preview moves to
// the snapped row on release.
static void draw_compose_audio_span_drag_ghost(const ComposePaintContext& paint) {
    const auto& drag = state.audio_span_drag;
    if (!drag.active || drag.chart_id != paint.view->chart_id.value) return;
    const auto scale = paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    const auto y = paint.viewport.y + static_cast<float>(drag.current_y_pixels) * scale;
    const auto color = drag.export_cut_id.value
        ? wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::export_cut) : nk_rgb(150, 240, 180);
    field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 1.6f, color, drag.export_cut_id.value ? 10 : 4);
}

static void draw_compose_preview_span(const ComposePaintContext& paint,
    std::optional<wormedit::ui::bookmark_label_box_t>& previous) {
    const auto& span = paint.view->preview_span;
    if (!span.available) return;
    if (!state.audio_span_drag.export_cut_id.value) draw_compose_audio_span_drag_ghost(paint);
    const auto line = nk_rgb(110, 224, 150);
    const auto end_line = span.has_length ? line : nk_rgba(110, 224, 150, 110);
    if (span.span_projection_visible) {
        const auto top = compose_projected_y(paint, span.projected_span_top_y_pixels);
        const auto bottom = compose_projected_y(paint, span.projected_span_bottom_y_pixels);
        field_rect(paint, nk_rect(paint.viewport.x, top, paint.viewport.w, std::max(1.0f, bottom - top)),
                   0.0f, nk_rgba(96, 214, 140, span.has_length ? 26 : 14), 4);
    }
    if (span.begin_projection_visible) {
        const auto y = compose_projected_y(paint, span.projected_begin_y_pixels);
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 1.4f, line, 4);
        draw_compose_bookmark_label(paint, span.chart_override ? "Preview (this chart)" : "Preview",
                                    y, line, previous);
    }
    if (span.end_projection_visible) {
        const auto y = compose_projected_y(paint, span.projected_end_y_pixels);
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                   span.has_length ? 1.4f : 1.0f, end_line, 4);
    }
}

static void draw_compose_audio_bookmarks(const ComposePaintContext& paint,
    std::optional<wormedit::ui::bookmark_label_box_t>& previous) {
    const auto hovered = compose_hovered_bookmark_id(paint);
    for (size_t index = 0; index < paint.view->audio_bookmarks.size(); ++index) {
        const auto& bookmark = paint.view->audio_bookmarks[index];
        if (!bookmark.projection_visible) {
            continue;
        }
        const auto y = compose_projected_y(paint, bookmark.projected_y_pixels);
        const auto hover = bookmark.bookmark_id.value == hovered;
        if (hover) draw_compose_bookmark_hover(paint, y, bookmark.label.data() && bookmark.label.data()[0],
                                               nk_rgb(232, 166, 255));
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       hover ? 2.3f : 1.4f, nk_rgb(232, 166, 255), 4);
        draw_compose_bookmark_label(paint, bookmark.label.data(), y, nk_rgb(232, 166, 255), previous);
    }
}

// Generic lanes without a key color take their color from the lane role.
static auto compose_lane_key_color(wormedit::lane_key_color_t key_color, wormedit::lane_role_t role)
    -> wormedit::lane_key_color_t {
    if (key_color != wormedit::lane_key_color_t::generic) return key_color;
    switch (role) {
    case wormedit::lane_role_t::scratch: return wormedit::lane_key_color_t::scratch;
    case wormedit::lane_role_t::free_zone: return wormedit::lane_key_color_t::free_zone;
    case wormedit::lane_role_t::foot_pedal: return wormedit::lane_key_color_t::foot_pedal;
    default: return wormedit::lane_key_color_t::generic;
    }
}

struct ComposeTimingLaneLayout {
    std::array<struct nk_rect, wormedit::timing_lane_kind_count> lanes{};
    std::array<struct nk_color, wormedit::timing_lane_kind_count> colors{};
    float field_left{};
};

static auto compose_timing_marker_document_rect(const wormedit::ui::compose_view_t& view,
                                                const wormedit::ui::compose_timing_marker_view_t& marker)
    -> wormedit::ui::compose_rect_t {
    return wormedit::ui::compose_timing_marker_bounds(view, marker);
}

static auto compose_timing_marker_screen_rect(const ComposePaintContext& paint,
                                               const wormedit::ui::compose_timing_marker_view_t& marker)
    -> struct nk_rect {
    const auto bounds = compose_timing_marker_document_rect(*paint.view, marker);
    return compose_screen_rect(&bounds, paint.view, paint.viewport);
}

static auto compose_hovered_timing_marker(const ComposePaintContext& paint)
    -> const wormedit::ui::compose_timing_marker_view_t* {
    const wormedit::ui::compose_timing_marker_view_t* hovered = nullptr;
    for (size_t index = 0; index < paint.view->timing_markers.size(); ++index) {
        const auto& marker = paint.view->timing_markers[index];
        const auto bounds = compose_timing_marker_screen_rect(paint, marker);
        if (bounds.w > 0.0f && bounds.h > 0.0f &&
            nk_input_is_mouse_hovering_rect(&paint.context->input, bounds)) hovered = &marker;
    }
    return hovered;
}

auto compose_timing_marker_at(const wormedit::ui::compose_view_t& view, double x, double y)
    -> const wormedit::ui::compose_timing_marker_view_t* {
    const wormedit::ui::compose_timing_marker_view_t* hit = nullptr;
    for (size_t index = 0; index < view.timing_markers.size(); ++index) {
        const auto& marker = view.timing_markers[index];
        const auto bounds = compose_timing_marker_document_rect(view, marker);
        if (bounds.right > bounds.left && bounds.bottom > bounds.top &&
            x >= bounds.left && x < bounds.right && y >= bounds.top && y <= bounds.bottom)
            hit = &marker;
    }
    return hit;
}

static void compose_timing_lane_layout(const ComposePaintContext& paint, ComposeTimingLaneLayout& layout) {
    for (size_t index = 0; index < layout.colors.size(); ++index) {
        const auto colors = wormedit::ui::theme::compose_timing_color(index);
        layout.colors[index] = colors.label.value_or(colors.note);
    }
    for (size_t index = 0; index < layout.lanes.size(); ++index) {
        const auto extent = wormedit::ui::compose_timing_lane_extent(
            *paint.view, static_cast<wormedit::timing_lane_kind_t>(index));
        if (!extent) continue;
        const wormedit::ui::compose_rect_t bounds{extent->first, 0, extent->second,
                                       paint.view->viewport_height_pixels};
        layout.lanes[index] = compose_screen_rect(&bounds, paint.view, paint.viewport);
    }
}

constexpr std::array<wormedit::ui::lucide::Glyph, wormedit::timing_lane_kind_count> timing_lane_icons{
    wormedit::ui::lucide::Glyph::metronome, wormedit::ui::lucide::Glyph::waves_horizontal,
    wormedit::ui::lucide::Glyph::gauge, wormedit::ui::lucide::Glyph::pause,
    wormedit::ui::lucide::Glyph::hourglass, wormedit::ui::lucide::Glyph::fast_forward,
    wormedit::ui::lucide::Glyph::flame, wormedit::ui::lucide::Glyph::anchor,
    wormedit::ui::lucide::Glyph::audio_lines, wormedit::ui::lucide::Glyph::music_2};

static void draw_compose_timing_header(const ComposePaintContext& paint, std::size_t index,
                                        struct nk_rect lane, nk_color tint) {
    if (lane.w < 17.0f) return;
    const auto* caption = paint.view->timing_lane_captions.label(static_cast<wormedit::timing_lane_kind_t>(static_cast<std::uint32_t>(index)), "");
    const auto* font = paint.context->style.font;
    const auto length = static_cast<int>(strlen(caption));
    const auto caption_width = font->width(font->userdata, font->height, caption, length);
    auto* texture = wormedit::ui::lucide::texture(timing_lane_icons[index]);
    const bool label_fits = lane.w >= caption_width + 26;
    if (texture) {
        const auto x = label_fits ? lane.x + 2 : lane.x + (lane.w - 16) * .5f;
        paint_field(paint.context).image(0, {x, paint.viewport.y + 2, 16, 16}, texture, field_color(tint));
    }
    if (!texture || label_fits) {
        const auto offset = texture ? 22.0f : 2.0f;
        const auto title = nk_rect(lane.x + offset, paint.viewport.y + 2,
                                   std::max(1.0f, lane.w - offset - 2), 14);
        field_text(paint, title, caption, length, font, compose(ComposeColor::timing_title_bg), tint);
    }
}

static void draw_compose_timing_lanes(const ComposePaintContext& paint,
                                      ComposeTimingLaneLayout& layout) {
    for (size_t index = 0; index < layout.lanes.size(); ++index) {
        const auto& lane = layout.lanes[index];
        if (lane.w <= 0) continue;
        field_rect(paint, lane, 0, wormedit::ui::theme::compose_timing_color(index).lane);
        field_line(paint, lane.x, paint.viewport.y, lane.x, paint.viewport.y + paint.viewport.h,
                   1, compose(ComposeColor::grid));
        draw_compose_timing_header(paint, index, lane, layout.colors[index]);
    }
}

static void draw_compose_timing_header_tooltip(const ComposePaintContext& paint,
                                               const ComposeTimingLaneLayout& layout) {
    if (paint.view->input_occluded) return;
    for (size_t index = 0; index < layout.lanes.size(); ++index) {
        const auto& lane = layout.lanes[index];
        if (lane.w <= 0) continue;
        if (wormedit::ui::nuklear::tooltip::hovered(paint.context,
                nk_rect(lane.x, paint.viewport.y, lane.w, 20))) {
            wormedit::ui::nuklear::tooltip::text(paint.context, paint.view->timing_lanes.label(static_cast<wormedit::timing_lane_kind_t>(static_cast<std::uint32_t>(index)), ""));
            return;
        }
    }
}

static void draw_compose_bands(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bands.size(); ++index) {
        const auto& band = paint.view->bands[index];
        field_rect(paint, compose_screen_rect(&band.bounds, paint.view, paint.viewport), 0,
                   band.split_stage ? compose(ComposeColor::band_split) : compose(ComposeColor::band));
        const auto top = compose_projected_y(paint, band.bounds.top);
        const auto bottom = compose_projected_y(paint, band.bounds.bottom);
        field_line(paint, paint.viewport.x, top, paint.viewport.x + paint.viewport.w,
                       top, 1.0f, compose(ComposeColor::band_top));
        field_line(paint, paint.viewport.x, bottom, paint.viewport.x + paint.viewport.w,
                       bottom, 1.0f, compose(ComposeColor::band_bottom));
    }
}

static void draw_compose_play_lanes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->lanes.size(); ++index) {
        const auto& lane = paint.view->lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto key = wormedit::ui::theme::compose_key_color(
            compose_lane_key_color(lane.key_color, lane.role), lane.lane_id);
        field_rect(paint, rect, 0.0f, (lane.display_order & 1U) ? key.lane_alternate : key.lane);
        field_line(paint, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                       compose(ComposeColor::lane_line));

    }
}

static void draw_compose_play_lane_titles(const ComposePaintContext& paint) {
    for (size_t index=0; index<paint.view->lanes.size(); ++index) {
        const auto& lane=paint.view->lanes[index];
        const auto rect=compose_screen_rect(&lane.bounds,paint.view,paint.viewport);
        if (paint.view->lane_key_legend_visible && rect.w >= 28.0f && rect.h >= 18.0f) {
            char label[16]{};
            const auto label_length = snprintf(label, sizeof(label), "%u", lane.display_order + 1U);
            if (label_length > 0) {
                const auto label_bounds = nk_rect(rect.x + 2.0f, rect.y + 2.0f,
                                                  std::min(28.0f, rect.w - 4.0f), 15.0f);
                field_text_outlined(paint, label_bounds, label, static_cast<int>(strlen(label)),
                                    paint.context->style.font, wormedit::ui::theme::compose_key_color(lane.key_color, lane.lane_id).lane_label);
            }
        }
    }
}

static void draw_compose_bgm_lanes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bgm_lanes.size(); ++index) {
        const auto& lane = paint.view->bgm_lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto alternate = (lane.source_order & 1U) != 0;
        field_rect(paint, rect, 0.0f,
                     alternate ? wormedit::ui::theme::compose_bgm_color().lane_alternate : wormedit::ui::theme::compose_bgm_color().lane);
        field_line(paint, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                       compose(ComposeColor::bgm_line));
    }
}

static void draw_compose_scene_background(const ComposePaintContext& paint,
                                          ComposeTimingLaneLayout& layout) {
    field_rect(paint, paint.rail, 0.0f,
                 wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::rail));
    field_line(paint, paint.rail.x + paint.rail.w, paint.rail.y,
                   paint.rail.x + paint.rail.w, paint.rail.y + paint.rail.h, 1.0f,
                   compose(ComposeColor::band_top));
    compose_timing_lane_layout(paint,layout);
    draw_compose_cached_pass(paint,1,false,false,[&](const ComposePaintContext& cached) { draw_compose_timing_lanes(cached,layout); });
    draw_compose_timing_header_tooltip(paint, layout);
    // The original bodies keep their order. Titles stay live inside their
    // own lane margins, disjoint from the subsequent BGM background bodies.
    draw_compose_cached_pass(paint,2,true,false,[&](const ComposePaintContext& cached) {
        draw_compose_bands(cached); draw_compose_play_lanes(cached); draw_compose_bgm_lanes(cached);
    });
    draw_compose_play_lane_titles(paint);
}

static auto finite_envelope_row(const wormedit::ui::compose_waveform_row_view_t& row) -> bool {
    return std::isfinite(row.y_pixels) && std::isfinite(row.rms[0]) && std::isfinite(row.rms[1]) &&
           std::isfinite(row.absolute_peak[0]) && std::isfinite(row.absolute_peak[1]);
}

static void draw_compose_retained_gaps(const ComposePaintContext& paint,
        const wormedit::ui::compose_waveform_row_view_t* source, size_t count, float center,
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
        const wormedit::ui::compose_waveform_row_view_t* source, size_t count, float center, float half_width,
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
    auto key = view.document_revision ^ (view.project_id.value * 1099511628211ULL);
    key ^= view.chart_id.value * 1469598103934665603ULL;
    if (!stem_key) {
        key ^= view.selected_audio_asset_id.value * 1099511628211ULL;
        key ^= view.channel_waveform_source_lane_id.value * static_cast<uint64_t>(slot);
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
    for (size_t index = 1; index < view.waveform_rows.size(); ++index) {
        const auto& before = view.waveform_rows[index - 1U];
        const auto& after = view.waveform_rows[index];
        if (!before.sample_point || !after.sample_point || !std::isfinite(before.y_pixels) ||
            !std::isfinite(after.y_pixels)) continue;
        for (size_t channel = 0; channel < 2; ++channel) {
            if (!std::isfinite(before.samples[channel]) || !std::isfinite(after.samples[channel]))
                continue;
            const auto color = channel == 0 ? wormedit::ui::theme::field_waveform_color(nk_rgba(114, 222, 239, 190))
                                            : wormedit::ui::theme::field_waveform_color(nk_rgba(238, 160, 114, 190));
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
    if (view.waveform_rows.size() == 0) {
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
    if (draw_compose_envelope_texture(paint, view.waveform_rows.data(), view.waveform_rows.size(),
            center, half_width, 0, wormedit::ui::theme::field_waveform_color(nk_rgba(57, 137, 167, 96)), wormedit::ui::theme::field_waveform_color(nk_rgba(114, 222, 239, 142)), false))
        return;
    for (size_t index = 0; index < view.waveform_rows.size(); ++index) {
        const auto& row = view.waveform_rows[index];
        if (!finite_envelope_row(row)) continue;
        const auto y = compose_projected_y(paint, row.y_pixels);
        field_line(paint, center - row.rms[0] * half_width, y, center, y, 2.0f,
                       wormedit::ui::theme::field_waveform_color(nk_rgba(57, 137, 167, 96)), 6);
        field_line(paint, center, y, center + row.rms[1] * half_width, y, 2.0f,
                       wormedit::ui::theme::field_waveform_color(nk_rgba(57, 137, 167, 96)), 6);
        field_line(paint, center - row.absolute_peak[0] * half_width, y, center, y,
                       1.0f, wormedit::ui::theme::field_waveform_color(nk_rgba(114, 222, 239, 142)), 6);
        field_line(paint, center, y, center + row.absolute_peak[1] * half_width, y,
                       1.0f, wormedit::ui::theme::field_waveform_color(nk_rgba(114, 222, 239, 142)), 6);
    }
}

static auto compose_waveform_lane_rect(const ComposePaintContext& paint,
                                      const wormedit::ui::compose_bgm_lane_view_t& lane) -> struct nk_rect {
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
    compose_waveform_lane_clip_t(const ComposePaintContext& paint, const wormedit::ui::compose_bgm_lane_view_t& lane)
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
    if (view.channel_waveform_rows.size() == 0) {
        return;
    }
    const auto selected_lane = std::find_if(
        view.bgm_lanes.begin(), view.bgm_lanes.end(), [&](const auto& lane) {
            return lane.source_lane_id == view.channel_waveform_source_lane_id;
        });
    if (selected_lane == view.bgm_lanes.end()) {
        return;
    }
    const auto rect = compose_waveform_lane_rect(paint, *selected_lane);
    const compose_waveform_lane_clip_t clip(paint, *selected_lane);
    if (rect.w <= 1.0f || rect.h <= 0.0f) {
        return;
    }
    const auto center = rect.x + rect.w * 0.5f;
    const auto half_width = rect.w * 0.43f;
    if (draw_compose_envelope_texture(paint, view.channel_waveform_rows.data(),
            view.channel_waveform_rows.size(), center, half_width, 1,
            wormedit::ui::theme::field_waveform_color(nk_rgba(87, 157, 196, 126)), wormedit::ui::theme::field_waveform_color(nk_rgba(146, 230, 251, 224)), true)) return;
    for (size_t index = 0; index < view.channel_waveform_rows.size(); ++index) {
        const auto& row = view.channel_waveform_rows[index];
        if (!finite_envelope_row(row)) continue;
        const auto y = compose_projected_y(paint, row.y_pixels);
        field_line(paint, center - row.rms[0] * half_width, y,
                       center + row.rms[1] * half_width, y, 2.0f, wormedit::ui::theme::field_waveform_color(nk_rgba(87, 157, 196, 126)), 6);
        field_line(paint, center - row.absolute_peak[0] * half_width, y,
                       center + row.absolute_peak[1] * half_width, y, 1.0f,
                       wormedit::ui::theme::field_waveform_color(nk_rgba(146, 230, 251, 224)), 6);
    }
}

// The BGM lane column a stem piece or cut sits in, in screen coordinates; nullopt when that lane
// is not on screen (scrolled out of the BGM pane).
static auto compose_stem_lane_rect(const ComposePaintContext& paint, wormedit::object_id_t lane_id)
    -> std::optional<struct nk_rect> {
    const auto& view = *paint.view;
    const auto lanes = std::span(view.bgm_lanes);
    const auto found = std::find_if(lanes.begin(), lanes.end(),
        [&](const auto& lane) { return lane.source_lane_id == lane_id; });
    if (found == lanes.end()) return std::nullopt;
    const wormedit::ui::compose_rect_t* bounds = &found->bounds;
    const auto rect = compose_screen_rect(bounds, &view, paint.viewport);
    if (rect.w <= 1.0f || rect.h <= 0.0f) return std::nullopt;
    return rect;
}

static auto compose_pending_piece_rect(const ComposePaintContext& paint,
        const wormedit::ui::compose_stem_waveform_view_t& piece, struct nk_rect rect) -> struct nk_rect {
    if (!piece.bounded) return rect;
    const auto first = compose_projected_y(paint, piece.begin_y_pixels);
    const auto last = compose_projected_y(paint, piece.end_y_pixels);
    const auto low = std::clamp(std::min(first, last), rect.y, rect.y + rect.h);
    const auto high = std::clamp(std::max(first, last), rect.y, rect.y + rect.h);
    return nk_rect(rect.x, low, rect.w, high - low);
}

// One stem piece: the waveform of the slices that sit in one lane.
static void draw_compose_stem_piece(const ComposePaintContext& paint, const wormedit::ui::compose_stem_waveform_view_t& stem) {
    const auto rect = compose_stem_lane_rect(paint, stem.source_lane_id);
    if (!rect) return;
    if (stem.pending || stem.rows.size() == 0) {
        field_rect(paint, compose_pending_piece_rect(paint, stem, *rect), 0.0f,
            wormedit::ui::theme::field_waveform_color(nk_rgba(146, 230, 251, stem.pending ? 22 : 0)), 6);
        return;
    }
    const auto lanes = std::span(paint.view->bgm_lanes);
    const auto lane = std::find_if(lanes.begin(), lanes.end(),
        [&](const auto& value) { return value.source_lane_id == stem.source_lane_id; });
    const auto waveform_rect = compose_waveform_lane_rect(paint, *lane);
    const compose_waveform_lane_clip_t clip(paint, *lane);
    const auto center = waveform_rect.x + waveform_rect.w * 0.5f;
    const auto half_width = waveform_rect.w * 0.43f;
    if (draw_compose_envelope_texture(paint, stem.rows.data(), stem.rows.size(), center, half_width,
            0, wormedit::ui::theme::field_waveform_color(nk_rgba(87, 157, 196, 96)), wormedit::ui::theme::field_waveform_color(nk_rgba(146, 230, 251, 190)), true,
            stem.source_lane_id.value, wormedit::ui::field::waveform_piece_key_t{
                static_cast<wormedit::ui::field::waveform_piece_kind_t>(stem.piece_kind), stem.stem_key})) return;
    for (size_t row_index = 0; row_index < stem.rows.size(); ++row_index) {
        const auto& row = stem.rows[row_index];
        if (!finite_envelope_row(row)) continue;
        const auto y = compose_projected_y(paint, row.y_pixels);
        field_line(paint, center - row.absolute_peak[0] * half_width, y,
                   center + row.absolute_peak[1] * half_width, y, 1.0f, wormedit::ui::theme::field_waveform_color(nk_rgba(146, 230, 251, 190)), 6);
    }
}

static auto compose_stem_point_tile(const ComposePaintContext& paint, wormedit::object_id_t object_id, bool playable)
    -> std::optional<std::pair<struct nk_rect, bool>> {
    const auto& view = *paint.view;
    if (playable) {
        for (size_t index = 0; index < view.note_endpoints.size(); ++index) {
            const auto& note = view.note_endpoints[index];
            if (note.note_id != object_id || note.proposal_ghost) continue;
            return std::pair{compose_screen_rect(&note.bounds, &view, paint.viewport), note.selected};
        }
        return std::nullopt;
    }
    for (size_t index = 0; index < view.bgm_events.size(); ++index) {
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
    for (size_t index = 0; index < view.stem_cuts.size(); ++index) {
        const auto& cut = view.stem_cuts[index];
        const auto rect = compose_stem_lane_rect(paint, cut.bgm_lane_id);
        if (!rect) continue;
        const auto tile = compose_stem_point_tile(paint, cut.object_id, cut.object_is_note);
        const auto hovered = tile && !view.input_occluded && wormedit::ui::is_point_in_rect(
            {tile->first.x, tile->first.y, tile->first.w, tile->first.h}, mouse.x, mouse.y);
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
    for (size_t index = 0; index < view.stem_waveforms.size(); ++index)
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
    for (size_t index = 0; index < paint.view->differences.size(); ++index) {
        field_rect(paint, compose_screen_rect(&paint.view->differences[index].bounds,
                                              paint.view, paint.viewport),
                   0.0f, nk_rgba(229, 190, 58, 74), 6);
    }
}

static void reserve_compose_song_envelope(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (view.waveform_rows.size() == 0 || view.waveform_rows[0].sample_point) return;
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
    if (view.channel_waveform_rows.size() == 0) return;
    const auto rect = compose_stem_lane_rect(paint, wormedit::object_id_t{view.channel_waveform_source_lane_id});
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
    ordered.reserve(view.stem_waveforms.size());
    for (size_t index = 0; index < view.stem_waveforms.size(); ++index) {
        const auto& stem = view.stem_waveforms[index];
        if (stem.pending || stem.rows.size() == 0) continue;
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

// The song, stem and channel waveforms skip Stop and Delay bands: that time takes no rows.
static void draw_compose_waveforms_outside_pauses(const ComposePaintContext& paint) {
    std::vector<std::pair<float, float>> bands;
    for (size_t index = 0; index < paint.view->dead_spans.size(); ++index) {
        const auto& span = paint.view->dead_spans[index];
        if (span.kind != wormedit::timing_lane_kind_t::stop &&
            span.kind != wormedit::timing_lane_kind_t::delay) continue;
        const auto rect = compose_screen_rect(&span.bounds, paint.view, paint.viewport);
        if (rect.h > 0.0f) bands.emplace_back(rect.y, rect.y + rect.h);
    }
    auto& field = paint_field(paint.context);
    for (const auto& strip : compose_waveform_strips(
             paint.viewport.y, paint.viewport.y + paint.viewport.h, std::move(bands))) {
        const auto previous = field.push_clip(
            {paint.viewport.x, strip.top, paint.viewport.w, strip.bottom - strip.top});
        draw_compose_waveform(paint);
        draw_compose_stem_waveforms(paint);
        draw_compose_channel_waveform(paint);
        field.pop_clip(previous);
    }
}

static void draw_compose_waveform_layer(const ComposePaintContext& paint,
                                        const ComposeTimingLaneLayout& layout) {
    pin_compose_stem_envelopes(paint);
    reserve_compose_song_envelope(paint);
    reserve_compose_channel_envelope(paint);
    draw_compose_waveforms_outside_pauses(paint);
    draw_compose_beat_fit_region(paint, layout);
    draw_compose_differences(paint);
}

static void draw_compose_proposed_grid(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->proposed_grid_lines.size(); ++index) {
        const auto& line = paint.view->proposed_grid_lines[index];
        const auto y = compose_projected_y(paint, line.y_pixels);
        const auto color = line.kind == wormedit::editor_grid_line_kind_t::measure                     ? nk_rgba(255, 207, 104, 230)
                           : line.kind == wormedit::editor_grid_line_kind_t::beat || line.kind == wormedit::editor_grid_line_kind_t::group ? nk_rgba(255, 207, 104, 155)
                                                              : nk_rgba(255, 207, 104, 75);
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       line.kind == wormedit::editor_grid_line_kind_t::measure ? 1.6f : 1.0f, color, 4);
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
    const auto original = wormedit::to_double(wormedit::signed_rational_t{view.beat_fit_original_bpm_numerator,
                          view.beat_fit_original_bpm_denominator});
    const auto proposed = wormedit::to_double(wormedit::signed_rational_t{view.beat_fit_proposed_bpm_numerator,
                          view.beat_fit_proposed_bpm_denominator});
    char original_text[32]{};
    char proposed_text[32]{};
    format_beat_fit_bpm(original, original_text, sizeof(original_text));
    format_beat_fit_bpm(proposed, proposed_text, sizeof(proposed_text));
    auto length = snprintf(label, sizeof(label), "Fit %s -> %s BPM", original_text, proposed_text);
    if (view.beat_fit_snap_active && view.beat_fit_snap_step_denominator != 0) {
        const auto step = wormedit::to_double(wormedit::signed_rational_t{view.beat_fit_snap_step_numerator,
                          view.beat_fit_snap_step_denominator});
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
    if (state.measure_resize.measure_resize_active || view.input_occluded || view.measure_rows.size() == 0 ||
        view.viewport_height_pixels <= 0.0 || (SDL_GetModState() & SDL_KMOD_ALT) == 0) return;
    const auto band = 6.0f;
    const auto inside = nk_input_is_mouse_hovering_rect(&paint.context->input, paint.viewport);
    const auto mouse_y = paint.context->input.mouse.pos.y;
    for (std::size_t index = 0; index < view.grid_lines.size(); ++index) {
        const auto& line = view.grid_lines[index];
        if (line.measure_number < 2 || line.measure_number - 2 >= view.measure_rows.size()) continue;
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
    if (!state.measure_resize.measure_resize_active) return;
    const auto y = compose_projected_y(paint, state.measure_resize.measure_resize_ghost_y_pixels);
    const auto color = state.measure_resize.measure_resize_valid ? nk_rgba(110, 222, 236, 215) : nk_rgba(232, 112, 112, 215);
    if (y >= paint.viewport.y && y <= paint.viewport.y + paint.viewport.h)
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y, 1.6f, color, 4);
    const auto* label = state.measure_resize.measure_resize_label.data();
    if (label[0] == '\0') return;
    const auto bounds = nk_rect(paint.viewport.x + paint.rail.w + 4.0f,
                                std::clamp(y - 20.0f, paint.viewport.y, paint.viewport.y + paint.viewport.h - 18.0f),
                                std::min(310.0f, paint.viewport.w - paint.rail.w - 8.0f), 18.0f);
    field_text(paint, bounds, label, static_cast<int>(strlen(label)), paint.context->style.font,
               nk_rgba(18, 21, 29, 235),
               state.measure_resize.measure_resize_valid ? nk_rgb(148, 229, 238) : nk_rgb(240, 150, 150), 4);
}

static void draw_compose_offset_fit_overlay(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.offset_fit_drag_active) return;
    const auto y = compose_projected_y(paint, view.offset_fit_target_y_pixels);
    if (y >= paint.viewport.y && y <= paint.viewport.y + paint.viewport.h) {
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       1.3f, nk_rgba(110, 222, 236, 215), 4);
    }
    const auto original = wormedit::to_double(wormedit::signed_rational_t{view.waveform_audio_offset_numerator,
                          view.waveform_audio_offset_denominator});
    const auto proposed = wormedit::to_double(wormedit::signed_rational_t{view.waveform_proposed_audio_offset_numerator,
                          view.waveform_proposed_audio_offset_denominator});
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
    if (paint.view->timing_preview_rects.size() == 0) {
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
    if (!state.timing_marker.timing_marker_drag_active ||
        state.timing_drag.timing_marker_capture.chart != paint.view->chart_id.value ||
        state.timing_drag.timing_marker_capture.revision != paint.view->document_revision) return;
    bool selected_fixed = false;
    for (size_t index = 0; index < paint.view->timing_markers.size(); ++index)
        selected_fixed |= paint.view->timing_markers[index].selected &&
                          paint.view->timing_markers[index].fixed_initial;
    const auto ghost_color = selected_fixed ? nk_rgb(242, 111, 104) : nk_rgb(255, 207, 99);
    const auto delta = static_cast<float>(state.timing_marker.timing_marker_current_y_pixels -
                                           state.timing_marker.timing_marker_anchor_y_pixels) *
                       paint.viewport.h / static_cast<float>(paint.view->viewport_height_pixels);
    for (size_t index = 0; index < paint.view->timing_markers.size(); ++index) {
        const auto& marker = paint.view->timing_markers[index];
        if (!marker.selected) continue;
        auto ghost = compose_timing_marker_screen_rect(paint, marker);
        ghost.y += delta;
        field_rect(paint, ghost, 1.0f,
                     selected_fixed ? nk_rgba(242, 111, 104, 45) : nk_rgba(255, 207, 99, 40), 4);
        field_stroke_rect(paint, ghost, 1.0f, 2.0f, ghost_color, 4);
    }
}

namespace {
struct field_hitsound_stroke_t {
    const ComposePaintContext& paint;
    struct nk_rect bounds;
    nk_color tint;
    wormedit::ui::field::layer_t layer{2};
    void line(float x0, float y0, float x1, float y1) {
        field_line(paint, bounds.x + x0*bounds.w/24, bounds.y + y0*bounds.h/24,
            bounds.x + x1*bounds.w/24, bounds.y + y1*bounds.h/24, std::max(1.0f, bounds.h/14), tint, layer);
    }
    void path(std::span<const wormedit::ui::hitsound_icons::point_t> points) {
        for (std::size_t index = 1; index < points.size(); ++index)
            line(points[index-1][0], points[index-1][1], points[index][0], points[index][1]);
    }
    void circle(float x, float y, float radius) {
        for (int index = 0; index < 12; ++index) {
            const auto a = static_cast<float>(index)*6.2831853f/12;
            const auto b = static_cast<float>(index+1)*6.2831853f/12;
            line(x+radius*std::cos(a), y+radius*std::sin(a), x+radius*std::cos(b), y+radius*std::sin(b));
        }
    }
};



}

static void draw_compose_timing_markers(const ComposePaintContext& paint,
                                        const ComposeTimingLaneLayout& layout) {
    const auto* hovered = tile_renderer_override ? nullptr : compose_hovered_timing_marker(paint);
    for (size_t index = 0; index < paint.view->timing_markers.size(); ++index) {
        const auto& marker = paint.view->timing_markers[index];
        const auto marker_lane_ordinal = static_cast<size_t>(marker.lane);
        if (marker_lane_ordinal >= layout.lanes.size() ||
            layout.lanes[marker_lane_ordinal].w <= 0.0f) {
            continue;
        }
        const auto bounds = compose_timing_marker_screen_rect(paint, marker);
        field_rect(paint, bounds, 1.0f, wormedit::ui::theme::compose_timing_color(marker_lane_ordinal).hold_body, 4);
        const auto selected = marker.selected ||
                              (marker.fixed_initial && marker.object_id.value == 0 &&
                               state.timing_marker.timing_missing_initial_selected);
        const auto outline = hovered == &marker ? compose(ComposeColor::timing_hover)
                             : selected ? compose(ComposeColor::timing_selected)
                                        : compose(ComposeColor::timing_border);
        field_stroke_rect(paint, bounds, 1.0f, hovered == &marker ? 2.0f : 1.5f,
                       outline, 4);
        if (marker.sample_set) {
            const auto side = std::min(bounds.h - 2, bounds.w - 4);
            field_hitsound_stroke_t stroke{.paint = paint,
                .bounds = nk_rect(bounds.x + (bounds.w-side)*.5f, bounds.y + (bounds.h-side)*.5f, side, side),
                .tint = layout.colors[marker_lane_ordinal], .layer = 4};
            wormedit::ui::hitsound_icons::draw(stroke, wormedit::ui::hitsound_icons::sample_set_shape(*marker.sample_set));
            continue;
        }
        if (marker.short_label.empty()) continue;
        const auto* font = paint.context->style.font;
        const auto length = static_cast<int>(marker.short_label.size());
        const auto text_bounds = nk_rect(bounds.x + 2.0f, bounds.y + (bounds.h - font->height) * 0.5f,
                                         std::max(0.0f, bounds.w - 4.0f), font->height);
        field_text(paint, text_bounds, marker.short_label.data(), length, font,
                     nk_rgba(18, 22, 31, 0), layout.colors[marker_lane_ordinal], 4);
    }
    draw_compose_timing_marker_drag_ghosts(paint);
}

static void draw_compose_timing_preview_markers(const ComposePaintContext& paint,
                                                const ComposeTimingLaneLayout& layout) {
    for (size_t index = 0; index < paint.view->timing_preview_markers.size(); ++index) {
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
auto compose_timing_lane_inner_extent(const wormedit::ui::compose_view_t& view, wormedit::timing_lane_kind_t lane)
    -> std::optional<std::pair<double, double>> {
    return wormedit::ui::compose_timing_inner_extent(view, lane);
}

// While an edge or a new area is dragged, the area it would become follows the pointer.
static void draw_compose_kiai_drag_preview(const ComposePaintContext& paint,
                                           const std::pair<double, double>& extent) {
    if (!state.timing_drag.kiai_drag_active || !state.timing_drag.kiai_drag_moved || state.timing_drag.kiai_drag_speed) return;
    double fixed_y = state.timing_drag.kiai_drag_anchor_y;
    if (state.timing_drag.kiai_drag_edge == WORM_UI_SPAN_HANDLE_BODY) {
        // The whole area follows the pointer's travel.
        const std::span spans(paint.view->kiai_spans);
        const auto found = std::ranges::find_if(
            spans, [](const auto& value) { return value.head_object_id.value == state.timing_drag.kiai_drag_head_id; });
        if (found == spans.end()) return;
        const auto travel = state.timing_drag.kiai_drag_current_y - state.timing_drag.kiai_drag_anchor_y;
        const wormedit::ui::compose_rect_t moved{extent.first, found->bounds.top + travel, extent.second,
                                      found->bounds.bottom + travel};
        const auto rect = compose_screen_rect(&moved, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) return;
        field_rect(paint, rect, 0.0f, nk_rgba(255, 236, 170, 70), 10);
        field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(255, 236, 170), 10);
        return;
    }
    if (state.timing_drag.kiai_drag_edge != WORM_UI_SPAN_HANDLE_CREATE) {
        const std::span spans(paint.view->kiai_spans);
        const auto found = std::ranges::find_if(
            spans, [](const auto& value) { return value.head_object_id.value == state.timing_drag.kiai_drag_head_id; });
        if (found == spans.end()) return;
        const auto* span = &*found;
        const bool head_is_top = !paint.view->downscroll;
        const bool fixed_is_top = state.timing_drag.kiai_drag_edge == WORM_UI_SPAN_HANDLE_BEGIN ? !head_is_top : head_is_top;
        fixed_y = fixed_is_top ? span->bounds.top : span->bounds.bottom;
    }
    const wormedit::ui::compose_rect_t bounds{extent.first, std::min(fixed_y, state.timing_drag.kiai_drag_current_y),
                                   extent.second, std::max(fixed_y, state.timing_drag.kiai_drag_current_y)};
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 0.0f, nk_rgba(255, 236, 170, 70), 10);
    field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(255, 236, 170), 10);
}

// The captured endpoint stays lit even after the pointer leaves its original hit band.
static auto compose_span_feedback_target(const ComposePaintContext& paint)
    -> std::optional<compose_span_target_t> {
    if (state.timing_drag.recipe_drag_active)
        return compose_span_target_t{.family = compose_span_family_t::recipe,
            .id = state.timing_drag.recipe_drag_id, .edge = state.timing_drag.recipe_drag_edge};
    if (state.timing_drag.kiai_drag_active)
        return compose_span_target_t{.family = state.timing_drag.kiai_drag_speed
                ? compose_span_family_t::speed : compose_span_family_t::kiai,
            .id = state.timing_drag.kiai_drag_head_id, .edge = state.timing_drag.kiai_drag_edge};
    const auto& view = *paint.view;
    if (view.input_occluded || view.y_lock || view.waveform_timing_locked ||
        state.inline_timing.inline_timing_open || view.timing_stage_active ||
        compose_input_queries_t{state}.is_canvas_hover_blocked() ||
        !nk_input_is_mouse_hovering_rect(&paint.context->input, paint.viewport)) return std::nullopt;
    const auto& mouse = paint.context->input.mouse.pos;
    const auto x = (mouse.x - paint.viewport.x) * view.viewport_width_pixels / paint.viewport.w;
    const auto y = (mouse.y - paint.viewport.y) * view.viewport_height_pixels / paint.viewport.h;
    return compose_span_target_t::from(view, x, y,
        compose_drag_capture_t::edge_tolerance_pixels * view.viewport_height_pixels / paint.viewport.h);
}

struct compose_span_feedback_t {
    bool selected{};
    std::optional<std::uint8_t> hot_edge;

    static auto from(compose_span_family_t family, std::uint64_t id, bool selected,
                     const std::optional<compose_span_target_t>& target) -> compose_span_feedback_t {
        compose_span_feedback_t feedback{.selected = selected};
        if (target && target->family == family && target->id == id) feedback.hot_edge = target->edge;
        return feedback;
    }
    auto is_lit() const -> bool { return selected || hot_edge.has_value(); }
    auto operator==(const compose_span_feedback_t&) const -> bool = default;
};

// A broad edge with a small contrasting grip reads as a resize target before it is grabbed.
static void draw_compose_span_handle(const ComposePaintContext& paint, struct nk_rect rect, float y,
                                     nk_color base, const compose_span_feedback_t& feedback, bool begin) {
    const auto edge = begin ? WORM_UI_SPAN_HANDLE_BEGIN : WORM_UI_SPAN_HANDLE_END;
    const bool hot = feedback.hot_edge == edge;
    const bool selected = feedback.selected;
    const auto color = hot ? compose(ComposeColor::timing_hover)
                          : selected ? compose(ComposeColor::selection_outline) : base;
    field_line(paint, rect.x, y, rect.x + rect.w, y, hot ? 5.0f : 3.0f, color, 10);
    const auto width = std::min(18.0f, rect.w - 2.0f);
    if (width < 4.0f) return;
    const auto height = hot ? 9.0f : 7.0f;
    const auto grip = nk_rect(rect.x + (rect.w - width) * .5f, y - height * .5f, width, height);
    field_rect(paint, grip, 2.0f, color, 10);
    field_line(paint, grip.x + 2, y - 1, grip.x + grip.w - 2, y - 1, 1, nk_rgb(24, 29, 39), 10);
    field_line(paint, grip.x + 2, y + 1, grip.x + grip.w - 2, y + 1, 1, nk_rgb(24, 29, 39), 10);
}

static void draw_compose_kiai_spans(const ComposePaintContext& paint,
                                    const std::optional<compose_span_target_t>& target) {
    const auto extent = compose_timing_lane_inner_extent(*paint.view, wormedit::timing_lane_kind_t::kiai);
    if (!extent) return;
    draw_compose_kiai_drag_preview(paint, *extent);
    for (size_t index = 0; index < paint.view->kiai_spans.size(); ++index) {
        const auto& span = paint.view->kiai_spans[index];
        const wormedit::ui::compose_rect_t bounds{extent->first, span.bounds.top, extent->second, span.bounds.bottom};
        const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) continue;
        const auto feedback = compose_span_feedback_t::from(compose_span_family_t::kiai,
            span.head_object_id.value, span.selected, target);
        field_rect(paint, rect, 0.0f, nk_rgba(250, 204, 92, feedback.is_lit() ? 96 : 60), 10);
        field_stroke_rect(paint, rect, 0.0f, feedback.is_lit() ? 2.0f : 1.0f,
                          feedback.is_lit() ? nk_rgb(255, 236, 170) : nk_rgba(250, 204, 92, 170), 10);
        // Head (opening edge) and tail (closing edge) handles, solid so they read as grabbable.
        const auto head_y = paint.view->downscroll ? rect.y + rect.h : rect.y;
        const auto tail_y = paint.view->downscroll ? rect.y : rect.y + rect.h;
        if (span.head_visible)
            draw_compose_span_handle(paint, rect, head_y, nk_rgb(250, 204, 92), feedback, true);
        if (span.tail_visible)
            draw_compose_span_handle(paint, rect, tail_y, nk_rgb(214, 160, 46), feedback, false);
    }
}

// Stored SV recipes: a translucent area over the recipe's rows with its relative value as a
// continuous line, grabbable begin and end edges, and a muted look when disabled.
// While a recipe edge or body is dragged, the span it would become follows the pointer.
static void draw_compose_recipe_drag_preview(const ComposePaintContext& paint) {
    if (!state.timing_drag.recipe_drag_active || !state.timing_drag.recipe_drag_moved) return;
    const std::span areas(paint.view->recipe_areas);
    const auto found = std::ranges::find_if(
        areas, [](const auto& area) { return area.recipe_id.value == state.timing_drag.recipe_drag_id; });
    if (found == areas.end()) return;
    const auto extent = compose_timing_lane_inner_extent(
        *paint.view, static_cast<wormedit::timing_lane_kind_t>(found->lane));
    if (!extent) return;
    double top = found->bounds.top, bottom = found->bounds.bottom;
    const bool begin_is_top = !paint.view->downscroll;
    if (state.timing_drag.recipe_drag_edge == 2) {
        const auto travel = state.timing_drag.recipe_drag_current_y - state.timing_drag.recipe_drag_anchor_y;
        top += travel;
        bottom += travel;
    } else {
        const bool moving_top = (state.timing_drag.recipe_drag_edge == 0) == begin_is_top;
        const auto fixed = moving_top ? bottom : top;
        top = std::min(fixed, state.timing_drag.recipe_drag_current_y);
        bottom = std::max(fixed, state.timing_drag.recipe_drag_current_y);
    }
    const wormedit::ui::compose_rect_t bounds{extent->first, top, extent->second, bottom};
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 0.0f, nk_rgba(214, 226, 255, 60), 10);
    field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(214, 226, 255), 10);
}

// The line of one recipe's relative value inside its area rectangle.
static void draw_compose_recipe_graph(const ComposePaintContext& paint,
                                      const wormedit::ui::compose_recipe_area_view_t& area, struct nk_rect rect) {
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
                                      const wormedit::ui::compose_recipe_area_view_t& area, struct nk_rect rect,
                                      const std::optional<compose_span_target_t>& target) {
    const auto feedback = compose_span_feedback_t::from(compose_span_family_t::recipe,
        area.recipe_id.value, area.selected, target);
    const auto base = area.enabled ? nk_rgb(150, 170, 255) : nk_rgb(120, 126, 142);
    field_rect(paint, rect, 0.0f, nk_rgba(base.r, base.g, base.b, feedback.is_lit() ? 70 : 38), 10);
    field_stroke_rect(paint, rect, 0.0f, feedback.is_lit() ? 2.0f : 1.0f,
                      nk_rgba(base.r, base.g, base.b, area.enabled ? 210 : 140), 10);
    const auto begin_y = paint.view->downscroll ? rect.y + rect.h : rect.y;
    const auto end_y = paint.view->downscroll ? rect.y : rect.y + rect.h;
    if (area.begin_visible) draw_compose_span_handle(paint, rect, begin_y, base, feedback, true);
    if (area.end_visible) draw_compose_span_handle(paint, rect, end_y, base, feedback, false);
    if (!area.evaluable) {
        // No graph to draw (a Lua expression, for now): a diagonal says "not evaluated".
        field_line(paint, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h, 1.0f,
                   nk_rgba(base.r, base.g, base.b, 120), 10);
    }
}

static void draw_compose_recipe_areas(const ComposePaintContext& paint,
                                      const std::optional<compose_span_target_t>& target) {
    draw_compose_recipe_drag_preview(paint);
    for (size_t index = 0; index < paint.view->recipe_areas.size(); ++index) {
        const auto& area = paint.view->recipe_areas[index];
        const auto extent = compose_timing_lane_inner_extent(
            *paint.view, static_cast<wormedit::timing_lane_kind_t>(area.lane));
        if (!extent) continue;
        const wormedit::ui::compose_rect_t bounds{extent->first, area.bounds.top, extent->second, area.bounds.bottom};
        const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) continue;
        draw_compose_recipe_frame(paint, area, rect, target);
        if (area.evaluable) draw_compose_recipe_graph(paint, area, rect);
    }
}

// SPEEDS transitions: a faint band over the span a SPEEDS event interpolates across, stronger
// under the pointer, with its target and length written next to it when there is room.
// While a Speed edge is dragged, the span it would become follows the pointer.
static void draw_compose_speed_drag_preview(const ComposePaintContext& paint,
                                            const std::pair<double, double>& extent) {
    if (!state.timing_drag.kiai_drag_active || !state.timing_drag.kiai_drag_moved || !state.timing_drag.kiai_drag_speed) return;
    const std::span spans(paint.view->speed_spans);
    const auto found = std::ranges::find_if(
        spans, [](const auto& value) { return value.object_id.value == state.timing_drag.kiai_drag_head_id; });
    if (found == spans.end()) return;
    if (state.timing_drag.kiai_drag_edge == WORM_UI_SPAN_HANDLE_BODY) {
        // The whole span follows the pointer's travel.
        const auto travel = state.timing_drag.kiai_drag_current_y - state.timing_drag.kiai_drag_anchor_y;
        const wormedit::ui::compose_rect_t moved{extent.first, found->bounds.top + travel, extent.second,
                                      found->bounds.bottom + travel};
        const auto rect = compose_screen_rect(&moved, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) return;
        field_rect(paint, rect, 0.0f, nk_rgba(136, 203, 238, 70), 10);
        field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(137, 207, 239), 10);
        return;
    }
    // The edge that is not dragged stays; an instant Speed grows from its single edge.
    const bool head_is_top = !paint.view->downscroll;
    const bool fixed_is_top = state.timing_drag.kiai_drag_edge == WORM_UI_SPAN_HANDLE_BEGIN ? !head_is_top : head_is_top;
    const auto fixed_y = fixed_is_top ? found->bounds.top : found->bounds.bottom;
    const wormedit::ui::compose_rect_t bounds{extent.first, std::min(fixed_y, state.timing_drag.kiai_drag_current_y),
                                   extent.second, std::max(fixed_y, state.timing_drag.kiai_drag_current_y)};
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 0.0f, nk_rgba(136, 203, 238, 70), 10);
    field_stroke_rect(paint, rect, 0.0f, 1.5f, nk_rgb(137, 207, 239), 10);
}

static void draw_compose_speed_span_frame(const ComposePaintContext& paint,
                                           const wormedit::ui::compose_speed_span_view_t& span,
                                           struct nk_rect rect, const compose_span_feedback_t& feedback) {
    field_rect(paint, rect, 0.0f, nk_rgba(136, 203, 238, feedback.is_lit() ? 84 : 34), 10);
    field_stroke_rect(paint, rect, 0.0f, feedback.is_lit() ? 2.0f : 1.0f,
                      nk_rgba(137, 207, 239, feedback.is_lit() ? 230 : 120), 10);
    const auto begin_y = paint.view->downscroll ? rect.y + rect.h : rect.y;
    const auto end_y = paint.view->downscroll ? rect.y : rect.y + rect.h;
    const auto head_y = paint.view->downscroll ? span.bounds.bottom : span.bounds.top;
    const auto tail_y = paint.view->downscroll ? span.bounds.top : span.bounds.bottom;
    if (head_y >= 0 && head_y <= paint.view->viewport_height_pixels)
        draw_compose_span_handle(paint, rect, begin_y, nk_rgb(137, 207, 239), feedback, true);
    if (tail_y >= 0 && tail_y <= paint.view->viewport_height_pixels)
        draw_compose_span_handle(paint, rect, end_y, nk_rgb(91, 145, 177), feedback, false);
}

static void draw_compose_speed_spans(const ComposePaintContext& paint,
                                     const std::optional<compose_span_target_t>& target) {
    const auto extent = compose_timing_lane_inner_extent(*paint.view, wormedit::timing_lane_kind_t::speed);
    if (!extent) return;
    draw_compose_speed_drag_preview(paint, *extent);
    for (size_t index = 0; index < paint.view->speed_spans.size(); ++index) {
        const auto& span = paint.view->speed_spans[index];
        const wormedit::ui::compose_rect_t bounds{extent->first, span.bounds.top, extent->second, span.bounds.bottom};
        auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f) continue;
        if (rect.h < 3.0f) rect.h = 3.0f;
        const auto feedback = compose_span_feedback_t::from(compose_span_family_t::speed,
            span.object_id.value, span.selected, target);
        draw_compose_speed_span_frame(paint, span, rect, feedback);
    }
}

// A value curve inside one timing lane: a faint 1.0x reference line, then the polyline.
static void draw_compose_value_graph(const ComposePaintContext& paint, wormedit::timing_lane_kind_t lane,
                                     const wormedit::ui::compose_sv_graph_point_view_t* points, size_t count,
                                     double one_fraction, struct nk_color color) {
    const auto extent = compose_timing_lane_inner_extent(*paint.view, lane);
    if (!extent || count < 2) return;
    const auto x_of = [&](double fraction) {
        const wormedit::ui::compose_rect_t probe{extent->first + (extent->second - extent->first) * fraction, 0.0,
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
    draw_compose_value_graph(paint, wormedit::timing_lane_kind_t::scroll, paint.view->sv_graph_points.data(),
                             paint.view->sv_graph_points.size(), paint.view->sv_graph_one_fraction,
                             nk_rgb(110, 222, 236));
}

static void draw_compose_speed_graph(const ComposePaintContext& paint) {
    draw_compose_value_graph(paint, wormedit::timing_lane_kind_t::speed, paint.view->speed_graph_points.data(),
                             paint.view->speed_graph_points.size(),
                             paint.view->speed_graph_one_fraction, nk_rgb(137, 207, 239));
}

static void draw_compose_timing_layer(const ComposePaintContext& paint,
                                      const ComposeTimingLaneLayout& layout) {
    const auto span_target = compose_span_feedback_target(paint);
    draw_compose_kiai_spans(paint, span_target);
    draw_compose_speed_spans(paint, span_target);
    draw_compose_recipe_areas(paint, span_target);
    draw_compose_sv_graph(paint);
    draw_compose_speed_graph(paint);
    draw_compose_proposed_grid(paint);
    draw_compose_beat_fit_overlay(paint);
    draw_compose_measure_resize_affordance(paint);
    draw_compose_measure_resize_overlay(paint);
    draw_compose_offset_fit_overlay(paint);
    draw_compose_timing_preview_legend(paint);
    draw_compose_unbound_lane_keys_hint(paint);
    const bool active_marker=state.timing_marker.timing_marker_drag_active || state.timing_marker.timing_missing_initial_selected ||
        compose_hovered_timing_marker(paint)!=nullptr ||
        std::ranges::any_of(std::span(paint.view->timing_markers),
                           [](const auto& marker) { return marker.selected; });
    const auto holes=compose_timing_coverage_holes(paint);
    draw_compose_cached_pass(paint,6,true,active_marker || !holes,
        [&](const ComposePaintContext& cached) { draw_compose_timing_markers(cached,layout); },
        holes ? std::span<const SDL_FRect>(*holes) : std::span<const SDL_FRect>{});
    draw_compose_timing_preview_markers(paint, layout);
    draw_compose_timing_stage_marker(paint, layout);
    draw_compose_beat_sections(paint);
    std::optional<wormedit::ui::bookmark_label_box_t> previous_bookmark_label;
    draw_compose_beat_bookmarks(paint, previous_bookmark_label);
    draw_compose_audio_sections(paint);
    draw_compose_preview_span(paint, previous_bookmark_label);
    draw_compose_audio_bookmarks(paint, previous_bookmark_label);
}

static struct nk_color compose_selection_outline_color() {
    return wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::selection_outline);
}

static auto compose_move_conflict_color(bool ghost = false) -> nk_color {
    auto color = compose(ComposeColor::conflict);
    color.a = ghost ? 153 : 255;
    return color;
}

static void draw_compose_hold_ghost(const ComposePaintContext& paint,
                                    const wormedit::ui::compose_hold_view_t& hold, const struct nk_rect& rect) {
    const auto fill =
        (hold.move_conflict != wormedit::note_move_conflict_t::none) ? compose_move_conflict_color(true) : nk_rgba(240, 171, 71, 74);
    const auto border = (hold.move_conflict != wormedit::note_move_conflict_t::none) ? fill : nk_rgb(255, 224, 156);
    field_rect(paint, rect, 0.0f, fill, 2);
    field_stroke_rect(paint, rect, 0.0f, 1.4f, border, 2);
}

static void draw_compose_hold_bodies(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->hold_bodies.size(); ++index) {
        const auto& hold = paint.view->hold_bodies[index];
        const auto rect = compose_screen_rect(&hold.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        if (hold.proposal_ghost) {
            draw_compose_hold_ghost(paint, hold, rect);
            continue;
        }
        const auto color = hold.read_only ? nk_rgb(80, 85, 100) : hold.proposal_origin
                               ? nk_rgb(51, 82, 100)
                               : compose_note_base_fill(
                                     worm_ui_nuklear_compose_state().input_queue.color_view, hold);
        field_rect(paint, rect, 0.0f, color, 2);
        if (hold.selected) {
            field_stroke_rect(paint, rect, 0.0f, 2.0f, compose_selection_outline_color(), 2);
        }
        if (paint.view->hover_note_visible && hold.note_id.value == paint.view->hover_note_id.value) {
            field_stroke_rect(paint, rect, 0.0f, 2.0f, compose(ComposeColor::hold_hover), 2);
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
static bool compose_note_shows_selection_outline(const wormedit::ui::compose_view_t& view,
                                                 const wormedit::ui::compose_note_view_t& note) {
    const bool hovered = view.hover_note_visible && note.note_id.value == view.hover_note_id.value;
    const bool preview = note.proposal_ghost || note.paint_invalid_preview ||
                         note.paint_erase_preview || note.paint_preview;
    return note.selected && !hovered && !preview;
}

static auto compose_move_note_style(const wormedit::ui::compose_note_view_t& note, ComposeNoteStyle style)
    -> ComposeNoteStyle {
    if (note.proposal_ghost && (note.move_conflict != wormedit::note_move_conflict_t::none))
        return {compose_move_conflict_color(true), compose_move_conflict_color(true), 2.0f};
    if (note.move_conflict_occupant) {
        style.border = compose_move_conflict_color();
        style.border_width = 2.0f;
    }
    return style;
}

static ComposeNoteStyle compose_note_style(const wormedit::ui::compose_view_t& view,
                                           const wormedit::ui::compose_note_view_t& note) {
    auto fill = note.proposal_origin
                    ? nk_rgb(73, 101, 120)
                    : compose_note_base_fill(
                          worm_ui_nuklear_compose_state().input_queue.color_view, note);
    auto border = compose(ComposeColor::note_border);
    auto border_width = 1.0f;
    if (note.proposal_ghost) {
        fill = nk_rgba(240, 171, 71, 132);
        border = nk_rgb(255, 224, 156);
        border_width = 2.0f;
    } else if (view.hover_note_visible && note.note_id.value == view.hover_note_id.value) {
        fill = compose(ComposeColor::hover_fill);
        border = compose(ComposeColor::hover_border);
        border_width = 2.0f;
    } else if (note.paint_invalid_preview) {
        fill = nk_rgba(245, 178, 65, 210);
        border = nk_rgb(255, 229, 166);
        border_width = 2.0f;
    } else if (note.paint_erase_preview) {
        fill = compose(ComposeColor::erase_fill);
        border = compose(ComposeColor::erase_border);
        border_width = 2.0f;
    } else if (note.paint_preview) {
        fill = nk_rgba(81, 218, 145, 210);
        border = nk_rgb(185, 255, 216);
        border_width = 2.0f;
    } else if (note.kind == wormedit::note_kind_t::mine) {
        fill = nk_rgb(214, 91, 104);
    } else if (note.kind == wormedit::note_kind_t::lift) {
        fill = nk_rgb(236, 194, 112);
    } else if (note.tail) {
        fill = wormedit::ui::theme::compose_key_color(note.key_color, note.lane_id).long_note.value_or(compose(ComposeColor::tail));
    }
    if (compose_note_shows_selection_outline(view, note)) {
        border = compose_selection_outline_color();
        border_width = 2.0f;
    }
    if (note.read_only) {
        fill = nk_rgb(105, 110, 125);
        border = nk_rgb(155, 160, 175);
    }
    return compose_move_note_style(note, {fill, border, border_width});
}

static void draw_compose_arrow_outline(const ComposePaintContext& paint,
                                        const wormedit::ui::compose_note_view_t& note,
                                        const std::array<SDL_FPoint, 7>& points) {
    if (compose_arrow_layer_outline_visible(
            worm_ui_nuklear_compose_state().input_queue.color_view, *paint.view, note)) {
        paint_field(paint.context).stroke_polygon(
            2, points, 4.0f,
            {note.semantic_red, note.semantic_green, note.semantic_blue, note.semantic_alpha}, 1);
    }
    const auto style = compose_note_style(*paint.view, note);
    if (note.selected || note.proposal_ghost || note.paint_preview || note.paint_erase_preview ||
        note.paint_invalid_preview ||
        (paint.view->hover_note_visible && note.note_id.value == paint.view->hover_note_id.value) ||
        note.move_conflict_occupant) {
        auto border = note.selected && note.move_conflict == wormedit::note_move_conflict_t::none && !note.move_conflict_occupant
                          ? compose(ComposeColor::arrow_selection)
                          : style.border;
        if (note.proposal_ghost && note.move_conflict == wormedit::note_move_conflict_t::none)
            border.a = 170;
        paint_field(paint.context).stroke_polygon(2, points, 2.0f, field_color(border), 1);
    }
}

static bool draw_compose_note_glyph(const ComposePaintContext& paint,
                                    const wormedit::ui::compose_note_view_t& note, struct nk_rect tile) {
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
        note.proposal_ghost && (note.move_conflict != wormedit::note_move_conflict_t::none)
            ? compose_move_conflict_color(true)
            : compose_note_base_fill(transient ? worm_ui_nuklear_compose_color_view_t::snap
                                               : worm_ui_nuklear_compose_state().input_queue.color_view,
                                     note);
    auto& field = paint_field(paint.context);
    // Proposal ghosts stay translucent so they do not read as materialized notes.
    const auto alpha =
        static_cast<std::uint8_t>(note.proposal_ghost ? ((note.move_conflict != wormedit::note_move_conflict_t::none) ? 153 : 120) : 255);
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
    const auto prefix = wormedit::ui::chrome::fit_text_prefix(
        std::string_view(text, static_cast<std::size_t>(length)),
        wormedit::ui::chrome::font_text_width_t{font}, max_width,
        wormedit::ui::chrome::text_ellipsis, 1);
    return prefix ? static_cast<int>(*prefix) : -1;
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

namespace {

void draw_compose_note_hitsound(const ComposePaintContext& paint, const wormedit::ui::compose_note_view_t& note,
                                struct nk_rect tile, struct nk_color fill) {
    using wormedit::ui::hitsound_icons::shape_t;
    if (note.tail || !note.hitsound) return;
    const auto& sample = *note.hitsound;
    std::array<shape_t, 5> icons{};
    std::size_t count{};
    icons[count++] = wormedit::ui::hitsound_icons::sample_set_shape(sample.resolved_normal_set);
    if (sample.additions != 0 && sample.resolved_addition_set != sample.resolved_normal_set)
        icons[count++] = wormedit::ui::hitsound_icons::sample_set_shape(sample.resolved_addition_set);
    if (sample.additions & 2) icons[count++] = shape_t::whistle;
    if (sample.additions & 4) icons[count++] = shape_t::cymbal;
    if (sample.additions & 8) icons[count++] = shape_t::clap;
    const auto side = std::min({14.0f, tile.h-2, (tile.w-4)/static_cast<float>(count)});
    if (side < 6) { draw_compose_note_sound_dot(paint, tile, fill); return; }
    const auto tint = note_label_is_light(fill) ? nk_rgb(16, 24, 36) : nk_rgb(248, 251, 255);
    auto x = tile.x + (tile.w-side*static_cast<float>(count))*.5f;
    for (std::size_t index = 0; index < count; ++index) {
        field_hitsound_stroke_t stroke{.paint = paint,
            .bounds = nk_rect(x, tile.y+(tile.h-side)*.5f, side, side), .tint = tint};
        wormedit::ui::hitsound_icons::draw(stroke, icons[index]);
        x += side;
    }
}
} // namespace

static void draw_compose_note_sound_label(const ComposePaintContext& paint,
                                          const wormedit::ui::compose_note_view_t& note,
                                          struct nk_rect tile, struct nk_color fill) {
    if (note.hitsound) {
        draw_compose_note_hitsound(paint, note, tile, fill);
        return;
    }
    if ((note.glyph != wormedit::lane_glyph_t::bar && note.glyph != wormedit::lane_glyph_t::square) || note.tail || note.sound_label.empty()) return;
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
                 (note.kind == wormedit::note_kind_t::hold || note.kind == wormedit::note_kind_t::roll
                     ? wormedit::ui::theme::compose_key_color(note.key_color, note.lane_id).long_label
                     : wormedit::ui::theme::compose_key_color(note.key_color, note.lane_id).label).value_or(
                     light_fill ? nk_rgb(16, 24, 36) : nk_rgb(248, 251, 255)), 2);
}

// Semantic layer color as a 3 px stripe inside the left edge; the Layer view already fills with it.
static void draw_compose_note_layer_stripe(const ComposePaintContext& paint,
                                           const wormedit::ui::compose_note_view_t& note, struct nk_rect tile) {
    if (!note.semantic_color_visible || tile.w < 6.0f ||
        worm_ui_nuklear_compose_state().input_queue.color_view == worm_ui_nuklear_compose_color_view_t::layer ||
        compose_note_is_transient(*paint.view, note)) return;
    field_rect(paint, nk_rect(tile.x + 1.0f, tile.y, 3.0f, tile.h), 0.0f,
               nk_rgba(note.semantic_red, note.semantic_green, note.semantic_blue,
                       note.semantic_alpha), 2);
}

// Selected tiles keep their fill and get a 2 px green ring just outside the tile.
static void draw_compose_note_border(const ComposePaintContext& paint,
                                     const wormedit::ui::compose_note_view_t& note,
                                     const ComposeNoteStyle& style, struct nk_rect tile) {
    if (compose_note_shows_selection_outline(*paint.view, note)) {
        field_stroke_rect(paint, tile, 0.0f, 1.0f, compose(ComposeColor::note_border), 2);
        field_stroke_rect(paint, nk_rect(tile.x - 1.0f, tile.y - 1.0f, tile.w + 2.0f, tile.h + 2.0f),
                          0.0f, 2.0f, compose_selection_outline_color(), 2);
        return;
    }
    auto border = style.border;
    if (note.proposal_ghost && note.move_conflict == wormedit::note_move_conflict_t::none) border.a = 170;
    field_stroke_rect(paint, tile, 0.0f, style.border_width, border, 2);
}

static void draw_compose_note(const ComposePaintContext& paint,
                              const wormedit::ui::compose_note_view_t& note) {
    const auto style = compose_note_style(*paint.view, note);
    const auto rect = compose_screen_rect(&note.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto tile = nk_rect(rect.x + 1.0f, rect.y, std::max(1.0f, rect.w - 2.0f),
                              std::max(2.0f, rect.h));
    if (draw_compose_note_glyph(paint, note, tile)) {
        if (!note.proposal_ghost) draw_compose_note_hitsound(paint, note, tile, style.fill);
        return;
    }
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
    auto fill = compose_note_base_fill(worm_ui_nuklear_compose_state().input_queue.color_view, note);
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
    for (size_t index = 0; index < paint.view->note_endpoints.size(); ++index) {
        draw_compose_note(paint, paint.view->note_endpoints[index]);
    }
}

// Timing focus mutes each note without touching what lies behind it, so the
// waveform stays fully opaque.
static void draw_compose_notes_veil(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->note_endpoints.size(); ++index) {
        const auto rect = compose_screen_rect(&paint.view->note_endpoints[index].bounds,
                                              paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) continue;
        field_rect(paint, rect, 0.0f, nk_rgba(22, 25, 33, 150), 10);
    }
}

static void draw_compose_timing_preview_rects(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->timing_preview_rects.size(); ++index) {
        const auto& preview = paint.view->timing_preview_rects[index];
        const auto rect = compose_screen_rect(&preview.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto color = preview.kind == wormedit::ui::compose_timing_preview_kind_t::bgm_event ? nk_rgba(220, 164, 244, 200)
                                             : nk_rgba(110, 222, 236, 195);
        const auto fill = preview.kind == wormedit::ui::compose_timing_preview_kind_t::bgm_event ? nk_rgba(220, 164, 244, 38)
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

static nk_color compose_dead_span_color(wormedit::timing_lane_kind_t kind, std::uint8_t alpha) {
    switch (kind) {
    case wormedit::timing_lane_kind_t::stop: return nk_rgba(226, 110, 84, alpha);  // stop
    case wormedit::timing_lane_kind_t::delay: return nk_rgba(232, 184, 82, alpha);  // delay
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

static void draw_compose_hatched_span(const ComposePaintContext& paint, struct nk_rect rect,
                                      nk_color color, std::string_view label) {
    field_rect(paint, rect, 0.0f, nk_rgba(14, 16, 22, 150), 10);
    color.a = 70;
    draw_compose_dead_span_hatch(paint, rect, color);
    color.a = 200;
    field_line(paint, rect.x, rect.y, rect.x + rect.w, rect.y, 1.0f, color, 10);
    field_line(paint, rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h, 1.0f, color, 10);
    if (rect.h >= 12.0f && rect.w >= 60.0f) {
        const auto text = nk_rect(rect.x + rect.w - 120.0f, rect.y, 116.0f, std::min(rect.h, 16.0f));
        color.a = 255;
        field_text(paint, text, label.data(), static_cast<int>(label.size()), paint.context->style.font,
                   compose(ComposeColor::timing_title_bg), color, 10);
    }
}

static void draw_compose_dead_spans(const ComposePaintContext& paint) {
    for (const auto& span : paint.view->dead_spans) {
        const auto rect = compose_screen_rect(&span.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) continue;
        draw_compose_hatched_span(paint, rect, compose_dead_span_color(span.kind, 255), span.label);
    }
}

static void draw_compose_export_cut_handle(const ComposePaintContext& paint,
    const wormedit::ui::compose_export_cut_handle_t& handle) {
    const auto rect = compose_screen_rect(&handle.bounds, paint.view, paint.viewport);
    const auto hovered = wormedit::ui::nuklear::tooltip::hovered(paint.context, rect);
    const auto active = state.audio_span_drag.candidate_active &&
        state.audio_span_drag.export_cut_id == handle.cut && state.audio_span_drag.edge == handle.edge;
    const auto color = hovered || active ? compose(ComposeColor::timing_hover)
                                        : compose(ComposeColor::export_cut);
    field_rect(paint, rect, 2.0f, nk_rgb(24, 27, 36), 10);
    field_stroke_rect(paint, rect, 2.0f, hovered || active ? 2.0f : 1.0f, color, 10);
    for (const auto offset : {-2.0f, 2.0f})
        field_line(paint, rect.x + 4, rect.y + rect.h / 2 + offset,
            rect.x + 12, rect.y + rect.h / 2 + offset, 1.0f, color, 10);
    const auto begin = handle.edge == WORM_UI_SPAN_HANDLE_BEGIN;
    const auto label = begin ? "Cut start" : "Cut end";
    field_text(paint, nk_rect(rect.x + 17, rect.y + 1, std::max(0.0f, rect.w - 19), rect.h - 2),
        label, static_cast<int>(std::strlen(label)), paint.context->style.font, {}, color, 10);
    if (hovered && !active)
        wormedit::ui::nuklear::tooltip::text(paint.context,
            begin ? "Drag to resize the export cut start on the active grid."
                  : "Drag to resize the export cut end on the active grid.");
}

static void draw_compose_export_cuts(const ComposePaintContext& paint) {
    if (!wormedit::ui::compose_export_cuts_visible(compose_mode_of(*paint.view))) return;
    const auto color = compose(ComposeColor::export_cut);
    for (const auto& cut : paint.view->export_cuts) {
        if (!cut.span.span_projection_visible) continue;
        const auto top = compose_projected_y(paint, cut.span.projected_span_top_y_pixels);
        const auto bottom = compose_projected_y(paint, cut.span.projected_span_bottom_y_pixels);
        const auto rect = nk_rect(paint.viewport.x, top, paint.viewport.w, std::max(1.0f, bottom - top));
        draw_compose_hatched_span(paint, rect, color, cut.label.empty() ? std::string_view{"EXPORT CUT"}
                                                                   : std::string_view{cut.label});
    }
    for (const auto& cut : paint.view->export_cuts)
        for (const auto edge : {WORM_UI_SPAN_HANDLE_BEGIN, WORM_UI_SPAN_HANDLE_END})
            if (const auto handle = wormedit::ui::compose_export_cut_handle_t::from(*paint.view, cut, edge))
                draw_compose_export_cut_handle(paint, *handle);
    if (state.audio_span_drag.export_cut_id.value) draw_compose_audio_span_drag_ghost(paint);
}

static void draw_compose_foreground_markers(const ComposePaintContext& paint) {
    draw_compose_timing_preview_rects(paint);
    draw_compose_playhead(paint);
    draw_compose_cursor(paint);
}

struct ComposeHoveredBgm {
    std::string_view detail{};
    bool restart{};
    bool slice{};
};

static auto compose_bgm_event_fill(const wormedit::ui::compose_bgm_event_view_t& event) -> nk_color {
    auto fill = event.has_sound ? wormedit::ui::theme::compose_bgm_color().note : wormedit::ui::theme::compose_bgm_color().hold_body;
    if (worm_ui_nuklear_compose_state().input_queue.color_view ==
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
                                  const wormedit::ui::compose_bgm_event_view_t& event,
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
        if (event.selected) field_stroke_rect(paint, body, 0.0f, 1.0f, compose(ComposeColor::bgm_selected), 3);
    }
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 1.5f, fill, 3);
    field_stroke_rect(paint, rect, 1.5f, event.selected ? 2.0f : 1.0f,
                   event.selected ? compose(ComposeColor::bgm_selected) : compose(ComposeColor::bgm_border), 3);
    if (event.bmson_slice) {
        const auto color = event.bmson_restart ? nk_rgb(255, 188, 94) : nk_rgb(119, 231, 226);
        field_line(paint, rect.x + 3.0f, rect.y + 2.0f, rect.x + 3.0f,
                       rect.y + rect.h - 2.0f, 2.0f, color, 3);
    }
    if (!event.sound_label.empty() && rect.w >= 18.0f && rect.h >= 13.0f) {
        const auto text_rect = nk_rect(rect.x + (event.bmson_slice ? 6.0f : 3.0f), rect.y + 2.0f,
                                       std::max(1.0f, rect.w - (event.bmson_slice ? 8.0f : 5.0f)),
                                       std::min(15.0f, rect.h - 3.0f));
        field_text(paint, text_rect, event.sound_label.data(),
                     static_cast<int>(event.sound_label.size()), paint.context->style.font, fill,
                     wormedit::ui::theme::compose_bgm_color().label.value_or(compose(ComposeColor::bgm_label)), 3);
    }
    if (!event.proposal_ghost && !event.sound_detail.empty() && nk_input_is_mouse_hovering_rect(&paint.context->input, rect)) {
        hovered = {event.sound_detail, event.bmson_restart, event.bmson_slice};
    }
}

static void draw_compose_bgm_events(const ComposePaintContext& paint,
                                   ComposeHoveredBgm& hovered) {
    for (size_t index = 0; index < paint.view->bgm_events.size(); ++index) {
        draw_compose_bgm_event(paint, paint.view->bgm_events[index], hovered);
    }
}

static void draw_compose_bmson_marker_drafts(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bmson_marker_drafts.size(); ++index) {
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
    std::string_view filename, struct nk_rect rect) {
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
    const wormedit::ui::compose_bgm_lane_view_t& lane, struct nk_rect rect) -> std::string_view {
    if (!lane.stem || lane.filename.empty() || paint.view->input_occluded || rect.w <= 0) return {};
    if (wormedit::ui::nuklear::tooltip::hovered(paint.context,
            compose_bgm_mix_button_rect(lane, *paint.view, paint.viewport, false)) ||
        wormedit::ui::nuklear::tooltip::hovered(paint.context,
            compose_bgm_mix_button_rect(lane, *paint.view, paint.viewport, true))) return {};
    const auto header = nk_rect(rect.x, paint.viewport.y, rect.w, 18);
    return wormedit::ui::nuklear::tooltip::hovered(paint.context, header) ? lane.filename : std::string_view{};
}
auto compose_bgm_mix_button_rect(const wormedit::ui::compose_bgm_lane_view_t& lane,
    const wormedit::ui::compose_view_t& view, struct nk_rect viewport, bool solo) -> struct nk_rect {
    const auto lane_rect = compose_screen_rect(&lane.bounds, &view, viewport);
    if (lane_rect.w < 24.0f || lane_rect.h <= 0.0f) return nk_rect(0, 0, 0, 0);
    const auto width = std::clamp((lane_rect.w - 11.0f) / 2.0f, 10.0f, 16.0f);
    const auto left = lane_rect.x + lane_rect.w - 1.0f - width;
    return nk_rect(solo ? left : left - width - 1.0f, viewport.y + 2.0f, width, 14.0f);
}

static auto bgm_mix_tooltip(bool solo, bool active) -> const char* {
    if (solo) return active ? "Remove BGM lane from solo mix" : "Solo BGM lane (additive)";
    return active ? "Unmute BGM lane" : "Mute BGM lane";
}

static void show_bgm_mix_tooltip(nk_context* context, const wormedit::ui::compose_bgm_lane_view_t& lane,
                                  bool solo, bool active) {
    auto text = std::string{bgm_mix_tooltip(solo, active)};
    if (!lane.filename.empty()) text += "\n" + std::string{lane.filename};
    wormedit::ui::nuklear::tooltip::text(context, text.c_str());
}

static void draw_compose_bgm_mix_controls(const ComposePaintContext& paint,
                                         const wormedit::ui::compose_bgm_lane_view_t& lane) {
    for (const auto solo : {false, true}) {
        const auto rect = compose_bgm_mix_button_rect(lane, *paint.view, paint.viewport, solo);
        if (rect.w <= 0) continue;
        const auto active = solo ? lane.solo : lane.muted;
        const auto fill = wormedit::ui::chrome::mix_toggle_fill(
            solo ? wormedit::ui::chrome::MixToggleKind::solo : wormedit::ui::chrome::MixToggleKind::mute, active);
        const auto text = !paint.view->bgm_mix_available ? nk_rgb(90, 94, 105)
            : lane.mix_silent && !active ? wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted)
            : active ? nk_rgb(255, 255, 255) : wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text);
        field_rect(paint, rect, 2, fill, 5);
        field_text(paint, rect, solo ? "S" : "M", 1, paint.context->style.font, fill, text, 5);
        if (wormedit::ui::nuklear::tooltip::hovered(paint.context, rect))
            show_bgm_mix_tooltip(paint.context, lane, solo, active);
    }
}

static auto bgm_mix_controls_visible(const ComposePaintContext& paint,
    const wormedit::ui::compose_bgm_lane_view_t& lane, struct nk_rect rect) -> bool {
    if (!lane.stem || lane.filename.empty() || rect.w >= 64 || lane.muted || lane.solo)
        return true;
    return nk_input_is_mouse_hovering_rect(&paint.context->input,
        nk_rect(rect.x, paint.viewport.y, rect.w, 18));
}

static auto bgm_header_label_rect(const ComposePaintContext& paint,
    const wormedit::ui::compose_bgm_lane_view_t& lane, struct nk_rect rect, bool controls) -> struct nk_rect {
    if (!controls) return rect;
    const auto button = compose_bgm_mix_button_rect(lane, *paint.view, paint.viewport, false);
    if (button.w > 0) rect.w = std::max(0.0f, button.x - rect.x - 1.0f);
    return rect;
}

static auto draw_compose_bgm_lane_labels(const ComposePaintContext& paint) -> std::string_view {
    std::string_view hovered_filename{};
    for (size_t index = 0; index < paint.view->bgm_lanes.size(); ++index) {
        const auto& lane = paint.view->bgm_lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (const auto name = hovered_compose_stem_header(paint, lane, rect); !name.empty()) hovered_filename = name;
        if (rect.w < 18.0f || rect.h <= 0.0f) {
            continue;
        }
        const auto controls = bgm_mix_controls_visible(paint, lane, rect);
        if (controls) draw_compose_bgm_mix_controls(paint, lane);
        const auto label_rect = bgm_header_label_rect(paint, lane, rect, controls);
        if (label_rect.w < 8.0f) continue;
        if (lane.stem && !lane.filename.empty()) {
            draw_compose_stem_filename(paint, lane.filename, label_rect);
            continue;
        }
        char label[20]{};
        const auto length = snprintf(label, sizeof(label), "%zu", lane.source_order + 1);
        if (length > 0) {
            const auto title = nk_rect(label_rect.x + 2.0f, paint.viewport.y + 2.0f,
                                       std::max(1.0f, label_rect.w - 4.0f), 14.0f);
            field_text(paint, title, label, static_cast<int>(strlen(label)),
                         paint.context->style.font, nk_rgba(31, 25, 38, 215),
                         nk_rgb(211, 175, 226), 3);
        }
    }
    return hovered_filename;
}

static void draw_compose_bga_stack(const ComposePaintContext& paint,
                                   const wormedit::ui::compose_bga_event_view_t& event) {
    const auto rect = compose_screen_rect(&event.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    field_rect(paint, rect, 1.0f, nk_rgb(95, 74, 132), 4);
    field_stroke_rect(paint, rect, 1.0f, 2.0f, nk_rgb(228, 206, 255), 4);
    char label[32];
    std::snprintf(label, sizeof(label), "%zu events", event.overlap_count);
    field_text(paint, rect, label, static_cast<int>(std::strlen(label)),
               paint.context->style.font, nk_rgb(95, 74, 132), nk_rgb(255, 255, 255), 4);
    if (wormedit::ui::nuklear::tooltip::hovered(paint.context, rect))
        wormedit::ui::nuklear::tooltip::text(paint.context,
            "Choose a BGA event at this measure");
}

static void draw_compose_bga_move_ghost(const ComposePaintContext& paint,
                                        const wormedit::ui::compose_bga_event_view_t& event,
                                        const struct nk_rect& original) {
    if (!paint.view->bga_move_preview_valid ||
        paint.view->bga_move_preview_event_id.value != event.event_id.value) return;
    const auto y = compose_projected_y(paint, paint.view->bga_move_preview_y_pixels);
    const auto ghost = nk_rect(original.x, y, original.w, original.h);
    field_rect(paint, ghost, 1.0f, nk_rgba(145, 206, 255, 135), 4);
    field_stroke_rect(paint, ghost, 1.0f, 2.0f, nk_rgb(195, 234, 255), 4);
}

static bool is_first_bga_stack_event(const wormedit::ui::compose_view_t& view, size_t index) {
    const auto row = view.bga_events[index].canvas_row;
    const auto lane = wormedit::editor_bga_lane_index(
        static_cast<wormedit::visual_event_kind_t>(view.bga_events[index].kind));
    return !std::ranges::any_of(std::span(view.bga_events).first(index),
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
    const bool video = event.kind == wormedit::visual_event_kind_t::video;
    const auto fill = event.missing ? nk_rgb(123, 58, 65)
            : video ? nk_rgb(74, 126, 151) : nk_rgb(104, 133, 78);
    field_rect(paint, rect, 1.0f, fill, 4);
    field_stroke_rect(paint, rect, 1.0f, event.selected ? 2.0f : 1.0f,
                          event.selected ? nk_rgb(255, 240, 173) : nk_rgb(34, 39, 32), 4);
    const char* label = !event.bmp_id.empty() ? event.bmp_id.data() : event.name.data();
    if (label && rect.w >= 18.0f && rect.h >= 13.0f) {
        const auto text_rect = nk_rect(rect.x + 1.0f, rect.y + 1.0f,
                                           std::max(1.0f, rect.w - 2.0f),
                                           std::min(14.0f, rect.h - 2.0f));
        field_text(paint, text_rect, label, static_cast<int>(strlen(label)),
                       paint.context->style.font, fill, nk_rgb(255, 255, 245), 4);
    }
    if (wormedit::ui::nuklear::tooltip::hovered(paint.context, rect))
        wormedit::ui::nuklear::tooltip::text(paint.context,
            event.missing ? "Missing BGA resource" : event.name.data());
}

static void draw_compose_bga_events(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bga_events.size(); ++index)
        draw_compose_bga_event(paint, index);
}

static void draw_compose_bga_move_preview(const ComposePaintContext& paint) {
    if (!state.bga_interaction.bga_event_move_active) return;
    const auto events = std::span(paint.view->bga_events);
    const auto event = std::ranges::find(events, wormedit::object_id_t{state.bga_interaction.bga_event_move_object_id}, &wormedit::ui::compose_bga_event_view_t::event_id);
    if (event == events.end()) return;
    auto rect = compose_screen_rect(&event->bounds, paint.view, paint.viewport);
    const auto target = compose_bga_lane_kind_at(*paint.view,
        state.bga_interaction.bga_event_move_current_x_pixels, state.bga_interaction.bga_event_move_current_y_pixels);
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
    case wormedit::visual_event_kind_t::poor: return "Poor (miss)";
    default: return "BGA";
    }
}

static void draw_compose_bga_lane(const ComposePaintContext& paint,
                                 const wormedit::ui::compose_rect_t& rect, std::uint8_t kind) {
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
        std::span(paint.view->bga_events),
        [](const auto& event) { return event.selected; });
    draw_compose_cached_pass(paint, 10, true, selected,
        [&](const ComposePaintContext& cached) { draw_compose_bga_events(cached); });
    draw_compose_bga_move_preview(paint);
}

// Highlights the right edge of the stem lane that the pointer would resize.
static void draw_compose_stem_resize_edge(const ComposePaintContext& paint) {
    const auto hovered = worm_ui_nuklear_compose_state().stem_lanes.stem_lane_resize_hover_lane;
    if (!hovered) return;
    for (const auto& lane : std::span(paint.view->bgm_lanes)) {
        if (lane.source_lane_id.value != hovered || !lane.stem ||
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
        std::span(paint.view->bgm_events),
        [](const auto& event) { return event.selected || event.proposal_ghost; });
    if (draw_events)
        draw_compose_cached_pass(paint,8,true,selected,[&](const ComposePaintContext& cached) { draw_compose_bgm_events(cached,hovered); });
    // Hover details are independent of the retained visual pass.
    for (size_t index=0; index<paint.view->bgm_events.size(); ++index) {
        const auto& event=paint.view->bgm_events[index];
        const auto head = compose_bgm_head_bounds(event);
        if (!event.proposal_ghost && !event.sound_detail.empty() && nk_input_is_mouse_hovering_rect(&paint.context->input,
            compose_screen_rect(&head,paint.view,paint.viewport)))
            hovered={event.sound_detail,event.bmson_restart,event.bmson_slice};
    }
    draw_compose_bmson_marker_drafts(paint);
    draw_compose_bmson_move_preview(paint);
    draw_compose_stem_resize_edge(paint);
    if (const auto filename = draw_compose_bgm_lane_labels(paint); !filename.empty()) {
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
                                           const wormedit::ui::compose_rect_t& bounds,
                                           WormUiComposeSelectionMode mode) {
    const auto rect = compose_screen_rect(&bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto fill = mode == WORM_UI_COMPOSE_SELECTION_ADD ? compose(ComposeColor::selection_add)
                     : mode == WORM_UI_COMPOSE_SELECTION_TOGGLE ? compose(ComposeColor::selection_toggle)
                                                               : compose(ComposeColor::selection_box);
    const auto border = mode == WORM_UI_COMPOSE_SELECTION_ADD ? compose(ComposeColor::selection_add_border)
                        : mode == WORM_UI_COMPOSE_SELECTION_TOGGLE ? compose(ComposeColor::selection_toggle_border)
                                                                  : compose(ComposeColor::selection_box_border);
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

static auto compose_hover_display_lane_number(const wormedit::ui::compose_view_t& view) -> uint32_t {
    for (size_t index = 0; index < view.note_endpoints.size(); ++index) {
        const auto& note = view.note_endpoints[index];
        if (note.note_id.value == view.hover_note_id.value && note.display_lane_number != 0) {
            return note.display_lane_number;
        }
    }
    for (size_t index = 0; index < view.hold_bodies.size(); ++index) {
        const auto& hold = view.hold_bodies[index];
        if (hold.note_id.value == view.hover_note_id.value && hold.display_lane_number != 0) {
            return hold.display_lane_number;
        }
    }
    return 0;
}

static auto compose_note_tooltip_title(const wormedit::ui::compose_view_t& view) -> std::string {
    std::string title = view.hover_tooltip.c_str();
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

static auto compose_note_tooltip_lines(const wormedit::ui::compose_view_t& view) -> std::vector<std::string> {
    std::vector<std::string> lines{compose_note_tooltip_title(view)};
    for (size_t index = 0; index < view.note_endpoints.size(); ++index) {
        const auto& note = view.note_endpoints[index];
        if (note.note_id.value != view.hover_note_id.value) continue;
        if (!note.sound_detail.empty()) {
            auto resource_text = std::string{note.sound_detail};
            if (!note.sound_label.empty() && resource_text != note.sound_label)
                resource_text += std::string{"  (ID "} + std::string{note.sound_label} + ")";
            lines.push_back(std::move(resource_text));
        }
        if (note.hitsound) {
            const auto details = note.hitsound->details();
            lines.insert(lines.end(), details.begin(), details.end());
        }
        break;
    }
    return lines;
}

static void draw_compose_note_tooltip(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.hover_note_visible || !view.hover_tooltip.c_str() || view.hover_tooltip.c_str()[0] == '\0') return;
    const auto lines = compose_note_tooltip_lines(view);
    const auto* font = paint.context->style.font;
    float text_width{};
    for (const auto& line : lines)
        text_width = std::max(text_width, font->width(font->userdata, font->height,
            line.c_str(), static_cast<int>(line.size())));
    const auto bounds = compose_pointer_tooltip_bounds(
        paint, text_width, static_cast<float>(lines.size())*26-2);
    field_rect(paint, bounds, 3.0f, nk_rgba(12, 16, 24, 235), 5);
    field_stroke_rect(paint, bounds, 3.0f, 1.0f, compose(ComposeColor::hold_hover), 5);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& line = lines[index];
        field_text(paint, nk_rect(bounds.x + 8, bounds.y+26*static_cast<float>(index), bounds.w-16, 24),
            line.c_str(), static_cast<int>(line.size()), font, nk_rgb(12, 16, 24),
            index == 0 ? nk_rgb(255, 244, 192) : nk_rgb(245, 227, 255), 5);
    }
}

static void draw_compose_timing_tooltip(const ComposePaintContext& paint) {
    const auto* marker = compose_hovered_timing_marker(paint);
    if (!marker || marker->detail.empty()) return;
    const auto* font = paint.context->style.font;
    const auto length = static_cast<int>(marker->detail.size());
    const auto text_width = font->width(font->userdata, font->height, marker->detail.data(), length);
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
    field_stroke_rect(paint, bounds, 3.0f, 1.0f, compose(ComposeColor::timing_hover), 5);
    field_text(paint, bounds, marker->detail.data(), length, font,
                 nk_rgb(12, 16, 24), nk_rgb(255, 244, 192), 5);
}

static void draw_compose_bgm_tooltip(const ComposePaintContext& paint,
                                     const ComposeHoveredBgm& hovered) {
    if (hovered.detail.empty()) return;
    const auto* font = paint.context->style.font;
    const auto length = static_cast<int>(hovered.detail.size());
    const auto detail_width = font->width(font->userdata, font->height,
                                          hovered.detail.data(), length);
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
    field_text(paint, name_bounds, hovered.detail.data(), length, font,
                 nk_rgb(12, 16, 24), nk_rgb(245, 227, 255), 5);
}

static bool draw_compose_paste_arrow(const ComposePaintContext& paint,
                                     const wormedit::ui::compose_paste_ghost_view_t& ghost,
                                     struct nk_rect tile, nk_color color) {
    if (ghost.kind != wormedit::ui::compose_paste_ghost_kind_t::note_endpoint || !wormedit::ui::is_arrow_glyph(ghost.glyph) || tile.w < 10.0f) return false;
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
                                      const wormedit::ui::compose_paste_ghost_view_t& ghost) -> struct nk_rect {
    wormedit::ui::compose_timing_marker_view_t marker{};
    marker.lane = ghost.timing_lane;
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

static void draw_compose_ghosts(const ComposePaintContext& paint,
    std::span<const wormedit::ui::compose_paste_ghost_view_t> ghosts, struct nk_color color) {
    const auto& view = *paint.view;
    for (const auto& ghost : ghosts) {
        const auto rect = ghost.kind == wormedit::ui::compose_paste_ghost_kind_t::timing_object
            ? compose_timing_ghost_rect(paint, ghost) : compose_screen_rect(&ghost.bounds, &view, paint.viewport);
        if (draw_compose_paste_arrow(paint, ghost, rect, color)) continue;
        field_rect(paint, rect, 0.0f, color, 5);
        field_stroke_rect(paint, rect, 0.0f, 2.0f, color, 5);
    }
}
static void draw_compose_paste_ghosts(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    draw_compose_ghosts(paint, view.paste_ghosts,
        view.paste_valid ? nk_rgba(240, 171, 71, 130) : nk_rgba(242, 70, 83, 155));
    draw_compose_ghosts(paint, view.generator_ghosts, nk_rgba(100, 210, 170, 150));
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

static auto compose_move_status_hint(const wormedit::ui::compose_view_t* view) -> const char* {
    static std::string message;
    if (!view) return nullptr;
    if (view->move_conflict_count) message = view->selected_bgm_event_count
        ? "Selected objects cannot move here"
        : std::to_string(view->move_conflict_count) + " notes would overlap";
    else if (view->move_to_bgm_count) message = std::to_string(view->move_to_bgm_count) + " notes will move to BGM";
    else return nullptr;
    return message.c_str();
}

static auto compose_idle_bgm_move_hint(const wormedit::ui::compose_view_t& view) -> const char* {
    if (view.bms_measure_labels) return "drag BMS BGM to move";
    if (view.bmson_tools_available) return "drag BMSON BGM to move";
    return "drag BGM to move";
}

static auto compose_range_status_hint(const wormedit::ui::compose_view_t& view) -> const char* {
    if (!state.range_selection.space_range_latched) return nullptr;
    return view.inline_playback_active
        ? "Range follows playback · Esc: stop, then cancel · Ctrl+Space: select"
        : "Range latched · Esc/outside lanes: cancel · Ctrl+Space: select · Space: play";
}

auto compose_editor_status_hint(const wormedit::ui::compose_view_t* view, const wormedit::ui::timing_view_t* timing_view)
    -> const char* {
    if (const auto* conflict = compose_move_status_hint(view))
        return conflict;
    if (view && view->paste_active)
        return view->paste_valid ? "Paste preview · click or Enter commits · Esc cancels"
                                : (view->paste_diagnostic.data() ? view->paste_diagnostic.data() : "Paste collision");
    if (const auto* range = compose_range_status_hint(*view))
        return range;
    if (state.timing_drag.timing_stage_drag_active)
        return "Move timing event · release to keep staged position · Apply to commit";
    if (state.waveform_drag.beat_fit_drag_active)
        return "Fit beat to waveform · Ctrl snaps BPM · Ctrl+Alt snaps onsets · release to commit "
               "· Esc cancels";
    if (state.waveform_drag.offset_fit_drag_active)
        return "Shift offset to waveform · release to commit · Esc cancels";
    if (state.waveform_drag.waveform_beat_zero_drag_active)
        return "Move beat zero on waveform overview · release to commit · Esc cancels";
    if (state.bmson_input.bmson_marker_move_active)
        return "Move BMSON BGM marker · release to commit · Esc cancels";
    if (state.resource_picker.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place)
        return "Resource placement armed · click a chart lane · Esc disarms";
    if (state.resource_picker.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::replace)
        return "Resource replacement armed · click an existing object · Esc disarms";
    if (state.bmson_input.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off)
        return "BMSON razor armed · click/drag in selected BGM lane · Esc disarms";
    if (timing_view->stage_active && bool(timing_view->stage_existing_object_id))
        return "Drag outlined timing marker to resnap · edit/apply in side inspector";
    if (view->semantic_layer_paint_mode_active)
        return "";
    if (view->semantic_layer_solo_active)
        return "Layer solo active · only selected-layer notes shown";
    if (view->note_paint_mode_active)
        return "Draw: click or use lane keys · Alt-drag holds · double-click a sound to arm · Space plays";
    if (view->waveform_overview_rows.size() != 0 && !view->waveform_timing_locked)
        return "Drag beat squares in rail to fit BPM · Shift+drag to shift offset · drag beat zero in song overview";
    static std::string message;
    message = "Draw: Alt+2 · ↑/↓ move · drag selects/moves · Space plays · ";
    message += compose_idle_bgm_move_hint(*view);
    return message.c_str();
}

static auto compose_notes_require_direct(const wormedit::ui::compose_view_t& view) -> bool {
    if (view.selected_note_count || view.note_paint_mode_active || view.semantic_layer_paint_mode_active)
        return true;
    // A published reusable source cannot contain transient note proposals;
    // the core window policy rejects those previews before source publication.
    if (view.static_scene) return false;
    return std::ranges::any_of(std::span(view.note_endpoints),
        [](const auto& note) { return note.selected || note.proposal_ghost || note.proposal_origin; }) ||
        std::ranges::any_of(std::span(view.hold_bodies),
        [](const auto& hold) { return hold.selected || hold.proposal_ghost || hold.proposal_origin; });
}

static void bind_compose_field_layout(const wormedit::ui::compose_view_t& view) {
    if (!wormedit::ui::theme::field_uses_positions()) return;
    const auto& lanes = view.lanes;
    const auto count = view.lanes.size();
    std::vector<wormedit::ui::theme::field_lane_t> field_lanes;
    field_lanes.reserve(count);
    for (const auto& lane : std::span(lanes))
        field_lanes.push_back({lane.display_order, static_cast<std::uint8_t>(lane.stage),
            static_cast<std::uint8_t>(lane.key_color), static_cast<std::uint8_t>(lane.role), lane.lane_id.value});
    wormedit::ui::theme::bind_field_layout(field_lanes);
}

static void begin_compose_scene_field(nk_context* context, const wormedit::ui::compose_view_t& view,
                                      struct nk_rect viewport) {
    bind_compose_field_layout(view);
    auto& field=wormedit::ui::nuklear::begin_compose_field(context,
        {viewport.x,viewport.y,viewport.w,viewport.h});
    if (!field.can_retain_tiles()) return;
    refresh_static_work(context,view);
    field.account_tile_source(canvas_accounted_source_bytes(context,view));
}

void draw_compose_scene_canvas(nk_context* context, const wormedit::ui::compose_view_t* view,
                                      nk_command_buffer* canvas, struct nk_rect viewport) {
    last_compose_viewport_for_testing = wormedit::ui::testing_rect_view_t{
        viewport.x, viewport.y, viewport.w, viewport.h};
    quantize_overlay_viewport = last_compose_viewport_for_testing;
    if (!wormedit::ui::nuklear::can_draw_compose_field(context)) {
        auto& owner = wormedit::ui::nuklear::compose_canvas_cache(context);
        if (owner) static_cast<canvas_cache_state_t&>(*owner).static_work = {};
        return;
    }
    begin_compose_scene_field(context,*view,viewport);
    const auto parent_clip = canvas->clip;
    nk_push_scissor(canvas, viewport);
    const ComposePaintContext background{context, view, canvas, viewport, viewport};
    field_rect(background, viewport, 0.0f,
                 wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::viewport));
    if (!view->scene_available) {
        const auto* status = view->status_text.data() ? view->status_text.data() : "Chart field is unavailable.";
        field_text(background, viewport, status, static_cast<int>(strlen(status)),
                     context->style.font,
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::viewport),
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::label));
    } else {
        const auto rail_width = static_cast<float>(compose_measure_rail_width * viewport.w / view->viewport_width_pixels);
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
            !wormedit::ui::nuklear::can_retain_compose_grid(context) ||
            view->selected_bms_measure_number_plus_one!=0,[&](const ComposePaintContext& cached) { draw_compose_grid_lines(cached); });
        draw_compose_lane_layout_segments(paint);
        draw_compose_measure_lane(paint);
        draw_compose_dead_spans(paint);
        draw_compose_export_cuts(paint);
        draw_compose_area_band(paint);
        draw_compose_timing_layer(paint, timing_layout);
        const bool active_notes=compose_notes_require_direct(*view);
        const bool active_bgm=std::ranges::any_of(std::span(view->bgm_events),
            [](const auto& event) { return event.selected || event.proposal_ghost; });
        const auto hold_holes=compose_hover_holes(paint,7);
        const auto note_holes=compose_note_holes(paint);
        const bool grouped=!active_notes && !active_bgm && hold_holes.has_value();
        draw_compose_cached_pass(paint,7,true,!grouped,[&](const ComposePaintContext& cached) {
            draw_compose_hold_bodies(cached);
            if (grouped) {
                ComposeHoveredBgm ignored{};
                draw_compose_bgm_events(cached,ignored);
            }
        },hold_holes ? std::span<const SDL_FRect>(*hold_holes) : std::span<const SDL_FRect>{});
        const auto hovered_bgm = draw_compose_audio_markers(paint,!grouped);
        draw_compose_cached_pass(paint,9,true,active_notes || !note_holes ||
            !wormedit::ui::nuklear::can_retain_compose_glyphs(context),
            [&](const ComposePaintContext& cached) { draw_compose_notes(cached); },
            note_holes ? std::span<const SDL_FRect>(*note_holes) : std::span<const SDL_FRect>{});
        if (wormedit::ui::compose_notes_dimmed(compose_mode_of(*paint.view)))
            draw_compose_notes_veil(paint);
        draw_compose_paint_ghost(paint);
        draw_compose_foreground_markers(paint);
        draw_compose_editor_overlays(paint, hovered_bgm);
    }
    finish_static_work(context);
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
    length = static_cast<int>(wormedit::text_util::truncate_utf8(
        std::string_view(text, static_cast<std::size_t>(length) + (text[length] ? 1 : 0)),
        static_cast<std::size_t>(length)).size());
    if (font->width(font->userdata, font->height, text, length) <= width) {
        std::memcpy(chip.source.data(), text, static_cast<std::size_t>(length));
        return;
    }
    const auto prefix = fit_label_prefix(font, text, length, width);
    if (prefix < 0) return;
    std::memcpy(chip.source.data(), text, static_cast<std::size_t>(prefix));
    std::memcpy(chip.source.data() + prefix, "…", 3);
}

void set_paint_chip_source(ToolChip& chip, const wormedit::ui::compose_view_t& view,
                           const char*& source) {
    if (!view.resource_paint_mode_active) return;
    chip.prefix = view.resource_paint_has_sample
        ? (view.hitsounds.visible ? "Keysound + osu! · " : "Keysound · ") : "Hitsounds · ";
    chip.suffix = " · paint existing notes";
    if (!view.resource_paint_has_sample) { source = "osu! hitsound palette"; chip.warning = false; }
}

auto tool_chip(nk_context* context, const wormedit::ui::compose_view_t& view, struct nk_rect field) -> ToolChip {
    ToolChip chip;
    const auto* font = context->style.font;
    const auto available = field.w - 24.0f;
    if (!font || available < 64 || field.h < 54 || (!view.note_paint_mode_active && !view.semantic_layer_paint_mode_active && !view.resource_paint_mode_active))
        return chip;
    chip.layer = view.semantic_layer_paint_mode_active;
    chip.prefix = chip.layer ? "Layer · "
        : view.draw_source_kind == 1 && view.bmson_tools_available
            ? (view.draw_source_is_stem ? "Draw stem · " : "Draw packed keysound · ")
            : "Draw · ";
    chip.suffix = available < 360 || (!chip.layer && !view.draw_source_kind) ? ""
        : chip.layer ? " · Alt+drag removes" : " · right-drag erases";
    chip.warning = !chip.layer && view.draw_source_missing;
    const auto* source = chip.layer ? view.semantic_layer_target.name.data() : view.draw_source_label.c_str();
    set_paint_chip_source(chip, view, source);
    if (!source) source = chip.layer ? "Pick a layer" : wormedit::ui::no_draw_sound_resource_label;
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

void reveal_chip_sound(const wormedit::ui::compose_view_t& view) {
    state.dock_layout.layout.show(wormedit::dock_panel_t::sounds, true);
    state.dock_layout.sounds_dock_bmson_tab = false;
    state.keysound_filters.keysound_slots_mode = false;
    state.keysound_catalog.keysound_search.fill(0);
    state.keysound_filters.keysound_filters = {};
    state.keysound_filters.keysound_filters.include_ungrouped = true;
    state.keysound_catalog.keysound_selection.primary = view.draw_source_id.value;
    state.keysound_catalog.keysound_selection.selected = {view.draw_source_id.value};
    state.keysound_catalog.keysound_reveal_primary = true;
}

void reveal_chip_source(const wormedit::ui::compose_view_t& view, bool layer) {
    if (view.resource_paint_mode_active && !view.resource_paint_has_sample) return;
    state.pointer_capture.source_chip_reveal_kind = 0;
    state.pointer_capture.source_chip_reveal_id = state.pointer_capture.layer_chip_reveal_id = 0;
    state.keysound_catalog.keysound_reveal_primary = state.bga_catalog.bga_reveal_primary = false;
    state.pointer_capture.chip_reveal_project = view.project_id.value;
    state.pointer_capture.chip_reveal_chart = view.chart_id.value;
    if (layer) {
        if (!view.semantic_layer_target.layer_id.value) return;
        state.dock_layout.layout.show(wormedit::dock_panel_t::layers, true);
        state.pointer_capture.layer_chip_reveal_id = view.semantic_layer_target.layer_id.value;
        return;
    }
    if (!view.draw_source_id.value || view.draw_source_missing) return;
    state.pointer_capture.source_chip_reveal_kind = view.draw_source_kind;
    state.pointer_capture.source_chip_reveal_id = view.draw_source_id.value;
    if (view.draw_source_kind == 1) reveal_chip_sound(view);
    else if (view.draw_source_kind == 2) {
        state.dock_layout.layout.show(wormedit::dock_panel_t::images, true);
        state.bga_catalog.bga_search.fill(0);
        state.bga_catalog.bga_selection.primary = view.draw_source_id.value;
        state.bga_catalog.bga_selection.selected = {view.draw_source_id.value};
        state.bga_catalog.bga_reveal_primary = true;
    }
}
}

void draw_compose_tool_chip(nk_context* context, const wormedit::ui::compose_view_t& view,
                           nk_command_buffer* canvas, struct nk_rect field) {
    const auto chip = tool_chip(context, view, field);
    if (chip.bounds.w <= 0) return;
    const auto clip = canvas->clip;
    wormedit::ui::chrome::paint_surface(canvas, chip.bounds, 1, field);
    nk_push_scissor(canvas, field);
    const auto text = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text);
    wormedit::ui::lucide::draw(context, nk_rect(chip.bounds.x + 4, chip.bounds.y + 3, 24, 24),
        chip.layer ? wormedit::ui::lucide::Glyph::layers
                   : view.resource_paint_mode_active ? wormedit::ui::lucide::Glyph::audio_lines
                                                     : wormedit::ui::lucide::Glyph::paintbrush, text);
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

auto process_compose_tool_chip(nk_context* context, const wormedit::ui::compose_view_t& view,
                              struct nk_rect field, bool focused) -> bool {
    const auto chip = tool_chip(context, view, field);
    const auto& button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto source = chip.layer ? view.semantic_layer_target.layer_id.value : view.draw_source_id.value;
    const auto kind = static_cast<std::uint8_t>(chip.layer ? 3
        : view.note_paint_mode_active ? view.draw_source_kind : 4);
    if (state.pointer_capture.tool_chip_pointer_active) {
        const auto& stamp = state.pointer_capture.tool_chip_stamp;
        state.pointer_capture.tool_chip_pointer_canceled |= !focused || view.input_occluded ||
            stamp.project != view.project_id.value || stamp.chart != view.chart_id.value ||
            stamp.revision != view.document_revision || stamp.source != source || stamp.kind != kind;
        if (!button.down) state.pointer_capture.tool_chip_pointer_active = false;
        return true;
    }
    if (!focused || view.input_occluded) return false;
    if (chip.bounds.w <= 0 || !button.clicked ||
        !nk_input_has_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, chip.bounds)) return false;
    state.pointer_capture.tool_chip_pointer_active = button.down != 0;
    state.pointer_capture.tool_chip_pointer_canceled = false;
    state.pointer_capture.tool_chip_stamp = {view.project_id.value, view.chart_id.value, view.document_revision, source, kind};
    if (nk_input_has_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, chip.source_bounds))
        reveal_chip_source(view, chip.layer);
    return true;
}
