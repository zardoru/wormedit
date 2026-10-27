#include "pixel_comparison.h"
#include "sys/compose/views/compose_view.h"
#include "sys/project/workspace_view.h"
#include "ui/compose_timing_geometry.h"
#include "ui/nuklear/app/sdl_backend.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/compose/compose_export_cuts.h"
#include "ui/nuklear/project/compare_panel.h"
#include <wormedit/core/export_cut_axis.h>
#include <wormedit/core/marker_editor.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <source_location>
#include <cstring>
#include <cstdio>
#include <utility>
#include <vector>
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
    explicit field_submission_fixture_t(bool window_backed = false, int width = 128, int height = 128) {
        SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);
        if (!SDL_Init(SDL_INIT_VIDEO)) return;
        window = SDL_CreateWindow("clipped stem", width, height, SDL_WINDOW_HIDDEN);
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (!window || !surface) return;
        renderer = window_backed ? SDL_CreateRenderer(window, "software")
                                 : SDL_CreateSoftwareRenderer(surface);
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

#include "ui/nuklear/compose/compose_canvas.h"
#include "ui/nuklear/audio/audio_panel.h"
#include "ui/nuklear/compose/compose_state.h"
namespace {
void check_clipped_wide_stem_canvas(bool selected) {
    field_submission_fixture_t fixture;
    expect_field(fixture.context != nullptr);
    wormedit::ui::compose_bgm_lane_view_t lane{.bounds = {80, 0, 128, 128}, .source_lane_id = wormedit::object_id_t{44},
        .stem = true, .width_pixels = 96, .resize_edge_pixels = 128};
    const std::array<wormedit::ui::compose_waveform_row_view_t, 2> rows{{
        {.y_pixels = 40, .absolute_peak = {1, 1}, .rms = {.5f, .5f}},
        {.y_pixels = 41, .absolute_peak = {1, 1}, .rms = {.5f, .5f}}}};
    wormedit::ui::compose_stem_waveform_view_t stem{.stem_key = 45, .source_lane_id = wormedit::object_id_t{44},
        .rows = rows};
    wormedit::ui::compose_view_t view{};
    view.scene_available = true;
    view.viewport_width_pixels = 128;
    view.viewport_height_pixels = 128;
    view.playable_field_left_pixels = 42;
    view.playable_field_right_pixels = 80;
    view.bgm_lanes = {lane};

    if (selected) {
        view.channel_waveform_source_lane_id = wormedit::object_id_t{44};
        view.channel_waveform_rows.assign(rows.begin(), rows.end());

    } else {
        view.stem_waveforms = {stem};

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
void check_hitsound_note_contrast(bool light) {
    INFO("light note fill: " << light);
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    const wormedit::osu_hitsample_t sample{.whistle = true, .finish = true, .clap = true,
        .normal_set = wormedit::osu_sample_set_t::soft, .addition_set = wormedit::osu_sample_set_t::drum};
    const auto hitsound = wormedit::ui::hitsound_note_view_t::from(sample, {}, "");
    wormedit::ui::compose_note_view_t note{};
    note.bounds = {10, 40, 110, 62};
    note.note_id.value = 1;
    note.lane_id.value = 1;
    note.hitsound = &hitsound;
    note.semantic_color_visible = true;
    note.semantic_red = note.semantic_green = note.semantic_blue = light ? 240 : 20;
    note.semantic_alpha = 255;
    wormedit::ui::compose_view_t view{};
    view.scene_available = true;
    view.viewport_width_pixels = view.viewport_height_pixels = 128;
    view.note_endpoints = {note};
    auto& color_view = worm_ui_nuklear_compose_state().input_queue.color_view;
    const auto previous = color_view;
    color_view = worm_ui_nuklear_compose_color_view_t::layer;
    nk_input_begin(fixture.context);
    nk_input_end(fixture.context);
    REQUIRE(nk_begin(fixture.context, "hitsound contrast", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR));
    draw_compose_scene_canvas(fixture.context, &view, nk_window_get_canvas(fixture.context), nk_rect(0, 0, 128, 128));
    nk_end(fixture.context);
    color_view = previous;
    const auto& field = wormedit::ui::nuklear::compose_field(fixture.context);
    const auto expected = light ? 16.0f/255 : 248.0f/255;
    std::size_t ink_vertices{};
    for (const auto& batch : field.batches()) {
        if (batch.layer != 2) continue;
        for (std::size_t index = batch.index_offset; index < batch.index_offset+batch.index_count; ++index) {
            const auto& vertex = field.vertices()[field.indices()[index]];
            if (std::abs(vertex.color.r-expected) < .5f/255 && vertex.position.x > 20 && vertex.position.x < 100 &&
                vertex.position.y > 42 && vertex.position.y < 60) ++ink_vertices;
        }
    }
    CHECK(ink_vertices > 100);
}
} // namespace

TEST_CASE("Hitsound note icons invert their ink against light and dark note fills",
          "[ui][field][hitsound][isolated-software-render]") {
    check_hitsound_note_contrast(false);
    check_hitsound_note_contrast(true);
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
void check_stem_header_golden(double width, bool stem, const char* expected,
                              const char* filename = "音楽1234567890123456789.ogg") {
    field_submission_fixture_t fixture;
    expect_field(fixture.context != nullptr);
    wormedit::ui::compose_bgm_lane_view_t lane{.bounds = {32, 0, 32 + width, 128}, .source_lane_id = wormedit::object_id_t{44},
        .stem = stem, .width_pixels = width, .resize_edge_pixels = 32 + width,
        .filename = filename};
    wormedit::ui::compose_view_t view{};
    view.scene_available = true;
    view.viewport_width_pixels = 128;
    view.viewport_height_pixels = 128;
    view.playable_field_left_pixels = 42;
    view.playable_field_right_pixels = 32;
    view.bgm_lanes = {lane};

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
TEST_CASE("Stem headers retain useful UTF8 filenames and compact BGM numbers",
          "[ui][field][compose][stem-lane-width][isolated-software-render]") {
    check_stem_header_golden(96, true, "音楽1234…");
    check_stem_header_golden(36, true, "音楽1…");
    check_stem_header_golden(96, false, "1");
    check_stem_header_golden(36, false, "1");
    check_stem_header_golden(36, true, "1", "");
}

namespace {
void check_bgm_tail_with_offscreen_head(bool selected) {
    field_submission_fixture_t fixture;
    expect_field(fixture.context != nullptr);
    wormedit::ui::compose_bgm_lane_view_t lane{.bounds = {80, 0, 120, 128}, .source_lane_id = wormedit::object_id_t{44}};
    wormedit::ui::compose_bgm_event_view_t event{.bounds = {80, -100, 120, 110}, .event_id = wormedit::object_id_t{50},
        .source_lane_id = wormedit::object_id_t{44}, .sound_id = wormedit::resource_id_t{1}, .has_sound = true, .selected = selected,
        .has_tail = true, .sound_label = "offscreen head", .head_top_pixels = -100,
        .head_bottom_pixels = -90};
    wormedit::ui::compose_view_t view{};
    view.scene_available = true;
    view.viewport_width_pixels = view.viewport_height_pixels = 128;
    view.playable_field_left_pixels = 42;
    view.playable_field_right_pixels = 80;
    view.bgm_lanes = {lane};

    view.bgm_events = {event};

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

namespace {
auto fit_overlay_glyphs(bool offset, bool invalid) -> std::vector<SDL_FPoint> {
    field_submission_fixture_t fixture;
    expect_field(fixture.context != nullptr);
    wormedit::ui::compose_view_t view{};
    view.scene_available = true;
    view.viewport_width_pixels = 128;
    view.viewport_height_pixels = 128;
    view.beat_fit_drag_active = !offset;
    view.offset_fit_drag_active = offset;
    view.beat_fit_original_bpm_numerator = invalid ? 120 : 0;
    view.beat_fit_proposed_bpm_numerator = invalid ? 121 : 0;
    view.beat_fit_original_bpm_denominator = invalid ? 0 : 1;
    view.beat_fit_proposed_bpm_denominator = invalid ? 0 : 1;
    view.waveform_audio_offset_numerator = invalid ? 3 : 0;
    view.waveform_proposed_audio_offset_numerator = invalid ? 4 : 0;
    view.waveform_audio_offset_denominator = invalid ? 0 : 1;
    view.waveform_proposed_audio_offset_denominator = invalid ? 0 : 1;
    nk_input_begin(fixture.context);
    nk_input_end(fixture.context);
    expect_field(nk_begin(fixture.context, "fit overlay", nk_rect(0, 0, 128, 128), NK_WINDOW_NO_SCROLLBAR));
    draw_compose_scene_canvas(fixture.context, &view, nk_window_get_canvas(fixture.context), nk_rect(0, 0, 128, 128));
    nk_end(fixture.context);
    const auto& field = wormedit::ui::nuklear::compose_field(fixture.context);
    std::vector<SDL_FPoint> glyphs;
    for (const auto& vertex : field.vertices()) {
        constexpr auto unit = 1.0f / 255.0f;
        const auto red = offset ? 148.0f : 255.0f;
        const auto green = offset ? 229.0f : 219.0f;
        const auto blue = offset ? 238.0f : 148.0f;
        if (vertex.color.r == red * unit && vertex.color.g == green * unit &&
            vertex.color.b == blue * unit) glyphs.push_back(vertex.tex_coord);
    }
    return glyphs;
}
}

TEST_CASE("Fit overlays display zero for invalid rational denominators",
          "[ui][field][shared-text][isolated-software-render]") {
    for (const bool offset : {false, true}) {
        const auto zero = fit_overlay_glyphs(offset, false);
        const auto invalid = fit_overlay_glyphs(offset, true);
        expect_field(!zero.empty());
        expect_field(invalid.size() == zero.size());
        for (std::size_t index = 0; index < zero.size(); ++index) {
            expect_field(invalid[index].x == zero[index].x);
            expect_field(invalid[index].y == zero[index].y);
        }
    }
}

TEST_CASE("Audio overview rendering guards a zero sample rate", "[ui][shared-text][isolated-software-render]") {
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context != nullptr);
    wormedit::ui::audio_slices_view_t view{};
    view.visible = view.has_selected_asset = view.waveform_available = true;
    view.total_frames = 480000;
    wormedit::ui::workspace_action_t action{};
    nk_input_begin(fixture.context);
    nk_input_end(fixture.context);
    REQUIRE(nk_begin(fixture.context, "audio overview", nk_rect(0, 0, 1280, 900), NK_WINDOW_NO_SCROLLBAR));
    worm_ui_nuklear_render_audio_slices(fixture.context, &view, &action, false, false);
    nk_end(fixture.context);
    bool has_zero_seconds{};
    const nk_command* command;
    nk_foreach(command, fixture.context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        const auto label = std::string_view(text->string, static_cast<std::size_t>(text->length));
        if (label.find("Stereo overview · 0.00 seconds · 0 Hz") != std::string_view::npos)
            has_zero_seconds = true;
    }
    CHECK(has_zero_seconds);
}

namespace {
void draw_static_work_frame(field_submission_fixture_t& fixture, const wormedit::ui::compose_view_t& view) {
    nk_sdl_begin_frame(fixture.context);
    nk_clear(fixture.context);
    nk_input_begin(fixture.context);
    nk_input_end(fixture.context);
    const auto viewport = nk_rect(0, 0, static_cast<float>(fixture.surface->w), static_cast<float>(fixture.surface->h));
    REQUIRE(nk_begin(fixture.context, "static workload", viewport, NK_WINDOW_NO_SCROLLBAR));
    draw_compose_scene_canvas(fixture.context, &view, nk_window_get_canvas(fixture.context), viewport);
    nk_end(fixture.context);
}
void check_measured_note_pass(const wormedit::ui::nuklear::compose_static_work_t& work) {
    using wormedit::ui::nuklear::compose_static_draw_kind_t;
    const auto& direct = work.passes[9][static_cast<std::size_t>(compose_static_draw_kind_t::direct)];
    CHECK(direct.calls == 1);
    CHECK(direct.vertices > 0);
    CHECK(direct.indices > 0);
    CHECK(work.vertices >= direct.vertices);
}
}
TEST_CASE("Static field workload attribution is opt-in and resets each frame", "[ui][field][tile][workload][isolated-software-render]") {
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    wormedit::ui::compose_view_t view{};
    view.scene_available = true;
    view.viewport_width_pixels = view.viewport_height_pixels = 128;
    view.selected_note_count = 1;
    view.note_endpoints = {{.bounds = {20, 40, 60, 50}, .note_id = wormedit::object_id_t{1},
        .lane_id = wormedit::lane_id_t{1}, .selected = true}};
    draw_static_work_frame(fixture, view);
    CHECK(wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context) == wormedit::ui::nuklear::compose_static_work_t{});
    wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context, true);
    draw_static_work_frame(fixture, view);
    check_measured_note_pass(wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context));
    draw_static_work_frame(fixture, view);
    check_measured_note_pass(wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context));
    wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context, false);
    draw_static_work_frame(fixture, view);
    CHECK(wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context) == wormedit::ui::nuklear::compose_static_work_t{});
}

