#include <catch2/catch_test_macros.hpp>
#include <wormedit/core/interaction.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// All production code, including core actions, uses the cursor helpers in interaction.h.
const std::regex& raw_cursor_write() {
    static const std::regex pattern(
        R"(\b(?:\w*state|temporary|candidate|proposed)(?:\.|->))"
        R"((?:cursor(?:\.\w+)*|cursor_subrow|virtual_bms_cursor_row|viewport_scroll_pixels|fixed_cursor_screen_y_pixels|scroll_direction|viewport_height_pixels|pixels_per_standard_measure))"
        R"(\s*(?:=[^=]|\.reset\(|\.emplace\(|\+=|-=))");
    return pattern;
}

auto source_text(const fs::path& path) -> std::string {
    std::ifstream stream(path);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
}

auto offending_lines(const fs::path& root) -> std::vector<std::string> {
    std::vector<std::string> found;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        const auto& path = entry.path();
        if (!entry.is_regular_file()) continue;
        const auto cursor_owner = fs::path(WORMEDIT_SOURCE_DIR).parent_path() /
            "include/wormedit/core/interaction.h";
        if (path == cursor_owner) continue;
        const auto extension = path.extension().string();
        if (extension != ".cpp" && extension != ".h") continue;
        std::istringstream lines(source_text(path));
        std::string line;
        for (int number = 1; std::getline(lines, line); ++number)
            if (std::regex_search(line, raw_cursor_write()))
                found.push_back(path.lexically_relative(root).string() + ":" +
                                std::to_string(number) + ": " + line);
    }
    return found;
}

} // namespace

TEST_CASE("All editor cursor writes use the centralized cursor helpers",
          "[unit][core][interaction][cursor_write_guard]") {
    const auto source = fs::path(WORMEDIT_SOURCE_DIR);
    auto offenders = offending_lines(source);
    const auto headers = offending_lines(source.parent_path() / "include");
    offenders.insert(offenders.end(), headers.begin(), headers.end());
    INFO("Use capture_cursor_pose/restore_cursor_pose/place_cursor_at or an editor action:");
    for (const auto& line : offenders) INFO(line);
    CHECK(offenders.empty());
}

TEST_CASE("The cursor write guard recognizes raw writes",
          "[unit][core][interaction][cursor_write_guard]") {
    const struct { const char* source; bool is_write; } cases[] = {
        {"    state.cursor = position;", true},
        {"context.state.virtual_bms_cursor_row.reset();", true},
        {"temporary.viewport_scroll_pixels += 1;", true},
        {"proposed.fixed_cursor_screen_y_pixels = 48;", true},
        {"candidate.viewport_height_pixels = 400;", true},
        {"candidate.pixels_per_standard_measure = 192;", true},
        {"request.viewport_scroll_pixels = first;", false},
        {"if (state.cursor == other.cursor) {", false},
    };
    for (const auto& example : cases) {
        INFO(example.source);
        CHECK(std::regex_search(example.source, raw_cursor_write()) == example.is_write);
    }
}

TEST_CASE("Cursor moves and temporary restoration preserve the screen anchor",
          "[unit][core][interaction][cursor]") {
    wormedit::editor_interaction_state_t state;
    wormedit::configure_cursor_viewport(state, 400, wormedit::editor_field_scroll_direction_t::down);
    wormedit::set_cursor_subrow(state, .5);
    const auto before = wormedit::capture_cursor_pose(state);
    wormedit::place_cursor_at(state, {wormedit::measure_id_t{1}, 240});
    CHECK(state.fixed_cursor_screen_y_pixels == 320);
    CHECK(state.cursor_subrow == 0);
    wormedit::place_cursor_on_canvas_row(state, std::nullopt, 96000, .25);
    CHECK(state.virtual_bms_cursor_row == 96000);
    CHECK(state.fixed_cursor_screen_y_pixels == 320);
    wormedit::set_cursor_screen_anchor(state, 50, wormedit::editor_field_scroll_direction_t::up);
    wormedit::restore_cursor_pose(state, before);
    CHECK(wormedit::capture_cursor_pose(state) == before);
}

TEST_CASE("Cursor viewport changes keep the anchor fraction and preserve source geometry",
          "[unit][core][interaction][cursor]") {
    wormedit::editor_interaction_state_t state;
    wormedit::configure_cursor_viewport(state, 400, wormedit::editor_field_scroll_direction_t::down);
    wormedit::set_viewport_scroll_pixels(state, 80);
    wormedit::configure_cursor_viewport(state, 800, wormedit::editor_field_scroll_direction_t::down);
    CHECK(state.fixed_cursor_screen_y_pixels == 640);
    CHECK(state.viewport_scroll_pixels == 0);
    wormedit::configure_cursor_viewport(state, 800, wormedit::editor_field_scroll_direction_t::up);
    CHECK(state.fixed_cursor_screen_y_pixels == 160);
    CHECK(state.viewport_scroll_pixels == 0);
}

TEST_CASE("Cursor write guard includes individual row and measure writes",
          "[unit][core][interaction][cursor_write_guard]") {
    CHECK(std::regex_search("state.cursor.row = 240;", raw_cursor_write()));
    CHECK(std::regex_search("state.cursor.measure.value = 2;", raw_cursor_write()));
}
