#include "pixel_comparison.h"
#include "ui/nuklear/app/sdl_backend.h"
#include "ui/nuklear/compose/compose_canvas.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <cstring>
#include "ui/frontend.h"
#include "sys/app/frame_timing.h"
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <string_view>
#include <source_location>
#include <limits>
#include <utility>

using namespace wormedit::ui::nuklear;

namespace {
template <typename Value> void expect_backend(const Value& value,
    const std::source_location& location = std::source_location::current()) {
    INFO(location.file_name() << ":" << location.line());
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

    explicit backend_fixture_t(int pixels = 128) {
        expect_backend(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("backend test", 128, 128, SDL_WINDOW_HIDDEN);
        expect_backend(window);
        surface = SDL_CreateSurface(pixels, pixels, SDL_PIXELFORMAT_RGBA32);
        expect_backend(surface);
        renderer = std::getenv("WORMEDIT_TEST_OPENGL")
            ? SDL_CreateRenderer(window, "opengl") : SDL_CreateSoftwareRenderer(surface);
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

void send_backend_key(backend_fixture_t& backend, SDL_Keycode key, bool down,
                      SDL_Keymod modifiers = SDL_KMOD_NONE, bool repeat = false) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = SDL_GetWindowID(backend.window);
    event.key.key = key;
    event.key.mod = modifiers;
    event.key.down = down;
    event.key.repeat = repeat;
    expect_backend(nk_sdl_handle_event(backend.context, &event));
}
} // namespace

TEST_CASE("SDL ordinary letters never press their inactive editing shortcuts", "[ui][sdl-keyboard]") {
    backend_fixture_t backend;
    constexpr std::array<std::pair<SDL_Keycode, nk_keys>, 8> mappings{{
        {SDLK_A, NK_KEY_TEXT_SELECT_ALL}, {SDLK_Z, NK_KEY_TEXT_UNDO},
        {SDLK_R, NK_KEY_TEXT_REDO}, {SDLK_C, NK_KEY_COPY}, {SDLK_V, NK_KEY_PASTE},
        {SDLK_X, NK_KEY_CUT}, {SDLK_B, NK_KEY_TEXT_LINE_START}, {SDLK_E, NK_KEY_TEXT_LINE_END}}};
    for (const auto& [key, shortcut] : mappings) {
        nk_input_begin(backend.context);
        send_backend_key(backend, key, true);
        send_backend_key(backend, key, false);
        nk_input_end(backend.context);
        expect_backend(!nk_input_is_key_pressed(&backend.context->input, shortcut));
    }
}

TEST_CASE("SDL editing keys retain repeated down events", "[ui][sdl-keyboard]") {
    backend_fixture_t backend;
    nk_input_begin(backend.context);
    send_backend_key(backend, SDLK_BACKSPACE, true);
    expect_backend(nk_input_is_key_pressed(&backend.context->input, NK_KEY_BACKSPACE));
    nk_input_end(backend.context);
    nk_input_begin(backend.context);
    send_backend_key(backend, SDLK_BACKSPACE, true, SDL_KMOD_NONE, true);
    expect_backend(nk_input_is_key_pressed(&backend.context->input, NK_KEY_BACKSPACE));
    nk_input_end(backend.context);
}

TEST_CASE("SDL shortcut releases clear their control after modifier changes", "[ui][sdl-keyboard]") {
    backend_fixture_t backend;
    constexpr std::array<std::pair<SDL_Keycode, nk_keys>, 3> mappings{{
        {SDLK_A, NK_KEY_TEXT_SELECT_ALL}, {SDLK_LEFT, NK_KEY_TEXT_WORD_LEFT},
        {SDLK_RIGHT, NK_KEY_TEXT_WORD_RIGHT}}};
    for (const auto& [key, shortcut] : mappings) {
        nk_input_begin(backend.context);
        send_backend_key(backend, key, true, SDL_KMOD_CTRL);
        expect_backend(nk_input_is_key_pressed(&backend.context->input, shortcut));
        nk_input_end(backend.context);
        nk_input_begin(backend.context);
        send_backend_key(backend, key, false);
        expect_backend(!backend.context->input.keyboard.keys[shortcut].down);
        expect_backend(!nk_input_is_key_pressed(&backend.context->input, shortcut));
        nk_input_end(backend.context);
    }
}

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

namespace {
void overview_frame(backend_fixture_t& backend, std::uint64_t generation,
                    float width = 90, float phase = 0, float scale = 1, float marker = 55) {
    SDL_SetRenderScale(backend.renderer, scale, scale);
    SDL_SetRenderDrawColor(backend.renderer, 45, 35, 55, 255);
    SDL_RenderClear(backend.renderer);
    nk_input_begin(backend.context);
    nk_input_end(backend.context);
    std::array<wormedit::ui::compose_waveform_overview_row_view_t, 512> rows{};
    for (std::size_t index = 0; index < rows.size(); ++index)
        rows[index] = {(index + .5) / rows.size(), .1f + .8f * std::abs(std::sin(index * .173f))};
    wormedit::ui::compose_view_t view{};
    view.waveform_overview_rows.assign(rows.begin(), rows.end());

    view.waveform_overview_generation = generation;
    if (nk_begin(backend.context, "overview", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR)) {
        auto* canvas = nk_window_get_canvas(backend.context);
        const auto bounds = nk_rect(14 + phase, 30 + phase, width, 32);
        const auto background = nk_rgb(20, 25, 30), peak = nk_rgba(114, 222, 239, 190);
        if (!begin_compose_overview(backend.context, canvas, view, bounds, background, peak)) {
            nk_fill_rect(canvas, bounds, 0, background);
            for (const auto& row : rows) {
                const auto x = bounds.x + static_cast<float>(row.audio_fraction) * bounds.w;
                const auto center = bounds.y + bounds.h * .5f, half = bounds.h * .43f;
                nk_stroke_line(canvas, x, center - row.absolute_peak * half,
                    x, center + row.absolute_peak * half, 1, peak);
            }
            end_compose_overview(backend.context, canvas, bounds);
        }
        // Ordering regression: this dynamic marker must never enter cached pixels.
        nk_fill_rect(canvas, nk_rect(marker, 31, 2, 30), 0, nk_rgb(255, 0, 0));
    }
    nk_end(backend.context);
    render_sdl_backend(backend.context, NK_ANTI_ALIASING_ON);
}
auto backend_pixels(const backend_fixture_t& backend) -> std::vector<unsigned char> {
    auto* surface = SDL_RenderReadPixels(backend.renderer, nullptr);
    expect_backend(surface);
    auto* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    expect_backend(rgba);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(rgba->w) * rgba->h * 4);
    for (int y = 0; y < rgba->h; ++y)
        std::copy_n(static_cast<const unsigned char*>(rgba->pixels) + y * rgba->pitch,
            rgba->w * 4, pixels.data() + static_cast<std::size_t>(y) * rgba->w * 4);
    SDL_DestroySurface(rgba);
    SDL_DestroySurface(surface);
    return pixels;
}
}
TEST_CASE("SDL native window coordinates scale full coverage and scissors after resize", "[ui][sdl-backend]") {
    for (const int pixels : {128, 160, 192, 256}) {
        INFO("backbuffer=" << pixels);
        backend_fixture_t backend{pixels};
        for (const int points : {64, 128}) {
            expect_backend(set_window_render_coordinates(backend.renderer, points, points));
            const auto scale = static_cast<float>(pixels) / points;
            float sx{}, sy{};
            expect_backend(SDL_GetRenderScale(backend.renderer, &sx, &sy));
            expect_backend(sx == scale && sy == scale);
            int width{}, height{};
            SDL_RendererLogicalPresentation mode{};
            expect_backend(SDL_GetRenderLogicalPresentation(backend.renderer, &width, &height, &mode));
            expect_backend(mode == SDL_LOGICAL_PRESENTATION_DISABLED);
        }
        SDL_SetRenderDrawColor(backend.renderer, 255, 0, 255, 255);
        SDL_RenderClear(backend.renderer);
        nk_input_begin(backend.context);
        nk_input_end(backend.context);
        if (nk_begin(backend.context, "scaled", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR)) {
            auto* canvas = nk_window_get_canvas(backend.context);
            nk_push_scissor(canvas, nk_rect(-8192, -8192, 16384, 16384));
            nk_fill_rect(canvas, nk_rect(0, 0, 128, 128), 0, nk_rgb(20, 25, 30));
            // Extends beyond the right/bottom edges: the backend clamps in points,
            // then SDL scales the scissor exactly once to the physical viewport.
            nk_push_scissor(canvas, nk_rect(64, 80, 80, 80));
            nk_fill_rect(canvas, nk_rect(0, 0, 128, 128), 0, nk_rgb(240, 40, 50));
        }
        nk_end(backend.context);
        render_sdl_backend(backend.context, NK_ANTI_ALIASING_OFF);
        const auto image = backend_pixels(backend);
        expect_backend(image.size() == static_cast<std::size_t>(pixels * pixels * 4));
        for (int y = 0; y < pixels; ++y) for (int x = 0; x < pixels; ++x) {
            const bool clipped_fill = x >= pixels / 2 && y >= pixels * 5 / 8;
            const auto offset = static_cast<std::size_t>((y * pixels + x) * 4);
            INFO("pixel=" << x << "," << y);
            expect_backend(image[offset] == (clipped_fill ? 240 : 20));
            expect_backend(image[offset + 1] == (clipped_fill ? 40 : 25));
            expect_backend(image[offset + 2] == (clipped_fill ? 50 : 30));
        }
    }
}
TEST_CASE("SDL overview retained interior preserves pixels and warm uploads", "[ui][sdl-backend]") {
    backend_fixture_t backend;
    for (float scale : {1.f, 1.25f, 2.f}) for (float phase : {0.f, .25f, .75f}) {
        INFO("scale=" << scale << " phase=" << phase);
        overview_frame(backend, 1, 90, phase, scale);
        const auto direct = backend_pixels(backend);
        const auto cold = compose_overview_work(backend.context);
        expect_backend(cold.texture_bytes > 0);
        for (unsigned frame = 0; frame < 8; ++frame) overview_frame(backend, 1, 90, phase, scale);
        expect_backend(backend_pixels(backend) == direct);
        const auto warm = compose_overview_work(backend.context);
        expect_backend(warm.uploads == cold.uploads);
        expect_backend(warm.readbacks == cold.readbacks);
        expect_backend(warm.reused_frames == cold.reused_frames + 8);
        expect_backend(warm.texture_bytes <= 4096 * 256 * 4);
        expect_backend(warm.warm_vertices < warm.cold_vertices);
        if (std::getenv("WORMEDIT_TEST_OPENGL"))
            std::printf("overview scale=%.2f phase=%.2f cold_vertices=%zu warm_vertices=%zu warm_draws=%zu texture_bytes=%zu geometry_bytes=%zu peak_cpu_bytes=%zu cold_readback_ns=%llu warm_uploads=%llu\n",
                scale, phase, warm.cold_vertices, warm.warm_vertices, warm.warm_draw_calls,
                warm.texture_bytes, warm.geometry_bytes, warm.transient_bytes,
                static_cast<unsigned long long>(warm.cold_readback_ns),
                static_cast<unsigned long long>(warm.uploads - cold.uploads));
    }
}
TEST_CASE("SDL overview cache excludes dynamic markers", "[ui][sdl-backend]") {
    backend_fixture_t backend;
    overview_frame(backend, 1);
    const auto before = compose_overview_work(backend.context);
    overview_frame(backend, 1, 90, 0, 1, 65);
    const auto cached = backend_pixels(backend);
    expect_backend(compose_overview_work(backend.context).uploads == before.uploads);
    overview_frame(backend, 0, 90, 0, 1, 65);
    expect_backend(backend_pixels(backend) == cached);
}
TEST_CASE("SDL overview invalidates source size scale and windowless renderer resets", "[ui][sdl-backend]") {
    backend_fixture_t backend;
    overview_frame(backend, 1);
    const auto initial = compose_overview_work(backend.context).uploads;
    overview_frame(backend, 2);
    expect_backend(compose_overview_work(backend.context).uploads == initial + 1);
    overview_frame(backend, 2, 89.75f);
    expect_backend(compose_overview_work(backend.context).uploads == initial + 2);
    SDL_Event other_reset{};
    other_reset.type = SDL_EVENT_RENDER_TARGETS_RESET;
    other_reset.render.windowID = SDL_GetWindowID(backend.window) + 1;
    expect_backend(nk_sdl_handle_event(backend.context, &other_reset) == 0);
    overview_frame(backend, 2, 89.75f);
    expect_backend(compose_overview_work(backend.context).uploads == initial + 2);
    SDL_Event reset{};
    reset.type = SDL_EVENT_RENDER_TARGETS_RESET;
    expect_backend(nk_sdl_handle_event(backend.context, &reset) == 1);
    overview_frame(backend, 2, 89.75f);
    expect_backend(compose_overview_work(backend.context).uploads == initial + 3);
    overview_frame(backend, 2, 89.75f, 0, 1.25f);
    expect_backend(compose_overview_work(backend.context).uploads == initial + 4);
    const auto before = compose_overview_work(backend.context);
    overview_frame(backend, 0);
    expect_backend(compose_overview_work(backend.context).uploads == before.uploads);
}

namespace {
SDL_Surface* fail_overview_readback(SDL_Renderer*, const SDL_Rect*) { return nullptr; }
SDL_Surface* oversized_overview_readback(SDL_Renderer*, const SDL_Rect* rect) {
    return SDL_CreateSurface(rect->w + 1, rect->h, SDL_PIXELFORMAT_RGBA32);
}
SDL_Surface* unsupported_overview_readback(SDL_Renderer*, const SDL_Rect* rect) {
    return SDL_CreateSurface(rect->w, rect->h, SDL_PIXELFORMAT_RGBA128_FLOAT);
}
}
TEST_CASE("SDL overview failed source capture uses original strokes and recovers", "[ui][sdl-backend]") {
    backend_fixture_t backend;
    overview_frame(backend, 1);
    const auto before = compose_overview_work(backend.context);
    set_compose_overview_readback_for_testing(backend.context, fail_overview_readback);
    overview_frame(backend, 2, 85, .75f);
    const auto original = backend_pixels(backend);
    const auto failure = compose_overview_work(backend.context);
    expect_backend(failure.uploads == before.uploads);
    expect_backend(failure.failures == before.failures + 1);
    expect_backend(failure.texture_bytes == 0);
    expect_backend(failure.geometry_bytes == 0);
    overview_frame(backend, 0, 85, .75f);
    expect_backend(backend_pixels(backend) == original);
    set_compose_overview_readback_for_testing(backend.context, nullptr);
    overview_frame(backend, 2, 85, .75f);
    expect_backend(compose_overview_work(backend.context).uploads == before.uploads + 1);
    overview_frame(backend, 2, 85, .75f);
    expect_backend(compose_overview_work(backend.context).reused_frames == before.reused_frames + 1);
    expect_backend(backend_pixels(backend) == original);
}

TEST_CASE("SDL overview rejects oversized and unsupported readback surfaces", "[ui][sdl-backend]") {
    backend_fixture_t backend;
    for (const auto readback : {oversized_overview_readback, unsupported_overview_readback}) {
        set_compose_overview_readback_for_testing(backend.context, readback);
        const auto before = compose_overview_work(backend.context);
        overview_frame(backend, 1);
        const auto failed = backend_pixels(backend);
        const auto after = compose_overview_work(backend.context);
        expect_backend(after.failures == before.failures + 1);
        expect_backend(after.uploads == before.uploads);
        expect_backend(after.texture_bytes == 0);
        overview_frame(backend, 0);
        expect_backend(backend_pixels(backend) == failed);
    }
    set_compose_overview_readback_for_testing(backend.context, nullptr);
    overview_frame(backend, 1);
    expect_backend(compose_overview_work(backend.context).uploads == 1);
}

TEST_CASE("SDL overview warm cache rejects custom viewport and target without changing state", "[ui][sdl-backend]") {
    backend_fixture_t backend;
    overview_frame(backend, 1);
    const auto before = compose_overview_work(backend.context);
    const SDL_Rect viewport{0, 0, 120, 120};
    expect_backend(SDL_SetRenderViewport(backend.renderer, &viewport));
    overview_frame(backend, 1);
    SDL_Rect retained{};
    expect_backend(SDL_GetRenderViewport(backend.renderer, &retained));
    expect_backend(SDL_RenderViewportSet(backend.renderer));
    expect_backend(retained.w == viewport.w);
    expect_backend(retained.h == viewport.h);
    expect_backend(compose_overview_work(backend.context).readbacks == before.readbacks);
    expect_backend(compose_overview_work(backend.context).texture_bytes == 0);
    expect_backend(SDL_SetRenderViewport(backend.renderer, nullptr));
    overview_frame(backend, 1);
    auto* target = SDL_CreateTexture(backend.renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_TARGET, 128, 128);
    expect_backend(target);
    expect_backend(SDL_SetRenderTarget(backend.renderer, target));
    const auto warmed = compose_overview_work(backend.context);
    overview_frame(backend, 1);
    expect_backend(SDL_GetRenderTarget(backend.renderer) == target);
    expect_backend(compose_overview_work(backend.context).readbacks == warmed.readbacks);
    expect_backend(compose_overview_work(backend.context).texture_bytes == 0);
    expect_backend(SDL_SetRenderTarget(backend.renderer, nullptr));
    SDL_DestroyTexture(target);
}

TEST_CASE("SDL overview preserves production logical stretch pixels and state", "[ui][sdl-backend]") {
    backend_fixture_t backend;
    for (const int logical_size : {128, 102, 64}) for (const float phase : {0.f, .25f, .75f}) {
        INFO("logical_size=" << logical_size << " phase=" << phase);
        expect_backend(SDL_SetRenderLogicalPresentation(backend.renderer, logical_size, logical_size,
            SDL_LOGICAL_PRESENTATION_STRETCH));
        overview_frame(backend, 1, 90, phase);
        const auto direct = backend_pixels(backend);
        const auto cold = compose_overview_work(backend.context);
        expect_backend(cold.texture_bytes > 0);
        for (unsigned frame = 0; frame < 8; ++frame) overview_frame(backend, 1, 90, phase);
        expect_backend(backend_pixels(backend) == direct);
        const auto warm = compose_overview_work(backend.context);
        expect_backend(warm.uploads == cold.uploads);
        expect_backend(warm.readbacks == cold.readbacks);
        int width{}, height{};
        SDL_RendererLogicalPresentation mode{};
        expect_backend(SDL_GetRenderLogicalPresentation(backend.renderer, &width, &height, &mode));
        expect_backend(width == logical_size && height == logical_size);
        expect_backend(mode == SDL_LOGICAL_PRESENTATION_STRETCH);
        expect_backend(!SDL_RenderViewportSet(backend.renderer));
        float sx{}, sy{};
        expect_backend(SDL_GetRenderScale(backend.renderer, &sx, &sy));
        expect_backend(sx == 1 && sy == 1);
    }
}

namespace {
void counted_window(nk_context* context, const char* name, struct nk_rect bounds) {
    if (nk_begin(context, name, bounds, NK_WINDOW_NO_SCROLLBAR)) {
        auto* canvas = nk_window_get_canvas(context);
        nk_fill_rect(canvas, nk_rect(bounds.x + 4, bounds.y + 4, 20, 10), 3, nk_rgb(90, 140, 200));
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, name, NK_TEXT_LEFT);
    }
    nk_end(context);
}
void diagnostic_window(nk_context* context) {
    const diagnostic_window_scope_t diagnostic{context};
    if (nk_begin(context, "diagnostics", nk_rect(64, 0, 64, 128),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "frame", NK_TEXT_LEFT);
        const float points[]{70, 90, 90, 60, 110, 100, 120, 70};
        nk_stroke_polyline(nk_window_get_canvas(context), points, 4, 1, nk_rgb(240, 200, 60));
        nk_fill_rect(nk_window_get_canvas(context), nk_rect(70, 104, 50, 20), 0,
                     nk_rgb(250, 20, 20));
    }
    nk_end(context);
}
struct measured_work_t {
    wormedit::ui::frame_gpu_work_view_t application{};
    wormedit::ui::frame_gpu_work_view_t diagnostic{};
    bool diagnostic_available{};
};
auto measured_frame(backend_fixture_t& backend, bool diagnostics) -> measured_work_t {
    SDL_SetRenderDrawColor(backend.renderer, 0, 0, 0, 255);
    SDL_RenderClear(backend.renderer);
    nk_input_begin(backend.context);
    nk_input_end(backend.context);
    counted_window(backend.context, "before", nk_rect(0, 0, 64, 64));
    if (diagnostics) diagnostic_window(backend.context);
    counted_window(backend.context, "after", nk_rect(0, 64, 64, 64));
    auto& timer = wormedit::sys::frame_timing();
    timer.begin_frame();
    render_sdl_backend(backend.context, NK_ANTI_ALIASING_ON);
    timer.end_frame();
    wormedit::ui::frame_timing_view_t view;
    worm_ui_frame_timing_view(view);
    expect_backend(view.gpu_work_available);
    return {view.gpu_work_latest, view.diagnostic_gpu_work_latest,
            view.diagnostic_gpu_work_available != 0};
}
auto red_pixels(const std::vector<unsigned char>& pixels) -> std::size_t {
    std::size_t count = 0;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4)
        count += pixels[index] > 200 && pixels[index + 1] < 60 && pixels[index + 2] < 60;
    return count;
}
void expect_same_geometry(const wormedit::ui::frame_gpu_work_view_t& left, const wormedit::ui::frame_gpu_work_view_t& right) {
    INFO("vertices " << left.vertices << " vs " << right.vertices << ", indices " << left.indices
         << " vs " << right.indices << ", draws " << left.draw_calls << " vs " << right.draw_calls);
    expect_backend(left.vertices == right.vertices);
    expect_backend(left.indices == right.indices);
    expect_backend(left.draw_calls == right.draw_calls);
}
} // namespace