namespace {
using namespace wormedit::ui::testing;
auto render_static_pixels(field_submission_fixture_t& fixture, const wormedit::ui::compose_view_t& view)
    -> field_pixels_t {
    SDL_SetRenderDrawColor(fixture.renderer,0,0,0,255);
    SDL_RenderClear(fixture.renderer);
    draw_static_work_frame(fixture,view);
    nk_sdl_render(fixture.context,NK_ANTI_ALIASING_OFF);
    SDL_RenderPresent(fixture.renderer);
    const auto width = fixture.surface->w;
    field_pixels_t pixels(static_cast<std::size_t>(width * fixture.surface->h));
    for (int row=0; row<fixture.surface->h; ++row)
        std::memcpy(pixels.data()+row*width,static_cast<const std::uint8_t*>(fixture.surface->pixels)+
            row*fixture.surface->pitch,static_cast<std::size_t>(width)*4);
    return pixels;
}
void expect_static_pixels(const field_pixels_t& direct,const field_pixels_t& cached) {
    const auto difference=static_pixel_difference(direct,cached);
    CHECK(difference.interior==0);
    CHECK(difference.coverage==0);
}
void expect_direct_note_work(const wormedit::ui::nuklear::compose_static_work_t& work) {
    using enum wormedit::ui::nuklear::compose_static_draw_kind_t;
    CHECK(work.passes[9][static_cast<std::size_t>(direct)].calls==1);
    CHECK(work.passes[9][static_cast<std::size_t>(composite)].calls==0);
}
auto edge_note_source(double zoom) -> wormedit::ui::compose_view_t {
    wormedit::ui::compose_view_t source;
    for (int row=-12; row<=12; ++row) for (unsigned lane=0; lane<2; ++lane) {
        const auto y=double(row)*zoom/12;
        source.note_endpoints.push_back({.bounds={40.0+lane*30,y-4,64.0+lane*30,y+4},
            .note_id=wormedit::object_id_t{static_cast<std::uint64_t>((row+12)*2+lane+1)},
            .lane_id=wormedit::lane_id_t{lane+1},
            .glyph=lane ? wormedit::lane_glyph_t::arrow_up : wormedit::lane_glyph_t::arrow_left});
    }
    return source;
}
void project_edge_notes(wormedit::ui::compose_view_t& view,const wormedit::ui::compose_view_t& source,double scroll) {
    view.area_scroll_pixels=scroll;
    const auto origin=view.downscroll ? -scroll-128 : scroll;
    view.note_endpoints=source.note_endpoints;
    for (auto& note:view.note_endpoints) {
        note.bounds.top-=origin;
        note.bounds.bottom-=origin;
    }
}
void check_static_note_edge_pixels(double zoom,bool downscroll) {
    INFO("zoom " << zoom << " downscroll " << downscroll);
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    const auto source=edge_note_source(zoom);
    wormedit::ui::compose_view_t view;
    view.scene_available=true;
    view.viewport_width_pixels=view.viewport_height_pixels=128;
    view.pixels_per_standard_measure=zoom;
    view.downscroll=downscroll;
    view.static_scene=&source;
    view.static_scene_style_generation=view.static_scene_source_generation=1;
    view.static_scene_covered_begin_pixels=-2048;
    view.static_scene_covered_end_pixels=2048;
    project_edge_notes(view,source,.25);
    for (unsigned frame=0; frame<20; ++frame) render_static_pixels(fixture,view);
    wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context,true);
    for (const auto scroll:{.25,.75,1.25,1.75,.5,.25}) {
        project_edge_notes(view,source,scroll);
        view.selected_note_count=1;
        const auto direct=render_static_pixels(fixture,view);
        expect_direct_note_work(wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context));
        view.selected_note_count=0;
        const auto cached=render_static_pixels(fixture,view);
        expect_static_pixels(direct,cached);
        const auto work=wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context);
        using draw_kind_t = wormedit::ui::nuklear::compose_static_draw_kind_t;
        CHECK(work.passes[9][static_cast<std::size_t>(draw_kind_t::guard)].calls==0);
        CHECK(work.passes[9][static_cast<std::size_t>(draw_kind_t::hole)].calls>0);
        CHECK(work.passes[9][static_cast<std::size_t>(draw_kind_t::hole)].calls<=2);
    }
}
}
TEST_CASE("Retained note tiles repair clipped head artwork across fractional viewport edges",
          "[ui][field][tile][pixel][isolated-software-render]") {
    check_static_note_edge_pixels(192,false);
    check_static_note_edge_pixels(192,true);
    check_static_note_edge_pixels(4096,false);
    check_static_note_edge_pixels(4096,true);
}

