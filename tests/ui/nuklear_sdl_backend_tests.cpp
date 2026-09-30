#include "ui/nuklear/sdl_backend.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <limits>

using namespace wormedit::ui::nuklear;

namespace {
template <typename Value> void expect_backend(const Value& value) {
    REQUIRE(static_cast<bool>(value));
}
SDL_Texture* texture(unsigned index) {
    static unsigned identities[2]{};
    return reinterpret_cast<SDL_Texture*>(&identities[index]);
}

struct allocation_counter_t {
    std::size_t calls{};
};
void* allocate_backend(nk_handle userdata, void*, nk_size size) {
    ++static_cast<allocation_counter_t*>(userdata.ptr)->calls;
    return std::malloc(size);
}
void free_backend(nk_handle, void* memory) {
    std::free(memory);
}

struct backend_fixture_t {
    allocation_counter_t allocations;
    SDL_Window* window{};
    SDL_Surface* surface{};
    SDL_Renderer* renderer{};
    nk_context* context{};

    backend_fixture_t() {
        expect_backend(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("backend test", 128, 128, SDL_WINDOW_HIDDEN);
        expect_backend(window);
        surface = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
        expect_backend(surface);
        renderer = SDL_CreateSoftwareRenderer(surface);
        expect_backend(renderer);
        context = nk_sdl_init(window, renderer,
                              {nk_handle_ptr(&allocations), allocate_backend, free_backend});
        expect_backend(context);
        auto* atlas = nk_sdl_font_stash_begin(context);
        auto* font = nk_font_atlas_add_default(atlas, 14.0f, nullptr);
        expect_backend(font);
        nk_sdl_font_stash_end(context);
        nk_style_set_font(context, &font->handle);
    }
    ~backend_fixture_t() {
        if (context)
            nk_sdl_shutdown(context);
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (surface)
            SDL_DestroySurface(surface);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
    }
    void frame() {
        nk_input_begin(context);
        nk_input_end(context);
        if (nk_begin(context, "test", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR)) {
            auto* canvas = nk_window_get_canvas(context);
            for (unsigned index = 0; index < 1000; ++index) {
                nk_fill_rect(canvas, nk_rect(5, 5 + index % 100, 40, 1), 0,
                             nk_rgba(100, 180, 220, 180));
            }
        }
        nk_end(context);
        render_sdl_backend(context, NK_ANTI_ALIASING_ON);
    }
};
} // namespace

TEST_CASE("SDL backend merges only consecutive compatible nonempty commands", "[ui][sdl-backend]") {
    sdl_draw_batcher_t batches;
    const SDL_Rect clip{1, 2, 30, 40};
    expect_backend(!(batches.append(texture(0), clip, 6)));
    expect_backend(!(batches.append(texture(1), SDL_Rect{9, 9, 1, 1}, 0)));
    expect_backend(!(batches.append(texture(0), clip, 12)));
    const auto first = batches.append(texture(1), clip, 6);
    expect_backend(first);
    expect_backend(first->texture == texture(0));
    expect_backend(first->index_offset == 0);
    expect_backend(first->index_count == 18);
    const auto second = batches.append(texture(0), clip, 3);
    expect_backend(second);
    expect_backend(second->texture == texture(1));
    expect_backend(second->index_offset == 18);
    expect_backend(second->index_count == 6);
    const auto last = batches.finish();
    expect_backend(last);
    expect_backend(last->texture == texture(0));
    expect_backend(last->index_offset == 24);
    expect_backend(last->index_count == 3);
    expect_backend(!(batches.finish()));
}

TEST_CASE("SDL backend keeps clip boundaries and signed index limits", "[ui][sdl-backend]") {
    sdl_draw_batcher_t batches;
    const SDL_Rect first_clip{-1, 2, 30, 40};
    const SDL_Rect next_clip{-1, 2, 31, 40};
    expect_backend(!(batches.append(texture(0), first_clip, 6)));
    const auto clipped = batches.append(texture(0), next_clip, 3);
    expect_backend(clipped);
    expect_backend(clipped->clip.x == -1);
    expect_backend(clipped->clip.w == 30);
    expect_backend(batches.finish());
    sdl_draw_batcher_t large;
    const auto maximum = static_cast<unsigned>(std::numeric_limits<int>::max());
    expect_backend(!(large.append(texture(0), first_clip, maximum - 6)));
    expect_backend(!(large.append(texture(0), first_clip, 6)));
    const auto full = large.append(texture(0), first_clip, 3);
    expect_backend(full);
    expect_backend(full->index_count == maximum);
    const auto remainder = large.finish();
    expect_backend(remainder);
    expect_backend(remainder->index_offset == maximum);
    expect_backend(remainder->index_count == 3);
}

TEST_CASE("SDL backend retains converted buffers after dense frame warmup", "[ui][sdl-backend]") {
    backend_fixture_t backend;
    backend.frame();
    backend.frame();
    backend.frame();
    const auto before = backend.allocations.calls;
    for (unsigned frame = 0; frame < 10; ++frame)
        backend.frame();
    expect_backend(backend.allocations.calls == before);
    SDL_Rect clip{3, 4, 50, 60};
    expect_backend(SDL_SetRenderClipRect(backend.renderer, &clip));
    backend.frame();
    SDL_Rect retained{};
    expect_backend(SDL_GetRenderClipRect(backend.renderer, &retained));
    expect_backend(SDL_RenderClipEnabled(backend.renderer));
    expect_backend(retained.x == clip.x);
    expect_backend(retained.y == clip.y);
    expect_backend(retained.w == clip.w);
    expect_backend(retained.h == clip.h);
}
