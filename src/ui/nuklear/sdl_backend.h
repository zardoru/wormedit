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

#pragma once
#include "wormedit/nuklear_config.h"
#include <SDL3/SDL.h>
#include <cstddef>
#include <optional>

/* nuklear - public domain */

/*
 * ==============================================================
 *
 *                              API
 *
 * ===============================================================
 */

#ifndef NK_SDL3_RENDERER_H_
#define NK_SDL3_RENDERER_H_

#if SDL_MAJOR_VERSION < 3
#error "nk_sdl3_renderer requires at least SDL 3.0.0"
#endif
#ifndef NK_INCLUDE_COMMAND_USERDATA
#error "nk_sdl3_renderer requires the NK_INCLUDE_COMMAND_USERDATA define"
#endif
#ifndef NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#error "nk_sdl3_renderer requires the NK_INCLUDE_VERTEX_BUFFER_OUTPUT define"
#endif

/* We have to redefine it because demos do not include any headers
 * This is the same default value as the one from "src/nuklear_internal.h" */
#ifndef NK_BUFFER_DEFAULT_INITIAL_SIZE
#define NK_BUFFER_DEFAULT_INITIAL_SIZE (4 * 1024)
#endif

#ifdef __cplusplus
extern "C" {
#endif

NK_API struct nk_context* nk_sdl_init(SDL_Window* win, SDL_Renderer* renderer,
                                      struct nk_allocator allocator);
#ifdef NK_INCLUDE_FONT_BAKING
NK_API struct nk_font_atlas* nk_sdl_font_stash_begin(struct nk_context* ctx);
NK_API void nk_sdl_font_stash_end(struct nk_context* ctx);
#endif
NK_API int nk_sdl_handle_event(struct nk_context* ctx, SDL_Event* evt);
NK_API void nk_sdl_render(struct nk_context* ctx, enum nk_anti_aliasing);
NK_API void nk_sdl_update_TextInput(struct nk_context* ctx);
NK_API void nk_sdl_shutdown(struct nk_context* ctx);
NK_API nk_handle nk_sdl_get_userdata(struct nk_context* ctx);
NK_API void nk_sdl_set_userdata(struct nk_context* ctx, nk_handle userdata);
NK_API void nk_sdl_style_set_debug_font(struct nk_context* ctx);
NK_API struct nk_allocator nk_sdl_allocator(void);

#ifdef __cplusplus
}
#endif

#endif /* NK_SDL3_RENDERER_H_ */

/*
 * ==============================================================
 *
 *                          IMPLEMENTATION
 *
 * ===============================================================
 */

namespace wormedit::ui::nuklear {
// Draw-list indices stay contiguous; batching never reorders geometry.
struct sdl_draw_batch_t {
    SDL_Texture* texture{};
    SDL_Rect clip{};
    std::size_t index_offset{};
    unsigned index_count{};
};
class sdl_draw_batcher_t {
  public:
    auto append(SDL_Texture* texture, SDL_Rect clip, unsigned count)
        -> std::optional<sdl_draw_batch_t>;
    auto finish() -> std::optional<sdl_draw_batch_t>;

  private:
    std::optional<sdl_draw_batch_t> pending_;
    std::size_t next_index_{};
};
void render_sdl_backend(nk_context* context, nk_anti_aliasing anti_aliasing);
} // namespace wormedit::ui::nuklear