namespace {
auto mutation_glyph_pixels(int left,int width) -> field_pixels_t {
    field_pixels_t pixels(128*128,{0,0,0,255});
    for (int y=30; y<90; ++y) for (int x=left; x<left+width; ++x)
        pixels[static_cast<std::size_t>(y*128+x)]={220,60,100,255};
    return pixels;
}
}
TEST_CASE("Static pixel coverage gate detects missing thin glyphs and shifts beyond one pixel",
          "[ui][field][tile][pixel]") {
    const field_pixels_t empty(128*128,{0,0,0,255});
    CHECK(static_pixel_difference(mutation_glyph_pixels(40,1),empty).coverage>0);
    CHECK(static_pixel_difference(mutation_glyph_pixels(40,8),mutation_glyph_pixels(42,8)).coverage>0);
    CHECK(static_pixel_difference(mutation_glyph_pixels(40,8),mutation_glyph_pixels(41,8)).coverage==0);
}

namespace {
auto presentation_test_view(const wormedit::ui::compose_view_t& source) -> wormedit::ui::compose_view_t {
    wormedit::ui::compose_view_t view;
    view.scene_available = true;
    view.viewport_width_pixels = view.viewport_height_pixels = 128;
    view.pixels_per_standard_measure = 192;
    view.static_scene = &source;
    view.static_scene_style_generation = view.static_scene_source_generation = 1;
    view.static_scene_covered_begin_pixels = -2048;
    view.static_scene_covered_end_pixels = 2048;
    project_edge_notes(view, source, .25);
    return view;
}

void check_suspended_viewport() {
    const auto viewport = compose_canvas_viewport_for_testing();
    REQUIRE(viewport);
    CHECK(viewport->width == 128);
}

void check_suspended_field(field_submission_fixture_t& fixture,
                            const wormedit::ui::compose_view_t& view) {
    using namespace wormedit::ui::nuklear;
    REQUIRE_FALSE(can_draw_compose_field(fixture.context));
    measure_compose_static_work_for_testing(fixture.context, true);
    draw_static_work_frame(fixture, view);
    const auto& field = compose_field(fixture.context);
    CHECK(field.vertices().empty());
    CHECK(field.indices().empty());
    CHECK(field.tile_work().raster_draws == 0);
    CHECK(compose_static_work_for_testing(fixture.context) == compose_static_work_t{});
    check_suspended_viewport();
    nk_sdl_discard_frame(fixture.context);
}

void check_resumed_field(field_submission_fixture_t& fixture,
                          const wormedit::ui::compose_view_t& view) {
    using namespace wormedit::ui::nuklear;
    REQUIRE(SDL_ShowWindow(fixture.window));
    REQUIRE(SDL_SetBooleanProperty(SDL_GetWindowProperties(fixture.window),
        "wormedit.gpu.presentation_pending", false));
    REQUIRE(can_draw_compose_field(fixture.context));
    std::size_t raster_draws{};
    for (unsigned frame = 0; frame < 24; ++frame) {
        draw_static_work_frame(fixture, view);
        raster_draws += compose_field(fixture.context).tile_work().raster_draws;
        nk_sdl_discard_frame(fixture.context);
    }
    CHECK_FALSE(compose_field(fixture.context).vertices().empty());
    CHECK(raster_draws > 0);
}

void check_hidden_field_diagnostics_reset(field_submission_fixture_t& fixture,
                                          const wormedit::ui::compose_view_t& view) {
    using namespace wormedit::ui::nuklear;
    REQUIRE(compose_static_work_for_testing(fixture.context).vertices > 0);
    REQUIRE(SDL_HideWindow(fixture.window));
    draw_static_work_frame(fixture, view);
    CHECK(compose_static_work_for_testing(fixture.context) == compose_static_work_t{});
    nk_sdl_discard_frame(fixture.context);
}

void check_window_field_suspension(bool pending) {
    field_submission_fixture_t fixture{true};
    REQUIRE(fixture.context);
    REQUIRE(SDL_GetRenderWindow(fixture.renderer) == fixture.window);
    if (pending) {
        REQUIRE(SDL_ShowWindow(fixture.window));
        REQUIRE(SDL_SetBooleanProperty(SDL_GetWindowProperties(fixture.window),
            "wormedit.gpu.presentation_pending", true));
    }
    const auto source = edge_note_source(192);
    const auto view = presentation_test_view(source);
    check_suspended_field(fixture, view);
    check_resumed_field(fixture, view);
    check_hidden_field_diagnostics_reset(fixture, view);
}
} // namespace

