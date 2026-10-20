#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/theme.h"
#include <array>
#include <wormedit/core/lane_profiles.h>
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
TEST_CASE("Semantic palettes alias classes and follow each object class", "[theme][semantic-skins]") {
    theme_fixture_t fixture;
    fixture.write("classes", R"({"version":2,"name":"Classes","lane_classes":{
      "white":{"note":"#01020304","long_note":"#09080706","label":"#102030"},
      "red":{"lane":"#404142"}},"timing":{"stop":{"label":"#223344"}}})");
    load(fixture.directory);
    REQUIRE(select_field_scheme("classes"));
    const std::array lanes{field_lane_t{7, 2, 3, 1, 11}, field_lane_t{0, 0, 1, 0, 12},
        field_lane_t{1, 0, 6, 0, 13}, field_lane_t{2, 2, 2, 0, 14}};
    bind_field_layout(lanes);
    check_color(compose_key_color(1, 12).note, nk_rgba(1, 2, 3, 4));
    check_color(compose_key_color(6, 13).note, compose_key_color(1, 12).note);
    check_color(compose_key_color(3, 11).lane, nk_rgb(64, 65, 66));
    check_color(compose_key_color(2, 14).note, compose_key_color(2).note);
    const wormedit::ui::compose_note_view_t hold{.lane_id = {12}, .key_color = wormedit::lane_key_color_t::white, .kind = wormedit::note_kind_t::hold};
    check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, hold), nk_rgba(9, 8, 7, 6));
    expect_theme(!field_uses_positions());
    expect_theme(field_scheme_supports_layout("classes", lanes));
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
TEST_CASE("Field theme switching discards semantic overrides and unchanged selection is stable", "[theme]") {
    theme_fixture_t fixture;
    fixture.write("partial", R"({"version":2,"name":"Partial","lane_classes":{"white":{"note":"#123456"}}})");
    load(fixture.directory);
    REQUIRE(select_field_scheme("partial"));
    const std::array lanes{field_lane_t{0, 0, 1, 0, 1}, field_lane_t{1, 0, 2, 0, 2}};
    bind_field_layout(lanes);
    check_color(compose_key_color(1, 1).note, nk_rgb(18, 52, 86));
    const auto generation = field_generation();
    REQUIRE(select_field_scheme("partial"));
    expect_theme(field_generation() == generation);
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

namespace {
void check_bmse_profile(const wormedit::lane_input_profile_t& profile) {
    std::vector<field_lane_t> lanes;
    for (const auto& lane : profile.layout.lanes)
        lanes.push_back({lane.display_order, static_cast<std::uint8_t>(lane.stage),
            static_cast<std::uint8_t>(lane.key_color), static_cast<std::uint8_t>(lane.role), lane.id.value});
    bind_field_layout(lanes);
    for (const auto& lane : lanes) {
        INFO("Lane " << lane.lane_id << " stage " << static_cast<int>(lane.stage));
        const auto color = compose_key_color(lane.key_color, lane.lane_id);
        if (lane.role != 0 && lane.role != 1) {
            check_color(color.note, compose_key_color(lane.key_color).note);
            continue;
        }
        const auto expected = lane.role == 1 ? nk_rgb(128, 0, 0)
                              : lane.key_color == 2 ? nk_rgb(0, 0, 255)
                                                    : nk_rgb(128, 128, 128);
        check_color(color.note, expected);
        const wormedit::ui::compose_note_view_t note{.lane_id = {lane.lane_id}, .key_color = static_cast<wormedit::lane_key_color_t>(lane.key_color)};
        check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, note), expected);
        const wormedit::ui::compose_hold_view_t hold{.lane_id = {lane.lane_id}, .key_color = static_cast<wormedit::lane_key_color_t>(lane.key_color)};
        check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, hold), expected);
    }
}
}
TEST_CASE("BMSE colors real BMS 10K stages with overlapping local lane numbers", "[theme]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(select_field_scheme("ibmsc-bmse"));
    using namespace wormedit;
    for (const auto left : {scratch_placement_t::outside, scratch_placement_t::inside}) {
        for (const auto right : {scratch_placement_t::outside, scratch_placement_t::inside}) {
            check_bmse_profile(make_bms_10k_standard_profile(left, right));
            check_bmse_profile(make_bms_10k_free_zone_profile(left, right));
            check_bmse_profile(make_bms_10k_foot_pedal_profile(left, right));
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
        nk_rgba(30, 37, 50, 160), nk_rgba(48, 34, 40, 160)};
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

TEST_CASE("osu mania skin colors semantic green and yellow lanes yellow at any count", "[theme][semantic-skins]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(select_field_scheme("osu-mania"));
    const std::array lanes{field_lane_t{0, 0, 1, 0, 1}, field_lane_t{1, 0, 2, 0, 2},
        field_lane_t{2, 0, 8, 0, 3}, field_lane_t{3, 0, 7, 0, 4}, field_lane_t{4, 0, 3, 1, 5}};
    bind_field_layout(lanes);
    check_color(compose_key_color(8, 3).note, nk_rgb(250, 204, 21));
    check_color(compose_key_color(7, 4).note, nk_rgb(250, 204, 21));
    check_color(compose_key_color(1, 1).note, nk_rgb(226, 232, 240));
    check_color(compose_key_color(2, 2).note, nk_rgb(59, 130, 246));
    expect_theme(field_scheme_supports_layout("osu-mania", lanes));
    expect_theme(!field_uses_positions());
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

TEST_CASE("Skins suggest scratch sides separately without changing field color identity", "[theme][scratch-presentation]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(select_field_scheme("wormedit"));
    expect_theme(!field_scratch_side(0).has_value());
    REQUIRE(select_field_scheme("ibmsc-bmse"));
    expect_theme(!field_scratch_side(0).has_value());
    expect_theme(!field_scratch_side(1).has_value());
    expect_theme(!field_scratch_side(2).has_value());
    expect_theme(!field_scratch_side(3).has_value());
    REQUIRE(select_field_scheme("ibmsc-iidx"));
    expect_theme(!field_scratch_side(1).has_value());
    expect_theme(!field_scratch_side(2).has_value());
}

TEST_CASE("Skin scratch metadata validates atomically and supports independent sides", "[theme][scratch-presentation]") {
    theme_fixture_t fixture;
    fixture.write("right-left", R"({"version":2,"name":"Right Left","scratch_placement":{"1p":"right","2p":"left"}})");
    fixture.write("bad-side", R"({"version":2,"name":"Bad Side","scratch_placement":{"1p":"outside"}})");
    fixture.write("bad-player", R"({"version":2,"name":"Bad Player","scratch_placement":{"3p":"left"}})");
    fixture.write("bad-shape", R"({"version":2,"name":"Bad Shape","scratch_placement":["left","right"]})");
    diagnostic_capture_t diagnostics;
    load(fixture.directory);
    REQUIRE(field_schemes().size() == 2);
    REQUIRE(select_field_scheme("right-left"));
    expect_theme(field_scratch_side(1) == true);
    expect_theme(field_scratch_side(2) == false);
    expect_theme(!select_field_scheme("bad-side"));
    expect_theme(field_scheme_id() == "wormedit");
    expect_theme(!field_scratch_side(1).has_value());
    expect_theme(diagnostics.output.str().find("bad-side.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("bad-player.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("bad-shape.json") != std::string::npos);
}

namespace {
struct imported_lane_palette_t {
    const char* id;
    nk_color scratch;
    nk_color white;
    nk_color black;
};

void check_imported_stages(const wormedit::lane_input_profile_t& profile,
                           const imported_lane_palette_t& expected) {
    std::vector<field_lane_t> lanes;
    for (const auto& lane : profile.layout.lanes)
        lanes.push_back({lane.display_order, static_cast<std::uint8_t>(lane.stage),
            static_cast<std::uint8_t>(lane.key_color), static_cast<std::uint8_t>(lane.role), lane.id.value});
    // Presentation can enumerate lanes in a different order; lookup follows authored IDs.
    std::ranges::reverse(lanes);
    bind_field_layout(lanes);
    std::array<int, 2> scratches{};
    for (const auto& lane : lanes) {
        if (lane.role != 0 && lane.role != 1) continue;
        INFO(expected.id << " lane " << lane.lane_id << " stage " << static_cast<int>(lane.stage));
        const auto color = compose_key_color(lane.key_color, lane.lane_id);
        if (lane.role == 1) {
            ++scratches[lane.stage == 2 ? 1 : 0];
            check_color(color.note, expected.scratch);
        } else if (expected.white.a != 0) {
            const auto key = lane.key_color == 2 ? expected.black : expected.white;
            // Check alpha as well: source hidden-note opacity must not dim normal notes.
            check_color(color.note, key);
        }
        const wormedit::ui::compose_note_view_t note{.lane_id = {lane.lane_id}, .key_color = static_cast<wormedit::lane_key_color_t>(lane.key_color)};
        check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, note), color.note);
        const wormedit::ui::compose_hold_view_t hold{.lane_id = {lane.lane_id}, .key_color = static_cast<wormedit::lane_key_color_t>(lane.key_color)};
        check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, hold), color.hold_body);
    }
    expect_theme(scratches[0] == 1 && scratches[1] == 1);
}
}

