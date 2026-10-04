#include "ui/nuklear/compose_bookmark_label.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

using namespace wormedit::ui;

namespace {

// Monospace measure: 6 px per code point.
auto width_of(std::string_view text) -> float {
    float count{};
    for (const auto byte : text) count += (static_cast<unsigned char>(byte) & 0xC0) != 0x80;
    return count * 6.0f;
}

constexpr bookmark_label_field_t field{100.0f, 50.0f, 400.0f, 300.0f};

} // namespace

TEST_CASE("bookmark label sits right-aligned above its line", "[unit][ui][bookmark-label]") {
    const auto box = layout_bookmark_label("Chorus", width_of, 12.0f, 200.0f, field, std::nullopt);
    REQUIRE(box);
    CHECK(box->text == "Chorus");
    CHECK(box->x + box->w == 100.0f + 400.0f - 4.0f);
    CHECK(box->y + box->h == 200.0f);
}

TEST_CASE("bookmark label truncates with an ellipsis at half the field", "[unit][ui][bookmark-label]") {
    const std::string long_name(100, 'a');
    const auto box = layout_bookmark_label(long_name, width_of, 12.0f, 200.0f, field, std::nullopt);
    REQUIRE(box);
    CHECK(box->w <= field.w * 0.5f);
    CHECK(box->text.ends_with("\xE2\x80\xA6"));
    CHECK(box->text.size() < long_name.size());
}

TEST_CASE("empty or invalid names draw nothing", "[unit][ui][bookmark-label]") {
    CHECK_FALSE(layout_bookmark_label("", width_of, 12.0f, 200.0f, field, std::nullopt));
    CHECK_FALSE(layout_bookmark_label("bad\xFF", width_of, 12.0f, 200.0f, field, std::nullopt));
    CHECK_FALSE(layout_bookmark_label("trunc\xE2\x80", width_of, 12.0f, 200.0f, field, std::nullopt));
}

TEST_CASE("overlong UTF-8 encodings are rejected", "[unit][ui][bookmark-label]") {
    CHECK_FALSE(layout_bookmark_label("\xC0\xAF", width_of, 12.0f, 200.0f, field, std::nullopt));
    CHECK_FALSE(layout_bookmark_label("\xE0\x80\xAF", width_of, 12.0f, 200.0f, field, std::nullopt));
    CHECK_FALSE(layout_bookmark_label("\xF0\x80\x80\xAF", width_of, 12.0f, 200.0f, field, std::nullopt));
}

TEST_CASE("non-scalar UTF-8 encodings are rejected", "[unit][ui][bookmark-label]") {
    CHECK_FALSE(layout_bookmark_label("\xED\xA0\x80", width_of, 12.0f, 200.0f, field, std::nullopt));
    CHECK_FALSE(layout_bookmark_label("\xF4\x90\x80\x80", width_of, 12.0f, 200.0f, field, std::nullopt));
}

TEST_CASE("an overlapping label is skipped and the earlier one stays", "[unit][ui][bookmark-label]") {
    const auto first = layout_bookmark_label("First", width_of, 12.0f, 200.0f, field, std::nullopt);
    REQUIRE(first);
    CHECK_FALSE(layout_bookmark_label("Second", width_of, 12.0f, 205.0f, field, first));
    CHECK(layout_bookmark_label("Third", width_of, 12.0f, 230.0f, field, first));
}

TEST_CASE("a label near the top edge stays inside the field", "[unit][ui][bookmark-label]") {
    const auto box = layout_bookmark_label("Top", width_of, 12.0f, 52.0f, field, std::nullopt);
    REQUIRE(box);
    CHECK(box->y == field.y);
}

TEST_CASE("bookmark rename target chooses the closest line with a stable ID tie break",
          "[unit][ui][bookmark-label]") {
    const std::array lines{
        bookmark_rename_line_t{.bookmark_id = 9, .y = 100.0},
        bookmark_rename_line_t{.bookmark_id = 3, .y = 108.0},
        bookmark_rename_line_t{.bookmark_id = 0, .y = 100.0},
    };
    CHECK(bookmark_rename_target_at(lines, 198.0, 104.0, 200.0) == 3);
    CHECK(bookmark_rename_target_at(lines, 165.0, 100.0, 200.0) == 9);
    CHECK_FALSE(bookmark_rename_target_at(lines, 163.0, 100.0, 200.0));
    CHECK_FALSE(bookmark_rename_target_at(lines, 198.0, 120.0, 200.0));
    CHECK_FALSE(bookmark_rename_target_at(lines, NAN, 100.0, 200.0));
}
