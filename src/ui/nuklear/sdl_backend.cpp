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
#include "ui/nuklear/sdl_backend.h"
#include "ui/frontend.h"
#include "ui/nuklear/image_quad.h"
#include <limits>
#include <cmath>
#include <utility>

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
// draw command, so their vertices and indices are counted once per frame.
struct GpuSubmissionCounters {
    WormUiFrameGpuWork work{};
    SDL_Rect current_clip{};
    SDL_Rect previous_clip{};
    SDL_Texture* previous_texture{};
    bool current_clip_enabled{};
    bool previous_clip_enabled{};
    bool submitted{};
};
static GpuSubmissionCounters g_gpu_submission;

static nk_flags convert_nuklear_commands(nk_context* context, nk_buffer* commands,
                                         nk_buffer* vertices, nk_buffer* indices,
                                         const nk_convert_config* config) {
    const RenderSubphaseScope convert_phase{WORM_UI_FRAME_RENDER_CONVERT};
    const auto result = nk_convert(context, commands, vertices, indices, config);
    g_gpu_submission.work.vertices +=
        config->vertex_size == 0 ? 0 : vertices->allocated / config->vertex_size;
    g_gpu_submission.work.indices += indices->allocated / sizeof(nk_draw_index);
    return result;
}

static bool set_nuklear_clip_rect(SDL_Renderer* renderer, const SDL_Rect* rect) {
    const auto result = SDL_SetRenderClipRect(renderer, rect);
    if (result) {
        g_gpu_submission.current_clip_enabled = rect != nullptr;
        g_gpu_submission.current_clip = rect ? *rect : SDL_Rect{};
    }
    return result;
}