TEST_CASE("Cold hidden Compose windows skip field geometry and tile rasterization before discard",
          "[ui][field][presentation][isolated-software-render]") {
    check_window_field_suspension(false);
}

TEST_CASE("Pending Compose windows skip field geometry and tile rasterization and resume",
          "[ui][field][presentation][isolated-software-render]") {
    check_window_field_suspension(true);
}

TEST_CASE("Offscreen Compose surface renderers allow their separate hidden input window",
          "[ui][field][presentation][isolated-software-render]") {
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    CHECK(SDL_GetRenderWindow(fixture.renderer) == nullptr);
    CHECK(wormedit::ui::nuklear::can_draw_compose_field(fixture.context));
    const auto source = edge_note_source(192);
    const auto view = presentation_test_view(source);
    draw_static_work_frame(fixture, view);
    CHECK_FALSE(wormedit::ui::nuklear::compose_field(fixture.context).vertices().empty());
}

namespace {
auto make_world_zero_source() -> wormedit::ui::compose_view_t {
    wormedit::ui::compose_view_t source;
    source.note_endpoints.push_back({.bounds={80,-4,108,4},.note_id={1},.lane_id={1},
        .glyph=wormedit::lane_glyph_t::arrow_left});
    source.timing_markers.push_back({.object_id={2},.marker_y_pixels=0,
        .marker_right_fraction=1,.lane=wormedit::timing_lane_kind_t::bpm,.short_label="120"});
    source.grid_lines.push_back({.y_pixels=0,.kind=wormedit::editor_grid_line_kind_t::measure});
    return source;
}
auto build_world_zero_preroll_view(const wormedit::ui::compose_view_t& source) -> wormedit::ui::compose_view_t {
    auto view=source;
    view.scene_available=true;
    view.viewport_width_pixels=128;
    view.viewport_height_pixels=160;
    view.pixels_per_standard_measure=192;
    view.timing_lane_visibility_mask=1;
    view.playable_field_left_pixels=72;
    view.playable_field_right_pixels=128;
    view.static_scene=&source;
    view.static_scene_style_generation=view.static_scene_source_generation=1;
    view.static_scene_covered_begin_pixels=0;
    view.static_scene_covered_end_pixels=2048;
    view.area_scroll_pixels=-37.75;
    for (auto& note:view.note_endpoints) {note.bounds.top+=37.75;note.bounds.bottom+=37.75;}
    view.timing_markers[0].marker_y_pixels+=37.75;
    view.grid_lines[0].y_pixels+=37.75;
    return view;
}
auto render_world_zero_reference(field_submission_fixture_t& fixture,
                                  wormedit::ui::compose_view_t view) -> field_pixels_t {
    view.selected_note_count=1;
    view.static_scene=nullptr;
    view.document_revision=2;
    auto& marker_state=worm_ui_nuklear_compose_state().timing_marker;
    const auto was_dragging=marker_state.timing_marker_drag_active;
    marker_state.timing_marker_drag_active=true;
    const auto pixels=render_static_pixels(fixture,view);
    marker_state.timing_marker_drag_active=was_dragging;
    return pixels;
}
void report_world_zero_boundaries(const field_pixels_t& direct,const field_pixels_t& cached,float scale=1) {
        const auto expected_edges=field_boundaries(direct,128,128);
        const auto actual_edges=field_boundaries(cached,128,128);
        for (int y=0;y<256;++y) for (int x=0;x<256;++x) {
            const auto index=static_cast<std::size_t>(y*256+x);
            if (expected_edges[index] && !has_near_field_boundary(actual_edges,x,y,128,128,static_cast<int>(2*scale)))
                UNSCOPED_INFO("missing direct boundary " << x*.5 << "," << y*.5);
            if (actual_edges[index] && !has_near_field_boundary(expected_edges,x,y,128,128,static_cast<int>(2*scale)))
                UNSCOPED_INFO("extra cached boundary " << x*.5 << "," << y*.5);
        }

}
void expect_world_zero_direct_fallback(field_submission_fixture_t& fixture,
                                        const field_pixels_t& direct,const field_pixels_t& cached) {
    const auto difference=static_pixel_difference(direct,cached,128,128,1.25f);
    CHECK(difference.interior==0);
    if (difference.coverage) report_world_zero_boundaries(direct,cached);
    CHECK(difference.coverage==0);
    const bool is_identical=direct==cached;
    CHECK(is_identical);
    const auto work=wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context);
    CHECK(work.tile_bytes==0);
    CHECK(work.tiles_prepared==0);
}
}
TEST_CASE("Retained WORLD zero glyphs preserve fractional-scale preroll label and arrow coverage",
          "[ui][field][tile][pixel][staging][isolated-software-render]") {
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    SDL_SetRenderScale(fixture.renderer,1.25f,1.25f);
    wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context,true);
    const auto source=make_world_zero_source();
    const auto view=build_world_zero_preroll_view(source);
    for (unsigned frame=0;frame<20;++frame) render_static_pixels(fixture,view);
    const auto direct=render_world_zero_reference(fixture,view);
    const auto cached=render_static_pixels(fixture,view);
    expect_world_zero_direct_fallback(fixture,direct,cached);
}
namespace {
void check_note_coverage_seam(bool has_seam) {
    INFO("retained coverage seam " << has_seam);
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context,true);
    auto source=make_world_zero_source();
    source.grid_lines.clear();
    source.timing_markers.clear();
    auto view=build_world_zero_preroll_view(make_world_zero_source());
    view.static_scene=&source;
    view.grid_lines.clear();
    view.timing_markers.clear();
    if (!has_seam) view.static_scene_covered_begin_pixels=-1024;
    for (unsigned frame=0;frame<20;++frame) render_static_pixels(fixture,view);
    const auto cached=render_static_pixels(fixture,view);
    const auto work=wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context);
    using draw_kind_t=wormedit::ui::nuklear::compose_static_draw_kind_t;
    const auto& holes=work.passes[9][static_cast<std::size_t>(draw_kind_t::hole)];
    CHECK((holes.vertices>0)==has_seam);
    CHECK(work.tile_bytes>0);
    CHECK(work.passes[9][static_cast<std::size_t>(draw_kind_t::composite)].calls>0);
    const auto direct=render_world_zero_reference(fixture,view);
    expect_static_pixels(direct,cached);
}
}
TEST_CASE("Retained coverage seams redraw complete glyphs without disabling neighboring tiles",
          "[ui][field][tile][pixel][coverage-seam][isolated-software-render]") {
    check_note_coverage_seam(true);
    check_note_coverage_seam(false);
}
TEST_CASE("Indexed grid repairs preserve published exclusion of a retained source row",
          "[ui][field][tile][pixel][grid-membership][isolated-software-render]") {
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    wormedit::ui::compose_view_t source;
    source.grid_lines.push_back({.y_pixels=0,.kind=wormedit::editor_grid_line_kind_t::measure,
                                 .chart_row=112});
    auto view=build_world_zero_preroll_view(make_world_zero_source());
    view.static_scene=&source;
    view.static_scene_world_origin_pixels=-2048;
    view.area_scroll_pixels=-48;
    view.viewport_height_pixels=160;
    view.downscroll=true;
    view.has_waveform_geometry=true;
    view.note_endpoints.clear(); view.timing_markers.clear(); view.grid_lines.clear();
    view.static_scene_covered_begin_pixels=-2048;
    view.static_scene_covered_end_pixels=0;
    // This source row projects onto the top edge, but the published visible
    // row interval excludes its end, so the original direct pass has no tick.
    source.grid_lines[0].y_pixels=1936;
    for (unsigned frame=0;frame<20;++frame) render_static_pixels(fixture,view);
    const auto cached=render_static_pixels(fixture,view);
    view.static_scene=nullptr;
    const auto direct=render_static_pixels(fixture,view);
    expect_static_pixels(direct,cached);
}
namespace {
void expect_scaled_grid_retention(const wormedit::ui::nuklear::compose_static_work_t& work) {
    using draw_kind_t=wormedit::ui::nuklear::compose_static_draw_kind_t;
    CHECK(work.passes[5][static_cast<std::size_t>(draw_kind_t::direct)].vertices>0);
    CHECK(work.passes[5][static_cast<std::size_t>(draw_kind_t::composite)].calls==0);
    CHECK(work.passes[9][static_cast<std::size_t>(draw_kind_t::composite)].calls>0);
    CHECK(work.tile_bytes>0);
    CHECK(work.passes[6][static_cast<std::size_t>(draw_kind_t::hole)].vertices>0);
}
}
TEST_CASE("Nonunit output keeps original grid snapping while neighboring field tiles remain retained",
          "[ui][field][tile][pixel][grid-scale][isolated-software-render]") {
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    SDL_SetRenderScale(fixture.renderer,2,2);
    CHECK_FALSE(wormedit::ui::nuklear::can_retain_compose_grid(fixture.context));
    wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context,true);
    const auto source=make_world_zero_source();
    const auto view=build_world_zero_preroll_view(source);
    for (unsigned frame=0;frame<20;++frame) render_static_pixels(fixture,view);
    const auto cached=render_static_pixels(fixture,view);
    const auto work=wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context);
    expect_scaled_grid_retention(work);
    const auto direct=render_world_zero_reference(fixture,view);
    const auto difference=static_pixel_difference(direct,cached,128,128,2);
    CHECK(difference.interior==0);
    if (difference.coverage) report_world_zero_boundaries(direct,cached,2);
    CHECK(difference.coverage==0);
}
namespace {
void expect_glyph_retention_work(const wormedit::ui::nuklear::compose_static_work_t& work,bool retained) {
    using draw_kind_t=wormedit::ui::nuklear::compose_static_draw_kind_t;
    CHECK((work.passes[9][static_cast<std::size_t>(draw_kind_t::direct)].calls==0)==retained);
    CHECK((work.passes[9][static_cast<std::size_t>(draw_kind_t::composite)].calls>0)==retained);
    CHECK(work.passes[9][static_cast<std::size_t>(draw_kind_t::hole)].calls==0);
}
void check_glyph_output_scale(float scale,bool retained) {
    INFO("output scale " << scale);
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    SDL_SetRenderScale(fixture.renderer,scale,scale);
    CHECK(wormedit::ui::nuklear::can_retain_compose_glyphs(fixture.context)==retained);
    wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context,true);
    auto source=make_world_zero_source();
    source.note_endpoints[0].bounds={12,44,40,52};
    source.timing_markers.clear(); source.grid_lines.clear();
    auto view=build_world_zero_preroll_view(make_world_zero_source());
    view.static_scene=&source;
    view.note_endpoints=source.note_endpoints;
    view.timing_markers.clear(); view.grid_lines.clear();
    view.area_scroll_pixels=0;
    view.playable_field_left_pixels=0;
    for (unsigned frame=0;frame<20;++frame) render_static_pixels(fixture,view);
    const auto cached=render_static_pixels(fixture,view);
    const auto work=wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context);
    expect_glyph_retention_work(work,retained);
    const auto direct=render_world_zero_reference(fixture,view);
    if (!retained) CHECK(direct==cached);
    const auto difference=static_pixel_difference(direct,cached,128,128,scale);
    CHECK(difference.interior==0);
    CHECK(difference.coverage==0);
}
}
TEST_CASE("Glyph retention preserves original fractional output and retains integral output",
          "[ui][field][tile][pixel][glyph-scale][isolated-software-render]") {
    CHECK_FALSE(wormedit::ui::nuklear::can_retain_compose_glyphs(nullptr));
    check_glyph_output_scale(1.25f,false);
    check_glyph_output_scale(1.5f,false);
    check_glyph_output_scale(2,true);
}
namespace {
void check_indexed_top_grid_row(bool downscroll) {
    INFO("downscroll " << downscroll);
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    wormedit::ui::compose_view_t source;
    source.grid_lines.push_back({.y_pixels=downscroll ? 1936.0 : 192.0,.kind=wormedit::editor_grid_line_kind_t::measure,
        .measure_number=2,.chart_row=downscroll ? std::uint64_t{112} : std::uint64_t{192}});
    auto view=source;
    view.scene_available=true;
    view.viewport_width_pixels=128;
    view.viewport_height_pixels=160;
    view.pixels_per_standard_measure=192;
    view.has_waveform_geometry=true;
    view.downscroll=downscroll;
    view.area_scroll_pixels=downscroll ? -48 : 192;
    view.static_scene_world_origin_pixels=downscroll ? -2048 : 0;
    view.grid_lines[0].y_pixels=0;
    view.static_scene=&source;
    view.static_scene_style_generation=view.static_scene_source_generation=1;
    view.static_scene_covered_begin_pixels=downscroll ? -2048 : 0;
    view.static_scene_covered_end_pixels=downscroll ? 0 : 2048;
    for (unsigned frame=0;frame<20;++frame) render_static_pixels(fixture,view);
    view.static_scene=nullptr;
    view.document_revision=2;
    const auto direct=render_static_pixels(fixture,view);
    view.static_scene=&source;
    view.document_revision=0;
    const auto cached=render_static_pixels(fixture,view);
    expect_static_pixels(direct,cached);
}
}
TEST_CASE("Indexed grid repairs preserve an exact top row and waveform presence without copied rows",
          "[ui][field][tile][pixel][staging][isolated-software-render]") {
    check_indexed_top_grid_row(false);
    check_indexed_top_grid_row(true);
}
TEST_CASE("Retained field tiles invalidate an uploaded atlas behind a stable font handle",
          "[ui][field][tile][font-epoch][isolated-software-render]") {
    field_submission_fixture_t fixture;
    REQUIRE(fixture.context);
    auto font=*fixture.context->style.font;
    nk_style_set_font(fixture.context,&font);
    const auto* handle=fixture.context->style.font;
    wormedit::ui::compose_view_t source;
    source.timing_markers.push_back({.object_id={2},.marker_y_pixels=64,
        .marker_right_fraction=1,.lane=wormedit::timing_lane_kind_t::bpm,.short_label="120"});
    auto view=source;
    view.scene_available=true;
    view.viewport_width_pixels=view.viewport_height_pixels=128;
    view.pixels_per_standard_measure=192;
    view.timing_lane_visibility_mask=1;
    view.playable_field_left_pixels=72;
    view.static_scene=&source;
    view.static_scene_style_generation=view.static_scene_source_generation=1;
    view.static_scene_covered_begin_pixels=0;
    view.static_scene_covered_end_pixels=2048;
    wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context,true);
    for (unsigned frame=0;frame<20;++frame) render_static_pixels(fixture,view);
    const auto generation=wormedit::ui::nuklear::compose_field_font_generation(fixture.context);
    auto* atlas=nk_sdl_font_stash_begin(fixture.context);
    auto* replacement=nk_font_atlas_add_default(atlas,font.height,nullptr);
    REQUIRE(replacement);
    nk_sdl_font_stash_end(fixture.context);
    font=replacement->handle;
    nk_style_set_font(fixture.context,&font);
    REQUIRE(fixture.context->style.font==handle);
    REQUIRE(wormedit::ui::nuklear::compose_field_font_generation(fixture.context)>generation);
    render_static_pixels(fixture,view);
    const auto work=wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context);
    using draw_kind_t=wormedit::ui::nuklear::compose_static_draw_kind_t;
    CHECK(work.passes[6][static_cast<std::size_t>(draw_kind_t::composite)].vertices==0);
    CHECK(work.passes[6][static_cast<std::size_t>(draw_kind_t::missing)].calls>0);
}

