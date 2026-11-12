#include "sys/app/frame_demand.h"

#include <cstdio>
#include <cstdlib>

using namespace wormedit::sys;

namespace {
void expect(bool condition, const char* what) {
    if (condition) return;
    std::fprintf(stderr, "frame_demand: %s\n", what);
    std::exit(1);
}
} // namespace

int main() {
    expect(frame_demand_t::none().is_none(), "none is idle");
    expect(!frame_demand_t::every_frame("a").is_none(), "continuous is not idle");

    const auto playback = frame_demand_t::every_frame("playback");
    const auto soon = frame_demand_t::at(100, "soon");
    const auto later = frame_demand_t::at(200, "later");
    expect(merge(soon, later) == soon, "earlier deadline wins");
    expect(merge(later, soon) == soon, "earlier deadline wins in either order");
    expect(merge(soon, playback) == playback, "continuous beats a deadline");
    expect(merge(playback, soon) == playback, "continuous beats a deadline in either order");
    expect(merge(frame_demand_t::none(), soon) == soon, "none yields to a deadline");
    expect(merge(soon, frame_demand_t::none()) == soon, "a deadline survives none");
    expect(merge(frame_demand_t::at(100, "x"), frame_demand_t::at(100, "y")).reason == "x",
           "ties keep the first source");
    expect(merge_all({}).is_none(), "merging nothing is idle");
    expect(merge_all({frame_demand_t::none(), later, soon, frame_demand_t::none()}) == soon,
           "merge_all folds every source");
    expect(frame_demand_t::after(50, 25, "tick").deadline_ns == 75, "after adds the period");

    constexpr std::optional<std::uint64_t> floor = 100;
    expect(frame_wait_budget_ns(playback, 10, floor) == 0, "continuous starts now");
    expect(frame_wait_budget_ns(frame_demand_t::none(), 10, floor) == 100, "idle waits the floor");
    expect(!frame_wait_budget_ns(frame_demand_t::none(), 10, std::nullopt),
           "idle without a floor waits without limit");
    expect(frame_wait_budget_ns(frame_demand_t::at(40, "d"), 10, floor) == 30, "deadline sets the wait");
    expect(frame_wait_budget_ns(frame_demand_t::at(500, "d"), 10, floor) == 100,
           "the floor caps a far deadline");
    expect(frame_wait_budget_ns(frame_demand_t::at(500, "d"), 10, std::nullopt) == 490,
           "no floor keeps a far deadline");
    expect(frame_wait_budget_ns(frame_demand_t::at(5, "d"), 10, floor) == 0, "a past deadline starts now");

    expect(wait_timeout_ms(std::nullopt) == -1, "no budget waits without limit");
    expect(wait_timeout_ms(0) == 0, "zero budget does not wait");
    expect(wait_timeout_ms(1) == 1, "a nanosecond rounds up to a millisecond");
    expect(wait_timeout_ms(1'000'000) == 1, "exact milliseconds stay exact");
    expect(wait_timeout_ms(1'000'001) == 2, "any excess rounds up");
    expect(wait_timeout_ms(1'000'000'000'000) == 60'000, "waits are bounded");
    return 0;
}