static bool same_nuklear_clip() {
    const auto& current = g_gpu_submission.current_clip;
    const auto& previous = g_gpu_submission.previous_clip;
    return g_gpu_submission.current_clip_enabled == g_gpu_submission.previous_clip_enabled &&
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
    ++g_gpu_submission.work.draw_calls;
    if (g_gpu_submission.submitted) {
        g_gpu_submission.work.clip_rect_changes += !same_nuklear_clip();
        g_gpu_submission.work.texture_switches += texture != g_gpu_submission.previous_texture;
    }
    g_gpu_submission.previous_clip = g_gpu_submission.current_clip;
    g_gpu_submission.previous_clip_enabled = g_gpu_submission.current_clip_enabled;
    g_gpu_submission.previous_texture = texture;
    g_gpu_submission.submitted = true;
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

struct ComposeFieldMarker {
    std::size_t first_index{}, index_count{};
};
struct ComposeFieldFrame {
    wormedit::ui::field::renderer_t geometry;
    std::array<ComposeFieldMarker, 32> markers{};
    std::size_t marker_count{}, next_index{};
};

struct nk_sdl {
    ComposeFieldFrame* field;
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
    for (std::size_t index = 0; index < sdl->field->marker_count; ++index)
        if (command->userdata.ptr == &sdl->field->markers[index])
            return &sdl->field->markers[index];
    return nullptr;
}
static void submit_field_marker(nk_sdl* sdl, const ComposeFieldMarker& marker) {
    const wormedit::ui::field::submission_t hooks{set_nuklear_clip_rect,
                                                render_nuklear_geometry_raw};
    sdl->field->geometry.submit_range(sdl->renderer, marker.first_index, marker.index_count, hooks);
}
static void submit_sdl_draw_list(nk_context* context, nk_sdl* sdl) {
    if (sdl->field->marker_count != 0) {
        const auto work = sdl->field->geometry.workload();
        g_gpu_submission.work.vertices += work.vertices;
        g_gpu_submission.work.indices += work.indices;
    }
    wormedit::ui::nuklear::sdl_draw_batcher_t batches;
    const nk_draw_command* command;
    nk_draw_foreach(command, context, &sdl->ogl.cmds) {
        if (const auto* marker = field_marker(sdl, command)) {
            if (const auto pending = batches.finish())
                submit_sdl_batch(sdl, *pending);
            submit_field_marker(sdl, *marker);
            continue;
        }
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

NK_API void nk_sdl_render(struct nk_context* ctx, enum nk_anti_aliasing AA) {
    /* setup global state */
    struct nk_sdl* sdl;
    NK_ASSERT(ctx);
    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

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

        sdl->field->marker_count = 0;
        nk_clear(ctx);
        nk_buffer_clear(&sdl->ogl.cmds);
    }
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

static int handle_sdl_key(nk_context* ctx, nk_sdl* sdl, const SDL_Event& event) {
    int down = event.type == SDL_EVENT_KEY_DOWN;
    int ctrl_down = event.key.mod & SDL_KMOD_CTRL;

    switch (event.key.key) {
    case SDLK_LALT:
    case SDLK_RALT:
        nk_input_key(ctx, NK_KEY_ALT, down);
        break;
    case SDLK_RSHIFT: /* RSHIFT & LSHIFT share same routine */
    case SDLK_LSHIFT:
        nk_input_key(ctx, NK_KEY_SHIFT, down);
        break;
    case SDLK_DELETE:
        nk_input_key(ctx, NK_KEY_DEL, down);
        break;
    case SDLK_RETURN:
        nk_input_key(ctx, NK_KEY_ENTER, down);
        break;
    case SDLK_TAB:
        nk_input_key(ctx, NK_KEY_TAB, down);
        break;
    case SDLK_BACKSPACE:
        nk_input_key(ctx, NK_KEY_BACKSPACE, down);
        break;
    case SDLK_HOME:
        nk_input_key(ctx, NK_KEY_TEXT_START, down);
        nk_input_key(ctx, NK_KEY_SCROLL_START, down);
        break;
    case SDLK_END:
        nk_input_key(ctx, NK_KEY_TEXT_END, down);
        nk_input_key(ctx, NK_KEY_SCROLL_END, down);
        break;
    case SDLK_PAGEDOWN:
        nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down);
        break;
    case SDLK_PAGEUP:
        nk_input_key(ctx, NK_KEY_SCROLL_UP, down);
        break;
    case SDLK_F1:
        nk_input_key(ctx, NK_KEY_F1, down);
        break;
    case SDLK_F2:
        nk_input_key(ctx, NK_KEY_F2, down);
        break;
    case SDLK_F3:
        nk_input_key(ctx, NK_KEY_F3, down);
        break;
    case SDLK_F4:
        nk_input_key(ctx, NK_KEY_F4, down);
        break;
    case SDLK_F5:
        nk_input_key(ctx, NK_KEY_F5, down);
        break;
    case SDLK_F6:
        nk_input_key(ctx, NK_KEY_F6, down);
        break;
    case SDLK_F7:
        nk_input_key(ctx, NK_KEY_F7, down);
        break;
    case SDLK_F8:
        nk_input_key(ctx, NK_KEY_F8, down);
        break;
    case SDLK_F9:
        nk_input_key(ctx, NK_KEY_F9, down);
        break;
    case SDLK_F10:
        nk_input_key(ctx, NK_KEY_F10, down);
        break;
    case SDLK_F11:
        nk_input_key(ctx, NK_KEY_F11, down);
        break;
    case SDLK_F12:
        nk_input_key(ctx, NK_KEY_F12, down);
        break;
    case SDLK_A:
        nk_input_key(ctx, NK_KEY_TEXT_SELECT_ALL, down && ctrl_down);
        break;
    case SDLK_Z:
        nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && ctrl_down);
        break;
    case SDLK_R:
        nk_input_key(ctx, NK_KEY_TEXT_REDO, down && ctrl_down);
        break;
    case SDLK_C:
        nk_input_key(ctx, NK_KEY_COPY, down && ctrl_down);
        break;
    case SDLK_V:
        nk_input_key(ctx, NK_KEY_PASTE, down && ctrl_down);
        break;
    case SDLK_X:
        nk_input_key(ctx, NK_KEY_CUT, down && ctrl_down);
        break;
    case SDLK_B:
        nk_input_key(ctx, NK_KEY_TEXT_LINE_START, down && ctrl_down);
        break;
    case SDLK_E:
        nk_input_key(ctx, NK_KEY_TEXT_LINE_END, down && ctrl_down);
        break;
    case SDLK_UP:
        nk_input_key(ctx, NK_KEY_UP, down);
        break;
    case SDLK_DOWN:
        nk_input_key(ctx, NK_KEY_DOWN, down);
        break;
    case SDLK_ESCAPE:
        nk_input_key(ctx, NK_KEY_TEXT_RESET_MODE, down);
        break;
    case SDLK_INSERT:
        if (down)
            sdl->insert_toggle = !sdl->insert_toggle;
        if (sdl->insert_toggle) {
            nk_input_key(ctx, NK_KEY_TEXT_INSERT_MODE, down);
        } else {
            nk_input_key(ctx, NK_KEY_TEXT_REPLACE_MODE, down);
        }
        break;
    case SDLK_LEFT:
        if (ctrl_down)
            nk_input_key(ctx, NK_KEY_TEXT_WORD_LEFT, down);
        else
            nk_input_key(ctx, NK_KEY_LEFT, down);
        break;
    case SDLK_RIGHT:
        if (ctrl_down)
            nk_input_key(ctx, NK_KEY_TEXT_WORD_RIGHT, down);
        else
            nk_input_key(ctx, NK_KEY_RIGHT, down);
        break;
    default:
        return 0;
    }
    return 1;
}

NK_API int nk_sdl_handle_event(struct nk_context* ctx, SDL_Event* evt) {
    struct nk_sdl* sdl;

    NK_ASSERT(ctx);
    NK_ASSERT(evt);

    sdl = (struct nk_sdl*)ctx->userdata.ptr;
    NK_ASSERT(sdl);

    /* We only care about Window currently used by Nuklear */
    if (sdl->win != SDL_GetWindowFromEvent(evt)) {
        return 0;
    }

    switch (evt->type) {
    case SDL_EVENT_RENDER_TARGETS_RESET:
    case SDL_EVENT_RENDER_DEVICE_RESET:
        sdl->field->geometry.invalidate_envelopes();
        return 1;
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

    case SDL_EVENT_TEXT_INPUT: {
        nk_glyph glyph;
        nk_size len;
        NK_ASSERT(evt->text.text);
        len = SDL_strlen(evt->text.text);
        NK_ASSERT(len <= NK_UTF_SIZE);
        NK_MEMCPY(glyph, evt->text.text, len);
        nk_input_glyph(ctx, glyph);
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

    delete sdl->field;
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
    bool success;
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
    bool success;
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
auto compose_field(nk_context* context) -> wormedit::ui::field::renderer_t& {
    return static_cast<nk_sdl*>(context->userdata.ptr)->field->geometry;
}
auto begin_compose_field(nk_context* context, SDL_FRect viewport)
    -> wormedit::ui::field::renderer_t& {
    const auto& backend = *static_cast<nk_sdl*>(context->userdata.ptr);
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
    marker = {frame.next_index, end - frame.next_index};
    frame.next_index = end;
    nk_push_custom(canvas, viewport, convert_field_marker, nk_handle_ptr(&marker));
}
void render_sdl_backend(nk_context* context, nk_anti_aliasing anti_aliasing, nk_context* overlay) {
    g_gpu_submission = {};
    auto* sdl = static_cast<nk_sdl*>(context->userdata.ptr);
    const auto ticks = SDL_GetTicks();
    context->delta_time_seconds = static_cast<float>(ticks - sdl->last_render) / 1000.0f;
    sdl->last_render = ticks;
    if (overlay) overlay->delta_time_seconds = context->delta_time_seconds;
    nk_sdl_render(context, anti_aliasing);
    if (overlay) nk_sdl_render(overlay, anti_aliasing);
    worm_ui_frame_timing_record_gpu_work(g_gpu_submission.work);
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
