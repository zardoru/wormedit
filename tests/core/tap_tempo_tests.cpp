#include <wormedit/core/tap_tempo.h>
#include <wormedit/core/sv_recipe.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <new>
#include <tuple>

namespace {
thread_local bool track_tap_allocations = false;
thread_local std::size_t tap_allocations = 0;
constexpr std::uint32_t sample_rate = 44'100;
constexpr std::uint64_t beat_frames = sample_rate / 2;
void regular_taps(wormedit::tap_tempo_estimator_t& estimator, std::uint64_t first,
                  std::uint64_t period, std::uint64_t count) {
    for (std::uint64_t i = 0; i < count; ++i) (void)estimator.tap(first + i * period);
}
auto required_estimate(const wormedit::tap_tempo_estimator_t& estimator)
    -> wormedit::tap_tempo_estimate_t {
    const auto result = estimator.estimate();
    REQUIRE(result);
    return *result;
}
void check_exact_stable_estimate(const wormedit::tap_tempo_estimate_t& result,
                                 std::uint64_t phase) {
    CHECK(std::tie(result.bpm, result.rounded_bpm, result.half_bpm,
        result.residual_standard_deviation_ms, result.tap_count, result.stable, result.confidence) ==
          std::tuple{120.0, 120.0, 120.0, 0.0, std::uint64_t{16}, true, 1.0});
    CHECK(result.rational_bpm == wormedit::rational_t{120, 1});
    CHECK(result.phase_frame == phase);
}
void check_expired_estimate(const wormedit::tap_tempo_estimator_t& estimator) {
    CHECK(estimator.tap_count() == 0);
    CHECK_FALSE(estimator.estimate());
}
} // namespace

