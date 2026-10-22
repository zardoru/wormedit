/*
 * Project-owned copy of Nuklear SDL3 demo backend.
 * Upstream: Immediate-Mode-UI/Nuklear demo/sdl3_renderer/nuklear_sdl3_renderer.h
 *
 * ALTERNATIVE A - MIT License
 * Copyright (c) 2017 Micha Mettke
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define NK_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
// Nuklear retains assertion-only bookkeeping in release builds.
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include "ui/nuklear/sdl_backend.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <wormedit/core/keyed_cache.h>
#include "ui/frontend.h"
#include "ui/nuklear/render/image_quad.h"
#include "ui/nuklear/compose_canvas.h"
#include <limits>
#include <cmath>
#include <utility>
#include <algorithm>
#include <array>
#include <vector>
#include <chrono>
#include <span>
#include <new>
#include <stdexcept>

auto wormedit::ui::nuklear::set_window_render_coordinates(SDL_Renderer* renderer,
    int width, int height) -> bool {
    int pixels_w{}, pixels_h{};
    if (width <= 0 || height <= 0) return false;
    if (SDL_GetRenderOutputSize(renderer, &pixels_w, &pixels_h) &&
        pixels_w > 0 && pixels_h > 0 &&
        SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) &&
        SDL_SetRenderScale(renderer, static_cast<float>(pixels_w) / width,
                           static_cast<float>(pixels_h) / height)) return true;
    // A rejected scale must not leave the previous frame's scale combined with
    // the fallback presentation. SDL applies both transformations independently.
    return SDL_SetRenderScale(renderer, 1, 1) &&
        SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_STRETCH);
}

class RenderSubphaseScope {
  public:
    explicit RenderSubphaseScope(WormUiFrameRenderPhase phase)
        : previous_(worm_ui_frame_render_phase_begin(phase)) {}
    ~RenderSubphaseScope() {
        worm_ui_frame_render_phase_end(previous_);
    }
    RenderSubphaseScope(const RenderSubphaseScope&) = delete;
    auto operator=(const RenderSubphaseScope&) -> RenderSubphaseScope& = delete;

  private:
    uint32_t previous_;
};

// Keep counters at the adapter boundary: converted buffers are shared by every
// draw command, so their vertices and indices are counted once per pass. The
// application and the diagnostic windows each own a set, so neither's
// clip-rect and texture-switch tracking sees the other's draws.
struct GpuSubmissionCounters {
    wormedit::ui::frame_gpu_work_view_t work{};
    SDL_Rect current_clip{};
    SDL_Rect previous_clip{};
    SDL_Texture* previous_texture{};
    bool current_clip_enabled{};
    bool previous_clip_enabled{};
    bool submitted{};
};
static GpuSubmissionCounters g_application_submission;
static GpuSubmissionCounters g_diagnostic_submission;
static GpuSubmissionCounters* g_gpu_submission = &g_application_submission;
// Command-buffer user data of windows begun inside diagnostic_window_scope_t.
static char g_diagnostic_window_tag;

static nk_flags convert_nuklear_commands(nk_context* context, nk_buffer* commands,
                                         nk_buffer* vertices, nk_buffer* indices,
                                         const nk_convert_config* config) {
    const RenderSubphaseScope convert_phase{WORM_UI_FRAME_RENDER_CONVERT};
    const auto result = nk_convert(context, commands, vertices, indices, config);
    g_gpu_submission->work.vertices +=
        config->vertex_size == 0 ? 0 : vertices->allocated / config->vertex_size;
    g_gpu_submission->work.indices += indices->allocated / sizeof(nk_draw_index);
    return result;
}

// Nuklear draws window headers and backgrounds under its "no clip" rect,
// {-8192, -8192, 16384, 16384}. The SDL GPU renderer forwards clip rects
// unclamped, and Metal's unsigned MTLScissorRect turns the negative origin
// into a huge offset that discards those draws. Keep scissors on the viewport.
static auto clamp_clip_to_viewport(SDL_Renderer* renderer, SDL_Rect rect) -> SDL_Rect {
    SDL_Rect viewport{};
    if (!SDL_GetRenderViewport(renderer, &viewport)) return rect;
    const SDL_Rect bounds{0, 0, viewport.w, viewport.h};
    SDL_Rect clamped{};
    return SDL_GetRectIntersection(&rect, &bounds, &clamped) ? clamped : SDL_Rect{};
}

static bool set_nuklear_clip_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
    // SDL converts this point-space rect to pixels using the current render
    // scale. Scaling it here too would clip fractional/high-DPI draws twice.
    const auto clamped = rect ? clamp_clip_to_viewport(renderer, *rect) : SDL_Rect{};
    const auto result = SDL_SetRenderClipRect(renderer, rect ? &clamped : nullptr);
    if (result) {
        g_gpu_submission->current_clip_enabled = rect != nullptr;
        g_gpu_submission->current_clip = clamped;
    }
    return result;
}

static bool same_nuklear_clip() {
    const auto& current = g_gpu_submission->current_clip;
    const auto& previous = g_gpu_submission->previous_clip;
    return g_gpu_submission->current_clip_enabled == g_gpu_submission->previous_clip_enabled &&
           current.x == previous.x && current.y == previous.y && current.w == previous.w &&
           current.h == previous.h;
}

// The bundled SDL adapter hardcodes 16-bit indices. Keep its conversion/render
// implementation, but submit the configured Nuklear index width to SDL.
static_assert(sizeof(nk_draw_index) == 4);
static bool render_nuklear_geometry_raw(SDL_Renderer* renderer, SDL_Texture* texture,
                                        const float* xy, int xy_stride, const SDL_FColor* colors,
                                        int color_stride, const float* uv, int uv_stride,
                                        int vertex_count, const void* indices, int index_count,
                                        int) {
    ++g_gpu_submission->work.draw_calls;
    if (g_gpu_submission->submitted) {
        g_gpu_submission->work.clip_rect_changes += !same_nuklear_clip();
        g_gpu_submission->work.texture_switches += texture != g_gpu_submission->previous_texture;
    }
    g_gpu_submission->previous_clip = g_gpu_submission->current_clip;
    g_gpu_submission->previous_clip_enabled = g_gpu_submission->current_clip_enabled;
    g_gpu_submission->previous_texture = texture;
    g_gpu_submission->submitted = true;
    return SDL_RenderGeometryRaw(renderer, texture, xy, xy_stride, colors, color_stride, uv,
                                 uv_stride, vertex_count, indices, index_count,
                                 sizeof(nk_draw_index));
}
#define nk_convert convert_nuklear_commands
#define SDL_SetRenderClipRect set_nuklear_clip_rect
#define SDL_RenderGeometryRaw render_nuklear_geometry_raw

struct nk_sdl_device {
    struct nk_buffer cmds;
    struct nk_buffer vertices;
    struct nk_buffer indices;
    struct nk_draw_null_texture tex_null;
    SDL_Texture* font_tex;
};

struct nk_sdl_vertex {
    float position[2];
    float uv[2];
    float col[4];
};

struct OverviewKey {
    std::uint64_t generation{}, renderer_generation{};
    std::array<float, 10> geometry{};
    std::array<unsigned char, 8> colors{};
    int output_width{}, output_height{}, logical_width{}, logical_height{};
    SDL_RendererLogicalPresentation logical_mode{SDL_LOGICAL_PRESENTATION_DISABLED};
    float nominal_scale_x{1}, nominal_scale_y{1};
    auto operator==(const OverviewKey&) const -> bool = default;
};
struct OverviewCache {
    struct Data {
        SDL_Texture* texture{};
        SDL_Rect interior{}, clip{};
        float scale_x{1}, scale_y{1};
        std::vector<nk_sdl_vertex> vertices;
        std::vector<nk_draw_index> outside_indices;
        std::size_t first_index{};
        // Capture completes in a later renderer callback; binding alone is not readiness.
        bool pending{}, is_ready{};
    };
    wormedit::keyed_cache_t<OverviewKey, Data> entry;
    SDL_Surface* (*readback)(SDL_Renderer*, const SDL_Rect*) = SDL_RenderReadPixels;
    int begin_marker{}, end_marker{}, warm_marker{};
    std::uint64_t renderer_generation{};
    wormedit::ui::nuklear::overview_work_t work{};
    void invalidate() {
        SDL_DestroyTexture(entry.value().texture);
        entry.value().texture = nullptr;
        entry.value().is_ready = entry.value().pending = false;
        entry.invalidate();
        std::vector<nk_sdl_vertex>{}.swap(entry.value().vertices);
        std::vector<nk_draw_index>{}.swap(entry.value().outside_indices);
        work.texture_bytes = work.geometry_bytes = 0;
    }
    ~OverviewCache() { SDL_DestroyTexture(entry.value().texture); }
};

struct ComposeFieldMarker {
    struct ComposeFieldFrame* owner{};
    std::size_t first_index{}, index_count{};
};
struct ComposeFieldFrame {
    std::unique_ptr<wormedit::ui::nuklear::compose_canvas_cache_t> canvas;
    wormedit::ui::field::renderer_t geometry;
    std::array<ComposeFieldMarker, 32> markers{};
    std::size_t marker_count{}, next_index{};
};
constexpr std::size_t maximum_compose_field_frames = 8;

struct nk_sdl {
    ComposeFieldFrame* field;
    ComposeFieldFrame* field_frames[maximum_compose_field_frames];
    // Zeroed with the rest of the struct by nk_sdl_init*, which allocate it raw.
    std::size_t field_frame_count, next_field_frame;
    OverviewCache* overview;
    SDL_Window* win;
    SDL_Renderer* renderer;
    struct nk_user_font* debug_font;
    struct nk_sdl_device ogl;
    struct nk_context ctx;
#ifdef NK_INCLUDE_FONT_BAKING
    struct nk_font_atlas atlas;
#endif
    struct nk_allocator allocator;
    nk_handle userdata;
    Uint64 last_render;
    bool insert_toggle;
    bool edit_was_active;
    bool headless;
};

NK_API nk_handle nk_sdl_get_userdata(struct nk_context* ctx) {
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);
    return sdl->userdata;
}

NK_API void nk_sdl_set_userdata(struct nk_context* ctx, nk_handle userdata) {
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);
    sdl->userdata = userdata;
}

NK_INTERN void* nk_sdl_alloc(nk_handle user, void* old, nk_size size) {
    NK_UNUSED(user);
    /* FIXME: nk_sdl_alloc should use SDL_realloc here, not SDL_malloc
     * but this could cause a double-free due to bug within Nuklear, see:
     * https://github.com/Immediate-Mode-UI/Nuklear/issues/768
     * */
