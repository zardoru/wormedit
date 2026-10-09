#include "ui/nuklear/sdl_backend.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <source_location>
using wormedit::ui::field::renderer_t;
namespace {
void expect_field(bool condition, const std::source_location location = std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}
struct field_submission_fixture_t {
    SDL_Window* window{};
    SDL_Surface* surface{};
    SDL_Renderer* renderer{};
    nk_context* context{};
    field_submission_fixture_t() {
        SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);
        if (!SDL_Init(SDL_INIT_VIDEO)) return;
        window = SDL_CreateWindow("clipped stem", 128, 128, SDL_WINDOW_HIDDEN);
        surface = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
        if (!window || !surface) return;
        renderer = SDL_CreateSoftwareRenderer(surface);
        if (!renderer) return;
        context = nk_sdl_init(window, renderer, nk_sdl_allocator());
        if (context) nk_sdl_style_set_debug_font(context);
    }
    ~field_submission_fixture_t() {
        if (context) nk_sdl_shutdown(context);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (surface) SDL_DestroySurface(surface);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
}

#include "ui/nuklear/compose_canvas.h"
namespace {
void check_clipped_wide_stem_canvas(bool selected) {
    field_submission_fixture_t fixture;
    expect_field(fixture.context != nullptr);
    WormUiComposeBgmLaneView lane{.bounds = {80, 0, 128, 128}, .source_lane_id = 44,
        .stem = true, .width_pixels = 96, .resize_edge_pixels = 128};
    const std::array<WormUiComposeWaveformRowView, 2> rows{{
        {.y_pixels = 40, .absolute_peak = {1, 1}, .rms = {.5f, .5f}},
        {.y_pixels = 41, .absolute_peak = {1, 1}, .rms = {.5f, .5f}}}};
    WormUiComposeStemWaveformView stem{.stem_key = 45, .source_lane_id = 44,
        .rows = rows.data(), .row_count = rows.size()};
    WormUiComposeView view{};
    view.scene_available = true;
    view.viewport_width_pixels = 128;
    view.viewport_height_pixels = 128;
    view.playable_field_left_pixels = 42;
    view.playable_field_right_pixels = 80;
    view.bgm_lanes = &lane;
    view.bgm_lane_count = 1;
    if (selected) {
        view.channel_waveform_source_lane_id = 44;
        view.channel_waveform_rows = rows.data();
        view.channel_waveform_row_count = rows.size();
    } else {
        view.stem_waveforms = &stem;
        view.stem_waveform_count = 1;
    }
    nk_input_begin(fixture.context);
    nk_input_end(fixture.context);
    expect_field(nk_begin(fixture.context, "wide stem", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR));
    draw_compose_scene_canvas(fixture.context, &view, nk_window_get_canvas(fixture.context), nk_rect(0, 0, 128, 128));
    nk_end(fixture.context);
    const auto& field = wormedit::ui::nuklear::compose_field(fixture.context);
    bool waveform{};
    for (const auto& batch : field.batches()) {
        if (batch.layer != 6 || batch.clip.x != 80 || batch.clip.w != 48) continue;
        waveform = true;
        float right{};
        for (std::size_t index = batch.index_offset; index < batch.index_offset + batch.index_count; ++index)
            right = std::max(right, field.vertices()[field.indices()[index]].position.x);
        expect_field(right > 120 && right < 124.5f);
    }
    expect_field(waveform);
}
}
TEST_CASE("Widened stem envelopes preserve true scale and clip to their visible lane",
          "[ui][field][compose][stem-lane-width][isolated-software-render]") {
    check_clipped_wide_stem_canvas(false);
    check_clipped_wide_stem_canvas(true);
}

namespace {
auto stem_header_glyphs(const renderer_t& field) -> std::vector<SDL_Vertex> {
    std::vector<SDL_Vertex> result;
    for (const auto& batch : field.batches()) {
        if (batch.layer != 3) continue;
        for (std::size_t index = batch.index_offset; index < batch.index_offset + batch.index_count; ++index) {
            const auto& vertex = field.vertices()[field.indices()[index]];
            constexpr auto unit = 1.0f / 255.0f;
            if (vertex.color.r == 211.0f * unit && vertex.color.g == 175.0f * unit &&
                vertex.color.b == 226.0f * unit) result.push_back(vertex);
        }
    }
    return result;
}
void check_stem_header_golden(double width, bool stem, const char* expected) {
    field_submission_fixture_t fixture;
    expect_field(fixture.context != nullptr);
    WormUiComposeBgmLaneView lane{.bounds = {32, 0, 32 + width, 128}, .source_lane_id = 44,
        .stem = stem, .width_pixels = width, .resize_edge_pixels = 32 + width,
        .filename = "音楽1234567890123456789.ogg"};
    WormUiComposeView view{};
    view.scene_available = true;
    view.viewport_width_pixels = 128;
    view.viewport_height_pixels = 128;
    view.playable_field_left_pixels = 42;
    view.playable_field_right_pixels = 32;
    view.bgm_lanes = &lane;
    view.bgm_lane_count = 1;
    nk_input_begin(fixture.context);
    nk_input_end(fixture.context);
    expect_field(nk_begin(fixture.context, "stem label", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR));
    draw_compose_scene_canvas(fixture.context, &view, nk_window_get_canvas(fixture.context), nk_rect(0, 0, 128, 128));
    nk_end(fixture.context);
    const auto actual = stem_header_glyphs(wormedit::ui::nuklear::compose_field(fixture.context));
    renderer_t reference;
    reference.begin({0, 0, 128, 128});
    reference.text(3, {34, 2}, expected, {211, 175, 226, 255},
        wormedit::ui::nuklear::compose_field_font(fixture.context->style.font));
    const auto golden = stem_header_glyphs(reference);
    INFO("width " << width << " stem " << stem << " font " << fixture.context->style.font->height <<
         " actual glyph indices " << actual.size() << " expected " << golden.size());
    expect_field(!actual.empty() && actual.size() == golden.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
        expect_field(actual[index].position.x == golden[index].position.x);
        expect_field(actual[index].position.y == golden[index].position.y);
        expect_field(actual[index].tex_coord.x == golden[index].tex_coord.x);
        expect_field(actual[index].tex_coord.y == golden[index].tex_coord.y);
    }
}
}
TEST_CASE("Stem headers draw UTF8 safe ellipsis while narrow and packed labels retain BGM",
          "[ui][field][compose][stem-lane-width][isolated-software-render]") {
    check_stem_header_golden(96, true, "音楽12345678…");
    check_stem_header_golden(36, true, "BGM ");
    check_stem_header_golden(96, false, "BGM 1");
}

namespace {
void check_bgm_tail_with_offscreen_head(bool selected) {
    field_submission_fixture_t fixture;
    expect_field(fixture.context != nullptr);
    WormUiComposeBgmLaneView lane{.bounds = {80, 0, 120, 128}, .source_lane_id = 44};
    WormUiComposeBgmEventView event{.bounds = {80, -100, 120, 110}, .event_id = 50,
        .source_lane_id = 44, .sound_id = 1, .has_sound = true, .selected = selected,
        .has_tail = true, .sound_label = "offscreen head", .head_top_pixels = -100,
        .head_bottom_pixels = -90};
    WormUiComposeView view{};
    view.scene_available = true;
    view.viewport_width_pixels = view.viewport_height_pixels = 128;
    view.playable_field_left_pixels = 42;
    view.playable_field_right_pixels = 80;
    view.bgm_lanes = &lane;
    view.bgm_lane_count = 1;
    view.bgm_events = &event;
    view.bgm_event_count = 1;
    nk_input_begin(fixture.context);
    nk_input_end(fixture.context);
    expect_field(nk_begin(fixture.context, "BGM tail", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR));
    draw_compose_scene_canvas(fixture.context, &view, nk_window_get_canvas(fixture.context), nk_rect(0, 0, 128, 128));
    nk_end(fixture.context);
    const auto& field = wormedit::ui::nuklear::compose_field(fixture.context);
    const auto body = std::ranges::any_of(field.vertices(), [](const auto& vertex) {
        return vertex.position.x >= 93 && vertex.position.x <= 107 && vertex.position.y >= 100 &&
               std::abs(vertex.color.r - 177.0f / 255.0f) < .001f &&
               std::abs(vertex.color.g - 116.0f / 255.0f) < .001f;
    });
    expect_field(body);
}
}
TEST_CASE("BGM tail painter clips the body independently from an offscreen head",
          "[ui][field][compose][t423-tail][isolated-software-render]") {
    check_bgm_tail_with_offscreen_head(false);
    check_bgm_tail_with_offscreen_head(true);
}
