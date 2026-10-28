#include <wormedit/core/ui_layout_preferences.h>
#include <wormedit/core/preferences.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#include <limits>
using namespace wormedit;

TEST_CASE("Dock selection shows one panel or a bounded pair", "[core][dock-layout]") {
    ui_layout_preferences_t layout;
    layout.activate(dock_panel_t::images, false);
    CHECK(layout.is_visible(dock_panel_t::images));
    CHECK_FALSE(layout.is_visible(dock_panel_t::inspector));
    layout.activate(dock_panel_t::markers, true);
    CHECK(layout.visible_panels()[1] == dock_panel_t::markers);
    layout.show(dock_panel_t::layers);
    CHECK_FALSE(layout.is_visible(dock_panel_t::markers));
    CHECK(layout.is_visible(dock_panel_t::layers));
    layout.activate(dock_panel_t::images, true);
    CHECK_FALSE(layout.visible_panels()[1]);
}
TEST_CASE("Dock order and dimensions round trip through application preferences", "[core][dock-layout]") {
    auto preferences = program_preferences_t::with_defaults();
    auto& layout = preferences.layout.dock;
    layout.reorder(dock_panel_t::sounds, 0);
    layout.set_width(410);
    layout.set_split(0.65f);
    layout.set_side_by_side(true);
    const auto text = serialize_program_preferences(preferences);
    REQUIRE(text);
    const auto parsed = preferences_parse_effect_t::from(*text);
    CHECK(parsed.is_success());
    CHECK(parsed.warnings.empty());
    CHECK(parsed.preferences == preferences);
    CHECK(layout.visible_panels()[0] == dock_panel_t::sounds);
    CHECK(layout.order().front() == dock_panel_t::sounds);
}
TEST_CASE("Dock dimensions clamp finite input and ignore nonfinite input", "[core][dock-layout]") {
    ui_layout_preferences_t layout;
    layout.set_width(-100);
    CHECK(layout.width() == dock_minimum_width);
    layout.set_width(9000);
    CHECK(layout.width() == dock_maximum_width);
    layout.set_split(-1);
    CHECK(layout.split() == static_cast<float>(dock_minimum_split));
    layout.set_split(2);
    CHECK(layout.split() == static_cast<float>(dock_maximum_split));
    layout.set_width(std::numeric_limits<float>::infinity());
    CHECK(layout.width() == dock_maximum_width);
}
TEST_CASE("Malformed or oversized dock records retain defaults and other application preferences", "[core][dock-layout]") {
    auto record = nlohmann::json::parse(ui_layout_preferences_t{}.serialized());
    const auto invalid = GENERATE(0, 1, 2, 3, 4, 5, 6);
    switch (invalid) {
    case 0: record["version"] = 99; break;
    case 1: record["order"][0] = "sounds"; break;
    case 2: record["visible"] = {"sounds", "images", "markers"}; break;
    case 3: record["width"] = 100; break;
    case 4: record["split"] = "wide"; break;
    case 5: record["visible"] = {"sounds", "sounds"}; break;
    case 6: record["order"] = std::string(5000, 'x'); break;
    }
    CHECK_FALSE(ui_layout_preferences_t::from(record.dump()));
    const auto text = nlohmann::json{{"version", 1}, {"general", {{"recent_files_count", 8}}},
        {"layout", {{"compose_dock", record}}}}.dump();
    const auto parsed = preferences_parse_effect_t::from(text);
    CHECK(parsed.is_success());
    CHECK(parsed.preferences.layout.dock == ui_layout_preferences_t{});
    CHECK_FALSE(parsed.warnings.empty());
    CHECK(parsed.preferences.general.recent_files_count == 8);
}
TEST_CASE("Legacy opaque layout placeholders keep the default dock", "[core][dock-layout]") {
    const auto result = preferences_parse_effect_t::from(R"({"version":1,"layout":{"dock_layout":"old:inspector"}})");
    CHECK(result.is_success());
    CHECK(result.preferences.layout.dock == ui_layout_preferences_t{});
    CHECK_FALSE(result.warnings.empty());
}
TEST_CASE("Programmatic dock activation keeps an already selected sole panel open", "[core][dock-layout]") {
    ui_layout_preferences_t layout;
    layout.set_width(410);
    layout.activate(dock_panel_t::sounds, false);
    const auto before = layout;
    layout.activate(dock_panel_t::sounds, false);
    CHECK(layout == before);
}

TEST_CASE("Version one dock preferences retain their vertical stack", "[core][dock-layout]") {
    auto record = nlohmann::json::parse(ui_layout_preferences_t{}.serialized());
    record["version"] = 1;
    record.erase("side_by_side");
    record["width"] = 410;
    const auto parsed = ui_layout_preferences_t::from(record.dump());
    REQUIRE(parsed);
    CHECK_FALSE(parsed->is_side_by_side());
    CHECK(parsed->width() == 410);
    CHECK(parsed->visible_panels()[1] == dock_panel_t::sounds);
}
TEST_CASE("Dock arrangement rejects an invalid preference without changing other settings", "[core][dock-layout]") {
    auto record = nlohmann::json::parse(ui_layout_preferences_t{}.serialized());
    record["side_by_side"] = "columns";
    CHECK_FALSE(ui_layout_preferences_t::from(record.dump()));
}
TEST_CASE("Dock records saved before the mixer and generator panels keep their order and gain them last", "[core][dock-layout]") {
    auto record = nlohmann::json::parse(ui_layout_preferences_t{}.serialized());
    record["order"] = {"markers", "inspector", "sounds", "images", "measures", "layers"};
    const auto parsed = ui_layout_preferences_t::from(record.dump());
    REQUIRE(parsed);
    CHECK(parsed->order().front() == dock_panel_t::markers);
    CHECK(parsed->order()[dock_panel_count - 2] == dock_panel_t::mixer);
    CHECK(parsed->order().back() == dock_panel_t::generator);
    CHECK_FALSE(parsed->is_visible(dock_panel_t::mixer));
    CHECK_FALSE(parsed->is_visible(dock_panel_t::generator));
}