TEST_CASE("SDL backend records diagnostic window GPU work apart from the application",
          "[ui][sdl-backend]") {
    backend_fixture_t backend;
    // First frame: the diagnostic window sits between the application ones in z-order.
    const auto between = measured_frame(backend, true);
    expect_backend(red_pixels(backend_pixels(backend)) > 0);
    measured_frame(backend, false);
    const auto plain = measured_frame(backend, false);
    expect_backend(plain.application.vertices > 0 && plain.application.draw_calls > 0);
    expect_backend(!plain.diagnostic_available);
    expect_backend(red_pixels(backend_pixels(backend)) == 0);
    // Reopened, it is last in z-order; both orders leave the application counts unchanged.
    const auto last = measured_frame(backend, true);
    expect_backend(red_pixels(backend_pixels(backend)) > 0);
    expect_same_geometry(between.application, plain.application);
    expect_same_geometry(last.application, plain.application);
    for (const auto& frame : {between, last}) {
        expect_backend(frame.diagnostic_available);
        expect_backend(frame.diagnostic.vertices > 0 && frame.diagnostic.indices > 0);
        expect_backend(frame.diagnostic.draw_calls > 0);
    }
    expect_same_geometry(between.diagnostic, last.diagnostic);
    // Each pass tracks its own clip and texture state from its own first draw.
    expect_backend(last.diagnostic.clip_rect_changes < last.diagnostic.draw_calls);
    expect_backend(last.application.clip_rect_changes == plain.application.clip_rect_changes);
    expect_backend(last.application.texture_switches == plain.application.texture_switches);
}