TEST_CASE("Every imported skin binds DP5 and DP7 scratches on both stages by role",
          "[theme][bundled-lane-mapping]") {
    theme_fixture_t restore;
    load(source_themes());
    const std::array palettes{
        imported_lane_palette_t{"ibmsc-bmse", nk_rgb(128, 0, 0), nk_rgb(128, 128, 128), nk_rgb(0, 0, 255)},
        imported_lane_palette_t{"ibmsc-bmsemdk", nk_rgb(255, 0, 0), nk_rgb(96, 96, 96), nk_rgb(0, 0, 255)},
        imported_lane_palette_t{"ibmsc-bmsemdkp", nk_rgb(255, 0, 0), nk_rgb(96, 96, 96), nk_rgb(0, 0, 255)},
        imported_lane_palette_t{"ibmsc-forme", nk_rgb(245, 89, 89), nk_rgb(170, 170, 170), nk_rgb(98, 137, 255)},
        imported_lane_palette_t{"ibmsc-iidx", nk_rgb(245, 89, 89), nk_rgb(170, 170, 170), nk_rgb(98, 137, 255)},
        imported_lane_palette_t{"ibmsc-pomu", nk_rgb(255, 0, 0), nk_rgb(128, 128, 128), nk_rgb(0, 0, 255)},
        imported_lane_palette_t{"ibmsc-myo2-4k", nk_rgba(248, 113, 113, 255), {}, {}},
        imported_lane_palette_t{"ibmsc-o2mania", nk_rgba(128, 128, 128, 255), {}, {}}};
    using namespace wormedit;
    for (const auto& palette : palettes) {
        REQUIRE(select_field_scheme(palette.id));
        for (const auto left : {scratch_placement_t::outside, scratch_placement_t::inside}) {
            for (const auto right : {scratch_placement_t::outside, scratch_placement_t::inside}) {
                check_imported_stages(make_bms_10k_standard_profile(left, right), palette);
                check_imported_stages(make_bms_14k_profile(left, right), palette);
            }
        }
    }
}