#if 0
    return SDL_realloc(old, size);
#else
    NK_UNUSED(old);
    return SDL_malloc(size);
#endif
}

NK_INTERN void nk_sdl_free(nk_handle user, void* old) {
    NK_UNUSED(user);
    SDL_free(old);
}

NK_API struct nk_allocator nk_sdl_allocator() {
    struct nk_allocator allocator;
    allocator.userdata.ptr = 0;
    allocator.alloc = nk_sdl_alloc;
    allocator.free = nk_sdl_free;
    return allocator;
}

NK_INTERN void nk_sdl_device_upload_atlas(struct nk_context* ctx, const void* image, int width,
                                          int height) {
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    NK_ASSERT(image);
    NK_ASSERT(width > 0);
    NK_ASSERT(height > 0);

    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    /* Clean up if the texture already exists. */
    if (sdl->ogl.font_tex != NULL) {
        SDL_DestroyTexture(sdl->ogl.font_tex);
        sdl->ogl.font_tex = NULL;
    }

    sdl->ogl.font_tex = SDL_CreateTexture(sdl->renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STATIC, width, height);
    NK_ASSERT(sdl->ogl.font_tex);
    SDL_UpdateTexture(sdl->ogl.font_tex, NULL, image, 4 * width);
    SDL_SetTextureBlendMode(sdl->ogl.font_tex, SDL_BLENDMODE_BLEND);
}

NK_API void nk_sdl_update_TextInput(struct nk_context* ctx) {
    struct nk_sdl* sdl;
    bool active;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    /* Determine if Nuklear is using any top-level "edit" widget.
     * Popups take higher priority because they block any incomming input.
     * This will not work, if the widget is not updating context state properly. */
    if (!ctx->active)
        active = false;
    else if (ctx->active->popup.win)
        active = ctx->active->popup.win->edit.active;
    else
        active = ctx->active->edit.active;

    /* decide, if TextInputActive should be unchanged/stoped/started
     * and change its state accordingly for owned SDL Window */
    if (active != sdl->edit_was_active) {
        const bool window_edit_active = SDL_TextInputActive(sdl->win);

        /* If you ever hit this check, it means that the demo and your app
         * (or something else) are all trying to manage TextInputActive state.
         * This can cause subtle bugs where the state won't be what you expect.
         * You can safely remove this assert and the demo will keep working,
         * but make sure it does not cause any issues for you */
        NK_ASSERT(window_edit_active == sdl->edit_was_active &&
                  "something else changed TextInputActive state for this Window");

        if (!window_edit_active && !sdl->edit_was_active && active)
            SDL_StartTextInput(sdl->win);
        else if (window_edit_active && sdl->edit_was_active && !active)
            SDL_StopTextInput(sdl->win);
        sdl->edit_was_active = active;
    }

    /* FIXME:
     * for full SDL3 integration, you also need to find current edit widget
     * bounds and the text cursor offset, and pass this data into SDL_SetTextInputArea.
     * This is currently not possible to do safely as Nuklear does not support it.
     * https://wiki.libsdl.org/SDL3/SDL_SetTextInputArea
     * https://github.com/Immediate-Mode-UI/Nuklear/pull/857
     */
}

static void submit_sdl_batch(nk_sdl* sdl, const wormedit::ui::nuklear::sdl_draw_batch_t& batch) {
    SDL_SetRenderClipRect(sdl->renderer, &batch.clip);
    const auto* vertices = static_cast<const nk_byte*>(nk_buffer_memory_const(&sdl->ogl.vertices));
    const auto* indices =
        static_cast<const nk_draw_index*>(nk_buffer_memory_const(&sdl->ogl.indices));
    constexpr auto stride = sizeof(nk_sdl_vertex);
    SDL_RenderGeometryRaw(
        sdl->renderer, batch.texture,
        reinterpret_cast<const float*>(vertices + NK_OFFSETOF(nk_sdl_vertex, position)), stride,
        reinterpret_cast<const SDL_FColor*>(vertices + NK_OFFSETOF(nk_sdl_vertex, col)), stride,
        reinterpret_cast<const float*>(vertices + NK_OFFSETOF(nk_sdl_vertex, uv)), stride,
        sdl->ogl.vertices.needed / stride, indices + batch.index_offset, batch.index_count,
        sizeof(nk_draw_index));
}

static const ComposeFieldMarker* field_marker(const nk_sdl* sdl, const nk_draw_command* command) {
    for (std::size_t frame = 0; frame < sdl->field_frame_count; ++frame) {
        const auto* field = sdl->field_frames[frame];
        if (!field) continue;
        for (std::size_t index = 0; index < field->marker_count; ++index)
            if (command->userdata.ptr == &field->markers[index])
                return &field->markers[index];
    }
    return nullptr;
}
static void submit_field_marker(nk_sdl* sdl, const ComposeFieldMarker& marker) {
    const wormedit::ui::field::submission_t hooks{set_nuklear_clip_rect,
                                                render_nuklear_geometry_raw};
    marker.owner->geometry.submit_range(sdl->renderer, marker.first_index, marker.index_count, hooks);
}
static auto overview_intersection(SDL_Rect first, SDL_Rect second) -> SDL_Rect {
    SDL_Rect result{};
    SDL_GetRectIntersection(&first, &second, &result);
    return result;
}
static void compact_overview_edges(OverviewCache& cache) {
    std::vector<nk_sdl_vertex> compact;
    std::vector<nk_draw_index> mapping(cache.entry.value().vertices.size(), std::numeric_limits<nk_draw_index>::max());
    compact.reserve(cache.entry.value().outside_indices.size());
    for (auto& index : cache.entry.value().outside_indices) {
        auto& translated = mapping[index];
        if (translated == std::numeric_limits<nk_draw_index>::max()) {
            translated = static_cast<nk_draw_index>(compact.size());
            compact.push_back(cache.entry.value().vertices[index]);
        }
        index = translated;
    }
    cache.work.transient_bytes = std::max(cache.work.transient_bytes,
        cache.entry.value().vertices.capacity() * sizeof(nk_sdl_vertex) + compact.capacity() * sizeof(nk_sdl_vertex) +
        mapping.capacity() * sizeof(nk_draw_index) + cache.entry.value().outside_indices.capacity() * sizeof(nk_draw_index));
    cache.entry.value().vertices = std::move(compact);
    cache.entry.value().vertices.shrink_to_fit();
    cache.entry.value().outside_indices.shrink_to_fit();
}

static bool overview_triangle_visible(const OverviewCache& cache,
                                      std::span<const nk_draw_index> triangle,
                                      nk_draw_index minimum) {
    float left = std::numeric_limits<float>::max(), top = left;
    float right = std::numeric_limits<float>::lowest(), bottom = right;
    for (const auto index : triangle) {
        const auto& point = cache.entry.value().vertices[index - minimum].position;
        left = std::min(left, point[0]);
        right = std::max(right, point[0]);
        top = std::min(top, point[1]);
        bottom = std::max(bottom, point[1]);
    }
    return left < cache.entry.value().clip.x + cache.entry.value().clip.w && right > cache.entry.value().clip.x &&
        top < cache.entry.value().clip.y + cache.entry.value().clip.h && bottom > cache.entry.value().clip.y;
}

