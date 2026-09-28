#include "sys/audio_source_registration.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/preview_bridge.h>
#include <wormedit/audio/decoder.h>

#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace wormedit::sys {

auto register_audition_sources(
    const song_document_t& document, const std::filesystem::path& asset_base,
    std::uint64_t generation, audio::decoded_source_registry_t& sources,
    std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings)
    -> std::size_t {
    std::size_t unavailable{};
    for (const auto& asset : document.audio_assets) {
        if (asset.source_path.empty() ||
            (!asset.source_path.is_absolute() && asset_base.empty())) {
            ++unavailable;
            continue;
        }
        const auto resolved_path = asset.source_path.is_absolute()
                                       ? asset.source_path
                                       : (asset_base / asset.source_path).lexically_normal();
        const auto registered = sources.register_source({
            .asset = asset.id,
            .path = resolved_path,
            .source =
                {
                    "project:" + std::to_string(document.id.value) +
                        ":asset:" + std::to_string(asset.id.value),
                    "binding:" + std::to_string(generation) + ":" +
                        resolved_path.generic_string(),
                },
        });
        if (!registered) {
            ++unavailable;
        }
    }

    constexpr std::array click_frequencies{1'100.0, 1'500.0, 1'900.0};
    constexpr std::array click_gains{0.16F, 0.23F, 0.31F};
    constexpr auto click_frames = audio::output_sample_rate * 45U / 1'000U;
    constexpr auto pi = 3.14159265358979323846;
    for (std::size_t accent = 0; accent < click_frequencies.size(); ++accent) {
        std::vector<float> pcm(click_frames * audio::output_channel_count);
        for (std::size_t frame = 0; frame < click_frames; ++frame) {
            const auto seconds = static_cast<double>(frame) /
                                 static_cast<double>(audio::output_sample_rate);
            const auto envelope = std::exp(-90.0 * seconds);
            const auto sample = click_gains[accent] *
                                static_cast<float>(envelope * std::sin(
                                                       2.0 * pi * click_frequencies[accent] *
                                                       seconds));
            const auto first_sample = frame * audio::output_channel_count;
            pcm[first_sample] = sample;
            pcm[first_sample + 1] = sample;
        }
        const auto generated = sources.register_generated_source({
            .source = {"internal:metronome:" + std::to_string(accent),
                       "binding:" + std::to_string(generation)},
            .interleaved_stereo = std::move(pcm),
        });
        if (!generated) {
            metronome_bindings.clear();
            break;
        }
        metronome_bindings.push_back({
            .accent = static_cast<metronome_accent_t>(accent),
            .voice = {.source = generated.binding->source,
                      .source_start_frame = 0,
                      .frame_count = generated.binding->output_frame_count,
                      .gain = 1.0F},
        });
    }
    return unavailable;
}

auto reconcile_audition_sources(
    audition_source_reconciliation_context_t context, bool stop_playback)
    -> std::optional<std::size_t> {
    std::vector<audition_asset_entry_t> next_inventory;
    next_inventory.reserve(context.document.audio_assets.size());
    for (const auto& asset : context.document.audio_assets) {
        next_inventory.emplace_back(asset.id, asset.source_path);
    }
    if (context.source_project == context.document.id &&
        context.asset_inventory == next_inventory) {
        return std::nullopt;
    }
    if (stop_playback) {
        context.stop_playback();
        context.invalidate_preview();
    }
    context.reset_waveform();
    context.sources.clear();
    context.metronome_bindings.clear();
    ++context.generation;
    const auto unavailable = register_audition_sources(
        context.document, context.asset_base, context.generation, context.sources,
        context.metronome_bindings);
    context.source_project = context.document.id;
    context.asset_inventory = std::move(next_inventory);
    return unavailable;
}

} // namespace wormedit::sys

#endif