TEST_CASE("SDL text input decodes multi-codepoint UTF-8 commits", "[frontend-state][backend][text-input]") {
    backend_fixture_t backend;
    nk_input_begin(backend.context);
    SDL_Event event{};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = SDL_GetWindowID(backend.window);
    event.text.text = "Aé🎵";
    expect_backend(nk_sdl_handle_event(backend.context, &event) == 1);
    const auto& keyboard = backend.context->input.keyboard;
    expect_backend(std::string_view(keyboard.text, keyboard.text_len) == "Aé🎵");
    event.text.text = nullptr;
    expect_backend(nk_sdl_handle_event(backend.context, &event) == 1);
    expect_backend(std::string_view(keyboard.text, keyboard.text_len) == "Aé🎵");
    nk_input_end(backend.context);
}

namespace {
struct BgmFrameOptions {
    std::uint64_t generation{1};
    std::size_t rows{1024};
    float gain{1};
    double begin{},end{1};
    nk_color background{nk_rgb(20,25,30)},peak{nk_rgba(114,222,239,190)};
};
auto bgm_waveform_frame(backend_fixture_t& backend, bool cached, float scale=1,
                         float phase=0, float marker=55,const BgmFrameOptions& options={}) -> bool {
    SDL_SetRenderScale(backend.renderer,scale,scale);
    SDL_SetRenderDrawColor(backend.renderer,45,35,55,255);
    SDL_RenderClear(backend.renderer);
    nk_input_begin(backend.context); nk_input_end(backend.context);
    wormedit::ui::compose_view_t view{};
    view.waveform_overview_generation=options.generation;
    for (std::size_t index=0; index<options.rows; ++index)
        view.bgm_bar_rows.push_back({options.begin+(index+.5)/options.rows*(options.end-options.begin),
            options.gain*(.1f+.8f*std::abs(std::sin(static_cast<float>(index*.173f+options.begin*1024*.173f))))});
    bool retained=false;
    if (nk_begin(backend.context,"bgm",nk_rect(0,0,128,128),NK_WINDOW_NO_SCROLLBAR)) {
        auto* canvas=nk_window_get_canvas(backend.context);
        const auto bounds=nk_rect(14+phase,30+phase,90,32);
        const auto peak=options.peak;
        nk_fill_rect(canvas,bounds,0,options.background);
        if (cached) retained=draw_compose_bgm_waveform(backend.context,canvas,view,bounds,options.begin,options.end,options.background,peak);
        if (!retained) for (const auto& row:view.bgm_bar_rows) {
            const auto x=bounds.x+static_cast<float>((row.audio_fraction-options.begin)/(options.end-options.begin))*bounds.w;
            nk_stroke_line(canvas,x,bounds.y+16-row.absolute_peak*32*.43f,
                x,bounds.y+16+row.absolute_peak*32*.43f,
                std::max(1.f,bounds.w/static_cast<float>(view.bgm_bar_rows.size())),peak);
        }
        nk_fill_rect(canvas,nk_rect(marker,31,2,30),0,nk_rgb(255,0,0));
    }
    nk_end(backend.context);
    render_sdl_backend(backend.context,NK_ANTI_ALIASING_ON);
    return retained;
}
void expect_bgm_pixels(const std::vector<unsigned char>& expected,
                       const std::vector<unsigned char>& actual,float scale=1) {
    expect_backend(expected.size()==actual.size());
    using namespace wormedit::ui::testing;
    field_pixels_t expected_pixels(expected.size()/4),actual_pixels(actual.size()/4);
    std::memcpy(expected_pixels.data(),expected.data(),expected.size());
    std::memcpy(actual_pixels.data(),actual.data(),actual.size());
    const auto width=static_cast<int>(std::sqrt(expected_pixels.size()));
    const auto difference=static_pixel_difference(expected_pixels,actual_pixels,width,width,scale);
    INFO("interior=" << difference.interior << " coverage=" << difference.coverage);
    expect_backend(difference.interior==0);
    expect_backend(difference.coverage==0);
}
}
TEST_CASE("SDL BGM waveform reuses shared envelope ring with live overlays and whole-window pixels",
          "[ui][sdl-backend][bgm-ring]") {
    backend_fixture_t backend(256);
    for (const float scale : {1.f,1.25f,2.f}) for (const float phase : {0.f,.25f,.75f}) {
        INFO("scale=" << scale << " phase=" << phase);
        bgm_waveform_frame(backend,false,scale,phase);
        const auto direct=backend_pixels(backend);
        expect_backend(bgm_waveform_frame(backend,true,scale,phase));
        expect_bgm_pixels(direct,backend_pixels(backend),scale);
        const auto before=compose_bgm_waveform_work(backend.context);
        expect_backend(bgm_waveform_frame(backend,true,scale,phase));
        expect_bgm_pixels(direct,backend_pixels(backend),scale);
        const auto after=compose_bgm_waveform_work(backend.context);
        expect_backend(after.reused_frames==before.reused_frames+1);
        expect_backend(after.target_rasters==before.target_rasters);
        expect_backend(after.warm_vertices<=8);
    }
}
TEST_CASE("SDL BGM waveform keeps the playhead live outside its shared ring",
          "[ui][sdl-backend][bgm-ring]") {
    backend_fixture_t backend;
    expect_backend(bgm_waveform_frame(backend,true));
    bgm_waveform_frame(backend,false,1,0,65);
    const auto direct=backend_pixels(backend);
    expect_backend(bgm_waveform_frame(backend,true,1,0,65));
    expect_bgm_pixels(direct,backend_pixels(backend));
    expect_backend(compose_bgm_waveform_work(backend.context).readbacks==0);
}
TEST_CASE("SDL BGM ring invalidates source background style and keeps selected overview independent",
          "[ui][sdl-backend][bgm-ring]") {
    backend_fixture_t backend;
    expect_backend(bgm_waveform_frame(backend,true));
    auto previous=compose_bgm_waveform_work(backend.context).target_rasters;
    for (const auto options : {BgmFrameOptions{.generation=2},
            BgmFrameOptions{.gain=.7f},BgmFrameOptions{.background=nk_rgb(35,45,55)},
            BgmFrameOptions{.peak=nk_rgba(230,120,70,190)}}) {
        bgm_waveform_frame(backend,false,1,0,55,options);
        const auto direct=backend_pixels(backend);
        expect_backend(bgm_waveform_frame(backend,true,1,0,55,options));
        expect_bgm_pixels(direct,backend_pixels(backend));
        const auto work=compose_bgm_waveform_work(backend.context);
        expect_backend(work.target_rasters==previous+1);
        previous=work.target_rasters;
    }
    overview_frame(backend,1);
    const auto selected=compose_overview_work(backend.context);
    expect_backend(bgm_waveform_frame(backend,true));
    expect_backend(compose_overview_work(backend.context).uploads==selected.uploads);
}
TEST_CASE("SDL BGM ring rejects transparent oversized and custom-target cases without readback",
          "[ui][sdl-backend][bgm-ring]") {
    backend_fixture_t backend;
    expect_backend(!bgm_waveform_frame(backend,true,1,0,55,{.rows=2049}));
    expect_backend(!bgm_waveform_frame(backend,true,1,0,55,{.background=nk_rgba(20,25,30,200)}));
    expect_backend(!bgm_waveform_frame(backend,true,1,0,55,{.gain=2}));
    auto* target=SDL_CreateTexture(backend.renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,128,128);
    expect_backend(target);
    expect_backend(SDL_SetRenderTarget(backend.renderer,target));
    expect_backend(!bgm_waveform_frame(backend,true));
    expect_backend(SDL_GetRenderTarget(backend.renderer)==target);
    expect_backend(SDL_SetRenderTarget(backend.renderer,nullptr));
    SDL_DestroyTexture(target);
    expect_backend(compose_bgm_waveform_work(backend.context).readbacks==0);
}
TEST_CASE("SDL BGM ring stable frames allocate no new Nuklear buffers",
          "[ui][sdl-backend][bgm-ring]") {
    backend_fixture_t backend;
    for (unsigned frame=0;frame<8;++frame) expect_backend(bgm_waveform_frame(backend,true));
    const auto allocations=backend.allocations.calls;
    expect_backend(bgm_waveform_frame(backend,true));
    expect_backend(backend.allocations.calls==allocations);
    expect_backend(compose_bgm_waveform_work(backend.context).warm_vertices<=8);
}
TEST_CASE("SDL BGM ring admits the full published row limit and preserves thick peak artwork",
          "[ui][sdl-backend][bgm-ring]") {
    backend_fixture_t backend;
    for (const auto count : {std::size_t{16},std::size_t{2048}}) {
        INFO("rows=" << count);
        const BgmFrameOptions options{.rows=count};
        bgm_waveform_frame(backend,false,1,0,55,options);
        const auto direct=backend_pixels(backend);
        expect_backend(bgm_waveform_frame(backend,true,1,0,55,options));
        expect_bgm_pixels(direct,backend_pixels(backend));
    }
}