static void retain_overview_edges(nk_sdl* sdl, std::size_t end) {
    auto& cache = *sdl->overview;
    const auto* source = static_cast<const nk_draw_index*>(nk_buffer_memory_const(&sdl->ogl.indices));
    const auto* vertices = static_cast<const nk_sdl_vertex*>(nk_buffer_memory_const(&sdl->ogl.vertices));
    if (end <= cache.entry.value().first_index || end - cache.entry.value().first_index > 16'000) return;
    if (!source || !vertices || end > sdl->ogl.indices.allocated / sizeof(nk_draw_index)) return;
    const auto range = std::span{source + cache.entry.value().first_index, end - cache.entry.value().first_index};
    const auto [minimum, maximum] = std::minmax_element(range.begin(), range.end());
    if (*maximum - *minimum >= 4096 ||
        *maximum >= sdl->ogl.vertices.allocated / sizeof(nk_sdl_vertex)) return;
    cache.entry.value().vertices.assign(vertices + *minimum, vertices + *maximum + 1);
    cache.work.cold_vertices = cache.entry.value().vertices.size();
    for (auto& vertex : cache.entry.value().vertices) {
        vertex.position[0] *= cache.entry.value().scale_x;
        vertex.position[1] *= cache.entry.value().scale_y;
    }
    for (std::size_t index = 0; index + 2 < range.size(); index += 3) {
        bool outside = false;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& vertex = cache.entry.value().vertices[range[index + corner] - *minimum];
            outside |= vertex.position[0] < cache.entry.value().interior.x ||
                vertex.position[0] > cache.entry.value().interior.x + cache.entry.value().interior.w ||
                vertex.position[1] < cache.entry.value().interior.y ||
                vertex.position[1] > cache.entry.value().interior.y + cache.entry.value().interior.h;
        }
        if (outside && overview_triangle_visible(cache, range.subspan(index, 3), *minimum))
            for (std::size_t corner = 0; corner < 3; ++corner)
                cache.entry.value().outside_indices.push_back(range[index + corner] - *minimum);
    }
    compact_overview_edges(cache);
    cache.work.geometry_bytes = cache.entry.value().vertices.capacity() * sizeof(nk_sdl_vertex) +
        cache.entry.value().outside_indices.capacity() * sizeof(nk_draw_index);
}
static bool overview_renderer_matches(nk_sdl* sdl) {
    const auto& key = sdl->overview->entry.key().value();
    int width{}, height{}, logical_width{}, logical_height{};
    float sx{}, sy{};
    SDL_RendererLogicalPresentation mode{};
    SDL_GetRenderOutputSize(sdl->renderer, &width, &height);
    SDL_GetRenderLogicalPresentation(sdl->renderer, &logical_width, &logical_height, &mode);
    SDL_GetRenderScale(sdl->renderer, &sx, &sy);
    return width == key.output_width && height == key.output_height &&
        logical_width == key.logical_width && logical_height == key.logical_height &&
        mode == key.logical_mode && sx == key.nominal_scale_x && sy == key.nominal_scale_y;
}
static void capture_overview_pixels_impl(nk_sdl* sdl, std::size_t end) {
    auto& cache = *sdl->overview;
    if (!cache.entry.value().pending) return;
    cache.entry.value().pending = false;
    if (!overview_renderer_matches(sdl)) { cache.invalidate(); return; }
    retain_overview_edges(sdl, end);
    if (cache.entry.value().vertices.empty()) return;
    const auto begin = std::chrono::steady_clock::now();
    ++cache.work.readbacks;
    auto* surface = cache.readback(sdl->renderer, &cache.entry.value().interior);
    cache.work.cold_readback_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - begin).count();
    if (!surface) { ++cache.work.failures; cache.invalidate(); return; }
    cache.work.transient_bytes = std::max(cache.work.transient_bytes,
        static_cast<std::size_t>(surface->pitch) * surface->h + cache.work.geometry_bytes);
    if (surface->w != cache.entry.value().interior.w || surface->h != cache.entry.value().interior.h ||
        SDL_BYTESPERPIXEL(surface->format) != 4 || surface->pitch < surface->w * 4 ||
        static_cast<std::size_t>(surface->pitch) * surface->h > 4096U * 256U * 4U) {
        SDL_DestroySurface(surface);
        ++cache.work.failures;
        cache.invalidate();
        return;
    }
    cache.entry.value().texture = SDL_CreateTexture(sdl->renderer, surface->format, SDL_TEXTUREACCESS_STATIC,
        surface->w, surface->h);
    const bool success = cache.entry.value().texture && SDL_SetTextureBlendMode(cache.entry.value().texture, SDL_BLENDMODE_NONE) &&
        SDL_SetTextureScaleMode(cache.entry.value().texture, SDL_SCALEMODE_NEAREST) &&
        SDL_UpdateTexture(cache.entry.value().texture, nullptr, surface->pixels, surface->pitch);
    SDL_DestroySurface(surface);
    if (!success) { ++cache.work.failures; cache.invalidate(); return; }
    cache.entry.value().is_ready = true;
    ++cache.work.rebuilds;
    ++cache.work.uploads;
    cache.work.texture_bytes = static_cast<std::size_t>(cache.entry.value().interior.w) * cache.entry.value().interior.h * 4;
    cache.work.uploaded_bytes += cache.work.texture_bytes;
}
static void capture_overview_pixels(nk_sdl* sdl, std::size_t end) {
    try {
        capture_overview_pixels_impl(sdl, end);
    } catch (const std::bad_alloc&) {
        ++sdl->overview->work.failures;
        sdl->overview->invalidate();
    } catch (const std::length_error&) {
        ++sdl->overview->work.failures;
        sdl->overview->invalidate();
    }
}
static void submit_overview_edge_clip(nk_sdl* sdl, SDL_Rect clip) {
    auto& cache = *sdl->overview;
    clip = overview_intersection(clip, cache.entry.value().clip);
    if (clip.w <= 0 || clip.h <= 0 || cache.entry.value().outside_indices.empty()) return;
    set_nuklear_clip_rect(sdl->renderer, &clip);
    g_gpu_submission->work.indices += cache.entry.value().outside_indices.size();
    cache.work.warm_vertices += cache.entry.value().vertices.size();
    cache.work.warm_indices += cache.entry.value().outside_indices.size();
    const auto& first = cache.entry.value().vertices.front();
    render_nuklear_geometry_raw(sdl->renderer, static_cast<SDL_Texture*>(sdl->ogl.tex_null.texture.ptr),
        first.position, sizeof(nk_sdl_vertex), reinterpret_cast<const SDL_FColor*>(first.col),
        sizeof(nk_sdl_vertex), first.uv, sizeof(nk_sdl_vertex), cache.entry.value().vertices.size(),
        cache.entry.value().outside_indices.data(), cache.entry.value().outside_indices.size(), sizeof(nk_draw_index));
}
static void submit_cached_overview(nk_sdl* sdl) {
    auto& cache = *sdl->overview;
    if (!cache.entry.value().is_ready) return;
    cache.work.warm_vertices = 4;
    cache.work.warm_indices = 6;
    const auto calls_before = g_gpu_submission->work.draw_calls;
    SDL_Rect viewport{}, previous_clip{};
    SDL_GetRenderViewport(sdl->renderer, &viewport);
    const bool clipped = SDL_RenderClipEnabled(sdl->renderer);
    SDL_GetRenderClipRect(sdl->renderer, &previous_clip);
    SDL_SetRenderLogicalPresentation(sdl->renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderScale(sdl->renderer, 1, 1);
    SDL_SetRenderViewport(sdl->renderer, nullptr);
    set_nuklear_clip_rect(sdl->renderer, &cache.entry.value().clip);
    const auto& rect = cache.entry.value().interior;
    const SDL_FRect destination{static_cast<float>(rect.x), static_cast<float>(rect.y),
        static_cast<float>(rect.w), static_cast<float>(rect.h)};
    const SDL_Vertex quad[] = {
        {{destination.x, destination.y}, {1, 1, 1, 1}, {0, 0}},
        {{destination.x + destination.w, destination.y}, {1, 1, 1, 1}, {1, 0}},
        {{destination.x + destination.w, destination.y + destination.h}, {1, 1, 1, 1}, {1, 1}},
        {{destination.x, destination.y + destination.h}, {1, 1, 1, 1}, {0, 1}}};
    const nk_draw_index quad_indices[]{0, 1, 2, 0, 2, 3};
    render_nuklear_geometry_raw(sdl->renderer, cache.entry.value().texture, &quad[0].position.x, sizeof(SDL_Vertex),
        &quad[0].color, sizeof(SDL_Vertex), &quad[0].tex_coord.x, sizeof(SDL_Vertex),
        4, quad_indices, 6, sizeof(nk_draw_index));
    g_gpu_submission->work.vertices += 4;
    g_gpu_submission->work.indices += 6;
    const auto& clip = cache.entry.value().clip;
    submit_overview_edge_clip(sdl, {clip.x, clip.y, rect.x - clip.x, clip.h});
    submit_overview_edge_clip(sdl, {rect.x + rect.w, clip.y,
        clip.x + clip.w - rect.x - rect.w, clip.h});
    submit_overview_edge_clip(sdl, {rect.x, clip.y, rect.w, rect.y - clip.y});
    submit_overview_edge_clip(sdl, {rect.x, rect.y + rect.h, rect.w,
        clip.y + clip.h - rect.y - rect.h});
    g_gpu_submission->work.vertices += cache.entry.value().vertices.size();
    SDL_SetRenderLogicalPresentation(sdl->renderer, cache.entry.key().value().logical_width, cache.entry.key().value().logical_height,
        cache.entry.key().value().logical_mode);
    SDL_SetRenderScale(sdl->renderer, cache.entry.key().value().nominal_scale_x, cache.entry.key().value().nominal_scale_y);
    SDL_SetRenderViewport(sdl->renderer, nullptr);
    set_nuklear_clip_rect(sdl->renderer, clipped ? &previous_clip : nullptr);
    ++cache.work.reused_frames;
    cache.work.warm_draw_calls = g_gpu_submission->work.draw_calls - calls_before;
}
static bool submit_overview_marker(nk_sdl* sdl, const nk_draw_command* command,
                                    std::size_t offset) {
    auto& cache = *sdl->overview;
    if (command->userdata.ptr == &cache.begin_marker) { cache.entry.value().first_index = offset; return true; }
    if (command->userdata.ptr == &cache.end_marker) { capture_overview_pixels(sdl, offset); return true; }
    if (command->userdata.ptr == &cache.warm_marker) { submit_cached_overview(sdl); return true; }
    return false;
}

static void submit_sdl_draw_list(nk_context* context, nk_sdl* sdl) {
    // Field markers stay set until the frame ends; only the application pass draws them.
    if (g_gpu_submission == &g_application_submission) {
        for (std::size_t index = 0; index < sdl->field_frame_count; ++index) {
            const auto* field = sdl->field_frames[index];
            if (!field || field->marker_count == 0) continue;
            const auto work = field->geometry.workload();
            g_gpu_submission->work.vertices += work.vertices;
            g_gpu_submission->work.indices += work.indices;
        }
    }
    wormedit::ui::nuklear::sdl_draw_batcher_t batches;
    const nk_draw_command* command;
    std::size_t index_offset = 0;
    nk_draw_foreach(command, context, &sdl->ogl.cmds) {
        if (const auto* marker = field_marker(sdl, command)) {
            if (const auto pending = batches.finish())
                submit_sdl_batch(sdl, *pending);
            submit_field_marker(sdl, *marker);
            continue;
        }
        if (sdl->overview && (command->userdata.ptr == &sdl->overview->begin_marker ||
            command->userdata.ptr == &sdl->overview->end_marker ||
            command->userdata.ptr == &sdl->overview->warm_marker)) {
            if (const auto pending = batches.finish()) submit_sdl_batch(sdl, *pending);
            submit_overview_marker(sdl, command, index_offset);
            continue;
        }
        index_offset += command->elem_count;
        const SDL_Rect clip{
            static_cast<int>(command->clip_rect.x), static_cast<int>(command->clip_rect.y),
            static_cast<int>(command->clip_rect.w), static_cast<int>(command->clip_rect.h)};
        const auto completed = batches.append(static_cast<SDL_Texture*>(command->texture.ptr), clip,
                                              command->elem_count);
        if (completed)
            submit_sdl_batch(sdl, *completed);
    }
    if (const auto last = batches.finish())
        submit_sdl_batch(sdl, *last);
}

// Converts and submits the context's visible windows without ending its frame.
static void render_nuklear_pass(struct nk_context* ctx, struct nk_sdl* sdl, enum nk_anti_aliasing AA) {
    {
        SDL_Rect saved_clip;
        bool clipping_enabled;
        auto& vbuf = sdl->ogl.vertices;
        auto& ebuf = sdl->ogl.indices;

        /* fill converting configuration */
        struct nk_convert_config config;
        static const struct nk_draw_vertex_layout_element vertex_layout[] = {
            {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_sdl_vertex, position)},
            {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_sdl_vertex, uv)},
            {NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT, NK_OFFSETOF(struct nk_sdl_vertex, col)},
            {NK_VERTEX_LAYOUT_END}};
        NK_MEMSET(&config, 0, sizeof(config));
        config.vertex_layout = vertex_layout;
        config.vertex_size = sizeof(struct nk_sdl_vertex);
        config.vertex_alignment = NK_ALIGNOF(struct nk_sdl_vertex);
        config.tex_null = sdl->ogl.tex_null;
        config.circle_segment_count = 22;
        config.curve_segment_count = 22;
        config.arc_segment_count = 22;
        config.global_alpha = 1.0f;
        config.shape_AA = AA;
        config.line_AA = AA;

        /* convert shapes into vertices */
        nk_buffer_clear(&sdl->ogl.cmds);
        nk_buffer_clear(&vbuf);
        nk_buffer_clear(&ebuf);
        nk_convert(ctx, &sdl->ogl.cmds, &vbuf, &ebuf, &config);

        clipping_enabled = SDL_RenderClipEnabled(sdl->renderer);
        SDL_GetRenderClipRect(sdl->renderer, &saved_clip);
        submit_sdl_draw_list(ctx, sdl);

        SDL_SetRenderClipRect(sdl->renderer, &saved_clip);
        if (!clipping_enabled) {
            SDL_SetRenderClipRect(sdl->renderer, NULL);
        }
    }
}

