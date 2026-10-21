#pragma once
#include <wormedit/core/preferences.h>
#include <optional>
#include <string>
#include <utility>

namespace wormedit::sys {
class sdl_audition_output_t;
class sdl_preview_output_t;
struct options_runtime_t;
struct metronome_t;
namespace runtime { struct chart_preview_t; }

class audio_options_runtime_t {
public:
    void refresh_devices(options_runtime_t&);
    // Requested device/buffer changes wait for a hard Stop, including paused previews.
    [[nodiscard]] auto service(const audio_preferences_t&, options_runtime_t&,
        sdl_audition_output_t&, sdl_preview_output_t&, metronome_t&,
        runtime::chart_preview_t&, bool audition_active) -> std::string;
private:
    std::optional<audio_preferences_t> last_preferences_;
    std::optional<std::pair<std::string, std::uint32_t>> configured_output_;
};
}