TEST_CASE("SDL BGM ring canonicalizes ULP window noise while preserving real pan changes",
          "[ui][sdl-backend][bgm-ring]") {
    backend_fixture_t backend;
    expect_backend(bgm_waveform_frame(backend,true));
    const auto before=compose_bgm_waveform_work(backend.context);
    const BgmFrameOptions noise{.begin=1.1102230246251565e-16};
    bgm_waveform_frame(backend,false,1,0,55,noise);
    const auto direct=backend_pixels(backend);
    expect_backend(bgm_waveform_frame(backend,true,1,0,55,noise));
    expect_bgm_pixels(direct,backend_pixels(backend));
    const auto after=compose_bgm_waveform_work(backend.context);
    expect_backend(after.target_rasters==before.target_rasters);
    expect_backend(after.warm_vertices<=8);
    expect_backend(!bgm_waveform_frame(backend,true,1,0,55,{.begin=.005}));
    const BgmFrameOptions changed{.begin=.25,.end=.75};
    bgm_waveform_frame(backend,false,1,0,55,changed);
    const auto changed_direct=backend_pixels(backend);
    expect_backend(bgm_waveform_frame(backend,true,1,0,55,changed));
    expect_bgm_pixels(changed_direct,backend_pixels(backend));
    expect_backend(compose_bgm_waveform_work(backend.context).target_rasters==after.target_rasters+1);
}

