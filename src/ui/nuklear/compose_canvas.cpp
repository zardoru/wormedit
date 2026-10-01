#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/arrow_glyph.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/sdl_backend.h"

#include "wormedit/nuklear_config.h"

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
    auto& field = wormedit::ui::nuklear::compose_field(paint.context);
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
    wormedit::ui::nuklear::compose_field(paint.context)
        .line(layer, {x0, y0}, {x1, y1}, width, field_color(color));
}
static void field_stroke_rect(const ComposePaintContext& paint, struct nk_rect bounds, float rounding,
                              float width, nk_color color,
                              wormedit::ui::field::layer_t layer = 0) {
    if (rounding >= 1) {
        const auto rounded = wormedit::ui::field::rounded_rectangle_points(
            {std::trunc(bounds.x), std::trunc(bounds.y), std::trunc(bounds.w), std::trunc(bounds.h)},
            std::trunc(rounding));
        wormedit::ui::nuklear::compose_field(paint.context).stroke_polygon(
            layer, rounded, width, field_color(color));
        return;
    }
    const std::array<SDL_FPoint, 4> points{{{bounds.x, bounds.y},
                                            {bounds.x + bounds.w, bounds.y},
                                            {bounds.x + bounds.w, bounds.y + bounds.h},
                                            {bounds.x, bounds.y + bounds.h}}};
    wormedit::ui::nuklear::compose_field(paint.context).stroke_polygon(layer, points, width,
                                                                     field_color(color));
}
static void field_text(const ComposePaintContext& paint, struct nk_rect bounds, const char* text,
                       int length, const nk_user_font* font, nk_color, nk_color foreground,
                       wormedit::ui::field::layer_t layer = 0) {
    wormedit::ui::nuklear::draw_compose_field_text(paint.context, layer, bounds, text, length, font,
                                                 foreground);
}

