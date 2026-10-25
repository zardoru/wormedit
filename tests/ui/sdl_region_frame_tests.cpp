#include "ui/nuklear/app/sdl_backend.h"
#include "ui/nuklear/render/sdl_region_frame.h"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <memory>

using namespace wormedit::ui::nuklear;
namespace {
enum class region_content_t { ordinary, nested, foreign, popup };

struct converted_region_fixture_t {
    SDL_Window* window{};
    SDL_Surface* surface{};
    SDL_Renderer* renderer{};
    nk_context* context{};
    SDL_Texture* font{};
    nk_buffer commands{}, vertices{}, indices{};
    std::unique_ptr<sdl_region_frame_t> regions = std::make_unique<sdl_region_frame_t>();
    char foreign_identity{};

    converted_region_fixture_t() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("region conversion", 192, 192, SDL_WINDOW_HIDDEN);
        REQUIRE(window);
        surface = SDL_CreateSurface(192, 192, SDL_PIXELFORMAT_RGBA32);
        REQUIRE(surface);
        renderer = SDL_CreateSoftwareRenderer(surface);
        REQUIRE(renderer);
        initialize_context();
        nk_buffer_init_default(&commands);
        nk_buffer_init_default(&vertices);
        nk_buffer_init_default(&indices);
    }
    void initialize_context() {
        context = nk_sdl_init(window, renderer, nk_sdl_allocator());
        REQUIRE(context);
        auto* atlas = nk_sdl_font_stash_begin(context);
        auto* baked = nk_font_atlas_add_default(atlas, 14, nullptr);
        REQUIRE(baked);
        nk_sdl_font_stash_end(context);
        nk_style_set_font(context, &baked->handle);
        font = static_cast<SDL_Texture*>(baked->handle.texture.ptr);
        REQUIRE(font);
    }
    ~converted_region_fixture_t() {
        regions.reset();
        nk_buffer_free(&commands);
        nk_buffer_free(&vertices);
        nk_buffer_free(&indices);
        if (context) nk_sdl_shutdown(context);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (surface) SDL_DestroySurface(surface);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
    void begin() {
        regions->reset();
        nk_sdl_begin_frame(context);
        nk_clear(context);
        nk_buffer_clear(&commands);
        nk_buffer_clear(&vertices);
        nk_buffer_clear(&indices);
        nk_input_begin(context);
        nk_input_end(context);
        REQUIRE(nk_begin(context, "region", nk_rect(0, 0, 192, 192), NK_WINDOW_NO_SCROLLBAR));
    }
    void rectangle(float left = 20) {
        nk_fill_rect(nk_window_get_canvas(context), nk_rect(left, 20, 60, 24), 0,
                     nk_rgba(90, 170, 210, 180));
    }
    void popup() {
        REQUIRE(nk_popup_begin(context, NK_POPUP_STATIC, "outside region",
            NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, nk_rect(60, 70, 90, 70)));
        nk_fill_rect(nk_window_get_canvas(context), nk_rect(80, 100, 20, 20), 0,
                     nk_rgba(200, 30, 70, 255));
        nk_popup_end(context);
    }
    void convert() {
        nk_end(context);
        static constexpr nk_draw_vertex_layout_element layout[]{
            {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(sdl_command_vertex_t, position)},
            {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(sdl_command_vertex_t, uv)},
            {NK_VERTEX_COLOR, NK_FORMAT_R32G32B32A32_FLOAT, NK_OFFSETOF(sdl_command_vertex_t, col)},
            {NK_VERTEX_LAYOUT_END}};
        nk_convert_config config{};
        config.vertex_layout = layout;
        config.vertex_size = sizeof(sdl_command_vertex_t);
        config.vertex_alignment = alignof(sdl_command_vertex_t);
        config.tex_null.texture = nk_handle_ptr(font);
        config.tex_null.uv = nk_vec2(.5F, .5F);
        config.global_alpha = 1;
        config.circle_segment_count = config.curve_segment_count = config.arc_segment_count = 22;
        REQUIRE(nk_convert(context, &commands, &vertices, &indices, &config) == NK_CONVERT_SUCCESS);
    }
    auto mesh() const -> sdl_command_mesh_view_t {
        return {{static_cast<const sdl_command_vertex_t*>(nk_buffer_memory_const(&vertices)),
                 vertices.needed / sizeof(sdl_command_vertex_t)},
                {static_cast<const sdl_command_index_t*>(nk_buffer_memory_const(&indices)),
                 indices.needed / sizeof(sdl_command_index_t)}, {}, {}};
    }
    auto prepare() -> bool {
        return regions->prepare(context, &commands, mesh(),
            {.renderer = renderer, .font = font, .texture_epoch = 1});
    }
    auto hits() const -> std::size_t {
        std::size_t count{};
        const nk_draw_command* command;
        nk_draw_foreach(command, context, &commands)
            if (regions->cached_slot(command)) ++count;
        return count;
    }
    void content(region_content_t kind, float left = 20) {
        begin();
        REQUIRE(regions->begin(context, sdl_region_t::header, nk_rgba(30, 40, 50, 255),
                               convert_sdl_region_marker));
        if (kind == region_content_t::nested)
            REQUIRE(regions->begin(context, sdl_region_t::status, nk_rgba(30, 40, 50, 255),
                                   convert_sdl_region_marker));
        rectangle(left);
        if (kind == region_content_t::nested)
            regions->end(context, sdl_region_t::status, convert_sdl_region_marker);
        if (kind == region_content_t::foreign)
            nk_push_custom(nk_window_get_canvas(context), nk_window_get_bounds(context),
                           convert_sdl_region_marker, nk_handle_ptr(&foreign_identity));
        if (kind == region_content_t::popup) popup();
        regions->end(context, sdl_region_t::header, convert_sdl_region_marker);
        convert();
    }
    void warm(region_content_t kind = region_content_t::ordinary) {
        for (unsigned frame = 0; frame < 16; ++frame) {
            content(kind);
            REQUIRE(prepare());
        }
        REQUIRE(hits() == 1);
    }
};

auto has_popup_geometry(sdl_command_mesh_view_t mesh, std::size_t first, std::size_t count) -> bool {
    std::array<float, 4> color{};
    nk_color_fv(color.data(), nk_rgba(200, 30, 70, 255));
    for (const auto index : mesh.indices.subspan(first, count)) {
        const auto& vertex = mesh.vertices[index];
        if (vertex.col[0] == color[0] && vertex.col[1] == color[1] &&
            vertex.col[2] == color[2]) return true;
    }
    return false;
}

void check_popup_after_region(converted_region_fixture_t& fixture) {
    bool ended{}, popup_after_end{};
    std::size_t offset{};
    const nk_draw_command* command;
    nk_draw_foreach(command, fixture.context, &fixture.commands) {
        if (fixture.regions->is_marker(command) && !fixture.regions->cached_slot(command)) ended = true;
        else if (ended && has_popup_geometry(fixture.mesh(), offset, command->elem_count))
            popup_after_end = true;
        offset += command->elem_count;
    }
    CHECK(popup_after_end);
}
} // namespace