namespace {
struct retirement_canvas_t final : compose_canvas_cache_t {
    int& retired;
    explicit retirement_canvas_t(int& count) : retired(count) {}
    ~retirement_canvas_t() override { ++retired; }
};
auto warmed_primary_field(backend_fixture_t& fixture) -> wormedit::ui::field::renderer_t& {
    nk_sdl_begin_frame(fixture.context);
    auto& field = begin_compose_field(fixture.context,{0,0,32,32});
    field.rect(1,{0,0,32,32},{210,30,40,255});
    wormedit::ui::field::tile_request_t request;
    request.key.pass=1; request.viewport={0,0,32,32};
    field.capture_tiles(request,0);
    expect_backend(field.tile_work().retained_bytes>0);
    nk_sdl_discard_frame(fixture.context);
    return field;
}
void expect_direct_field(wormedit::ui::field::renderer_t& field) {
    expect_backend(!field.can_retain_tiles());
    expect_backend(field.tile_work().retained_bytes==0 && field.tile_work().prepared==0);
}
}
TEST_CASE("Comparison field scope retires warmed owners and secondary fields stay direct", "[ui][backend][compare-retention]") {
    int retired{};
    backend_fixture_t fixture;
    auto& primary = warmed_primary_field(fixture);
    compose_canvas_cache(fixture.context)=std::make_unique<retirement_canvas_t>(retired);
    nk_sdl_begin_frame(fixture.context);
    {
        const direct_compose_fields_scope_t direct(fixture.context);
        expect_backend(retired==1 && primary.tile_work().retained_bytes==0);
        expect_direct_field(begin_compose_field(fixture.context,{0,0,32,32}));
        expect_backend(!compose_canvas_cache(fixture.context));
        expect_direct_field(begin_compose_field(fixture.context,{40,0,32,32}));
    }
    nk_sdl_discard_frame(fixture.context);
    nk_sdl_begin_frame(fixture.context);
    expect_backend(begin_compose_field(fixture.context,{0,0,32,32}).can_retain_tiles());
    expect_direct_field(begin_compose_field(fixture.context,{40,0,32,32}));
}

