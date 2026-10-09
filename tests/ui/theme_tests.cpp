#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/theme.h"
#include <array>
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
TEST_CASE("Field theme discovery loads all nine bundled schemes and preserves chrome", "[theme]") {
    theme_fixture_t restore;
    load(source_themes());
    REQUIRE(field_schemes().size() == 9);
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
                           field_lane_t{2, 0, 2, 0}, field_lane_t{3, 1, 1, 0},
                           field_lane_t{4, 1, 3, 1}};
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
    diagnostic_capture_t diagnostics;
    load(fixture.directory);
    REQUIRE(field_schemes().size() == 1);
    expect_theme(diagnostics.output.str().find("broken.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("version.json") != std::string::npos);
    expect_theme(diagnostics.output.str().find("type.json") != std::string::npos);
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