namespace {
constexpr int timing_raster_width = 768;
constexpr int timing_raster_height = 384;

auto make_cut_timing_raster_song() -> wormedit::song_document_t {
    using namespace wormedit;
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 1;
    song.export_cuts = {{.id = object_id_t{9001}, .label = "Shorten intro",
        .begin_milliseconds = 0, .end_milliseconds = 16000}};
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    for (std::uint64_t measure = 1; measure <= 12; ++measure)
        chart.measures.push_back({.id = measure_id_t{measure}});
    chart.lane_layouts = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "timing-raster", .display_name = "Timing raster",
            .lanes = {{.id = lane_id_t{1}, .name = "Key", .role = lane_role_t::playable}}}}};
    const local_position_t position{measure_id_t{10}, 240};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}},
        {.id = object_id_t{7001}, .position = position, .beats_per_minute = {150, 1}}};
    chart.scrolls = {{.id = object_id_t{7002}, .position = position, .multiplier = {2, 1}}};
    chart.speeds = {{.id = object_id_t{7003}, .position = position, .multiplier = {3, 1}}};
    chart.pauses = {{.id = object_id_t{7004}, .position = position, .duration = {1, 4}},
        {.id = object_id_t{7005}, .position = position, .kind = pause_kind_t::delay, .duration = {1, 4}}};
    chart.warps = {{.id = object_id_t{7006}, .span = {position, {position.measure, 480}}}};
    chart.timing_anchors = {{.id = object_id_t{7008}, .position = position}};
    chart.volumes = {{.id = object_id_t{7009}, .position = position, .value = 75}};
    chart.sample_sets = {{.id = object_id_t{7010}, .position = position, .value = 2}};
    song.charts.push_back(std::move(chart));
    return song;
}