TEST_CASE("Converted chrome regions retain ordinary nonnull-userdata geometry and miss changed content",
          "[ui][sdl-region-frame]") {
    converted_region_fixture_t fixture;
    REQUIRE(fixture.context->userdata.ptr != nullptr);
    fixture.warm();
    CHECK(fixture.regions->work().retained_cpu_bytes <= sdl_region_cache_t::cpu_reservation);
    CHECK(fixture.regions->direct_mesh().indices.size() < fixture.mesh().indices.size());
    fixture.content(region_content_t::ordinary, 21);
    REQUIRE(fixture.prepare());
    CHECK(fixture.hits() == 0);
    CHECK(fixture.regions->direct_mesh().indices.size() == fixture.mesh().indices.size());
}

TEST_CASE("Converted chrome frames reject oversized input before allocating cache geometry",
          "[ui][sdl-region-frame]") {
    converted_region_fixture_t fixture;
    fixture.begin();
    REQUIRE(fixture.regions->begin(fixture.context, sdl_region_t::header, nk_rgba(30, 40, 50, 255),
                                   convert_sdl_region_marker));
    for (unsigned index = 0; index < 9000; ++index) fixture.rectangle();
    fixture.regions->end(fixture.context, sdl_region_t::header, convert_sdl_region_marker);
    fixture.convert();
    CHECK(fixture.mesh().vertices.size_bytes() > sdl_region_cache_t::frame_geometry_budget);
    CHECK_FALSE(fixture.prepare());
    const auto work = fixture.regions->work();
    CHECK(work.tiles.prepared == 0);
    CHECK(work.retained_cpu_bytes <= sdl_region_cache_t::frame_reservation);
    CHECK(fixture.regions->direct_mesh().vertices.empty());
}

