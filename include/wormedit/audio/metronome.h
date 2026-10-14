#pragma once
#include <array>
#include <vector>
namespace wormedit::audio {
// Three immutable stereo clicks, indexed by metronome_accent_t.
using metronome_clicks_t = std::array<std::vector<float>, 3>;
[[nodiscard]] auto make_metronome_clicks() -> metronome_clicks_t;
}
