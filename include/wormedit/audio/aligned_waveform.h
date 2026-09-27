#pragma once

#include <wormedit/audio/waveform.h>
#include <wormedit/core/timing.h>

#include <array>
#include <cstddef>
#include <vector>

namespace wormedit::audio {

inline constexpr std::size_t maximum_aligned_waveform_rows = 4'096;
inline constexpr std::size_t maximum_waveform_overview_rows = 512;

struct aligned_waveform_request_t {
    double viewport_scroll_pixels{};
    double viewport_height_pixels{};
    double pixels_per_standard_measure{};
    std::size_t maximum_rows{maximum_aligned_waveform_rows};
};

struct aligned_waveform_row_t {
    double y_pixels{};
    std::array<float, output_channel_count> absolute_peak{};
    std::array<float, output_channel_count> rms{};
};

enum class aligned_waveform_status_t {
    projected,
    invalid_request,
    invalid_timing,
    invalid_waveform,
    capacity_exceeded,
};

struct aligned_waveform_result_t {
    aligned_waveform_status_t status{aligned_waveform_status_t::invalid_request};
    std::vector<aligned_waveform_row_t> rows;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == aligned_waveform_status_t::projected;
    }
};

struct waveform_overview_row_t {
    double audio_fraction{};
    float absolute_peak{};
};

struct waveform_overview_result_t {
    aligned_waveform_status_t status{aligned_waveform_status_t::invalid_request};
    double visible_begin_fraction{};
    double visible_end_fraction{};
    std::vector<waveform_overview_row_t> rows;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == aligned_waveform_status_t::projected;
    }
};

[[nodiscard]] auto project_waveform_overview(
    const waveform_t& waveform, const timing_index_t& timing, aligned_waveform_request_t viewport,
    std::size_t maximum_rows = maximum_waveform_overview_rows) -> waveform_overview_result_t;

// Projects normalized source peaks through the exact chart timing inverse onto
// the shared vertical musical viewport. It reads only derived waveform data.
[[nodiscard]] auto project_aligned_waveform(const waveform_t& waveform,
                                            const timing_index_t& timing,
                                            aligned_waveform_request_t request)
    -> aligned_waveform_result_t;

} // namespace wormedit::audio