// Keep the production projection owners across frames; only SDL submission is synthetic.
struct timing_raster_projection_t {
    wormedit::sys::compose_scene_cache_t cache;
    wormedit::sys::application_command_state_t application;
    std::optional<wormedit::editor_interaction_state_t> interaction;
    wormedit::sys::frontend_workspace_view_t flattened;

    void refill(wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
                double scroll) {
        if (interaction) interaction->viewport_scroll_pixels = scroll;
        application.project.compose_view.compose_timing_lane_visibility_mask = wormedit::all_timing_lane_mask;
        flattened.view.compose.visible = true;
        wormedit::sys::populate_compose_view(flattened, history, workspace, cache,
            timing_raster_width + 48, timing_raster_height + 188, interaction, application, nullptr);
        const auto& view = flattened.view.compose;
        expect_field(view.scene_available);
        expect_field(static_cast<bool>(view.cut_axis));
        expect_field(!(view.cut_axis->intervals().empty()));
        expect_field(view.static_scene);
        expect_field(view.viewport_width_pixels == timing_raster_width);
        expect_field(view.viewport_height_pixels == timing_raster_height);
        expect_field(view.selected_note_count == 0);
        expect_field(std::ranges::none_of(view.timing_markers, &wormedit::ui::compose_timing_marker_view_t::selected));
    }
};

void move_timing_raster_pointer(field_submission_fixture_t& fixture, int x = -100, int y = -100) {
    nk_input_begin(fixture.context);
    nk_input_motion(fixture.context, x, y);
    nk_input_end(fixture.context);
}

auto render_direct_timing_pixels(field_submission_fixture_t& fixture,
                                 const wormedit::ui::compose_view_t& view) -> field_pixels_t {
    auto& state = worm_ui_nuklear_compose_state().timing_marker;
    const auto was_dragging = state.timing_marker_drag_active;
    // With no selection this changes only pass 6's dispatch, without drawing drag ghosts.
    state.timing_marker_drag_active = true;
    const auto pixels = render_static_pixels(fixture, view);
    state.timing_marker_drag_active = was_dragging;
    return pixels;
}

void expect_timing_raster_dispatch(field_submission_fixture_t& fixture, bool retained) {
    using enum wormedit::ui::nuklear::compose_static_draw_kind_t;
    const auto work = wormedit::ui::nuklear::compose_static_work_for_testing(fixture.context);
    const auto& composite_work = work.passes[6][static_cast<std::size_t>(composite)];
    const auto& direct_work = work.passes[6][static_cast<std::size_t>(direct)];
    expect_field((composite_work.calls > 0) == retained);
    expect_field((composite_work.vertices > 0) == retained);
    expect_field((direct_work.vertices == 0) == retained);
    if (retained) {
        expect_field(work.tile_bytes > 0);
        expect_field(work.passes[6][static_cast<std::size_t>(hole)].vertices == 0);
        expect_field(work.passes[6][static_cast<std::size_t>(missing)].vertices == 0);
    }
}

auto crop_timing_marker_pixels(const field_pixels_t& pixels, SDL_Rect rect) -> field_pixels_t {
    field_pixels_t crop;
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        const auto begin = pixels.begin() + y * timing_raster_width + rect.x;
        crop.insert(crop.end(), begin, begin + rect.w);
    }
    return crop;
}

auto timing_raster_changed_pixels(const field_pixels_t& left, const field_pixels_t& right) -> std::size_t {
    std::size_t changed{};
    for (std::size_t index = 0; index < left.size(); ++index)
        changed += is_field_color_different(left[index], right[index]);
    return changed;
}

