#include <wormedit/audio/waveform_viewport.h>

#include <algorithm>
#include <cmath>

namespace wormedit::audio {
namespace {

auto maximum_frames_per_pixel(const waveform_viewport_t& viewport) noexcept -> double {
    return std::max(static_cast<double>(viewport.total_frames) / viewport.pixel_width,
                    minimum_waveform_frames_per_pixel);
}

auto visible_span(const waveform_viewport_t& viewport) noexcept -> long double {
    return static_cast<long double>(viewport.frames_per_pixel) * viewport.pixel_width;
}

auto to_begin_frame(long double frame, const waveform_viewport_t& viewport) noexcept
    -> std::uint64_t {
    const auto last_begin = std::max<long double>(
        static_cast<long double>(viewport.total_frames) - visible_span(viewport), 0.0L);
    return static_cast<std::uint64_t>(std::llround(std::clamp(frame, 0.0L, last_begin)));
}

} // namespace

auto is_waveform_viewport_valid(const waveform_viewport_t& viewport) noexcept -> bool {
    return viewport.total_frames != 0 && viewport.pixel_width != 0 &&
           std::isfinite(viewport.frames_per_pixel) &&
           viewport.frames_per_pixel >= minimum_waveform_frames_per_pixel &&
           viewport.frames_per_pixel <= maximum_frames_per_pixel(viewport) &&
           viewport.begin_frame < viewport.total_frames;
}

auto waveform_viewport_end_frame(const waveform_viewport_t& viewport) noexcept -> std::uint64_t {
    const auto end = static_cast<long double>(viewport.begin_frame) + visible_span(viewport);
    return static_cast<std::uint64_t>(
        std::min<long double>(std::ceil(end), static_cast<long double>(viewport.total_frames)));
}

auto fit_waveform_viewport(std::uint64_t total_frames, std::uint32_t pixel_width) noexcept
    -> waveform_viewport_t {
    if (total_frames == 0 || pixel_width == 0) return {};
    return clamp_waveform_viewport({.total_frames = total_frames,
                                    .begin_frame = 0,
                                    .frames_per_pixel =
                                        static_cast<double>(total_frames) / pixel_width,
                                    .pixel_width = pixel_width});
}

auto clamp_waveform_viewport(waveform_viewport_t viewport) noexcept -> waveform_viewport_t {
    if (viewport.total_frames == 0 || viewport.pixel_width == 0 ||
        !std::isfinite(viewport.frames_per_pixel))
        return {};
    viewport.frames_per_pixel = std::clamp(
        viewport.frames_per_pixel, minimum_waveform_frames_per_pixel,
        maximum_frames_per_pixel(viewport));
    viewport.begin_frame = to_begin_frame(static_cast<long double>(viewport.begin_frame), viewport);
    return viewport;
}

auto zoom_waveform_viewport(const waveform_viewport_t& viewport, double anchor_pixel,
                            double factor) noexcept -> waveform_viewport_t {
    if (!is_waveform_viewport_valid(viewport) || !std::isfinite(anchor_pixel) ||
        !std::isfinite(factor) || factor <= 0.0)
        return viewport;
    const auto anchor = std::clamp(anchor_pixel, 0.0, static_cast<double>(viewport.pixel_width));
    const auto anchor_frame = static_cast<long double>(viewport.begin_frame) +
                              static_cast<long double>(anchor) * viewport.frames_per_pixel;
    auto zoomed = viewport;
    zoomed.frames_per_pixel = std::clamp(viewport.frames_per_pixel / factor,
                                         minimum_waveform_frames_per_pixel,
                                         maximum_frames_per_pixel(viewport));
    zoomed.begin_frame = to_begin_frame(
        anchor_frame - static_cast<long double>(anchor) * zoomed.frames_per_pixel, zoomed);
    return zoomed;
}

auto pan_waveform_viewport(const waveform_viewport_t& viewport, double delta_pixels) noexcept
    -> waveform_viewport_t {
    if (!is_waveform_viewport_valid(viewport) || !std::isfinite(delta_pixels)) return viewport;
    auto panned = viewport;
    panned.begin_frame = to_begin_frame(
        static_cast<long double>(viewport.begin_frame) +
            static_cast<long double>(delta_pixels) * viewport.frames_per_pixel,
        viewport);
    return panned;
}

auto waveform_view_t::from(const waveform_t& waveform, const waveform_viewport_t& viewport)
    -> waveform_view_outcome_t {
    if (!is_waveform_viewport_valid(viewport) || viewport.total_frames != waveform.total_frames)
        return std::unexpected(waveform_projection_status_t::invalid_window);
    return waveform_view_t::from(waveform, {.begin_frame = viewport.begin_frame,
                                       .end_frame = waveform_viewport_end_frame(viewport),
                                       .pixel_width = viewport.pixel_width});
}

} // namespace wormedit::audio
