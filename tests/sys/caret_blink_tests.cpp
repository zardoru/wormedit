#include "sys/app/caret_blink.h"

#include <cstdio>
#include <cstdlib>

using namespace wormedit::sys;

namespace {
void expect(bool condition, const char* what) {
    if (condition) return;
    std::fprintf(stderr, "caret_blink: %s\n", what);
    std::exit(1);
}
constexpr auto half = caret_half_period_ns;
} // namespace

int main() {
    caret_blink_t blink;
    blink.restart(1'000);
    expect(blink.is_visible(1'000), "visible at the epoch");
    expect(blink.is_visible(1'000 + half - 1), "visible until the half period");
    expect(!blink.is_visible(1'000 + half), "hidden from the half period");
    expect(!blink.is_visible(1'000 + 2 * half - 1), "hidden until the full period");
    expect(blink.is_visible(1'000 + 2 * half), "visible again after a full period");
    expect(blink.is_visible(500), "a clock before the epoch reads as visible");

    expect(blink.next_change_ns(1'000) == 1'000 + half, "first change ends the visible phase");
    expect(blink.next_change_ns(1'000 + half - 1) == 1'000 + half, "change is strictly after now");
    expect(blink.next_change_ns(1'000 + half) == 1'000 + 2 * half, "the next change follows a flip");
    expect(blink.next_change_ns(500) == 1'000 + half, "before the epoch the first change is next");
    expect(blink.next_change_ns(1'000 + 5 * half + 3) == 1'000 + 6 * half, "far from the epoch");

    // The reported change is always where visibility actually flips.
    for (std::uint64_t now = 900; now < 1'000 + 7 * half; now += half / 3 + 7) {
        const auto change = blink.next_change_ns(now);
        expect(change > now, "change is in the future");
        expect(blink.is_visible(change - 1) != blink.is_visible(change), "visibility flips at the change");
    }

    blink.restart(1'000 + 3 * half + 11);
    expect(blink.is_visible(1'000 + 3 * half + 11), "restart makes the caret visible");
    return 0;
}
