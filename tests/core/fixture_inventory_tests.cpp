#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::array fixture_paths{
    "bms/control-flow-fixed-values.bms",
    "bms/control-flow-mixed.bms",
    "bms/custom-channel.bme",
    "bms/legacy-long-note.bml",
    "bms/negative-malformed-pairs.bms",
    "bms/negative-orphan-control.bms",
    "bms/negative-undefined-resource.bms",
    "bms/pms-nine-button.pms",
    "bms/repeated-bgm-ratio-stop-collision.bms",
    "stepmania/legacy-negative-bpm.sm",
    "stepmania/legacy-negative-stop.sm",
    "stepmania/matched-timing.sm",
    "stepmania/matched-timing.ssc",
    "stepmania/native-warp.ssc",
    "stepmania/negative-invalid-timing.ssc",
    "stepmania/stop-inside-warp.ssc",
    "osu/mania-tags-bookmarks-kiai.osu",
    "osu/negative-non-mania.osu",
    "bmson/conflicting-continuation-flags.bmson",
    "bmson/negative-fractional-pulse-position.bmson",
    "bmson/negative-overflow-pulse-position.bmson",
    "bmson/negative-pulse-position.bmson",
    "bmson/negative-wrong-resolution-type.bmson",
    "bmson/null-resolution-default.bmson",
    "bmson/omitted-resolution-default.bmson",
    "bmson/published-vox-slicing.bmson",
    "bmson/slicing-and-mixed-sounds.bmson",
    "bmson/zero-resolution-default.bmson",
};

auto read_file(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Stage 0 chart fixtures have checked-in semantic expectations",
          "[unit][fixture][format]") {
    const auto root = std::filesystem::path(WORMEDIT_FIXTURE_DIR);
    const auto inventory = read_file(root / "README.md");
    REQUIRE_FALSE(inventory.empty());

    for (const std::string_view relative_path : fixture_paths) {
        CAPTURE(relative_path);
        const auto contents = read_file(root / relative_path);
        CHECK_FALSE(contents.empty());
        CHECK(inventory.find(relative_path) != std::string::npos);
    }
}