TEST_CASE("Comparison field retirement preserves earlier queued texture references until the next frame", "[ui][backend][compare-retention]") {
    int retired{};
    backend_fixture_t fixture;
    warmed_primary_field(fixture);
    nk_sdl_begin_frame(fixture.context);
    nk_input_begin(fixture.context); nk_input_end(fixture.context);
    expect_backend(nk_begin(fixture.context,"queued field",nk_rect(0,0,128,128),NK_WINDOW_NO_SCROLLBAR));
    auto* canvas=nk_window_get_canvas(fixture.context);
    auto& primary=begin_compose_field(fixture.context,{0,0,32,32});
    wormedit::ui::field::tile_request_t request;
    request.key.pass=1; request.viewport={0,0,32,32};
    expect_backend(primary.composite_tiles(request));
    compose_canvas_cache(fixture.context)=std::make_unique<retirement_canvas_t>(retired);
    insert_compose_field(fixture.context,canvas,nk_rect(0,0,32,32));
    {
        const direct_compose_fields_scope_t direct(fixture.context);
        expect_backend(retired==0 && primary.tile_work().retained_bytes>0 && !primary.indices().empty());
        auto& second=begin_compose_field(fixture.context,{40,0,32,32});
        expect_direct_field(second);
        second.rect(1,{40,0,32,32},{10,200,30,255});
        insert_compose_field(fixture.context,canvas,nk_rect(40,0,32,32));
    }
    nk_end(fixture.context);
    nk_sdl_render(fixture.context,NK_ANTI_ALIASING_OFF);
    const auto pixels=backend_pixels(fixture);
    expect_backend(pixels[(8*128+8)*4]==210 && retired==0);
    nk_sdl_begin_frame(fixture.context);
    expect_backend(retired==1 && primary.tile_work().retained_bytes==0);
}