void check_timing_marker_raster(const wormedit::ui::compose_view_t& view,
                               const field_pixels_t& direct, const field_pixels_t& cached,
                               const field_pixels_t& textless, const field_pixels_t& empty) {
    std::size_t checked{};
    for (const auto& marker : view.timing_markers) {
        if (marker.object_id.value < 7001) continue;
        CAPTURE(marker.lane, marker.marker_y_pixels, view.area_scroll_pixels, view.downscroll);
        expect_field(!(marker.short_label.empty()));
        const auto bounds = wormedit::ui::compose_timing_marker_bounds(view, marker);
        // Exclude the hover outline so these same body/text pixels apply during hover.
        const SDL_Rect rect{static_cast<int>(std::ceil(bounds.left)) + 3,
            static_cast<int>(std::ceil(bounds.top)) + 3,
            static_cast<int>(std::floor(bounds.right) - std::ceil(bounds.left)) - 6,
            static_cast<int>(std::floor(bounds.bottom) - std::ceil(bounds.top)) - 6};
        expect_field(rect.x >= 0);
        expect_field(rect.y >= 0);
        expect_field(rect.w > 0);
        expect_field(rect.h > 0);
        expect_field(rect.x + rect.w <= timing_raster_width);
        expect_field(rect.y + rect.h <= timing_raster_height);
        const auto expected = crop_timing_marker_pixels(direct, rect);
        const auto actual = crop_timing_marker_pixels(cached, rect);
        const auto no_text = crop_timing_marker_pixels(textless, rect);
        const auto no_marker = crop_timing_marker_pixels(empty, rect);
        expect_field(timing_raster_changed_pixels(no_text, no_marker) > 20);
        expect_field(timing_raster_changed_pixels(expected, no_text) > 5);
        expect_field(timing_raster_changed_pixels(actual, no_text) > 5);
        const auto difference = static_pixel_difference(expected, actual, rect.w, rect.h);
        expect_field(difference.interior == 0);
        expect_field(difference.coverage == 0);
        ++checked;
    }
    // Kiai is represented by spans; every point-marker family is checked separately.
    expect_field(checked == wormedit::timing_lane_kind_count - 1);
}

void check_warm_timing_raster(field_submission_fixture_t& fixture,
                              const wormedit::ui::compose_view_t& view) {
    move_timing_raster_pointer(fixture);
    const auto cached = render_static_pixels(fixture, view);
    expect_timing_raster_dispatch(fixture, true);
    const auto direct = render_direct_timing_pixels(fixture, view);
    expect_timing_raster_dispatch(fixture, false);
    auto reference = view;
    for (auto& marker : reference.timing_markers) { marker.short_label = {}; marker.sample_set.reset(); }
    const auto textless = render_direct_timing_pixels(fixture, reference);
    reference.timing_markers.clear();
    const auto empty = render_direct_timing_pixels(fixture, reference);
    check_timing_marker_raster(view, direct, cached, textless, empty);
    const auto marker = std::ranges::find(view.timing_markers, wormedit::object_id_t{7001},
        &wormedit::ui::compose_timing_marker_view_t::object_id);
    expect_field(marker != view.timing_markers.end());
    const auto bounds = wormedit::ui::compose_timing_marker_bounds(view, *marker);
    move_timing_raster_pointer(fixture, static_cast<int>((bounds.left + bounds.right) * .5),
        static_cast<int>((bounds.top + bounds.bottom) * .5));
    const auto hovered = render_static_pixels(fixture, view);
    expect_timing_raster_dispatch(fixture, false);
    check_timing_marker_raster(view, direct, hovered, textless, empty);
    move_timing_raster_pointer(fixture);
    const auto unhovered = render_static_pixels(fixture, view);
    expect_timing_raster_dispatch(fixture, true);
    check_timing_marker_raster(view, direct, unhovered, textless, empty);
}

void check_cut_timing_raster_revision(field_submission_fixture_t& fixture,
                                     timing_raster_projection_t& projection,
                                     wormedit::document_history_t& history,
                                     const wormedit::workspace_controller_t& workspace) {
    // Reproject twice so cut-axis cursor anchoring has settled before warming tiles.
    projection.refill(history, workspace, 0);
    projection.refill(history, workspace, 0);
    move_timing_raster_pointer(fixture);
    for (unsigned frame = 0; frame < 24; ++frame)
        render_static_pixels(fixture, projection.flattened.view.compose);
    for (const double scroll : {0.0, 48.5, 96.0, 48.5}) {
        projection.refill(history, workspace, scroll);
        expect_field(projection.flattened.view.compose.area_scroll_pixels == scroll);
        check_warm_timing_raster(fixture, projection.flattened.view.compose);
    }
}
} // namespace

TEST_CASE("Original timing marker bodies and labels survive warm tiles cuts scrolling and hover",
          "[ui][field][tile][pixel][export-cut][timing-marker][isolated-software-render]") {
    using namespace wormedit;
    for (const bool downscroll : {false, true}) {
        CAPTURE(downscroll);
        field_submission_fixture_t fixture{false, timing_raster_width, timing_raster_height};
        expect_field(fixture.context);
        worm_ui_nuklear_reset_compose_state();
        wormedit::ui::nuklear::measure_compose_static_work_for_testing(fixture.context, true);
        auto history = document_history_t::from(make_cut_timing_raster_song());
        auto binding = workspace_controller_t::from(history);
        expect_field(static_cast<bool>(binding.value));
        auto& workspace = *binding.value;
        workspace.active_tab = workspace_tab_t::compose;
        workspace.selected_chart = chart_id_t{1};
        timing_raster_projection_t projection;
        projection.application.preferences.editor_preferences.scroll_directions[
            static_cast<std::size_t>(editor_mode_family_t::other)] = downscroll ? 2 : 1;
        check_cut_timing_raster_revision(fixture, projection, history, workspace);
        expect_field(projection.flattened.view.compose.downscroll == downscroll);
        const auto inserted = apply_export_cut_edit(history, insert_edit_t<export_cut_edit_value_t>{
            .chart = chart_id_t{1}, .value = {.label = "Shorten again",
                .begin_milliseconds = 16000, .end_milliseconds = 16500}});
        expect_field(inserted.applied);
        check_cut_timing_raster_revision(fixture, projection, history, workspace);
        expect_field(static_cast<bool>(undo_document_command(history)));
        check_cut_timing_raster_revision(fixture, projection, history, workspace);
        expect_field(static_cast<bool>(redo_document_command(history)));
        check_cut_timing_raster_revision(fixture, projection, history, workspace);
    }
}

TEST_CASE("Export cut edge grips paint visible labels outside their hatched band",
          "[ui][field][pixel][export-cut][cut-grip][isolated-software-render]") {
    using namespace wormedit;
    constexpr int width = 320, height = 240;
    field_submission_fixture_t fixture{false, width, height};
    expect_field(fixture.context != nullptr);
    worm_ui_nuklear_reset_compose_state();
    ui::compose_view_t view;
    view.scene_available = view.timing_focus = true;
    view.viewport_width_pixels = width;
    view.viewport_height_pixels = height;
    view.playable_field_left_pixels = 42;
    view.playable_field_right_pixels = width;
    view.export_cuts = {{.id = object_id_t{9001}, .span = {
        .available = true, .has_length = true,
        .begin_projection_visible = true, .end_projection_visible = true,
        .span_projection_visible = true, .projected_begin_y_pixels = 60,
        .projected_end_y_pixels = 180, .projected_span_top_y_pixels = 60,
        .projected_span_bottom_y_pixels = 180}}};
    auto without_grips = view;
    without_grips.export_cuts.front().span.begin_projection_visible = false;
    without_grips.export_cuts.front().span.end_projection_visible = false;
    const auto baseline = render_static_pixels(fixture, without_grips);
    const auto pixels = render_static_pixels(fixture, view);
    for (const auto edge : {WORM_UI_SPAN_HANDLE_BEGIN, WORM_UI_SPAN_HANDLE_END}) {
        const auto grip = ui::compose_export_cut_handle_t::from(view, view.export_cuts.front(), edge);
        expect_field(grip.has_value());
        std::size_t label_pixels{};
        for (auto y = static_cast<int>(grip->bounds.top) + 2; y < grip->bounds.bottom - 2; ++y)
            for (auto x = static_cast<int>(grip->bounds.left) + 18; x < grip->bounds.right - 2; ++x) {
                const auto at = static_cast<std::size_t>(y * width + x);
                const auto& pixel = pixels[at];
                if (pixel[0] > 160 && pixel[2] > 100 &&
                    is_field_color_different(pixel, baseline[at])) ++label_pixels;
            }
        expect_field(label_pixels > 30);
    }
}

