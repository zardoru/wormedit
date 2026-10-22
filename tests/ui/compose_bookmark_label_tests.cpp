#include "ui/nuklear/compose/compose_bookmark_label.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cmath>
#include <optional>

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

TEST_CASE("bookmark hit band spans the full width and stops at its edges",
          "[unit][ui][bookmark-label]") {
    const std::array lines{bookmark_rename_line_t{.bookmark_id = {4}, .y = 100.0}};
    CHECK(bookmark_double_click_target_at(lines, 0.0, 100.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{4});
    CHECK(bookmark_double_click_target_at(lines, 10.0, 105.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{4});
    CHECK(bookmark_double_click_target_at(lines, 400.0, 100.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{4});
    // The label zone above a named line is full width too, not only the right half.
    CHECK(bookmark_double_click_target_at(lines, 20.0, 88.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{4});
    CHECK_FALSE(bookmark_double_click_target_at(lines, 20.0, 120.0, 400.0, 18.0));
    CHECK_FALSE(bookmark_double_click_target_at(lines, 401.0, 100.0, 400.0, 18.0));
    CHECK_FALSE(bookmark_double_click_target_at(lines, NAN, 100.0, 400.0, 18.0));
}

TEST_CASE("bookmark hover extent and pointer target share line and label boundaries",
          "[unit][ui][bookmark-label]") {
    const auto labelled = GENERATE(false, true);
    const std::array lines{bookmark_rename_line_t{.bookmark_id = {4}, .y = 100.0,
                                                .labelled = labelled}};
    const auto [top, bottom] = lines.front().hit_extent(compose_bookmark_label_zone_height);
    CHECK((std::pair{top, bottom} == std::pair{labelled ? 82.0 : 94.0, 106.0}));
    for (const auto y : {top, bottom})
        CHECK(bookmark_double_click_target_at(lines, 400.0, y, 400.0,
            compose_bookmark_label_zone_height) == std::optional<std::uint64_t>{4});
    for (const auto y : {top - 0.001, bottom + 0.001})
        CHECK_FALSE(bookmark_double_click_target_at(lines, 400.0, y, 400.0,
            compose_bookmark_label_zone_height));
}

TEST_CASE("double-click target prefers an exact line hit over a neighbor's label zone",
          "[unit][ui][bookmark-label]") {
    // A at y=100 and B at y=110: y=100 is on A's line and inside B's label zone above it.
    const std::array forward{bookmark_rename_line_t{1, 100.0}, bookmark_rename_line_t{2, 110.0}};
    const std::array reversed{bookmark_rename_line_t{2, 110.0}, bookmark_rename_line_t{1, 100.0}};
    CHECK(bookmark_double_click_target_at(forward, 300.0, 100.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{1});
    CHECK(bookmark_double_click_target_at(reversed, 300.0, 100.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{1});
}

TEST_CASE("double-click label zones belong to labelled lines and the nearest line wins",
          "[unit][ui][bookmark-label]") {
    // Both lines are below y=80 (18 px zone): 100 is 20 away (out), 90 is 10 away.
    const std::array lines{bookmark_rename_line_t{1, 90.0}, bookmark_rename_line_t{2, 95.0}};
    CHECK(bookmark_double_click_target_at(lines, 300.0, 80.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{1});
    const std::array reversed{bookmark_rename_line_t{2, 95.0}, bookmark_rename_line_t{1, 90.0}};
    CHECK(bookmark_double_click_target_at(reversed, 300.0, 80.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{1});
    // An unnamed bookmark draws no label, so its zone is not a target.
    const std::array unnamed{bookmark_rename_line_t{1, 90.0, false}};
    CHECK_FALSE(bookmark_double_click_target_at(unnamed, 300.0, 80.0, 400.0, 18.0));
    // Equal distances go to the lower ID whatever the input order.
    const std::array tie{bookmark_rename_line_t{5, 90.0}, bookmark_rename_line_t{3, 90.0}};
    CHECK(bookmark_double_click_target_at(tie, 300.0, 80.0, 400.0, 18.0) ==
          std::optional<std::uint64_t>{3});
}