static void finish_nuklear_frame(struct nk_context* ctx, struct nk_sdl* sdl) {
    for (std::size_t index = 0; index < sdl->field_frame_count; ++index)
        sdl->field_frames[index]->marker_count = 0;
    sdl->field = sdl->field_frames[0];
    sdl->next_field_frame = 0;
    nk_clear(ctx);
    nk_buffer_clear(&sdl->ogl.cmds);
}

NK_API void nk_sdl_begin_frame(struct nk_context* ctx) {
    if (!ctx || !ctx->userdata.ptr) return;
    auto* sdl = static_cast<struct nk_sdl*>(ctx->userdata.ptr);
    for (std::size_t index = 0; index < sdl->field_frame_count; ++index)
        sdl->field_frames[index]->marker_count = 0;
    sdl->field = sdl->field_frames[0];
    sdl->next_field_frame = 0;
}

NK_API void nk_sdl_render(struct nk_context* ctx, enum nk_anti_aliasing AA) {
    NK_ASSERT(ctx);
    auto* sdl = static_cast<struct nk_sdl*>(ctx->userdata.ptr);
    NK_ASSERT(sdl);
    render_nuklear_pass(ctx, sdl, AA);
    finish_nuklear_frame(ctx, sdl);
}

namespace {
// Windows hidden for one render pass: Nuklear's command list skips hidden
// windows, so a pass converts only the windows of one counting group.
class hidden_windows_t {
  public:
    static auto diagnostic(const nk_window& window) -> bool {
        return window.buffer.userdata.ptr == &g_diagnostic_window_tag;
    }
    static auto drawn(const nk_context& ctx, const nk_window& window) -> bool {
        return !(window.flags & NK_WINDOW_HIDDEN) && window.seq == ctx.seq;
    }
    static auto count(const nk_context& ctx, bool diagnostic_group) -> std::size_t {
        std::size_t result = 0;
        for (const auto* window = ctx.begin; window; window = window->next)
            result += drawn(ctx, *window) && diagnostic(*window) == diagnostic_group;
        return result;
    }
    static auto fits(std::size_t windows) -> bool { return windows <= capacity; }
    void hide(nk_context& ctx, bool diagnostic_group) {
        for (auto* window = ctx.begin; window && count_ < capacity; window = window->next) {
            if (!drawn(ctx, *window) || diagnostic(*window) != diagnostic_group) continue;
            window->flags |= NK_WINDOW_HIDDEN;
            windows_[count_++] = window;
        }
    }
    void restore() {
        for (std::size_t index = 0; index < count_; ++index)
            windows_[index]->flags &= ~static_cast<nk_flags>(NK_WINDOW_HIDDEN);
        count_ = 0;
    }

  private:
    static constexpr std::size_t capacity = 64;
    std::array<nk_window*, capacity> windows_{};
    std::size_t count_{};
};
} // namespace

// Application windows (and every popup) render first; diagnostic windows
// follow on top in a pass whose GPU work is accumulated separately.
// Returns whether a diagnostic pass ran.
static auto render_application_then_diagnostics(nk_context* ctx, nk_sdl* sdl,
                                                nk_anti_aliasing AA) -> bool {
    const auto diagnostics = hidden_windows_t::count(*ctx, true);
    if (diagnostics == 0 || !hidden_windows_t::fits(diagnostics) ||
        !hidden_windows_t::fits(hidden_windows_t::count(*ctx, false))) {
        render_nuklear_pass(ctx, sdl, AA);
        return false;
    }
    hidden_windows_t hidden;
    hidden.hide(*ctx, true);
    render_nuklear_pass(ctx, sdl, AA);
    hidden.restore();
    hidden.hide(*ctx, false);
    ctx->build = nk_false; // relink the command list without the application windows
    g_gpu_submission = &g_diagnostic_submission;
    render_nuklear_pass(ctx, sdl, AA);
    g_gpu_submission = &g_application_submission;
    hidden.restore();
    return true;
}