TEST_CASE("Imported hidden-note opacity never dims normal white notes or holds",
          "[theme][semantic-skins][opacity]") {
    theme_fixture_t restore;
    load(source_themes());
    for (const auto* id : {"ibmsc-bmse", "ibmsc-bmsemdk", "ibmsc-bmsemdkp",
                           "ibmsc-forme", "ibmsc-iidx", "ibmsc-pomu"}) {
        INFO(id);
        REQUIRE(select_field_scheme(id));
        for (const auto key : {wormedit::lane_key_color_t::white, wormedit::lane_key_color_t::pms_white}) {
            const wormedit::ui::compose_note_view_t visible{.key_color = key};
            auto hidden = visible;
            hidden.kind = wormedit::note_kind_t::invisible;
            auto long_note = visible;
            long_note.kind = wormedit::note_kind_t::hold;
            const wormedit::ui::compose_hold_view_t hold{.key_color = key};
            const auto normal = worm_ui_nuklear_compose_color_view_t::normal;
            expect_theme(compose_note_base_fill(normal, visible).a == 255);
            expect_theme(compose_note_base_fill(normal, long_note).a == 255);
            expect_theme(compose_note_base_fill(normal, hold).a == 255);
            expect_theme(compose_note_base_fill(normal, hidden).a == 153);
        }
    }
}

