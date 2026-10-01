#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Raw writes to the editor cursor fields on an interaction state. `core` owns them; everything
// else uses cursor_pose_t helpers or editor actions (see wormedit/core/interaction.h).
const std::regex& raw_cursor_write() {
    static const std::regex pattern(
        R"(\b(?:\w*state|temporary|candidate|proposed)(?:\.|->))"
        R"((?:cursor|cursor_subrow|virtual_bms_cursor_row|viewport_scroll_pixels))"
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
        const auto extension = path.extension().string();
        if (extension != ".cpp" && extension != ".h") continue;
        if (path.lexically_relative(root).begin()->string() == "core") continue;
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

TEST_CASE("Only core assigns the editor cursor fields directly",
          "[unit][core][interaction][cursor_write_guard]") {
    const auto offenders = offending_lines(fs::path(WORMEDIT_SOURCE_DIR));
    INFO("Use capture_cursor_pose/restore_cursor_pose/place_cursor_at or an editor action:");
    for (const auto& line : offenders) INFO(line);
    CHECK(offenders.empty());
}

TEST_CASE("The cursor write guard recognizes raw writes",
          "[unit][core][interaction][cursor_write_guard]") {
    CHECK(std::regex_search("    state.cursor = position;", raw_cursor_write()));
    CHECK(std::regex_search("context.state.virtual_bms_cursor_row.reset();", raw_cursor_write()));
    CHECK(std::regex_search("temporary.viewport_scroll_pixels += 1;", raw_cursor_write()));
    CHECK_FALSE(std::regex_search("request.viewport_scroll_pixels = first;", raw_cursor_write()));
    CHECK_FALSE(std::regex_search("if (state.cursor == other.cursor) {", raw_cursor_write()));
}
