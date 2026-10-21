#include "ui/nuklear/sdl_backend.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
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
#include "ui/nuklear/audio_panel.h"
#include "ui/nuklear/compose_state.h"
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
    WormUiComposeView view{};
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
    WormUiComposeView view{};
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
    WormUiComposeView view{};
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
    WormUiComposeView view{};
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
    WormUiComposeView view{};
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
