#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/theme.h"
#include <array>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace wormedit::ui::theme;
namespace {
void expect_theme(bool value) {
    CHECK(value);
}
auto source_themes() -> std::filesystem::path {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
           "assets/themes";
}
struct theme_fixture_t {
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("wormedit-theme-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    theme_fixture_t() {
        std::filesystem::create_directories(directory / "field");
        std::filesystem::copy_file(source_themes() / "field/wormedit.json",
                                   directory / "field/wormedit.json");
    }
    ~theme_fixture_t() {
        load(source_themes());
        std::filesystem::remove_all(directory);
    }
    void write(std::string_view name, std::string_view contents) const {
        std::ofstream(directory / "field" / (std::string(name) + ".json")) << contents;
    }
};
void check_color(nk_color a, nk_color b) {
    expect_theme(a.r == b.r);
    expect_theme(a.g == b.g);
    expect_theme(a.b == b.b);
    expect_theme(a.a == b.a);
}
struct diagnostic_capture_t {
    std::ostringstream output;
    std::streambuf* previous = std::cerr.rdbuf(output.rdbuf());
    ~diagnostic_capture_t() {
        std::cerr.rdbuf(previous);
    }
};
} // namespace
TEST_CASE("Field theme discovery loads all ten bundled schemes and preserves chrome", "[theme]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(field_schemes().size() == 10);
    expect_theme(field_schemes().front().id == "wormedit");
    const auto chrome = ui(UiColor::background);
    const auto generation = field_generation();
    REQUIRE(select_field_scheme("ibmsc-iidx"));
    expect_theme(field_generation() > generation);
    expect_theme(field_scheme_id() == "ibmsc-iidx");
    check_color(ui(UiColor::background), chrome);
    REQUIRE(select_field_scheme(""));
    expect_theme(field_scheme_id() == "wormedit");
    check_color(compose(ComposeColor::measure_line), nk_rgb(95, 107, 128));
    check_color(compose_key_color(1).note, nk_rgb(226, 232, 240));
}
TEST_CASE("Field theme positions separate scratch and player sides and inherit missing roles",
          "[theme]") {
    theme_fixture_t fixture;
    fixture.write(
        "positions",
        R"({"version":2,"name":"Positions","key_colors":{"white":{"lane_alternate":"#050607"}},"keys":{
        "1p":{"1":{"note":"#01020304","long_note":"#09080706","label":"#102030"},"scratch":{"lane":"#404142"}},
        "2p":{"1":{"note":"#aabbcc"}}},"timing":{"stop":{"label":"#223344"}}})");
    load(fixture.directory);
    REQUIRE(select_field_scheme("positions"));
    const std::array lanes{field_lane_t{0, 0, 3, 1}, field_lane_t{1, 0, 1, 0},
                           field_lane_t{2, 0, 2, 0}, field_lane_t{3, 2, 1, 0},
                           field_lane_t{4, 2, 3, 1}};
    bind_field_layout(lanes);
    const auto first = compose_key_color(1, 2);
    check_color(first.note, nk_rgba(1, 2, 3, 4));
    check_color(first.hold_body, compose_key_color(1).hold_body);
    check_color(first.lane_alternate, nk_rgb(5, 6, 7));
    check_color(compose_key_color(3, 1).lane, nk_rgb(64, 65, 66));
    check_color(compose_key_color(1, 4).note, nk_rgb(170, 187, 204));
    check_color(compose_key_color(2, 3).lane, compose_key_color(2).lane);
    REQUIRE(compose_timing_color(3).label);
    check_color(*compose_timing_color(3).label, nk_rgb(34, 51, 68));
    const WormUiComposeNoteView hold{.display_lane_number = 2, .key_color = 1, .kind = 1};
    check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, hold),
                nk_rgba(9, 8, 7, 6));
}
TEST_CASE("Field theme invalid files reject atomically and unknown selection warns once",
          "[theme]") {
    theme_fixture_t fixture;
    fixture.write("broken", R"({"version":2,"name":"Broken","roles":{"viewport":"#nothex"}})");
    fixture.write("version", R"({"version":1,"name":"Old"})");
    fixture.write("type", R"({"version":2,"name":"Bad","bgm":[]})");
    fixture.write("key-count-zero", R"({"version":2,"name":"Zero","key_count":0})");
    fixture.write("key-count-text", R"({"version":2,"name":"Text","key_count":"10"})");
    fixture.write("key-count-negative", R"({"version":2,"name":"Negative","key_count":-1})");
    diagnostic_capture_t diagnostics;
    load(fixture.directory);
    REQUIRE(field_schemes().size() == 1);
    expect_theme(diagnostics.output.str().find("broken.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("version.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("type.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("key-count-zero.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("key-count-text.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("key-count-negative.json") != std::string::npos);
    const auto initial = diagnostics.output.str();
    expect_theme(!(select_field_scheme("broken")));
    expect_theme(diagnostics.output.str() == initial);
    expect_theme(!(select_field_scheme("missing")));
    const auto once = diagnostics.output.str();
    expect_theme(!(select_field_scheme("missing")));
    expect_theme(diagnostics.output.str() == once);
    expect_theme(field_scheme_id() == "wormedit");
    check_color(compose(ComposeColor::viewport), nk_rgb(22, 25, 33));
}
TEST_CASE("Field theme switching discards old position colors and binds scratch inside",
          "[theme]") {
    theme_fixture_t fixture;
    fixture.write("partial",
                  R"({"version":2,"name":"Partial","keys":{"1p":{"1":{"note":"#123456"}}}})");
    load(fixture.directory);
    REQUIRE(select_field_scheme("partial"));
    const std::array lanes{field_lane_t{0, 0, 1, 0}, field_lane_t{1, 0, 2, 0},
                           field_lane_t{2, 0, 3, 1}};
    bind_field_layout(lanes);
    check_color(compose_key_color(1, 1).note, nk_rgb(18, 52, 86));
    REQUIRE(select_field_scheme("wormedit"));
    bind_field_layout(lanes);
    check_color(compose_key_color(1, 1).note, compose_key_color(1).note);
}

TEST_CASE("Snap grid uses note hues and stronger beats with immediate style invalidation", "[theme][snap-grid]") {
    set_snap_colored_grid(true);
    const auto generation = field_generation();
    set_snap_colored_grid(true);
    expect_theme(field_generation() == generation);
    const auto red = compose_grid_line_style(1, 1, 4);
    const auto blue = compose_grid_line_style(0, 0, 8);
    check_color(red.color, nk_rgba(235, 73, 79, 89));
    check_color(blue.color, nk_rgba(75, 139, 240, 89));
    expect_theme(red.thickness > blue.thickness);
    expect_theme(compose_grid_line_style(3, 2, 4).thickness > red.thickness);
    set_snap_colored_grid(false);
    expect_theme(field_generation() > generation);
    expect_theme(!snap_colored_grid());
    check_color(compose_grid_line_style(3, 2, 4).color, compose(ComposeColor::measure_line));
    check_color(compose_grid_line_style(1, 1, 4).color, compose(ComposeColor::beat_line));
    check_color(compose_grid_line_style(0, 0, 8).color, compose(ComposeColor::subgrid_line));
    expect_theme(compose_grid_line_style(3, 2, 4).thickness == 1.2f);
    expect_theme(compose_grid_line_style(0, 0, 8).thickness == 0.7f);
    set_snap_colored_grid(true);
}

TEST_CASE("Snap grid opacity spans zero to full and invalidates styles only on changes", "[theme][snap-grid][opacity]") {
    set_snap_colored_grid(true);
    set_snap_grid_opacity(35);
    const auto generation = field_generation();
    set_snap_grid_opacity(35);
    expect_theme(field_generation() == generation);
    set_snap_grid_opacity(0);
    expect_theme(compose_grid_line_style(1, 1, 4).color.a == 0);
    expect_theme(field_generation() > generation);
    set_snap_grid_opacity(100);
    expect_theme(compose_grid_line_style(1, 1, 4).color.a == 255);
    const auto full_generation = field_generation();
    set_snap_grid_opacity(999);
    expect_theme(field_generation() == full_generation);
    set_snap_colored_grid(false);
    set_snap_grid_opacity(0);
    check_color(compose_grid_line_style(1, 1, 4).color, compose(ComposeColor::beat_line));
    set_snap_grid_opacity(35);
    set_snap_colored_grid(true);
}

TEST_CASE("BMSE scratch colors follow scratch roles on both sides and placements", "[theme]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(select_field_scheme("ibmsc-bmse"));
    const std::array outside{
        field_lane_t{0, 1, 3, 1}, field_lane_t{1, 1, 1, 0}, field_lane_t{2, 1, 2, 0},
        field_lane_t{3, 2, 1, 0}, field_lane_t{4, 2, 2, 0}, field_lane_t{5, 2, 3, 1}};
    const std::array inside{
        field_lane_t{0, 1, 1, 0}, field_lane_t{1, 1, 2, 0}, field_lane_t{2, 1, 3, 1},
        field_lane_t{3, 2, 3, 1}, field_lane_t{4, 2, 1, 0}, field_lane_t{5, 2, 2, 0}};
    for (const auto& lanes : {outside, inside}) {
        bind_field_layout(lanes);
        for (const auto& lane : lanes) {
            const auto color = compose_key_color(lane.key_color, lane.display_order + 1);
            const auto expected = lane.role == 1 ? nk_rgba(128, 0, 0, 128)
                                  : lane.key_color == 2 ? nk_rgba(0, 0, 255, 128)
                                                        : nk_rgba(128, 128, 128, 128);
            check_color(color.note, expected);
        }
    }
}

TEST_CASE("Wormedit lane fills are milder with matching missing-asset fallback", "[theme]") {
    theme_fixture_t fixture;
    load(source_themes());
    REQUIRE(select_field_scheme("wormedit"));
    const std::array expected_lanes{
        nk_rgba(33, 40, 53, 160), nk_rgba(44, 48, 58, 160), nk_rgba(30, 37, 50, 160),
        nk_rgba(48, 34, 40, 160), nk_rgba(30, 43, 47, 160), nk_rgba(47, 41, 32, 160),
        nk_rgba(44, 48, 58, 160), nk_rgba(47, 44, 31, 160), nk_rgba(29, 45, 36, 160),
        nk_rgba(30, 39, 55, 160), nk_rgba(48, 34, 40, 160)};
    std::array<key_color_t, 11> bundled;
    for (std::uint8_t key = 0; key < bundled.size(); ++key) {
        bundled[key] = compose_key_color(key);
        check_color(bundled[key].lane, expected_lanes[key]);
        const auto lane = bundled[key].lane;
        check_color(bundled[key].lane_alternate,
                    nk_rgba(lane.r + 4, lane.g + 4, lane.b + 4, lane.a));
        const auto chroma = std::max({lane.r, lane.g, lane.b}) -
                            std::min({lane.r, lane.g, lane.b});
        expect_theme(chroma <= 25);
        expect_theme(bundled[key].lane.a == 160);
        expect_theme(bundled[key].lane_alternate.a == 160);
        expect_theme(bundled[key].note.a == 255);
    }
    std::filesystem::remove(fixture.directory / "field/wormedit.json");
    load(fixture.directory);
    for (std::uint8_t key = 0; key < bundled.size(); ++key) {
        check_color(compose_key_color(key).lane, bundled[key].lane);
        check_color(compose_key_color(key).lane_alternate, bundled[key].lane_alternate);
    }
}

TEST_CASE("osu mania yellow center scheme colors only ten playable lanes", "[theme]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(select_field_scheme("osu-mania"));
    for (const bool split : {false, true}) {
        std::array<field_lane_t, 10> lanes;
        for (std::uint32_t index = 0; index < lanes.size(); ++index)
            lanes[index] = {index, static_cast<std::uint8_t>(split && index >= 5 ? 2 : 1), 1, 0};
        bind_field_layout(lanes);
        for (const auto& lane : lanes) {
            const auto index = lane.display_order;
            const auto expected = index == 4 || index == 5 ? nk_rgb(250, 204, 21)
                                  : index == 1 || index == 3 || index == 6 || index == 8
                                      ? nk_rgb(59, 130, 246) : nk_rgb(226, 232, 240);
            check_color(compose_key_color(1, index + 1).note, expected);
        }
        bind_field_layout(std::span(lanes).first(7));
        check_color(compose_key_color(1, 5).note, compose_key_color(1).note);
    }
}

TEST_CASE("Waveform opacity scales alpha without changing colors and invalidates retained fields", "[theme][feedback]") {
    set_field_waveform_opacity(75);
    const auto initial = field_generation();
    check_color(field_waveform_color(nk_rgba(87, 157, 196, 96)), nk_rgba(87, 157, 196, 72));
    set_field_waveform_opacity(0);
    expect_theme(field_generation() > initial);
    check_color(field_waveform_color(nk_rgba(146, 230, 251, 190)), nk_rgba(146, 230, 251, 0));
    set_field_waveform_opacity(999);
    check_color(field_waveform_color(nk_rgba(146, 230, 251, 190)), nk_rgba(146, 230, 251, 190));
    const auto unchanged = field_generation();
    set_field_waveform_opacity(100);
    expect_theme(field_generation() == unchanged);
    set_field_waveform_opacity(75);
}