namespace {
void draw_compare_work_frame(field_submission_fixture_t& fixture,const wormedit::ui::compose_view_t& pane) {
    nk_sdl_begin_frame(fixture.context);
    nk_clear(fixture.context);
    nk_input_begin(fixture.context); nk_input_end(fixture.context);
    expect_field(nk_begin(fixture.context,"Compare retention",nk_rect(0,0,128,128),NK_WINDOW_NO_SCROLLBAR));
    wormedit::ui::compare_view_t compare;
    compare.visible=compare.has_two_difficulties=true;
    compare.first_difficulty=compare.second_difficulty="Test chart";
    compare.first_pane=compare.second_pane=&pane;
    wormedit::ui::workspace_action_t action;
    worm_ui_nuklear_render_compare_panel(fixture.context,&compare,&action);
    nk_end(fixture.context);
}
}
TEST_CASE("Actual Compare panes release Compose retention and return to a fresh primary cache", "[ui][field][compare-retention][isolated-software-render]") {
    using namespace wormedit::ui::nuklear;
    field_submission_fixture_t fixture;
    expect_field(fixture.context);
    const auto source=edge_note_source(192);
    auto view=presentation_test_view(source);
    for (unsigned frame=0;frame<24;++frame) {
        draw_static_work_frame(fixture,view);
        nk_sdl_discard_frame(fixture.context);
    }
    auto& primary=compose_field(fixture.context);
    expect_field(primary.tile_work().retained_bytes>0 && compose_canvas_cache(fixture.context));
    for (unsigned frame=0;frame<12;++frame) {
        draw_compare_work_frame(fixture,view);
        auto& secondary=compose_field(fixture.context);
        expect_field(!primary.can_retain_tiles() && !secondary.can_retain_tiles());
        expect_field(primary.tile_work().prepared==0 && secondary.tile_work().prepared==0);
        expect_field(primary.tile_work().retained_bytes==0 && secondary.tile_work().retained_bytes==0);
        expect_field(!compose_canvas_cache(fixture.context));
        nk_sdl_discard_frame(fixture.context);
    }
    draw_static_work_frame(fixture,view);
    expect_field(compose_field(fixture.context).can_retain_tiles() && compose_canvas_cache(fixture.context));
}

namespace {
auto span_grip_pixels(const wormedit::ui::testing::field_pixels_t& pixels, int y)
    -> wormedit::ui::testing::field_pixels_t {
    const auto first = pixels.begin() + y * 128 + 66;
    return {first, first + 20};
}
void check_span_endpoint_feedback(wormedit::timing_lane_kind_t lane, bool downscroll) {
    field_submission_fixture_t fixture;
    expect_field(fixture.context != nullptr);
    worm_ui_nuklear_reset_compose_state();
    wormedit::ui::compose_view_t view{};
    view.visible = view.scene_available = true;
    view.downscroll = downscroll;
    view.viewport_width_pixels = view.viewport_height_pixels = 128;
    view.playable_field_left_pixels = 110;
    view.timing_lane_visibility_mask = 1U << static_cast<unsigned>(lane);
    const wormedit::ui::compose_rect_t bounds{0, 32, 0, 96};
    view.kiai_spans = {{.bounds = bounds, .head_object_id = wormedit::object_id_t{10},
        .tail_object_id = wormedit::object_id_t{11}, .head_visible = true, .tail_visible = true}};
    view.recipe_areas = {{.bounds = bounds, .recipe_id = wormedit::recipe_id_t{20},
        .lane = wormedit::timing_lane_kind_t::scroll, .enabled = true, .evaluable = true,
        .begin_visible = true, .end_visible = true}};
    view.speed_spans = {{.bounds = bounds, .object_id = wormedit::object_id_t{30}}};
    const auto head_y = downscroll ? 96 : 32;
    const auto tail_y = downscroll ? 32 : 96;
    // Sample the outer half of each grip, beyond the fill and border of the span.
    const auto head_sample = downscroll ? 98 : 29;
    const auto tail_sample = downscroll ? 29 : 98;
    nk_input_motion(fixture.context, 0, 0);
    const auto idle = render_static_pixels(fixture, view);
    nk_input_motion(fixture.context, 76, head_y);
    const auto hovered = render_static_pixels(fixture, view);
    expect_field(span_grip_pixels(hovered, head_sample) != span_grip_pixels(idle, head_sample));
    expect_field(span_grip_pixels(hovered, tail_sample) == span_grip_pixels(idle, tail_sample));
    nk_input_motion(fixture.context, 76, tail_y);
    const auto tail_hovered = render_static_pixels(fixture, view);
    expect_field(span_grip_pixels(tail_hovered, tail_sample) != span_grip_pixels(idle, tail_sample));
    nk_input_motion(fixture.context, 0, 0);
    auto& drag = worm_ui_nuklear_compose_state().timing_drag;
    drag.kiai_drag_active = lane != wormedit::timing_lane_kind_t::scroll;
    drag.kiai_drag_speed = lane == wormedit::timing_lane_kind_t::speed;
    drag.kiai_drag_head_id = drag.kiai_drag_speed ? 30 : 10;
    drag.kiai_drag_edge = WORM_UI_SPAN_HANDLE_END;
    drag.recipe_drag_active = lane == wormedit::timing_lane_kind_t::scroll;
    drag.recipe_drag_id = 20;
    drag.recipe_drag_edge = WORM_UI_SPAN_HANDLE_END;
    const auto pressed = render_static_pixels(fixture, view);
    expect_field(span_grip_pixels(pressed, tail_sample) != span_grip_pixels(idle, tail_sample));
    expect_field(span_grip_pixels(pressed, head_sample) == span_grip_pixels(idle, head_sample));
    drag = {};
    view.kiai_spans.front().selected = true;
    view.recipe_areas.front().selected = true;
    view.speed_spans.front().selected = true;
    const auto selected = render_static_pixels(fixture, view);
    expect_field(span_grip_pixels(selected, head_sample) != span_grip_pixels(idle, head_sample));
    expect_field(span_grip_pixels(selected, tail_sample) != span_grip_pixels(idle, tail_sample));
    worm_ui_nuklear_reset_compose_state();
}
}
TEST_CASE("Timing span grips distinguish hovered selected and captured endpoints in both directions",
          "[ui][field][span-affordance][isolated-software-render]") {
    for (const auto lane : {wormedit::timing_lane_kind_t::kiai, wormedit::timing_lane_kind_t::scroll,
                            wormedit::timing_lane_kind_t::speed}) {
        for (const bool downscroll : {false, true}) {
            INFO("lane " << static_cast<unsigned>(lane) << " downscroll " << downscroll);
            check_span_endpoint_feedback(lane, downscroll);
        }
    }
}
