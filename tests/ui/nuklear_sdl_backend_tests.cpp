#include "ui/nuklear/sdl_backend.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include "ui/frontend.h"
#include "sys/frame_timing.h"
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <source_location>
#include <limits>

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

    backend_fixture_t() {
        expect_backend(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("backend test", 128, 128, SDL_WINDOW_HIDDEN);
        expect_backend(window);
        surface = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
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

namespace {
void overview_frame(backend_fixture_t& backend, std::uint64_t generation,
                    float width = 90, float phase = 0, float scale = 1, float marker = 55) {
    SDL_SetRenderScale(backend.renderer, scale, scale);
    SDL_SetRenderDrawColor(backend.renderer, 45, 35, 55, 255);
    SDL_RenderClear(backend.renderer);
    nk_input_begin(backend.context);
    nk_input_end(backend.context);
    std::array<WormUiComposeWaveformOverviewRowView, 512> rows{};
    for (std::size_t index = 0; index < rows.size(); ++index)
        rows[index] = {(index + .5) / rows.size(), .1f + .8f * std::abs(std::sin(index * .173f))};
    WormUiComposeView view{};
    view.waveform_overview_rows = rows.data();
    view.waveform_overview_row_count = rows.size();
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
    const uncounted_window_scope_t uncounted{context};
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
auto measured_frame(backend_fixture_t& backend, bool diagnostics) -> WormUiFrameGpuWork {
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
    const auto view = worm_ui_frame_timing_view();
    expect_backend(view.gpu_work_available);
    return view.gpu_work_latest;
}
auto red_pixels(const std::vector<unsigned char>& pixels) -> std::size_t {
    std::size_t count = 0;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4)
        count += pixels[index] > 200 && pixels[index + 1] < 60 && pixels[index + 2] < 60;
    return count;
}
void expect_same_geometry(const WormUiFrameGpuWork& left, const WormUiFrameGpuWork& right) {
    INFO("vertices " << left.vertices << " vs " << right.vertices << ", indices " << left.indices
         << " vs " << right.indices << ", draws " << left.draw_calls << " vs " << right.draw_calls);
    expect_backend(left.vertices == right.vertices);
    expect_backend(left.indices == right.indices);
    expect_backend(left.draw_calls == right.draw_calls);
}
} // namespace

TEST_CASE("SDL backend draws uncounted windows without recording their GPU work",
          "[ui][sdl-backend]") {
    backend_fixture_t backend;
    // First frame: the diagnostic window sits between the counted ones in z-order.
    const auto between = measured_frame(backend, true);
    expect_backend(red_pixels(backend_pixels(backend)) > 0);
    measured_frame(backend, false);
    const auto plain = measured_frame(backend, false);
    expect_backend(plain.vertices > 0 && plain.draw_calls > 0);
    expect_backend(red_pixels(backend_pixels(backend)) == 0);
    // Reopened, it is last in z-order; both orders must leave the counts unchanged.
    const auto last = measured_frame(backend, true);
    expect_backend(red_pixels(backend_pixels(backend)) > 0);
    expect_same_geometry(between, plain);
    expect_same_geometry(last, plain);
}
