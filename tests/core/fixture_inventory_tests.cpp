#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
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
    "bms/non-gameplay-roundtrip.bme",
    "bms/pms-nine-button.pms",
    "bms/repeated-bgm-ratio-stop-collision.bms",
    "stepmania/legacy-negative-bpm.sm",
    "stepmania/legacy-negative-bpm-multiple.sm",
    "stepmania/legacy-negative-stop.sm",
    "stepmania/matched-timing.sm",
    "stepmania/matched-timing.ssc",
    "stepmania/native-warp.ssc",
    "stepmania/negative-invalid-timing.ssc",
    "stepmania/negative-version-0.69.ssc",
    "stepmania/stop-inside-warp.ssc",
    "stepmania/discontinuity-order.ssc",
    "stepmania/versioned-metadata.ssc",
    "osu/mania-tags-bookmarks-kiai.osu",
    "osu/negative-non-mania.osu",
    "osu/versioned-stable-v14.osu",
    "osu/lazer-v128-fractional.osu",
    "osu/non-gameplay-roundtrip.osu",
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

constexpr std::array byte_fixture_paths{
    "bms/encoding/utf8-bom.bms",
    "bms/encoding/cp932-shift-jis.bms",
    "bms/encoding/euc-kr.bms",
    "bms/encoding/cp949-extension.bms",
    "bms/encoding/windows-1252.bms",
    "bms/encoding/ascii-ambiguous.bms",
    "bms/encoding/invalid-utf8-after-bom.bms",
    "bms/encoding/invalid-cp932-truncated-lead.bms",
};

auto read_binary_file(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto decode_lowercase_hex(std::string_view source) -> std::optional<std::string> {
    std::string bytes;
    std::optional<unsigned char> high_nibble;
    for (const auto character : source) {
        if (character == ' ' || character == '\n' || character == '\r' || character == '\t') {
            continue;
        }
        unsigned char nibble{};
        if (character >= '0' && character <= '9') {
            nibble = static_cast<unsigned char>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            nibble = static_cast<unsigned char>(character - 'a' + 10);
        } else {
            return std::nullopt;
        }
        if (!high_nibble.has_value()) {
            high_nibble = nibble;
        } else {
            bytes.push_back(static_cast<char>((*high_nibble << 4U) | nibble));
            high_nibble.reset();
        }
    }
    if (high_nibble.has_value()) {
        return std::nullopt;
    }
    return bytes;
}

} // namespace

TEST_CASE("Stage 0 chart fixtures have checked-in semantic expectations",
          "[unit][fixture][format]") {
    const auto root = std::filesystem::path(WORMEDIT_FIXTURE_DIR);
    const auto inventory = read_binary_file(root / "README.md");
    REQUIRE_FALSE(inventory.empty());

    for (const std::string_view relative_path : fixture_paths) {
        CAPTURE(relative_path);
        const auto contents = read_binary_file(root / relative_path);
        CHECK_FALSE(contents.empty());
        CHECK(inventory.find(relative_path) != std::string::npos);
    }
}

TEST_CASE("Stage 0 BMS encoding fixtures are inventoried as byte streams",
          "[unit][fixture][format][encoding]") {
    const auto root = std::filesystem::path(WORMEDIT_FIXTURE_DIR);
    const auto inventory = read_binary_file(root / "bms/encoding/README.md");
    REQUIRE_FALSE(inventory.empty());

    for (const std::string_view relative_path : byte_fixture_paths) {
        CAPTURE(relative_path);
        const auto fixture_path = std::filesystem::path(relative_path);
        const auto bytes = read_binary_file(root / fixture_path);
        CHECK_FALSE(bytes.empty());
        const auto fixture_name = fixture_path.filename().string();
        CHECK(inventory.find(fixture_name) != std::string::npos);

        const auto recipe_path = root / fixture_path.parent_path() / "byte-source" /
                                 (fixture_path.stem().string() + ".hex");
        const auto recipe = decode_lowercase_hex(read_binary_file(recipe_path));
        REQUIRE(recipe.has_value());
        CHECK(bytes == *recipe);
    }
}