NK_INTERN void nk_sdl_clipboard_paste(nk_handle usr, struct nk_text_edit* edit) {
    char* text;
    int len;
    NK_UNUSED(usr);

    /* this function returns empty string on failure, not NULL */
    text = SDL_GetClipboardText();
    NK_ASSERT(text);

    if (text[0] != '\0') {
        /* FIXME: there is a bug in Nuklear that affects UTF8 clipboard handling
         * "len" should be a buffer length, but due to bug it must be a glyph count
         * see: https://github.com/Immediate-Mode-UI/Nuklear/pull/841 */
#if 0
        len = nk_strlen(text);
#else
        len = SDL_utf8strlen(text);
#endif
        nk_textedit_paste(edit, text, len);
    }
    SDL_free(text);
}

NK_INTERN void nk_sdl_clipboard_copy(nk_handle usr, const char* text, int len) {
    const char* ptext;
    char* str;
    size_t buflen;
    int i;
    struct nk_sdl* sdl = (struct nk_sdl*)usr.ptr;
    NK_ASSERT(sdl);
    if (len <= 0 || text == NULL)
        return;

    /* FIXME: there is a bug in Nuklear that affects UTF8 clipboard handling
     * "len" is expected to be a buffer length, but due to bug it actually is a glyph count
     * see: https://github.com/Immediate-Mode-UI/Nuklear/pull/841 */
#if 0
    buflen = len + 1;
    NK_UNUSED(ptext);
#else
    ptext = text;
    for (i = len; i > 0; i--)
        (void)SDL_StepUTF8(&ptext, NULL);
    buflen = (size_t)(ptext - text) + 1;
#endif

    str = (char*)sdl->allocator.alloc(sdl->allocator.userdata, 0, buflen);
    if (!str)
        return;
    SDL_strlcpy(str, text, buflen);
    SDL_SetClipboardText(str);
    sdl->allocator.free(sdl->allocator.userdata, str);
}

NK_API struct nk_context* nk_sdl_init_headless(struct nk_allocator allocator,
                                               struct nk_user_font* font) {
    NK_ASSERT(allocator.alloc);
    NK_ASSERT(allocator.free);
    NK_ASSERT(font);
    struct nk_sdl* sdl = (struct nk_sdl*)allocator.alloc(allocator.userdata, 0, sizeof(*sdl));
    if (!sdl) return nullptr;
    SDL_zerop(sdl);
    sdl->field = new (std::nothrow) ComposeFieldFrame;
    sdl->overview = new (std::nothrow) OverviewCache;
    if (sdl->field) {
        sdl->field_frames[0] = sdl->field;
        sdl->field_frame_count = 1;
    }
    if (!sdl->field || !sdl->overview) {
        delete sdl->overview;
        delete sdl->field;
        allocator.free(allocator.userdata, sdl);
        return nullptr;
    }
    sdl->allocator = allocator;
    sdl->headless = true;
    if (!nk_init(&sdl->ctx, &sdl->allocator, 0)) {
        delete sdl->field;
        delete sdl->overview;
        allocator.free(allocator.userdata, sdl);
        return nullptr;
    }
    sdl->ctx.userdata = nk_handle_ptr(sdl);
    sdl->ctx.clip.copy = nk_sdl_clipboard_copy;
    sdl->ctx.clip.paste = nk_sdl_clipboard_paste;
    sdl->ctx.clip.userdata = nk_handle_ptr(sdl);
    nk_buffer_init(&sdl->ogl.cmds, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_buffer_init(&sdl->ogl.vertices, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_buffer_init(&sdl->ogl.indices, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_style_set_font(&sdl->ctx, font);
    return &sdl->ctx;
}

NK_API struct nk_context* nk_sdl_init(SDL_Window* win, SDL_Renderer* renderer,
                                      struct nk_allocator allocator) {
    struct nk_sdl* sdl;
    NK_ASSERT(win);
    NK_ASSERT(renderer);
    NK_ASSERT(allocator.alloc);
    NK_ASSERT(allocator.free);
    sdl = (struct nk_sdl*)allocator.alloc(allocator.userdata, 0, sizeof(*sdl));
    NK_ASSERT(sdl);
    SDL_zerop(sdl);
    sdl->field = new ComposeFieldFrame;
    sdl->field_frames[0] = sdl->field;
    sdl->field_frame_count = 1;
    sdl->overview = new (std::nothrow) OverviewCache;
    sdl->allocator.userdata = allocator.userdata;
    sdl->allocator.alloc = allocator.alloc;
    sdl->allocator.free = allocator.free;
    sdl->win = win;
    sdl->renderer = renderer;
    nk_init(&sdl->ctx, &sdl->allocator, 0);
    sdl->ctx.userdata = nk_handle_ptr((void*)sdl);
    sdl->ctx.clip.copy = nk_sdl_clipboard_copy;
    sdl->ctx.clip.paste = nk_sdl_clipboard_paste;
    sdl->ctx.clip.userdata = nk_handle_ptr((void*)sdl);
    nk_buffer_init(&sdl->ogl.cmds, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_buffer_init(&sdl->ogl.vertices, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    nk_buffer_init(&sdl->ogl.indices, &sdl->allocator, NK_BUFFER_DEFAULT_INITIAL_SIZE);
    sdl->edit_was_active = false;
    sdl->insert_toggle = false;
    return &sdl->ctx;
}

#ifdef NK_INCLUDE_FONT_BAKING
NK_API struct nk_font_atlas* nk_sdl_font_stash_begin(struct nk_context* ctx) {
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);
    if (sdl->atlas.font_num > 0) nk_font_atlas_clear(&sdl->atlas);
    nk_font_atlas_init(&sdl->atlas, &sdl->allocator);
    nk_font_atlas_begin(&sdl->atlas);
    return &sdl->atlas;
}
#endif

#ifdef NK_INCLUDE_FONT_BAKING
NK_API void nk_sdl_font_stash_end(struct nk_context* ctx) {
    struct nk_sdl* sdl;
    const void* image;
    int w, h;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);
    image = nk_font_atlas_bake(&sdl->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    NK_ASSERT(image);
    nk_sdl_device_upload_atlas(&sdl->ctx, image, w, h);
    nk_font_atlas_end(&sdl->atlas, nk_handle_ptr(sdl->ogl.font_tex), &sdl->ogl.tex_null);
    if (sdl->atlas.default_font) {
        nk_style_set_font(&sdl->ctx, &sdl->atlas.default_font->handle);
    }
}
#endif

static int handle_sdl_mouse_button(nk_context* ctx, const SDL_Event& event) {
    const int x = event.button.x, y = event.button.y;
    const int down = event.button.down;
    switch (event.button.button) {
    case SDL_BUTTON_LEFT:
        if (event.button.clicks > 1)
            nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, down);
        nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
        break;
    case SDL_BUTTON_MIDDLE:
        nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down);
        break;
    case SDL_BUTTON_RIGHT:
        nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down);
        break;
    case SDL_BUTTON_X1:
        nk_input_button(ctx, NK_BUTTON_X1, x, y, down);
        break;
    case SDL_BUTTON_X2:
        nk_input_button(ctx, NK_BUTTON_X2, x, y, down);
        break;
    default:
        return 0;
    }
    return 1;
}

// Nuklear counts every input call as an edge. Inactive releases must not become
// phantom presses when a normal letter's down/up events share a frame. Preserve
// repeated down events so held editing keys retain their normal repeat behavior.
static void input_sdl_key(nk_context* context, nk_keys key, bool down) {
    if (down || context->input.keyboard.keys[key].down) nk_input_key(context, key, down);
}

static int handle_sdl_key(nk_context* ctx, nk_sdl* sdl, const SDL_Event& event) {
    int down = event.type == SDL_EVENT_KEY_DOWN;
    int ctrl_down = event.key.mod & SDL_KMOD_CTRL;

    switch (event.key.key) {
    case SDLK_LALT:
    case SDLK_RALT:
        input_sdl_key(ctx, NK_KEY_ALT, down);
        break;
    case SDLK_RSHIFT: /* RSHIFT & LSHIFT share same routine */
    case SDLK_LSHIFT:
        input_sdl_key(ctx, NK_KEY_SHIFT, down);
        break;
    case SDLK_DELETE:
        input_sdl_key(ctx, NK_KEY_DEL, down);
        break;
    case SDLK_RETURN:
        input_sdl_key(ctx, NK_KEY_ENTER, down);
        break;
    case SDLK_TAB:
        input_sdl_key(ctx, NK_KEY_TAB, down);
        break;
    case SDLK_BACKSPACE:
        input_sdl_key(ctx, NK_KEY_BACKSPACE, down);
        break;
    case SDLK_HOME:
        input_sdl_key(ctx, NK_KEY_TEXT_START, down);
        input_sdl_key(ctx, NK_KEY_SCROLL_START, down);
        break;
    case SDLK_END:
        input_sdl_key(ctx, NK_KEY_TEXT_END, down);
        input_sdl_key(ctx, NK_KEY_SCROLL_END, down);
        break;
    case SDLK_PAGEDOWN:
        input_sdl_key(ctx, NK_KEY_SCROLL_DOWN, down);
        break;
    case SDLK_PAGEUP:
        input_sdl_key(ctx, NK_KEY_SCROLL_UP, down);
        break;
    case SDLK_F1:
        input_sdl_key(ctx, NK_KEY_F1, down);
        break;
    case SDLK_F2:
        input_sdl_key(ctx, NK_KEY_F2, down);
        break;
    case SDLK_F3:
        input_sdl_key(ctx, NK_KEY_F3, down);
        break;
    case SDLK_F4:
        input_sdl_key(ctx, NK_KEY_F4, down);
        break;
    case SDLK_F5:
        input_sdl_key(ctx, NK_KEY_F5, down);
        break;
    case SDLK_F6:
        input_sdl_key(ctx, NK_KEY_F6, down);
        break;
    case SDLK_F7:
        input_sdl_key(ctx, NK_KEY_F7, down);
        break;
    case SDLK_F8:
        input_sdl_key(ctx, NK_KEY_F8, down);
        break;
    case SDLK_F9:
        input_sdl_key(ctx, NK_KEY_F9, down);
        break;
    case SDLK_F10:
        input_sdl_key(ctx, NK_KEY_F10, down);
        break;
    case SDLK_F11:
        input_sdl_key(ctx, NK_KEY_F11, down);
        break;
    case SDLK_F12:
        input_sdl_key(ctx, NK_KEY_F12, down);
        break;
    case SDLK_A:
        input_sdl_key(ctx, NK_KEY_TEXT_SELECT_ALL, down && ctrl_down);
        break;
    case SDLK_Z:
        input_sdl_key(ctx, NK_KEY_TEXT_UNDO, down && ctrl_down);
        break;
    case SDLK_R:
        input_sdl_key(ctx, NK_KEY_TEXT_REDO, down && ctrl_down);
        break;
    case SDLK_C:
        input_sdl_key(ctx, NK_KEY_COPY, down && ctrl_down);
        break;
    case SDLK_V:
        input_sdl_key(ctx, NK_KEY_PASTE, down && ctrl_down);
        break;
    case SDLK_X:
        input_sdl_key(ctx, NK_KEY_CUT, down && ctrl_down);
        break;
    case SDLK_B:
        input_sdl_key(ctx, NK_KEY_TEXT_LINE_START, down && ctrl_down);
        break;
    case SDLK_E:
        input_sdl_key(ctx, NK_KEY_TEXT_LINE_END, down && ctrl_down);
        break;
    case SDLK_UP:
        input_sdl_key(ctx, NK_KEY_UP, down);
        break;
    case SDLK_DOWN:
        input_sdl_key(ctx, NK_KEY_DOWN, down);
        break;
    case SDLK_ESCAPE:
        input_sdl_key(ctx, NK_KEY_TEXT_RESET_MODE, down);
        break;
    case SDLK_INSERT:
        if (down)
            sdl->insert_toggle = !sdl->insert_toggle;
        if (sdl->insert_toggle) {
            input_sdl_key(ctx, NK_KEY_TEXT_INSERT_MODE, down);
        } else {
            input_sdl_key(ctx, NK_KEY_TEXT_REPLACE_MODE, down);
        }
        break;
    case SDLK_LEFT:
        input_sdl_key(ctx, NK_KEY_TEXT_WORD_LEFT, down && ctrl_down);
        input_sdl_key(ctx, NK_KEY_LEFT, down && !ctrl_down);
        break;
    case SDLK_RIGHT:
        input_sdl_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down && ctrl_down);
        input_sdl_key(ctx, NK_KEY_RIGHT, down && !ctrl_down);
        break;
    default:
        return 0;
    }
    return 1;
}

