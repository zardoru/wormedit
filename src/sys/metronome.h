#pragma once
#include <wormedit/audio/preview_bridge.h>
#include <cstdint>
#include <span>
#include <vector>
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/preview_session.h>
#include <wormedit/audio/source_registry.h>
#endif
namespace wormedit::audio { class preview_session_t; class decoded_source_registry_t; }
namespace wormedit::sys {
struct metronome_settings_t {
    bool enabled{}, available{};
    std::uint8_t volume{100}, count_in{};
    std::uint8_t accent_volume{100};
    auto operator==(const metronome_settings_t&) const -> bool = default;
};
struct metronome_t {
    metronome_settings_t settings;
    std::vector<audio::preview_metronome_source_binding_t> source_bindings;
    audio::preview_session_t* session{};
    [[nodiscard]] auto bindings() const -> std::span<const audio::preview_metronome_source_binding_t> {
        return source_bindings;
    }
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    void register_into(audio::decoded_source_registry_t&, std::uint64_t generation);
    void attach_session(audio::preview_session_t& target, bool output_available) {
        session = &target;
        settings.available = output_available && !source_bindings.empty();
        session->set_metronome_enabled(settings.enabled);
        static_cast<void>(session->set_metronome_volume(settings.volume));
        static_cast<void>(session->set_metronome_accent_volume(settings.accent_volume));
        static_cast<void>(session->set_metronome_count_in(settings.count_in));
    }
#endif
    [[nodiscard]] auto set_enabled(bool enabled) -> bool {
        if (!settings.available) return false;
        settings.enabled = enabled;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        if (session) session->set_metronome_enabled(enabled);
#endif
        return true;
    }
    [[nodiscard]] auto set_volume(std::uint8_t volume) -> bool {
        if (volume > 100) return false;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        if (session && !session->set_metronome_volume(volume)) return false;
#endif
        settings.volume = volume;
        return true;
    }
    [[nodiscard]] auto set_count_in(std::uint8_t measures) -> bool {
        if (measures > 4) return false;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        if (session && !session->set_metronome_count_in(measures)) return false;
#endif
        settings.count_in = measures;
        return true;
    }
    [[nodiscard]] auto set_accent_volume(std::uint8_t volume) -> bool {
        if (volume > 100) return false;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        if (session && !session->set_metronome_accent_volume(volume)) return false;
#endif
        settings.accent_volume = volume;
        return true;
    }
};
}
