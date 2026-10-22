#include "sys/audio/metronome.h"
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/metronome.h>
#include <string>
#include <tuple>
#include <utility>
namespace wormedit::sys {
static_assert(static_cast<std::size_t>(metronome_accent_t::measure) + 1 ==
              std::tuple_size_v<audio::metronome_clicks_t>);
void metronome_t::register_into(audio::decoded_source_registry_t& registry, std::uint64_t generation) {
    source_bindings.clear();
    auto clicks = audio::make_metronome_clicks();
    for (std::size_t accent = 0; accent < clicks.size(); ++accent) {
        const auto generated = registry.create_generated_source({
            .source = {"internal:metronome:" + std::to_string(accent),
                       "binding:" + std::to_string(generation)},
            .interleaved_stereo = std::move(clicks[accent])});
        if (!generated) {
            source_bindings.clear();
            return;
        }
        source_bindings.push_back({.accent = static_cast<metronome_accent_t>(accent),
            .voice = {.source = generated->source, .source_start_frame = 0,
                      .frame_count = generated->output_frame_count, .gain = 1.0F}});
    }
}
}
#endif