namespace {
auto palette_lanes(const wormedit::lane_input_profile_t& profile) -> std::vector<field_lane_t> {
    std::vector<field_lane_t> result;
    for (const auto& lane : profile.layout.lanes)
        result.push_back({lane.display_order, static_cast<std::uint8_t>(lane.stage),
            static_cast<std::uint8_t>(lane.key_color), static_cast<std::uint8_t>(lane.role), lane.id.value});
    return result;
}
}

TEST_CASE("Numbered palette compatibility requires exact classes and stage count", "[theme][semantic-skins]") {
    theme_fixture_t restore;
    load(source_themes());
    const std::array four{field_lane_t{0, 1, 1, 0, 1}, field_lane_t{1, 1, 2, 0, 2},
        field_lane_t{2, 1, 2, 0, 3}, field_lane_t{3, 1, 1, 0, 4}};
    expect_theme(field_scheme_supports_layout("ibmsc-myo2-4k", four));
    auto wrong = four;
    wrong[2].key_color = 1;
    expect_theme(!field_scheme_supports_layout("ibmsc-myo2-4k", wrong));
    wrong = four;
    wrong[2].stage = wrong[3].stage = 2;
    expect_theme(!field_scheme_supports_layout("ibmsc-myo2-4k", wrong));
    wrong = four;
    wrong[0].role = 1;
    expect_theme(!field_scheme_supports_layout("ibmsc-myo2-4k", wrong));
    expect_theme(!field_scheme_supports_layout("missing", four));
    REQUIRE(select_field_scheme("ibmsc-myo2-4k"));
    bind_field_layout(four);
    check_color(compose_key_color(1, 1).note, nk_rgb(242, 116, 179));
    // An event from a different layout segment cannot borrow this numbered slot.
    check_color(compose_key_color(2, 1).note, compose_key_color(2).note);
    bind_field_layout(wrong);
    check_color(compose_key_color(1, 4).note, compose_key_color(1).note);
    expect_theme(!select_field_scheme_for_layout("ibmsc-myo2-4k", "ibmsc-forme", wrong));
    expect_theme(field_scheme_id() == "ibmsc-forme");
    expect_theme(!select_field_scheme_for_layout("missing", "ibmsc-myo2-4k", wrong));
    expect_theme(field_scheme_id() == "wormedit");
    expect_theme(!select_field_scheme_for_layout("", "ibmsc-forme", four));
    expect_theme(field_scheme_id() == "ibmsc-forme");
}