static void draw_compose_grid_tick(const ComposePaintContext& paint,
                                   const WormUiComposeGridLineView& line, float y) {
    const auto& view = *paint.view;
    auto& field = wormedit::ui::nuklear::compose_field(paint.context);
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
    auto& field = wormedit::ui::nuklear::compose_field(paint.context);
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

static void draw_compose_beat_bookmarks(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->beat_bookmark_count; ++index) {
        const auto& bookmark = paint.view->beat_bookmarks[index];
        if (bookmark.projection_visible) {
            const auto y = compose_projected_y(paint, bookmark.projected_y_pixels);
            field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w,
                           y, 1.3f, nk_rgb(111, 224, 201), 4);
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

static void draw_compose_audio_bookmarks(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->audio_bookmark_count; ++index) {
        const auto& bookmark = paint.view->audio_bookmarks[index];
        if (!bookmark.projection_visible) {
            continue;
        }
        const auto y = compose_projected_y(paint, bookmark.projected_y_pixels);
        field_line(paint, paint.viewport.x, y, paint.viewport.x + paint.viewport.w, y,
                       1.4f, nk_rgb(232, 166, 255), 4);
    }
}

static struct nk_color compose_lane_color(uint8_t role, size_t display_order) {
    const auto alternate = (display_order & 1U) != 0;
    switch (role) {
    case 1: // scratch
        return wormedit::ui::theme::compose_lane(0, alternate);
    case 5: // free zone
        return wormedit::ui::theme::compose_lane(1, alternate);
    case 6: // foot pedal
        return wormedit::ui::theme::compose_lane(2, alternate);
    default:
        return wormedit::ui::theme::compose_lane(3, alternate);
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
    const auto width = (view.playable_field_left_pixels - compose_measure_rail_width) / visible_count;
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

static void draw_compose_timing_lanes(const ComposePaintContext& paint,
                                      ComposeTimingLaneLayout& layout) {
    constexpr std::array<const char*, WORM_UI_TIMING_LANE_COUNT> labels{
        "BPM", "SV", "SPD", "STP", "DLY", "WRP", "KIA", "ANC"};
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
        field_rect(paint, rect, 0.0f, compose_lane_color(lane.role, lane.display_order));
        field_line(paint, rect.x, rect.y, rect.x, rect.y + rect.h, 1.0f,
                       nk_rgb(62, 70, 86));
        if (rect.w >= 28.0f && rect.h >= 18.0f) {
            char label[16]{};
            const auto label_length = snprintf(label, sizeof(label), "%u", lane.display_order + 1U);
            if (label_length > 0) {
                const auto label_bounds = nk_rect(rect.x + 2.0f, rect.y + 2.0f,
                                                  std::min(28.0f, rect.w - 4.0f), 15.0f);
                field_text(paint, label_bounds, label, static_cast<int>(strlen(label)),
                             paint.context->style.font, nk_rgb(37, 43, 55), nk_rgb(122, 133, 153));
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
    draw_compose_timing_lanes(paint, layout);
    draw_compose_bands(paint);
    draw_compose_play_lanes(paint);
    draw_compose_bgm_lanes(paint);
}

static auto draw_compose_envelope_texture(const ComposePaintContext& paint,
        const WormUiComposeWaveformRowView* source, size_t count, float center, float half_width,
        size_t slot, nk_color rms, nk_color peak, bool joined) -> bool {
    const auto& view = *paint.view;
    const auto y_scale = paint.viewport.h / static_cast<float>(view.viewport_height_pixels);
    if (!std::isfinite(y_scale) || y_scale <= 0) return false;
    // Envelope texels describe whole world pixels. PCM points keep their exact
    // projected coordinates in draw_compose_waveform_samples.
    const auto scroll = std::floor(view.area_scroll_pixels);
    const auto origin = view.downscroll
        ? -scroll - view.viewport_height_pixels : scroll;
    static std::vector<wormedit::ui::field::envelope_row_t> rows;
    rows.clear();
    rows.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const auto& row = source[index];
        rows.push_back({row.y_pixels + origin, {row.rms[0], row.rms[1]},
                                               {row.absolute_peak[0], row.absolute_peak[1]}});
    }
    auto key = view.document_revision ^ (view.project_id * 1099511628211ULL);
    key ^= view.chart_id * 1469598103934665603ULL;
    key ^= view.selected_audio_asset_id * 1099511628211ULL;
    key ^= view.channel_waveform_source_lane_id * static_cast<uint64_t>(slot);
    key ^= std::bit_cast<uint64_t>(view.pixels_per_standard_measure);
    key ^= std::bit_cast<uint64_t>(view.waveform_audio_offset_numerator);
    key ^= view.waveform_audio_offset_denominator;
    return wormedit::ui::nuklear::compose_field(paint.context).envelope(slot, 6, rows,
        {center - half_width, paint.viewport.y, half_width * 2, paint.viewport.h}, origin, key,
        {field_color(rms), field_color(peak), joined}, y_scale);
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
static auto finite_envelope_row(const WormUiComposeWaveformRowView& row) -> bool {
    return std::isfinite(row.y_pixels) && std::isfinite(row.rms[0]) && std::isfinite(row.rms[1]) &&
           std::isfinite(row.absolute_peak[0]) && std::isfinite(row.absolute_peak[1]);
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
    const auto rect = compose_screen_rect(&selected_lane->bounds, &view, paint.viewport);
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

static void draw_compose_beat_fit_region(const ComposePaintContext& paint,
                                         const ComposeTimingLaneLayout& layout) {
    const auto& view = *paint.view;
    if (!view.beat_fit_drag_active) {
        return;
    }
    const auto top = std::clamp(compose_projected_y(paint, view.beat_fit_anchor_y_pixels),
                                paint.viewport.y, paint.viewport.y + paint.viewport.h);
    const auto bottom = std::clamp(compose_projected_y(paint, view.beat_fit_beat_y_pixels),
                                   paint.viewport.y, paint.viewport.y + paint.viewport.h);
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

static void draw_compose_waveform_layer(const ComposePaintContext& paint,
                                        const ComposeTimingLaneLayout& layout) {
    draw_compose_waveform(paint);
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
    const auto* hovered = compose_hovered_timing_marker(paint);
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

static void draw_compose_timing_layer(const ComposePaintContext& paint,
                                      const ComposeTimingLaneLayout& layout) {
    draw_compose_proposed_grid(paint);
    draw_compose_beat_fit_overlay(paint);
    draw_compose_offset_fit_overlay(paint);
    draw_compose_timing_preview_legend(paint);
    draw_compose_unbound_lane_keys_hint(paint);
    draw_compose_timing_markers(paint, layout);
    draw_compose_timing_preview_markers(paint, layout);
    draw_compose_timing_stage_marker(paint, layout);
    draw_compose_beat_sections(paint);
    draw_compose_beat_bookmarks(paint);
    draw_compose_audio_sections(paint);
    draw_compose_audio_bookmarks(paint);
}

static void draw_compose_hold_bodies(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->hold_body_count; ++index) {
        const auto& hold = paint.view->hold_bodies[index];
        const auto rect = compose_screen_rect(&hold.bounds, paint.view, paint.viewport);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        if (hold.proposal_ghost) {
            field_rect(paint, rect, 0.0f, nk_rgba(240, 171, 71, 74), 2);
            field_stroke_rect(paint, rect, 0.0f, 1.4f, nk_rgb(255, 224, 156), 2);
            continue;
        }
        const auto color = hold.proposal_origin
                               ? nk_rgb(51, 82, 100)
                               : (hold.semantic_color_visible
                                      ? nk_rgba(hold.semantic_red, hold.semantic_green,
                                                hold.semantic_blue, hold.semantic_alpha)
                                      : (hold.selected ? nk_rgb(62, 170, 204)
                                                       : nk_rgb(48, 111, 151)));
        field_rect(paint, rect, 0.0f, color, 2);
        if (hold.selected) {
            field_stroke_rect(paint, rect, 0.0f, 1.6f, nk_rgb(220, 247, 255), 2);
        }
        if (paint.view->hover_note_visible && hold.note_id == paint.view->hover_note_id) {
            field_stroke_rect(paint, rect, 0.0f, 2.0f, nk_rgb(255, 235, 148), 2);
        }
    }
}

struct ComposeNoteStyle {
    struct nk_color fill;
    struct nk_color border;
    float border_width;
};

static ComposeNoteStyle compose_note_style(const WormUiComposeView& view,
                                           const WormUiComposeNoteView& note) {
    auto fill = note.proposal_origin
                    ? nk_rgb(73, 101, 120)
                    : (note.semantic_color_visible
                           ? nk_rgba(note.semantic_red, note.semantic_green, note.semantic_blue,
                                     note.semantic_alpha)
                           : (note.selected ? nk_rgb(111, 221, 236) : nk_rgb(133, 182, 222)));
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
    } else if (note.selected && note.semantic_color_visible) {
        border = nk_rgb(220, 247, 255);
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
    return {fill, border, border_width};
}

static void draw_compose_arrow_outline(const ComposePaintContext& paint,
                                        const WormUiComposeNoteView& note,
                                        const std::array<SDL_FPoint, 7>& points) {
    if (note.semantic_color_visible) {
        wormedit::ui::nuklear::compose_field(paint.context).stroke_polygon(
            2, points, 4.0f,
            {note.semantic_red, note.semantic_green, note.semantic_blue, note.semantic_alpha}, 1);
    }
    const auto style = compose_note_style(*paint.view, note);
    if (note.selected || note.proposal_ghost || note.paint_preview ||
        note.paint_erase_preview || note.paint_invalid_preview ||
        (paint.view->hover_note_visible && note.note_id == paint.view->hover_note_id)) {
        const auto border = note.selected ? nk_rgb(220, 247, 255) : style.border;
        wormedit::ui::nuklear::compose_field(paint.context).stroke_polygon(
            2, points, 2.0f, field_color(border), 1);
    }
}

static bool draw_compose_note_glyph(const ComposePaintContext& paint,
                                    const WormUiComposeNoteView& note, struct nk_rect tile) {
    if (note.glyph < 2 || note.glyph > 5 || tile.w < 10.0f) return false;
    // Artwork alone becomes square; retained scene bounds still govern hit testing.
    const auto authored = wormedit::ui::arrow_polygon(
        note.glyph, tile.x, tile.y + (tile.h - tile.w) * .5f, tile.w);
    std::array<SDL_FPoint, 7> points{};
    for (std::size_t index = 0; index < authored.size(); ++index)
        points[index] = {std::trunc(authored[index].x), std::trunc(authored[index].y)};
    const auto color = wormedit::ui::arrow_snap_color(note.snap_class);
    auto& field = wormedit::ui::nuklear::compose_field(paint.context);
    field.polygon(2, points, {color.red, color.green, color.blue}, 1);
    field.stroke_polygon(2, points, 1, {17, 24, 34}, 1);
    draw_compose_arrow_outline(paint, note, points);
    return true;
}

static void draw_compose_note_sound_label(const ComposePaintContext& paint,
                                          const WormUiComposeNoteView& note,
                                          struct nk_rect tile) {
    if (note.glyph > 1 || note.tail || !note.sound_label || !note.sound_label[0]) return;
    const auto* font = paint.context->style.font;
    const auto length = static_cast<int>(strlen(note.sound_label));
    const auto text_width = font->width(font->userdata, font->height, note.sound_label, length);
    if (tile.h < font->height || tile.w < text_width + 4.0f) return;

    const auto text_bounds = nk_rect(tile.x + (tile.w - text_width) * 0.5f,
                                     tile.y + (tile.h - font->height) * 0.5f,
                                     text_width, font->height);
    field_text(paint, text_bounds, note.sound_label, length, font,
                 nk_rgba(13, 21, 31, 220), nk_rgb(248, 251, 255), 2);
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
    field_stroke_rect(paint, tile, 0.0f, style.border_width, style.border, 2);
    field_line(paint, tile.x + 1.0f, tile.y + 1.0f,
                   tile.x + tile.w - 1.0f, tile.y + 1.0f, 1.0f,
                   nk_rgba(255, 255, 255, 105), 2);
    draw_compose_note_sound_label(paint, note, tile);
}

static void draw_compose_notes(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->note_endpoint_count; ++index) {
        draw_compose_note(paint, paint.view->note_endpoints[index]);
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

static void draw_compose_bgm_event(const ComposePaintContext& paint,
                                  const WormUiComposeBgmEventView& event,
                                  ComposeHoveredBgm& hovered) {
    const auto rect = compose_screen_rect(&event.bounds, paint.view, paint.viewport);
    if (rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const auto fill = event.has_sound ? nk_rgb(177, 116, 207) : nk_rgb(102, 88, 113);
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
    if (event.sound_detail && nk_input_is_mouse_hovering_rect(&paint.context->input, rect)) {
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

static void draw_compose_bgm_lane_labels(const ComposePaintContext& paint) {
    for (size_t index = 0; index < paint.view->bgm_lane_count; ++index) {
        const auto& lane = paint.view->bgm_lanes[index];
        const auto rect = compose_screen_rect(&lane.bounds, paint.view, paint.viewport);
        if (rect.w < 18.0f || rect.h <= 0.0f) {
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
}

static ComposeHoveredBgm draw_compose_audio_markers(const ComposePaintContext& paint) {
    ComposeHoveredBgm hovered{};
    draw_compose_bgm_events(paint, hovered);
    draw_compose_bmson_marker_drafts(paint);
    draw_compose_bmson_move_preview(paint);
    draw_compose_bgm_lane_labels(paint);
    return hovered;
}

static void draw_compose_area_band(const ComposePaintContext& paint) {
    const auto& view = *paint.view;
    if (!view.area_range_available || view.area_rows_per_measure == 0) return;
    const auto rect = compose_area_band_rect(view, paint.viewport);
    field_rect(paint, rect, 0, nk_rgba(75, 149, 215, 48), 5);
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
    const char* resource_detail = nullptr;
    for (size_t index = 0; index < view.note_endpoint_count; ++index) {
        const auto& note = view.note_endpoints[index];
        if (note.note_id == view.hover_note_id && note.sound_detail && note.sound_detail[0]) {
            resource_detail = note.sound_detail;
            break;
        }
    }
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

static void draw_compose_editor_overlays(const ComposePaintContext& paint,
                                         const ComposeHoveredBgm& hovered) {
    const auto& view = *paint.view;
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

auto compose_editor_status_hint(const WormUiComposeView* view,
                                       const WormUiTimingView* timing_view) -> const char* {
    if (state.timing_stage_drag_active)
        return "Move timing event · release to keep staged position · Apply to commit";
    if (state.beat_fit_drag_active)
        return "Fit beat to waveform · Ctrl snaps BPM · release to commit · Esc cancels";
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
        return "Layer paint armed · click/drag toggles layer · Esc cancels";
    if (view->semantic_layer_solo_active)
        return "Layer solo active · only selected-layer notes shown";
    if (view->note_paint_mode_active)
        return "Paint armed · click/drag draws or erases · Esc cancels";
    if (view->waveform_overview_row_count != 0 && !view->waveform_timing_locked)
        return "Drag beat squares in rail to fit BPM · Shift+drag to shift offset · drag beat zero in song overview";
    return "↑/↓ move · drag selects/moves · Alt-drag hold · drag BMSON BGM to move";
}

void draw_compose_scene_canvas(nk_context* context, const WormUiComposeView* view,
                                      nk_command_buffer* canvas, struct nk_rect viewport) {
    wormedit::ui::nuklear::begin_compose_field(context,
                                              {viewport.x, viewport.y, viewport.w, viewport.h});
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
        timing_layout.field_left = field_left;
        draw_compose_scene_background(paint, timing_layout);
        draw_compose_waveform_layer(paint, timing_layout);
        draw_compose_grid_lines(paint);
        draw_compose_area_band(paint);
        draw_compose_timing_layer(paint, timing_layout);
        draw_compose_hold_bodies(paint);
        const auto hovered_bgm = draw_compose_audio_markers(paint);
        draw_compose_notes(paint);
        draw_compose_foreground_markers(paint);
        draw_compose_editor_overlays(paint, hovered_bgm);
    }
    wormedit::ui::nuklear::insert_compose_field(context, canvas, viewport);
    nk_push_scissor(canvas, parent_clip);
}
