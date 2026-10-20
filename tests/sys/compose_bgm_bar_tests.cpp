#include "sys/compose_bgm_bar.h"

#include <wormedit/formats/bmson.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <variant>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

void check_bar_state(bool condition) { CHECK(condition); }
void require_bar_state(bool condition) { REQUIRE(condition); }

constexpr std::uint32_t sample_rate = 48'000;
constexpr std::uint64_t total_frames = 600ULL * sample_rate;

auto chart_fixture() -> chart_document_t {
    std::ifstream stream(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" /
                             "published-vox-slicing.bmson",
                         std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.value);
    return parsed.value->charts.front();
}

} // namespace

TEST_CASE("bgm bar minimum span follows the song length", "[sys][bgm-bar]") {
    CHECK(bgm_bar_minimum_span(0) == 1.0);
    CHECK(bgm_bar_minimum_span(1000) == 1.0);
    CHECK(bgm_bar_minimum_span(2 * bgm_bar_minimum_span_frames) == Catch::Approx(0.5));
}

TEST_CASE("bgm bar zoom keeps the anchored audio under the pointer", "[sys][bgm-bar]") {
    const double minimum = 0.001;
    const auto zoomed = bgm_bar_zoom({0.0, 1.0}, 0.25, 1.0, minimum);
    CHECK(zoomed.end - zoomed.begin == Catch::Approx(1.0 / bgm_bar_zoom_step));
    CHECK((0.25 - zoomed.begin) / (zoomed.end - zoomed.begin) == Catch::Approx(0.25));

    const auto out = bgm_bar_zoom(zoomed, 0.25, -1.0, minimum);
    CHECK(out.begin == Catch::Approx(0.0).margin(1e-12));
    CHECK(out.end == Catch::Approx(1.0));
}

TEST_CASE("bgm bar zoom stays within the song and the minimum span", "[sys][bgm-bar]") {
    bgm_bar_range_t range;
    for (int step = 0; step < 200; ++step) range = bgm_bar_zoom(range, 0.9, 1.0, 0.01);
    CHECK(range.end - range.begin == Catch::Approx(0.01));
    CHECK(range.begin >= 0.0);
    CHECK(range.end <= 1.0);
    for (int step = 0; step < 200; ++step) range = bgm_bar_zoom(range, 0.5, -1.0, 0.01);
    CHECK(range == bgm_bar_range_t{0.0, 1.0});
    CHECK(bgm_bar_zoom({0.0, 1.0}, std::nan(""), 1.0, 0.01) == bgm_bar_range_t{0.0, 1.0});
}

TEST_CASE("bgm bar pan keeps the span and clamps at the song edges", "[sys][bgm-bar]") {
    const auto moved = bgm_bar_pan({0.2, 0.4}, 0.1);
    CHECK(moved.begin == Catch::Approx(0.3));
    CHECK(moved.end == Catch::Approx(0.5));
    const auto left = bgm_bar_pan({0.2, 0.4}, -5.0);
    CHECK(left.begin == Catch::Approx(0.0));
    CHECK(left.end == Catch::Approx(0.2));
    const auto right = bgm_bar_pan({0.2, 0.4}, 5.0);
    CHECK(right.begin == Catch::Approx(0.8));
    CHECK(right.end == Catch::Approx(1.0));
    CHECK(bgm_bar_pan({0.2, 0.4}, std::nan("")) == bgm_bar_range_t{0.2, 0.4});
}

TEST_CASE("bgm bar measure marks cover the window in order", "[sys][bgm-bar]") {
    const auto chart = chart_fixture();
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const auto built = measure_index_t::from(chart);
    REQUIRE(built.has_value());
    const auto& measures = built.value();
    REQUIRE(measures.entries.size() > 2);

    const auto all = bgm_bar_measure_marks(measures, *timing.value, sample_rate, total_frames,
                                           {0.0, 1.0}, 4096);
    REQUIRE(all.size() == measures.entries.size());
    CHECK(all.front().measure == 0);
    CHECK(std::ranges::is_sorted(all, {}, &bgm_bar_mark_t::audio_fraction));

    const auto window = bgm_bar_range_t{all[1].audio_fraction, all[2].audio_fraction};
    const auto inside = bgm_bar_measure_marks(measures, *timing.value, sample_rate, total_frames,
                                              window, 4096);
    REQUIRE(inside.size() == 2);
    CHECK(inside[0].measure == 1);
    CHECK(inside[1].measure == 2);

    const auto thinned = bgm_bar_measure_marks(measures, *timing.value, sample_rate, total_frames,
                                               {0.0, 1.0}, 2);
    CHECK(thinned.size() <= 2);
    CHECK(thinned.front().measure == 0);

    CHECK(bgm_bar_measure_marks(measures, *timing.value, 0, total_frames, {0.0, 1.0}, 8).empty());
    CHECK(bgm_bar_measure_marks(measures, *timing.value, sample_rate, total_frames, {0.5, 0.5}, 8).empty());
    CHECK(bgm_bar_measure_marks(measures, *timing.value, sample_rate, total_frames, {0.0, 1.0}, 0).empty());
}

TEST_CASE("bgm bar inputs are consumed and the rest are kept in order", "[sys][bgm-bar]") {
    wormedit::ui::workspace_action_t action{};
    auto push = [&](WormUiComposeInputKind kind, double fraction, double amount) {
        auto& input = action.compose_inputs[action.compose_input_count++];
        input = {};
        input.kind = kind;
        input.audio_fraction = fraction;
        input.x_pixels = amount;
    };
    push(WORM_UI_COMPOSE_INPUT_UNDO, 0.0, 0.0);
    push(WORM_UI_COMPOSE_INPUT_BGM_BAR_ZOOM, 0.5, 2.0);
    push(WORM_UI_COMPOSE_INPUT_REDO, 0.0, 0.0);
    bgm_bar_range_t range;
    std::optional<bgm_bar_pan_override_t> pan_override;
    const bgm_bar_input_context_t context{.total_frames = total_frames,
        .visible_begin_fraction = 0.4, .visible_end_fraction = 0.6,
        .project = song_id_t{1}, .chart = chart_id_t{2}, .document_revision = 3,
        .asset = asset_id_t{4}};
    CHECK(consume_bgm_bar_inputs(action, range, pan_override, context));
    CHECK(action.compose_input_count == 2);
    CHECK(action.compose_inputs[0].kind == WORM_UI_COMPOSE_INPUT_UNDO);
    CHECK(action.compose_inputs[1].kind == WORM_UI_COMPOSE_INPUT_REDO);
    CHECK(range.end - range.begin == Catch::Approx(1.0 / (bgm_bar_zoom_step * bgm_bar_zoom_step)));

    push(WORM_UI_COMPOSE_INPUT_BGM_BAR_PAN, 0.0, 0.01);
    const auto before = range;
    CHECK(consume_bgm_bar_inputs(action, range, pan_override, context));
    require_bar_state(pan_override.has_value());
    check_bar_state(pan_override->range.begin == Catch::Approx(before.begin + 0.01));
    check_bar_state(range == before);

    push(WORM_UI_COMPOSE_INPUT_BGM_BAR_RESET, 0.0, 0.0);
    CHECK(consume_bgm_bar_inputs(action, range, pan_override, context));
    CHECK(range == bgm_bar_range_t{});
    check_bar_state(!pan_override.has_value());
    CHECK(action.compose_input_count == 2);
    CHECK_FALSE(consume_bgm_bar_inputs(action, range, pan_override, context));
}