static auto handle_sdl_renderer_reset(nk_sdl* sdl, const SDL_Event& event) -> std::optional<int> {
    if (event.type != SDL_EVENT_RENDER_TARGETS_RESET && event.type != SDL_EVENT_RENDER_DEVICE_RESET)
        return std::nullopt;
    if (event.render.windowID != 0 && event.render.windowID != SDL_GetWindowID(sdl->win)) return 0;
    for (std::size_t index = 0; index < sdl->field_frame_count; ++index)
        sdl->field_frames[index]->geometry.invalidate_envelopes();
    if (sdl->overview) {
        sdl->overview->invalidate();
        ++sdl->overview->renderer_generation;
    }
    return 1;
}

NK_API int nk_sdl_handle_event(struct nk_context* ctx, SDL_Event* evt) {
    struct nk_sdl* sdl;

    NK_ASSERT(ctx);
    NK_ASSERT(evt);

    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    if (!sdl->headless) {
        if (const auto reset = handle_sdl_renderer_reset(sdl, *evt)) return *reset;
        /* We only care about Window currently used by Nuklear */
        if (sdl->win != SDL_GetWindowFromEvent(evt)) return 0;
    }

    switch (evt->type) {
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_KEY_DOWN:
        return handle_sdl_key(ctx, sdl, *evt);

    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        return handle_sdl_mouse_button(ctx, *evt);

    case SDL_EVENT_MOUSE_MOTION:
        ctx->input.mouse.pos.x = evt->motion.x;
        ctx->input.mouse.pos.y = evt->motion.y;
        ctx->input.mouse.delta.x = ctx->input.mouse.pos.x - ctx->input.mouse.prev.x;
        ctx->input.mouse.delta.y = ctx->input.mouse.pos.y - ctx->input.mouse.prev.y;
        return 1;

    case SDL_EVENT_TEXT_INPUT:
        if (const auto* text = evt->text.text) {
            while (*text) {
                const auto rune = SDL_StepUTF8(&text, nullptr);
                if (rune == 0) break;
                nk_input_unicode(ctx, static_cast<nk_rune>(rune));
            }
        }
        return 1;

    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(ctx, nk_vec2(evt->wheel.x, evt->wheel.y));
        return 1;
    }
    return 0;
}

NK_API
void nk_sdl_shutdown(struct nk_context* ctx) {
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

#ifdef NK_INCLUDE_FONT_BAKING
    if (sdl->atlas.font_num > 0)
        nk_font_atlas_clear(&sdl->atlas);
#endif

    nk_buffer_free(&sdl->ogl.cmds);
    nk_buffer_free(&sdl->ogl.vertices);
    nk_buffer_free(&sdl->ogl.indices);

    if (sdl->ogl.font_tex != NULL) {
        SDL_DestroyTexture(sdl->ogl.font_tex);
        sdl->ogl.font_tex = NULL;
    }

    for (std::size_t index = 0; index < sdl->field_frame_count; ++index)
        delete sdl->field_frames[index];
    delete sdl->overview;
    nk_free(ctx);
    sdl->allocator.free(sdl->allocator.userdata, sdl->debug_font);
    sdl->allocator.free(sdl->allocator.userdata, sdl);
}

/* Debug Font Width/Height of internal texture atlas
 * This is a result of: ceil(sqrt('~' - ' '))
 * There is a sanity check for this value in nk_sdl_style_set_debug_font */
#define NK_SDL_DFWH (10)

NK_INTERN float nk_sdl_query_debug_font_width(nk_handle handle, float height, const char* text,
                                              int len) {
    NK_UNUSED(handle);
    return nk_utf_len(text, len) * height;
}

NK_INTERN void nk_sdl_query_debug_font_glypth(nk_handle handle, float height,
                                              struct nk_user_font_glyph* glyph, nk_rune codepoint,
                                              nk_rune next_codepoint) {
    char ascii;
    int idx, x, y;
    NK_UNUSED(next_codepoint);
    NK_UNUSED(handle);

    /* replace non-ASCII characters with question mark */
    ascii = (codepoint < (nk_rune)' ' || codepoint > (nk_rune)'~') ? '?' : (char)codepoint;
    NK_ASSERT(ascii >= ' ' && ascii <= '~');

    idx = (int)(ascii - ' ');
    x = idx / NK_SDL_DFWH;
    y = idx % NK_SDL_DFWH;
    NK_ASSERT(x >= 0 && x < NK_SDL_DFWH);
    NK_ASSERT(y >= 0 && y < NK_SDL_DFWH);

    glyph->height = height;
    glyph->width = height;
    glyph->xadvance = height;
    glyph->uv[0].x = (float)(x + 0) / NK_SDL_DFWH;
    glyph->uv[0].y = (float)(y + 0) / NK_SDL_DFWH;
    glyph->uv[1].x = (float)(x + 1) / NK_SDL_DFWH;
    glyph->uv[1].y = (float)(y + 1) / NK_SDL_DFWH;
    glyph->offset.x = 0.0f;
    glyph->offset.y = 0.0f;
}

