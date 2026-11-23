#pragma once

#include "ui/frontend/compose_audio_analysis.h"

#include <atomic>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace wormedit::audio {
struct display_row_table_t;
struct decoded_source_binding_t;
class decoded_source_registry_t;
}
namespace wormedit::ui { struct compose_view_t; }

namespace wormedit::sys {

struct compose_audio_analysis_bin_t {
    std::array<float, ui::compose_spectrum_band_count> spectrum{};
    std::array<std::array<float, 2>, 3> peaks{};
    std::array<std::array<float, 2>, 3> rms{};
};

struct compose_audio_analysis_t {
    [[nodiscard]] static auto from(audio::decoded_source_registry_t& sources,
                                   const audio::decoded_source_binding_t& binding,
                                   std::stop_token stop, std::atomic<unsigned>* progress = nullptr)
        -> std::shared_ptr<const compose_audio_analysis_t>;
    void populate_view(ui::compose_view_t& view, const audio::display_row_table_t& table,
                       ui::compose_audio_analysis_mode_t mode) const;
    [[nodiscard]] auto query(double begin_seconds, double end_seconds) const
        -> std::optional<compose_audio_analysis_bin_t>;

    std::uint32_t sample_rate{};
    std::uint64_t total_frames{};
    std::uint64_t frames_per_bin{};
    std::vector<compose_audio_analysis_bin_t> bins;
};

} // namespace wormedit::sys
