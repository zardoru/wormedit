#include "ui/nuklear/font_atlas.h"
#include "ui/nuklear/sdl_backend.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <SDL3/SDL.h>
#include <array>
#include <fstream>
#include <iterator>
#include <string>

namespace {
using wormedit::ui::nuklear::editor_font_t;
struct font_fixture_t {
    SDL_Window* window{};
    SDL_Renderer* renderer{};
    nk_context* context{};
    editor_font_t font;
    font_fixture_t() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        window = SDL_CreateWindow("font test", 640, 240, SDL_WINDOW_HIDDEN);
        REQUIRE(window);
        renderer = SDL_CreateRenderer(window, "software");
        REQUIRE(renderer);
        context = nk_sdl_init(window, renderer, nk_sdl_allocator());
        REQUIRE(context);
        font.initialize(context, std::filesystem::path{WORMEDIT_SOURCE_ROOT} / "assets/fonts");
    }
    ~font_fixture_t() {
        nk_sdl_shutdown(context);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
auto font_width(const editor_font_t& font, std::string_view text) -> float {
    const auto* handle = font.handle();
    return handle->width(handle->userdata, handle->height, text.data(), static_cast<int>(text.size()));
}
void label(nk_context* context, const char* text) {
    if (nk_begin(context, "text", nk_rect(0, 0, 640, 240), 0)) {
        nk_layout_row_dynamic(context, 25, 1);
        nk_label(context, text, NK_TEXT_LEFT);
    }
    nk_end(context);
}
void check_font_handles(editor_font_t& font, nk_context* context, nk_context* overlay,
                        const nk_user_font* stable) {
    CHECK(font.handle() == stable);
    CHECK(context->style.font == stable);
    CHECK(overlay->style.font == stable);
    CHECK(font.glyph_count() < 40);
}
auto check_glyph(const editor_font_t& font, nk_rune rune) -> std::pair<float, float> {
    REQUIRE(font.supported(rune));
    const auto* glyph = font.glyph(rune);
    REQUIRE(glyph);
    CHECK(glyph->codepoint == rune);
    CHECK(glyph->x1 > glyph->x0);
    CHECK(glyph->y1 > glyph->y0);
    return {glyph->u0, glyph->v0};
}
void check_cjk_glyphs(const editor_font_t& font) {
    std::set<std::pair<float, float>> positions;
    for (const nk_rune rune : {0x65e5u, 0x672cu, 0x8a9eu, 0x4e2du, 0x7e41u, 0xd55cu, 0xad6du})
        positions.insert(check_glyph(font, rune));
    CHECK(positions.size() == 7);
}
void check_mixed_metrics(const editor_font_t& font, float latin_width) {
    CHECK(font_width(font, "Wormedit 10K 123") == Catch::Approx(latin_width));
    CHECK(font_width(font, "A日B") == Catch::Approx(font_width(font, "AB") + font_width(font, "日")));
}
}

TEST_CASE("Bundled CJK fallback covers Japanese Chinese and Hangul before submission", "[font][cjk]") {
    font_fixture_t fixture;
    auto& font = fixture.font;
    const auto* stable_handle = font.handle();
    const auto latin_width = font_width(font, "Wormedit 10K 123");
    REQUIRE(font.glyph_count() == 1); // No unconditional CJK bake.
    label(fixture.context, "日本語 音楽 中文 繁體 한국어 소리.wav");
    nk_context overlay{};
    auto allocator = nk_sdl_allocator();
    REQUIRE(nk_init(&overlay, &allocator, stable_handle));
    overlay.userdata = fixture.context->userdata;
    label(&overlay, "通知: 새로운 音源");
    font.refresh(fixture.context, &overlay);
    check_font_handles(font, fixture.context, &overlay, stable_handle);
    check_cjk_glyphs(font);
    check_mixed_metrics(font, latin_width);
    // Submit queued commands after the rebuild; no command may retain the destroyed font.
    wormedit::ui::nuklear::render_sdl_backend(fixture.context, NK_ANTI_ALIASING_ON, &overlay);
    label(fixture.context, "追加 漢字");
    font.refresh(fixture.context);
    CHECK(font.handle() == stable_handle);
    CHECK(font.glyph(0x8ffdu)->codepoint == 0x8ffdu);
    nk_sdl_render(fixture.context, NK_ANTI_ALIASING_ON);
    nk_free(&overlay);
}

TEST_CASE("CJK discovery bounds unsupported invalid and excessive text", "[font][cjk]") {
    font_fixture_t fixture;
    auto& font = fixture.font;
    font.observe(std::string_view{"\xff\xfe", 2});
    font.observe("😀");
    font.refresh(fixture.context);
    CHECK(font.glyph_count() == 1);
    CHECK(font.glyph(0x1f600)->codepoint == 0x25a1);
    for (nk_rune rune = 0x4e00; rune < 0x9fff && !font.limited(); ++rune) {
        if (!font.supported(rune)) continue;
        std::array<char, 4> bytes{};
        const auto length = nk_utf_encode(rune, bytes.data(), static_cast<int>(bytes.size()));
        font.observe({bytes.data(), static_cast<std::size_t>(length)});
    }
    CHECK(font.limited());
    CHECK(font.glyph_count() == editor_font_t::glyph_limit);
    font.refresh(fixture.context);
    CHECK(font.glyph_count() == editor_font_t::glyph_limit);
}

TEST_CASE("CJK font coverage rejects absent truncated and corrupt directories", "[font][cjk]") {
    CHECK(editor_font_t::coverage({}).empty());
    std::ifstream input(std::filesystem::path{WORMEDIT_SOURCE_ROOT} /
                        "assets/fonts/NotoSansCJKjp-Regular.otf", std::ios::binary);
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>{input}, {}};
    REQUIRE_FALSE(editor_font_t::coverage(bytes).empty());
    CHECK(editor_font_t::coverage(std::span{bytes}.first(24)).empty());
    bytes[20] = bytes[21] = bytes[22] = bytes[23] = 0xff;
    CHECK(editor_font_t::coverage(bytes).empty());
}

TEST_CASE("Missing CJK bundle retains usable Latin fallback", "[font][cjk]") {
    font_fixture_t fixture;
    const auto missing = std::filesystem::temp_directory_path() /
        ("wormedit-cjk-missing-" + std::to_string(SDL_GetTicksNS()));
    fixture.font.initialize(fixture.context, missing);
    CHECK_FALSE(fixture.font.supported(0x65e5));
    CHECK(font_width(fixture.font, "Readable Latin") > 0.0f);
    fixture.font.observe("日本語");
    fixture.font.refresh(fixture.context);
    CHECK(fixture.font.glyph_count() == 0);
    CHECK(fixture.font.glyph(0x65e5)->codepoint == '?');
}