void* operator new(std::size_t size) {
    if (track_tap_allocations) ++tap_allocations;
    if (auto* value = std::malloc(size == 0 ? 1 : size)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

TEST_CASE("Tap tempo requires four taps and estimates exact 120 BPM and downbeat", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t estimator;
    const std::uint64_t phase = 7 * sample_rate;
    regular_taps(estimator, phase, beat_frames, 3);
    CHECK_FALSE(estimator.estimate());
    REQUIRE(estimator.tap(phase + 3 * beat_frames) == wormedit::tap_tempo_result_t::accepted);
    const auto seed = required_estimate(estimator);
    CHECK(std::tie(seed.bpm, seed.phase_frame, seed.stable) ==
          std::tuple{120.0, static_cast<double>(phase), false});
    CHECK(seed.rational_bpm == wormedit::rational_t{120, 1});
    regular_taps(estimator, phase + 4 * beat_frames, beat_frames, 12);
    const auto result = required_estimate(estimator);
    check_exact_stable_estimate(result, phase);
}

TEST_CASE("Tap tempo least squares tolerates ten millisecond jitter with accurate phase", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t estimator;
    const std::uint64_t phase = 9 * sample_rate;
    for (std::uint64_t i = 0; i < 16; ++i) {
        const auto jitter = i % 2 == 0 ? -441 : 441;
        (void)estimator.tap(static_cast<std::uint64_t>(static_cast<std::int64_t>(phase + i * beat_frames) + jitter));
    }
    const auto result = required_estimate(estimator);
    CHECK(result.bpm == Catch::Approx(120.0).margin(0.3));
    CHECK(result.phase_frame == Catch::Approx(static_cast<double>(phase)).margin(sample_rate * 0.005));
    CHECK(result.residual_standard_deviation_ms < 12.0);
    CHECK(result.stable);
    CHECK((result.confidence > 0.0 && result.confidence < 1.0));
}

TEST_CASE("Tap tempo advances inferred beat indices across a skipped tap", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t estimator;
    regular_taps(estimator, 0, beat_frames, 8);
    REQUIRE(estimator.tap(9 * beat_frames) == wormedit::tap_tempo_result_t::accepted);
    regular_taps(estimator, 10 * beat_frames, beat_frames, 8);
    const auto result = required_estimate(estimator);
    CHECK(result.bpm == 120.0);
    CHECK(result.phase_frame == 0.0);
    CHECK(result.tap_count == 16);
    CHECK(result.session_tap_count == 17);
}

TEST_CASE("Tap tempo ignores double taps without moving the accepted beat anchor", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t estimator;
    regular_taps(estimator, 0, beat_frames, 8);
    REQUIRE(estimator.tap(7 * beat_frames + beat_frames / 10) == wormedit::tap_tempo_result_t::ignored);
    CHECK(estimator.tap_count() == 8);
    REQUIRE(estimator.tap(8 * beat_frames) == wormedit::tap_tempo_result_t::accepted);
    const auto result = required_estimate(estimator);
    CHECK(result.bpm == 120.0);
    CHECK(result.phase_frame == 0.0);
    CHECK(result.session_tap_count == 9);
}

TEST_CASE("Tap tempo four measure window adapts after old tempo leaves", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t estimator;
    regular_taps(estimator, 0, beat_frames, 16);
    constexpr std::uint64_t faster_period = 19'845;
    regular_taps(estimator, 15 * beat_frames + faster_period, faster_period, 16);
    const auto result = required_estimate(estimator);
    CHECK(result.bpm == Catch::Approx(400.0 / 3.0).margin(1e-8));
    CHECK(result.rational_bpm == wormedit::rational_t{400, 3});
    CHECK(result.rounded_bpm == 133.0);
    CHECK(result.half_bpm == 133.5);
    CHECK(std::tie(result.tap_count, result.session_tap_count) == std::tuple{std::uint64_t{16}, std::uint64_t{32}});
    CHECK(result.residual_standard_deviation_ms < 0.001);
}

TEST_CASE("Tap tempo whole session keeps all sums while meter bounds window capacity", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t window({sample_rate, 3});
    wormedit::tap_tempo_estimator_t whole({sample_rate, 3, wormedit::tap_tempo_mode_t::whole_session});
    regular_taps(window, 0, beat_frames, 40);
    regular_taps(whole, 0, beat_frames, 40);
    const auto bounded = required_estimate(window);
    const auto session = required_estimate(whole);
    CHECK(bounded.tap_count == 12);
    CHECK(session.tap_count == 40);
    CHECK(session.session_tap_count == 40);
    CHECK(session.bpm == 120.0);
    CHECK(session.phase_frame == 0.0);
}

TEST_CASE("Tap tempo resets at two seconds without an accepted tap", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t estimator;
    regular_taps(estimator, 0, beat_frames, 8);
    const auto last = 7 * beat_frames;
    CHECK_FALSE(estimator.expire(last + 2 * sample_rate - 1));
    REQUIRE(estimator.tap(last + 2 * sample_rate) == wormedit::tap_tempo_result_t::restarted);
    CHECK(estimator.tap_count() == 1);
    CHECK_FALSE(estimator.estimate());
    regular_taps(estimator, last + 2 * sample_rate + beat_frames, beat_frames, 3);
    CHECK(required_estimate(estimator).phase_frame == last + 2 * sample_rate);
    CHECK(estimator.expire(last + 6 * sample_rate));
    check_expired_estimate(estimator);
}

TEST_CASE("Tap tempo rejects invalid configuration and nonmonotonic taps", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t zero_rate({0, 4});
    wormedit::tap_tempo_estimator_t zero_meter({sample_rate, 0});
    wormedit::tap_tempo_estimator_t huge_meter({sample_rate, 33});
    CHECK(std::tuple{zero_rate.is_valid(), zero_meter.is_valid(), huge_meter.is_valid()} == std::tuple{false, false, false});
    CHECK(zero_rate.tap(0) == wormedit::tap_tempo_result_t::invalid_configuration);
    wormedit::tap_tempo_estimator_t estimator;
    regular_taps(estimator, 123'456, beat_frames, 8);
    CHECK(estimator.tap(123'456) == wormedit::tap_tempo_result_t::ignored);
    CHECK(estimator.tap(123'456 + 7 * beat_frames) == wormedit::tap_tempo_result_t::ignored);
    CHECK(estimator.tap_count() == 8);
    estimator.reset();
    CHECK_FALSE(estimator.estimate());
}

TEST_CASE("Tap tempo allocates no storage after construction in either mode", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t window;
    wormedit::tap_tempo_estimator_t whole({sample_rate, 4, wormedit::tap_tempo_mode_t::whole_session});
    tap_allocations = 0;
    track_tap_allocations = true;
    for (std::uint64_t i = 0; i < 10'000; ++i) {
        (void)window.tap(i * beat_frames);
        (void)whole.tap(i * beat_frames);
        (void)window.estimate();
        (void)whole.estimate();
    }
    track_tap_allocations = false;
    CHECK(tap_allocations == 0);
    CHECK(required_estimate(window).bpm == 120.0);
    CHECK(required_estimate(whole).bpm == 120.0);
}

TEST_CASE("Tap tempo uses source frame differences without losing long source offsets", "[core][tap-tempo]") {
    wormedit::tap_tempo_estimator_t estimator;
    constexpr std::uint64_t phase = 9'000'000'000'000;
    regular_taps(estimator, phase, beat_frames, 20);
    const auto result = required_estimate(estimator);
    CHECK(result.bpm == 120.0);
    CHECK(result.phase_frame == phase);
}

namespace {
// Reuse this TU's existing allocation probe rather than installing a second global new.
auto rejected_sv_edit_allocations(wormedit::song_document_t song) -> std::size_t {
    using namespace wormedit;
    auto history = document_history_t::from(std::move(song));
    const auto edit = sv_recipe_edit_action_t{insert_edit_t<sv_recipe_t>{
        .chart = chart_id_t{10},
        .value = {.name = "invalid", .range = {{measure_id_t{11}, 0}, {measure_id_t{11}, 0}}}}};
    tap_allocations = 0;
    track_tap_allocations = true;
    const auto result = apply_sv_recipe_edit(history, edit);
    track_tap_allocations = false;
    CHECK_FALSE(result.applied);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_hold);
    CHECK(history.undo.empty());
    return tap_allocations;
}
} // namespace

TEST_CASE("Rejected SV edits do not copy unrelated document data", "[core][sv-recipe][allocation]") {
    using namespace wormedit;
    song_document_t song{.id = song_id_t{1}};
    song.charts.push_back({.id = chart_id_t{10},
        .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}}});
    const auto small = rejected_sv_edit_allocations(song);
    song.metadata.title.assign(1024 * 1024, 'x');
    const auto large = rejected_sv_edit_allocations(std::move(song));
    CHECK(large == small);
}
