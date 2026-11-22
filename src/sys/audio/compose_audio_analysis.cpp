#include "sys/audio/compose_audio_analysis.h"
#include "sys/audio/waveform_analysis_runtime.h"

#include "sys/compose/compose_scroll_geometry.h"
#include "ui/frontend/compose.h"
#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/aligned_waveform.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace wormedit::sys {
namespace {

constexpr std::uint64_t maximum_bins = 16'384;
constexpr std::uint64_t minimum_frames_per_bin = 1'024;
constexpr std::array band_limits_hz{40., 80., 160., 320., 640., 1280.,
                                    2560., 5120., 10240., 20480.};
// Two-pole bandpass at the geometric center. Transposed direct form II keeps two samples.
struct analysis_bandpass_t {
    static auto from(double low, double high, double rate) -> analysis_bandpass_t {
        // Prewarp both edges before the bilinear transform so the requested
        // half-power limits hold even for the bands close to Nyquist.
        const auto lower = std::tan(std::numbers::pi * low / rate);
        const auto upper = std::tan(std::numbers::pi * high / rate);
        const auto bandwidth = upper - lower;
        const auto center_squared = lower * upper;
        const auto scale = 1 / (1 + bandwidth + center_squared);
        return {bandwidth * scale, 2 * (center_squared - 1) * scale,
                (1 - bandwidth + center_squared) * scale};
    }
    auto process(double sample) -> double {
        const auto output = b * sample + state1;
        state1 = state2 - a1 * output;
        state2 = -b * sample - a2 * output;
        return output;
    }
    double b{}, a1{}, a2{}, state1{}, state2{};
};

struct analysis_filter_bank_t {
    explicit analysis_filter_bank_t(double rate) {
        static_assert(band_limits_hz.size() == ui::compose_spectrum_band_count + 1);
        for (std::size_t i = 0; i < spectrum.size(); ++i)
            spectrum[i].fill(analysis_bandpass_t::from(band_limits_hz[i], band_limits_hz[i + 1], rate));
        constexpr std::array range_hz{40., 250., 2500., 16000.};
        for (std::size_t band = 0; band < filtered.size(); ++band)
            filtered[band].fill(analysis_bandpass_t::from(range_hz[band], range_hz[band + 1], rate));
    }

    void append_frame(std::span<const float, 2> stereo) {
        // Store channel energy rather than summing opposite-polarity channels.
        for (std::size_t channel = 0; channel < stereo.size(); ++channel) {
            const auto sample = std::isfinite(stereo[channel]) ? stereo[channel] : 0.0f;
            for (std::size_t band = 0; band < spectrum.size(); ++band) {
                const auto value = spectrum[band][channel].process(sample);
                spectrum_energy[band] += value * value;
            }
            for (std::size_t band = 0; band < filtered.size(); ++band) {
                const auto value = filtered[band][channel].process(sample);
                peaks[band][channel] = std::max(peaks[band][channel], static_cast<float>(std::abs(value)));
                filter_energy[band][channel] += value * value;
            }
        }
        ++bin_frames;
    }

    auto finish_bin() -> compose_audio_analysis_bin_t {
        compose_audio_analysis_bin_t result{.peaks = peaks};
        for (std::size_t band = 0; band < spectrum.size(); ++band)
            result.spectrum[band] = static_cast<float>(std::sqrt(spectrum_energy[band] / (2 * bin_frames)));
        for (std::size_t band = 0; band < filtered.size(); ++band)
            for (std::size_t channel = 0; channel < audio::output_channel_count; ++channel)
                result.rms[band][channel] = static_cast<float>(std::sqrt(filter_energy[band][channel] / bin_frames));
        peaks = {};
        filter_energy = {};
        spectrum_energy = {};
        bin_frames = 0;
        return result;
    }

    // Keep separate histories so stereo polarity never cancels spectral energy.
    using stereo_filters_t = std::array<analysis_bandpass_t, audio::output_channel_count>;
    std::array<stereo_filters_t, ui::compose_spectrum_band_count> spectrum;
    std::array<stereo_filters_t, 3> filtered;
    std::array<double, ui::compose_spectrum_band_count> spectrum_energy{};
    std::array<std::array<double, audio::output_channel_count>, 3> filter_energy{};
    std::array<std::array<float, audio::output_channel_count>, 3> peaks{};
    std::uint64_t bin_frames{};
};

void append_block(analysis_filter_bank_t& filters, const audio::decoded_audio_block_t& block,
                  compose_audio_analysis_t& result) {
    for (std::size_t i = 0; i < block.frame_count; ++i) {
        filters.append_frame(std::span<const float, 2>{block.interleaved_stereo.data() + i * 2, 2});
        if (filters.bin_frames == result.frames_per_bin)
            result.bins.push_back(filters.finish_bin());
    }
}

void merge_bin(compose_audio_analysis_bin_t& result, const compose_audio_analysis_bin_t& bin) {
    for (std::size_t band = 0; band < result.spectrum.size(); ++band)
        result.spectrum[band] = std::max(result.spectrum[band], bin.spectrum[band]);
    for (std::size_t band = 0; band < result.peaks.size(); ++band)
        for (std::size_t channel = 0; channel < audio::output_channel_count; ++channel) {
            result.peaks[band][channel] = std::max(result.peaks[band][channel], bin.peaks[band][channel]);
            result.rms[band][channel] = std::max(result.rms[band][channel], bin.rms[band][channel]);
        }
}

} // namespace