static void draw_sdl_debug_font_atlas(SDL_Renderer* renderer) {
    char buf[2];
    int x, y;
    [[maybe_unused]] bool success;
    /* SPACE is the first printable ASCII character */
    NK_MEMCPY(buf, " ", sizeof(buf));
    for (x = 0; x < NK_SDL_DFWH; x++) {
        for (y = 0; y < NK_SDL_DFWH; y++) {
            success = SDL_RenderDebugText(renderer, (float)(x * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE),
                                          (float)(y * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE), buf);
            NK_ASSERT(success);
            buf[0]++;

            /* TILDE is the last printable ASCII character */
            if (buf[0] > '~')
                break;
        }
    }
    success = SDL_RenderPresent(renderer);
    NK_ASSERT(success);
}

NK_API void nk_sdl_style_set_debug_font(struct nk_context* ctx) {
    struct nk_user_font* font;
    struct nk_sdl* sdl;
    SDL_Surface* surface;
    SDL_Renderer* renderer;
    [[maybe_unused]] bool success;
    NK_ASSERT(ctx);

    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    if (sdl->debug_font) {
        sdl->allocator.free(sdl->allocator.userdata, sdl->debug_font);
        sdl->debug_font = 0;
    }

    /* sanity check: formal proof of NK_SDL_DFWH value (which is 10) */
    NK_ASSERT(SDL_ceil(SDL_sqrt('~' - ' ')) == NK_SDL_DFWH);

    /* We use another Software Renderer just to make sure
     * that we won't mutate any state in the main Renderer. */
    surface =
        SDL_CreateSurface(NK_SDL_DFWH * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,
                          NK_SDL_DFWH * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE, SDL_PIXELFORMAT_RGBA32);
    NK_ASSERT(surface);
    renderer = SDL_CreateSoftwareRenderer(surface);
    NK_ASSERT(renderer);
    success = SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    NK_ASSERT(success);

    draw_sdl_debug_font_atlas(renderer);

    font = (struct nk_user_font*)sdl->allocator.alloc(sdl->allocator.userdata, 0, sizeof(*font));
    NK_ASSERT(font);
    font->userdata.ptr = sdl;
    font->height = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    font->width = &nk_sdl_query_debug_font_width;
    font->query = &nk_sdl_query_debug_font_glypth;

    /* HACK: nk_sdl_device_upload_atlas turns pixels into SDL_Texture
     *       and sets said Texture into sdl->ogl.font_tex
     *       then nk_sdl_render expects same Texture at font->texture */
    nk_sdl_device_upload_atlas(ctx, surface->pixels, surface->w, surface->h);
    font->texture.ptr = sdl->ogl.font_tex;

    sdl->debug_font = font;
    nk_style_set_font(ctx, font);

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

#undef SDL_RenderGeometryRaw
#undef SDL_SetRenderClipRect
#undef nk_convert

namespace wormedit::ui::nuklear {
auto sdl_draw_batcher_t::append(SDL_Texture* texture, SDL_Rect clip, unsigned count)
    -> std::optional<sdl_draw_batch_t> {
    if (count == 0)
        return std::nullopt;
    const sdl_draw_batch_t next{texture, clip, next_index_, count};
    next_index_ += count;
    if (!pending_) {
        pending_ = next;
        return std::nullopt;
    }
    const auto& before = pending_->clip;
    const bool same_clip =
        before.x == clip.x && before.y == clip.y && before.w == clip.w && before.h == clip.h;
    const auto maximum = static_cast<unsigned>(std::numeric_limits<int>::max());
    if (pending_->texture == texture && same_clip && count <= maximum &&
        pending_->index_count <= maximum - count) {
        pending_->index_count += count;
        return std::nullopt;
    }
    auto completed = pending_;
    pending_ = next;
    return completed;
}

auto sdl_draw_batcher_t::finish() -> std::optional<sdl_draw_batch_t> {
    return std::exchange(pending_, std::nullopt);
}

static void convert_field_marker(void* canvas, short, short, unsigned short, unsigned short,
                                 nk_handle marker) {
    auto* list = static_cast<nk_draw_list*>(canvas);
    const auto previous = list->userdata;
    list->userdata = marker;
    nk_draw_list_push_command(list, list->clip_rect, list->config.tex_null.texture);
    list->userdata = previous;
    // Reserve a separate ordinary command: later texture changes may rewrite
    // empty commands, but must never overwrite the field's ordering barrier.
    nk_draw_list_push_command(list, list->clip_rect, list->config.tex_null.texture);
}
static auto overview_key(nk_sdl* sdl, nk_command_buffer* canvas, const wormedit::ui::compose_view_t& view,
                         struct nk_rect bounds, nk_color background, nk_color peak) -> OverviewKey {
    OverviewKey key{.generation = view.waveform_overview_generation,
        .renderer_generation = sdl->overview->renderer_generation};
    float sx{}, sy{};
    SDL_GetRenderScale(sdl->renderer, &sx, &sy);
    key.nominal_scale_x = sx;
    key.nominal_scale_y = sy;
    SDL_GetRenderOutputSize(sdl->renderer, &key.output_width, &key.output_height);
    SDL_GetRenderLogicalPresentation(sdl->renderer, &key.logical_width, &key.logical_height,
        &key.logical_mode);
    if (key.logical_mode == SDL_LOGICAL_PRESENTATION_STRETCH &&
        key.logical_width > 0 && key.logical_height > 0) {
        sx *= static_cast<float>(key.output_width) / key.logical_width;
        sy *= static_cast<float>(key.output_height) / key.logical_height;
    }
    key.geometry = {bounds.x, bounds.y, bounds.w, bounds.h, canvas->clip.x, canvas->clip.y,
        canvas->clip.w, canvas->clip.h, sx, sy};
    key.colors = {background.r, background.g, background.b, background.a,
        peak.r, peak.g, peak.b, peak.a};
    SDL_GetRenderOutputSize(sdl->renderer, &key.output_width, &key.output_height);
    return key;
}
static auto overview_physical_rect(struct nk_rect logical, float sx, float sy,
                                   bool interior) -> SDL_Rect {
    const auto left = interior ? std::ceil(std::trunc(logical.x) * sx) + 1 : std::floor(logical.x * sx);
    const auto top = interior ? std::ceil(std::trunc(logical.y) * sy) + 1 : std::floor(logical.y * sy);
    const auto right = interior ? std::floor((std::trunc(logical.x) + std::trunc(logical.w)) * sx) - 1
                               : std::ceil((logical.x + logical.w) * sx);
    const auto bottom = interior ? std::floor((std::trunc(logical.y) + std::trunc(logical.h)) * sy) - 1
                                : std::ceil((logical.y + logical.h) * sy);
    return {static_cast<int>(left), static_cast<int>(top),
        static_cast<int>(right - left), static_cast<int>(bottom - top)};
}
static bool prepare_overview_cache(nk_sdl* sdl, nk_command_buffer* canvas,
                                   const wormedit::ui::compose_view_t& view, OverviewKey key) {
    auto& cache = *sdl->overview;
    cache.invalidate();
    if (key.colors[3] != 255 || key.generation == 0 || view.waveform_overview_rows.empty() ||
        view.waveform_overview_rows.size() > 512 ||
        (key.logical_mode != SDL_LOGICAL_PRESENTATION_DISABLED &&
         key.logical_mode != SDL_LOGICAL_PRESENTATION_STRETCH)) return false;
    for (const auto value : key.geometry)
        if (!std::isfinite(value) || std::abs(value) > 1'000'000) return false;
    const auto sx = key.geometry[8], sy = key.geometry[9];
    if (sx <= 0 || sy <= 0 || sx > 8 || sy > 8) return false;
    SDL_Rect viewport{};
    SDL_GetRenderViewport(sdl->renderer, &viewport);
    if (viewport.x != 0 || viewport.y != 0 || SDL_RenderViewportSet(sdl->renderer) ||
        SDL_GetRenderTarget(sdl->renderer)) return false;
    for (std::size_t row = 0; row < view.waveform_overview_rows.size(); ++row)
        if (!std::isfinite(view.waveform_overview_rows[row].audio_fraction) ||
            !std::isfinite(view.waveform_overview_rows[row].absolute_peak)) return false;
    cache.entry.value().scale_x = sx;
    cache.entry.value().scale_y = sy;
    cache.entry.value().clip = overview_intersection({
        static_cast<int>(std::floor(std::trunc(canvas->clip.x) * sx)),
        static_cast<int>(std::floor(std::trunc(canvas->clip.y) * sy)),
        static_cast<int>(std::ceil(std::trunc(canvas->clip.w) * sx)),
        static_cast<int>(std::ceil(std::trunc(canvas->clip.h) * sy))},
        {0, 0, key.output_width, key.output_height});
    cache.entry.value().interior = overview_intersection(overview_physical_rect(
        nk_rect(key.geometry[0], key.geometry[1], key.geometry[2], key.geometry[3]), sx, sy, true), cache.entry.value().clip);
    if (cache.entry.value().interior.w <= 0 || cache.entry.value().interior.h <= 0 ||
        cache.entry.value().interior.w > 4096 || cache.entry.value().interior.h > 256) return false;
    cache.entry.value().pending = true;
    return true;
}
auto begin_compose_overview(nk_context* context, nk_command_buffer* canvas,
                            const wormedit::ui::compose_view_t& view, struct nk_rect bounds,
                            nk_color background, nk_color peak) -> bool {
    if (!context || !canvas || !context->userdata.ptr || !view.waveform_overview_generation) return false;
    auto* sdl = static_cast<nk_sdl*>(context->userdata.ptr);
    if (!sdl->overview) return false;
    auto& cache = *sdl->overview;
    if (SDL_RenderViewportSet(sdl->renderer) || SDL_GetRenderTarget(sdl->renderer)) {
        cache.invalidate();
        return false;
    }
    const auto key = overview_key(sdl, canvas, view, bounds, background, peak);
    const auto& data = cache.entry.get_or_build(key, [sdl, canvas, &view, &key](auto&) {
        return prepare_overview_cache(sdl, canvas, view, key);
    }, [](const auto& value) { return value.is_ready; });
    if (data.is_ready) {
        nk_push_custom(canvas, bounds, convert_field_marker, nk_handle_ptr(&cache.warm_marker));
        return true;
    }
    if (data.pending)
        nk_push_custom(canvas, bounds, convert_field_marker, nk_handle_ptr(&cache.begin_marker));
    return false;
}
void end_compose_overview(nk_context* context, nk_command_buffer* canvas, struct nk_rect bounds) {
    if (!context || !context->userdata.ptr) return;
    auto* overview = static_cast<nk_sdl*>(context->userdata.ptr)->overview;
    if (!overview) return;
    auto& cache = *overview;
    if (cache.entry.value().pending)
        nk_push_custom(canvas, bounds, convert_field_marker, nk_handle_ptr(&cache.end_marker));
}
auto compose_overview_work(nk_context* context) -> overview_work_t {
    const auto* cache = static_cast<nk_sdl*>(context->userdata.ptr)->overview;
    return cache ? cache->work : overview_work_t{};
}

void set_compose_overview_readback_for_testing(nk_context* context,
    SDL_Surface* (*readback)(SDL_Renderer*, const SDL_Rect*)) {
    auto* cache = static_cast<nk_sdl*>(context->userdata.ptr)->overview;
    if (cache) cache->readback = readback ? readback : SDL_RenderReadPixels;
}
auto compose_canvas_cache(nk_context* context) -> std::unique_ptr<compose_canvas_cache_t>& {
    return static_cast<nk_sdl*>(context->userdata.ptr)->field->canvas;
}
auto compose_field(nk_context* context) -> wormedit::ui::field::renderer_t& {
    return static_cast<nk_sdl*>(context->userdata.ptr)->field->geometry;
}
auto begin_compose_field(nk_context* context, SDL_FRect viewport)
    -> wormedit::ui::field::renderer_t& {
    auto& backend = *static_cast<nk_sdl*>(context->userdata.ptr);
    const auto slot = backend.next_field_frame++;
    if (slot >= maximum_compose_field_frames) {
        NK_ASSERT(slot < maximum_compose_field_frames);
        return backend.field->geometry;
    }
    if (!backend.field_frames[slot]) {
        backend.field_frames[slot] = new (std::nothrow) ComposeFieldFrame;
        if (!backend.field_frames[slot]) return backend.field->geometry;
        backend.field_frame_count = std::max(backend.field_frame_count, slot + 1);
    }
    backend.field = backend.field_frames[slot];
    auto& frame = *backend.field;
    frame.marker_count = 0;
    frame.next_index = 0;
    wormedit::ui::field::solid_texture_t solid{};
    const auto& white = backend.ogl.tex_null;
    if (context->style.font && context->style.font->texture.ptr == white.texture.ptr) {
        solid = {static_cast<SDL_Texture*>(white.texture.ptr), {white.uv.x, white.uv.y}};
    }
    frame.geometry.begin(viewport, {1, 1}, solid, backend.renderer);
    return frame.geometry;
}
static auto query_field_glyph(void* userdata, float height, std::uint32_t unicode,
                              std::uint32_t next) -> wormedit::ui::field::glyph_t {
    const auto* font = static_cast<const nk_user_font*>(userdata);
    nk_user_font_glyph glyph{};
    font->query(font->userdata, height, &glyph, unicode, next);
    return {{glyph.offset.x, glyph.offset.y}, {glyph.width, glyph.height},
            {glyph.uv[0].x, glyph.uv[0].y}, {glyph.uv[1].x, glyph.uv[1].y}, glyph.xadvance};
}
auto compose_field_font(const nk_user_font* font) -> wormedit::ui::field::font_t {
    if (!font || !font->query)
        return {};
    return {const_cast<nk_user_font*>(font), static_cast<SDL_Texture*>(font->texture.ptr),
            font->height, query_field_glyph};
}
void draw_compose_field_text(nk_context* context, wormedit::ui::field::layer_t layer,
                             struct nk_rect bounds, const char* text, int length,
                             const nk_user_font* font, nk_color foreground) {
    if (!font || !text || length <= 0 || bounds.w <= 0 || bounds.h <= 0)
        return;
    float width = font->width(font->userdata, font->height, text, length);
    if (width > bounds.w) {
        int glyphs{};
        length = nk_text_clamp(font, text, length, bounds.w, &glyphs, &width, nullptr, 0);
    }
    compose_field(context).text(layer, {std::trunc(bounds.x), std::trunc(bounds.y)},
                               {text, static_cast<std::size_t>(length)},
                               {foreground.r, foreground.g, foreground.b, foreground.a},
                               compose_field_font(font));
}
void insert_compose_field(nk_context* context, nk_command_buffer* canvas, struct nk_rect viewport) {
    auto& frame = *static_cast<nk_sdl*>(context->userdata.ptr)->field;
    const auto end = frame.geometry.indices().size();
    if (end == frame.next_index || frame.marker_count == frame.markers.size())
        return;
    auto& marker = frame.markers[frame.marker_count++];
    marker = {&frame, frame.next_index, end - frame.next_index};
    frame.next_index = end;
    nk_push_custom(canvas, viewport, convert_field_marker, nk_handle_ptr(&marker));
}
diagnostic_window_scope_t::diagnostic_window_scope_t(nk_context* context)
    : context_(context), previous_(context->userdata) {
    // nk_begin copies the context user data into the window's command buffer,
    // which is how the render pass recognizes the window.
    nk_set_user_data(context_, nk_handle_ptr(&g_diagnostic_window_tag));
}
diagnostic_window_scope_t::~diagnostic_window_scope_t() {
    nk_set_user_data(context_, previous_);
}
void render_sdl_backend(nk_context* context, nk_anti_aliasing anti_aliasing, nk_context* overlay) {
    g_application_submission = {};
    g_diagnostic_submission = {};
    g_gpu_submission = &g_application_submission;
    auto* sdl = static_cast<nk_sdl*>(context->userdata.ptr);
    const auto ticks = SDL_GetTicks();
    context->delta_time_seconds = static_cast<float>(ticks - sdl->last_render) / 1000.0f;
    sdl->last_render = ticks;
    if (overlay) overlay->delta_time_seconds = context->delta_time_seconds;
    const bool diagnostics = render_application_then_diagnostics(context, sdl, anti_aliasing);
    finish_nuklear_frame(context, sdl);
    if (overlay) nk_sdl_render(overlay, anti_aliasing);
    worm_ui_frame_timing_record_gpu_work(g_application_submission.work);
    if (diagnostics) worm_ui_frame_timing_record_diagnostic_gpu_work(g_diagnostic_submission.work);
}
} // namespace wormedit::ui::nuklear

void worm_ui_nuklear_append_image_quad(void* canvas, short, short, unsigned short, unsigned short,
                                       nk_handle callback_data) {
    auto* list = static_cast<struct nk_draw_list*>(canvas);
    const auto* quad = static_cast<const WormUiNuklearImageQuad*>(callback_data.ptr);
    if (!list || !quad) {
        return;
    }
    nk_draw_list_push_image(list, quad->image.handle);
    const auto base = static_cast<nk_draw_index>(list->vertex_count);
    auto* vertices = nk_draw_list_alloc_vertices(list, 4);
    auto* indices = nk_draw_list_alloc_elements(list, 6);
    if (!vertices || !indices) {
        return;
    }
    indices[0] = static_cast<nk_draw_index>(base + 0);
    indices[1] = static_cast<nk_draw_index>(base + 1);
    indices[2] = static_cast<nk_draw_index>(base + 2);
    indices[3] = static_cast<nk_draw_index>(base + 0);
    indices[4] = static_cast<nk_draw_index>(base + 2);
    indices[5] = static_cast<nk_draw_index>(base + 3);
    struct nk_colorf color{};
    nk_color_fv(&color.r, quad->color);
    for (std::size_t index = 0; index < 4; ++index) {
        vertices =
            nk_draw_vertex(vertices, &list->config, quad->points[index], quad->uvs[index], color);
    }
}
