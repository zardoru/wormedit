#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

constexpr std::array audio_fixture_names{
    "keysound-c4",      "keysound-e4",        "keysound-g4",
    "polyphonic-chord", "sliced-seek-source", "silence",
};

auto read_binary_file(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto has_ogg_signature(std::string_view bytes) -> bool {
    return bytes.size() >= 4 && bytes.substr(0, 4) == "OggS";
}

auto has_mp3_signature(std::string_view bytes) -> bool {
    const auto id3_tag = bytes.size() >= 3 && bytes.substr(0, 3) == "ID3";
    if (id3_tag) {
        return true;
    }

    const auto scan_size = std::min<std::size_t>(bytes.size(), 4096);
    for (std::size_t offset = 0; offset + 1 < scan_size; ++offset) {
        const auto first = static_cast<unsigned char>(bytes[offset]);
        const auto second = static_cast<unsigned char>(bytes[offset + 1]);
        const auto sync = first == 0xFFU && (second & 0xE0U) == 0xE0U;
        const auto layer_three = ((second >> 1U) & 0x03U) == 0x01U;
        if (sync && layer_three) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("Stage 0 synthetic audio fixtures are inventoried by file signature",
          "[unit][fixture][audio]") {
    const auto root = std::filesystem::path(WORMEDIT_FIXTURE_DIR);
    const auto inventory = read_binary_file(root / "audio/README.md");
    REQUIRE_FALSE(inventory.empty());

    for (const auto name : audio_fixture_names) {
        for (const auto extension : {"ogg", "mp3"}) {
            const auto filename = std::string(name) + "." + extension;
            const auto relative_path = std::filesystem::path("audio") / filename;
            CAPTURE(filename);

            const auto path = root / relative_path;
            CHECK(std::filesystem::exists(path));
            const auto bytes = read_binary_file(path);
            CHECK(bytes.size() > 128);
            CHECK(inventory.find(filename) != std::string::npos);

            if (extension == std::string_view("ogg")) {
                CHECK(has_ogg_signature(bytes));
            } else {
                CHECK(has_mp3_signature(bytes));
            }
        }
    }
}