TEST_CASE("Converted chrome frames retire cached eligibility when direct compaction fails late",
          "[ui][sdl-region-frame]") {
    converted_region_fixture_t fixture;
    fixture.warm();
    fixture.begin();
    REQUIRE(fixture.regions->begin(fixture.context, sdl_region_t::header, nk_rgba(30, 40, 50, 255),
                                   convert_sdl_region_marker));
    fixture.rectangle();
    fixture.regions->end(fixture.context, sdl_region_t::header, convert_sdl_region_marker);
    for (unsigned index = 0; index < 6000; ++index) fixture.rectangle(100);
    fixture.convert();
    const auto raw = fixture.mesh();
    CHECK(raw.vertices.size_bytes() + raw.indices.size_bytes() < sdl_region_cache_t::frame_geometry_budget);
    CHECK_FALSE(fixture.prepare());
    CHECK(fixture.hits() == 0);
    CHECK_FALSE(fixture.regions->submit(0, {}));
    CHECK(fixture.regions->work().retained_cpu_bytes <= sdl_region_cache_t::cpu_reservation);
}

TEST_CASE("Converted chrome regions reject nested barriers and foreign zero-element markers",
          "[ui][sdl-region-frame]") {
    converted_region_fixture_t fixture;
    fixture.warm();
    fixture.content(region_content_t::nested);
    CHECK_FALSE(fixture.prepare());
    CHECK(fixture.hits() == 0);
    fixture.content(region_content_t::foreign);
    REQUIRE(fixture.prepare());
    CHECK(fixture.hits() == 0);
    CHECK(fixture.regions->direct_mesh().indices.size() == fixture.mesh().indices.size());
}

TEST_CASE("Chrome scope RAII leaves nonopaque and image backgrounds in ordinary conversion",
          "[ui][sdl-region-frame]") {
    converted_region_fixture_t fixture;
    fixture.begin();
    fixture.context->style.window.fixed_background = nk_style_item_color(nk_rgba(30, 40, 50, 100));
    {
        const sdl_region_scope_t scope(fixture.context, sdl_region_t::header);
        fixture.rectangle();
    }
    fixture.context->style.window.fixed_background = nk_style_item_image(nk_image_ptr(fixture.font));
    {
        const sdl_region_scope_t scope(fixture.context, sdl_region_t::status);
        fixture.rectangle(25);
    }
    fixture.convert();
    std::size_t foreign{};
    const nk_draw_command* command;
    nk_draw_foreach(command, fixture.context, &fixture.commands)
        if (command->userdata.ptr != fixture.context->userdata.ptr) ++foreign;
    CHECK(foreign == 0);
    CHECK_FALSE(fixture.mesh().indices.empty());
}

TEST_CASE("Popup geometry remains after retained chrome in final converted command order",
          "[ui][sdl-region-frame]") {
    converted_region_fixture_t fixture;
    fixture.warm(region_content_t::popup);
    check_popup_after_region(fixture);
    CHECK_FALSE(fixture.regions->direct_mesh().indices.empty());
    CHECK(fixture.regions->direct_mesh().indices.size() < fixture.mesh().indices.size());
}