auto compose_audio_analysis_t::from(audio::decoded_source_registry_t& sources,
                                    const audio::decoded_source_binding_t& binding,
                                    std::stop_token stop)
    -> std::shared_ptr<const compose_audio_analysis_t> {
    if (binding.output_frame_count == 0 || binding.output_sample_rate <= 2 * band_limits_hz.back()) return {};
    auto result = std::make_shared<compose_audio_analysis_t>();
    result->sample_rate = binding.output_sample_rate;
    result->total_frames = binding.output_frame_count;
    result->frames_per_bin = std::max(minimum_frames_per_bin,
        1 + (binding.output_frame_count - 1) / maximum_bins);
    result->bins.reserve(1 + (binding.output_frame_count - 1) / result->frames_per_bin);
    audio::decoded_block_cache_t blocks;
    analysis_filter_bank_t filters(binding.output_sample_rate);
    const auto last_block = (binding.output_frame_count - 1) / audio::maximum_read_frames;
    for (std::uint64_t index = 0; index <= last_block; ++index) {
        const audio::audio_block_key_t key{binding.source, index};
        if (stop.stop_requested() || !sources.decode_block_into(blocks, key)) return {};
        if (!blocks.with_block(key, [&](const auto& block) { append_block(filters, block, *result); }))
            return {};
    }
    if (filters.bin_frames != 0) result->bins.push_back(filters.finish_bin());
    return result;
}

auto compose_audio_analysis_t::query(double begin_seconds, double end_seconds) const
    -> std::optional<compose_audio_analysis_bin_t> {
    const auto begin = begin_seconds * sample_rate / frames_per_bin;
    const auto end = end_seconds * sample_rate / frames_per_bin;
    if (bins.empty() || !std::isfinite(begin) || !std::isfinite(end) || end <= begin ||
        end <= 0 || begin >= static_cast<double>(total_frames) / frames_per_bin)
        return std::nullopt;
    const auto first = static_cast<std::size_t>(std::clamp(std::floor(begin), 0.0,
                                                         static_cast<double>(bins.size() - 1)));
    const auto last = static_cast<std::size_t>(std::min(std::ceil(end), static_cast<double>(bins.size())));
    compose_audio_analysis_bin_t result;
    for (auto bin = first; bin < last; ++bin) merge_bin(result, bins[bin]);
    return result;
}

namespace {
auto project_bin(const compose_audio_analysis_t& analysis, const audio::display_row_time_t& row,
                 const audio::display_row_table_t& table) -> std::optional<compose_audio_analysis_bin_t> {
    if (!table.cut_frames) return analysis.query(row.begin_seconds, row.end_seconds);
    std::optional<compose_audio_analysis_bin_t> result;
    std::uint64_t cursor{};
    while (const auto span = audio::next_display_audio_span(
               row, *table.cut_frames, analysis.sample_rate, cursor)) {
        const auto bin = analysis.query(span->begin_seconds, span->end_seconds);
        if (!bin) continue;
        if (!result) result = *bin;
        else merge_bin(*result, *bin);
    }
    return result;
}
} // namespace

void compose_audio_analysis_t::populate_view(ui::compose_view_t& view,
                                              const audio::display_row_table_t& table,
                                              ui::compose_audio_analysis_mode_t mode) const {
    using mode_t = ui::compose_audio_analysis_mode_t;
    if (mode == mode_t::waveform) return;
    const auto band = static_cast<std::size_t>(mode) - static_cast<std::size_t>(mode_t::lows);
    for (const auto& row : table.viewport.value().rows) {
        const auto y = static_cast<double>(row.world_pixel) + .5 - std::floor(table.scroll());
        if (!row.valid || y < 0 || y >= view.viewport_height_pixels) continue;
        const auto bin = project_bin(*this, row, table);
        if (!bin) continue;
        const auto oriented = compose_source_y(y, view.viewport_height_pixels, view.downscroll);
        if (mode == mode_t::spectrum)
            view.spectrum_rows.push_back({oriented, bin->spectrum});
        else
            view.filtered_waveform_rows.push_back({.y_pixels = oriented, .absolute_peak = bin->peaks[band],
                                          .rms = bin->rms[band]});
    }
}

void runtime::waveform_analysis_t::reset_compose_audio_analysis() {
    if (compose_audio_analysis_worker_) compose_audio_analysis_worker_.cancel();
    compose_audio_analysis_worker_ = {};
    compose_audio_analysis_task_.reset();
    compose_audio_analysis_binding_.reset();
    compose_audio_analysis_.reset();
}

void runtime::waveform_analysis_t::service_compose_audio_analysis(std::optional<asset_id_t> asset,
                                                         const audio::waveform_t* waveform,
                                                         bool requested) {
    if (compose_audio_analysis_binding_ &&
        (!waveform || asset != compose_audio_analysis_binding_->asset ||
         waveform->key.source != compose_audio_analysis_binding_->source))
        reset_compose_audio_analysis();
    if (!waveform || !asset) return;
    if (compose_audio_analysis_task_) {
        const auto result = compose_audio_analysis_worker_.outcome(*compose_audio_analysis_task_);
        if (result) {
            compose_audio_analysis_ = task_value_cast<compose_audio_analysis_t>(*result);
            compose_audio_analysis_worker_ = {};
            compose_audio_analysis_task_.reset();
        }
        return;
    }
    if (!requested || compose_audio_analysis_binding_) return;
    const auto binding = sources_.binding(*asset);
    if (!binding || binding->source != waveform->key.source) return;

    task_graph_t graph{1};
    const auto task = graph.create_task({}, [this, binding = *binding](
        std::span<const task_outcome_t>, std::stop_token stop) {
        return task_value_t::from(compose_audio_analysis_t::from(sources_, binding, stop));
    });
    if (!task) return;
    auto submitted = scheduler_.begin(std::move(graph));
    if (!submitted) return;
    compose_audio_analysis_worker_ = std::move(submitted.value());
    compose_audio_analysis_task_ = *task;
    compose_audio_analysis_binding_ = binding;
}

} // namespace wormedit::sys
