#include <wormedit/audio/playback_rate.h>
#include <wormedit/audio/preview_session.h>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <algorithm>
using namespace wormedit;
using namespace wormedit::audio;
namespace {
void expect(bool value) { REQUIRE(value); }
template<class T> void expect_false(const T& value) { REQUIRE_FALSE(static_cast<bool>(value)); }
}

TEST_CASE("audio.playback_rate table and exact rounding", "[audio]") {
    for (std::size_t i=0;i<playback_rates.size();++i) {
        const auto rate = playback_rates[i];
        expect(playback_rate(i) == rate);
        expect(playback_rate_index(rate) == i);
        expect_false(playback_rate_label(i).empty());
        expect(output_frames_for(rate.numerator,rate) == rate.denominator);
        expect(source_frames_for(rate.denominator,rate) == rate.numerator);
        expect(output_frames_for(1,rate) == (rate.denominator+rate.numerator-1)/rate.numerator);
        expect(source_frames_for(1,rate) == rate.numerator/rate.denominator);
    }
    expect(playback_rates[3] == rational_t{1,3});
    expect_false(playback_rate(9));
    expect_false(source_frames_for(1,{33,100}));
    expect_false(output_frames_for(std::numeric_limits<std::uint64_t>::max(),{1,10}));
}
TEST_CASE("audio.playback_rate carries callback fractions without ten minute drift", "[audio]") {
    source_frame_accumulator_t clock;
    expect(clock.advance(output_sample_rate,{1,2}) == output_sample_rate/2);
    clock.reset();
    std::uint64_t total=0;
    constexpr std::uint64_t duration=output_sample_rate*600;
    for (std::uint64_t output=0;output<duration;) {
        const auto block=std::min<std::uint64_t>(511,duration-output);
        total += *clock.advance(block,{1,3});
        output += block;
        expect(total == output/3);
    }
    expect(total == duration/3);
    expect_false(clock.advance(1,{2,1}));
}

TEST_CASE("audio.playback_rate fractional accumulation rejects atomically", "[audio]") {
    source_frame_accumulator_t clock;
    expect(clock.advance(1,{1,3}) == 0);
    expect_false(clock.advance(500,{2,1}));
    expect(clock.advance(2,{1,3}) == 1);
    expect(clock.advance(1,{1,3}) == 0);
    expect(clock.advance(1,{1,2}) == 0);
    expect(clock.advance(1,{1,2}) == 1);
    clock.reset();
    expect(clock.advance(2,{1,3}) == 0);
}

TEST_CASE("audio.playback_rate latency reports source frames without changing scheduling", "[audio]") {
    decoded_source_registry_t sources;
    decoded_block_cache_t cache;
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources,transport,bridge};
    preview_schedule_t schedule{.sample_rate=output_sample_rate};
    expect(static_cast<bool>(session.start_session(schedule,{}, {},100,{},10000)));
    expect(static_cast<bool>(session.pump_session()));
    std::array<float,200> samples{};
    expect(session.render_frames(samples).value().first_timeline_frame == 100);
    expect(session.timeline_frame() == 200);
    session.set_output_latency_frames(25);
    expect(session.timeline_frame() == 175);
    expect(session.render_frames(samples).value().first_timeline_frame == 200);
    expect(session.timeline_frame() == 275);
    session.set_output_latency_frames(500);
    expect(session.timeline_frame() == 100);
    expect(static_cast<bool>(session.seek_session(100)));
    expect(session.timeline_frame() == 100);
    static_cast<void>(session.stop());
    expect(session.timeline_frame() == 0);
    samples.fill(1.0F);
    expect(session.render_frames(samples).value().status == preview_session_render_status_t::stopped);
    for (const auto sample : samples) expect(sample == 0.0F);
}

TEST_CASE("Playback rate descriptors preserve rational identity and labels", "[audio][catalog]") {
    for (const auto& entry : playback_rate_catalog) {
        const auto id = static_cast<std::size_t>(entry.id);
        expect(playback_rate(id) == entry.rate);
        expect(playback_rate_label(id) == entry.label);
        expect(playback_rate_index(entry.rate) == id);
    }
    expect(playback_rate_label(playback_rate_count).empty());
}