TEST_CASE("Ungated positional files and malformed class descriptors reject atomically", "[theme][semantic-skins]") {
    theme_fixture_t fixture;
    fixture.write("legacy", R"({"version":2,"name":"Legacy","keys":{"1p":{"1":{"note":"#123456"}}}})");
    fixture.write("class", R"({"version":2,"name":"Class","lane_classes":{"purple":{"note":"#123456"}}})");
    fixture.write("guard", R"({"version":2,"name":"Guard","numbered_layout":{"stages":[["white"]],"colors":[[]]}})");
    fixture.write("rotate", R"({"version":2,"name":"Rotate","rotate_dp5":"yes"})");
    diagnostic_capture_t capture;
    load(fixture.directory);
    expect_theme(field_schemes().size() == 1);
    expect_theme(capture.output.str().find("ungated positional") != std::string::npos);
    expect_theme(capture.output.str().find("guard.json") != std::string::npos);
    expect_theme(capture.output.str().find("class.json") != std::string::npos);
    expect_theme(capture.output.str().find("rotate.json") != std::string::npos);
}

TEST_CASE("BMSE rotation default applies to DP5 only and preserves authored lanes", "[theme][semantic-skins]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(select_field_scheme("ibmsc-bmse"));
    const auto dp5 = palette_lanes(wormedit::make_bms_10k_standard_profile());
    const auto osu_dp5 = palette_lanes(*wormedit::make_osu_mania_profile(12, true));
    const auto dp7 = palette_lanes(wormedit::make_bms_14k_profile());
    const auto sp7 = palette_lanes(wormedit::make_bms_7k_profile());
    expect_theme(field_scratch_side(1, dp5) == false);
    expect_theme(field_scratch_side(2, dp5) == false);
    expect_theme(field_scratch_side(1, osu_dp5) == false);
    expect_theme(field_scratch_side(2, osu_dp5) == false);
    expect_theme(!field_scratch_side(1, dp7).has_value());
    expect_theme(!field_scratch_side(2, dp7).has_value());
    expect_theme(!field_scratch_side(1, sp7).has_value());
    expect_theme(!field_scratch_side(1).has_value());
}

TEST_CASE("BMS semantic palettes color osu layouts without source-family restrictions", "[theme][semantic-skins]") {
    theme_fixture_t restore;
    load(source_themes());
    const auto profile = wormedit::make_osu_mania_profile(10);
    REQUIRE(profile.has_value());
    const auto lanes = palette_lanes(*profile);
    REQUIRE(select_field_scheme_for_layout("ibmsc-forme", "wormedit", lanes));
    for (const auto& lane : lanes) {
        const auto key = compose_key_color(lane.key_color, lane.lane_id);
        check_color(key.note, compose_key_color(lane.key_color).note);
        if (lane.key_color == 1) check_color(key.note, nk_rgb(170, 170, 170));
        if (lane.key_color == 2) check_color(key.note, nk_rgb(98, 137, 255));
    }
}

TEST_CASE("Dynamic-layout events sharing an authored ID retain their own semantic class", "[theme][semantic-skins]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(select_field_scheme("ibmsc-forme"));
    const std::array current{field_lane_t{0, 1, 1, 0, 77}};
    bind_field_layout(current);
    check_color(compose_key_color(1, 77).note, nk_rgb(170, 170, 170));
    check_color(compose_key_color(2, 77).note, nk_rgb(98, 137, 255));
    const wormedit::ui::compose_note_view_t white{.lane_id = {77}, .key_color = wormedit::lane_key_color_t::white};
    const wormedit::ui::compose_note_view_t blue{.lane_id = {77}, .key_color = wormedit::lane_key_color_t::black};
    check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, white), compose_key_color(1).note);
    check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, blue), compose_key_color(2).note);
    const wormedit::ui::compose_hold_view_t white_hold{.lane_id = {77}, .key_color = wormedit::lane_key_color_t::white};
    const wormedit::ui::compose_hold_view_t blue_hold{.lane_id = {77}, .key_color = wormedit::lane_key_color_t::black};
    check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, white_hold), compose_key_color(1).hold_body);
    check_color(compose_note_base_fill(worm_ui_nuklear_compose_color_view_t::normal, blue_hold), compose_key_color(2).hold_body);
}

TEST_CASE("Bundled selection boxes stay translucent and distinguish selection modes", "[theme][selection-feedback]") {
    theme_fixture_t restore;
    load(source_themes());
    for (const auto& scheme : field_schemes()) {
        if (!scheme.id.starts_with("ibmsc-")) continue;
        expect_theme(select_field_scheme(scheme.id));
        const auto replace = compose(ComposeColor::selection_box);
        const auto add = compose(ComposeColor::selection_add);
        const auto toggle = compose(ComposeColor::selection_toggle);
        expect_theme(replace.a > 0 && replace.a <= 48);
        expect_theme(add.a > 0 && add.a <= 48);
        expect_theme(toggle.a > 0 && toggle.a <= 48);
        expect_theme(replace.b > replace.r);
        expect_theme(add.g > add.r);
        expect_theme(toggle.r > toggle.g);
        expect_theme(compose(ComposeColor::selection_box_border).a >= 192);
    }
}
