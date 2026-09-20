#include <wormedit/audio/source_registry.h>

#include <algorithm>
#include <ranges>

namespace wormedit::audio {
namespace {

auto valid_source(const audio_source_identity_t& source) -> bool {
    return !source.asset_identity.empty() && !source.source_identity.empty();
}

auto invalid_decode() -> block_decode_result_t {
    return {.status = block_decode_status_t::invalid_key,
            .frames = 0,
            .decoder_status = read_status_t::not_open};
}

} // namespace

struct decoded_source_registry_t::entry_t {
    decoded_source_binding_t binding;
    decoder_t decoder;
};

auto decoded_source_registry_t::register_source(decoded_source_registration_t registration)
    -> source_registration_result_t {
    if (!registration.asset || registration.path.empty() || !valid_source(registration.source)) {
        return {.status = source_registration_status_t::invalid_request,
                .binding = std::nullopt,
                .message = "decoded source registration is incomplete"};
    }

    auto candidate = std::make_shared<entry_t>();
    std::string message;
    if (!candidate->decoder.open(registration.path, &message)) {
        return {.status = source_registration_status_t::open_failed,
                .binding = std::nullopt,
                .message = std::move(message)};
    }
    const auto info = candidate->decoder.info();
    if (!info.has_value() || info->source_sample_rate == 0 || info->output_frame_count == 0) {
        return {.status = source_registration_status_t::open_failed,
                .binding = std::nullopt,
                .message = "decoded source did not expose valid stream geometry"};
    }
    candidate->binding = {
        .asset = registration.asset,
        .source = std::move(registration.source),
        .source_sample_rate = info->source_sample_rate,
        .source_frame_count = info->source_frame_count == 0
                                  ? std::optional<std::uint64_t>{}
                                  : std::optional{info->source_frame_count},
        .output_sample_rate = info->output_sample_rate,
        .output_frame_count = info->output_frame_count,
    };

    const std::scoped_lock lock(mutex_);
    if (std::ranges::any_of(entries_, [&](const auto& entry) {
            return entry->binding.asset == candidate->binding.asset;
        })) {
        return {.status = source_registration_status_t::duplicate_asset,
                .binding = std::nullopt,
                .message = "audio asset already has a decoded source"};
    }
    if (std::ranges::any_of(entries_, [&](const auto& entry) {
            return entry->binding.source == candidate->binding.source;
        })) {
        return {.status = source_registration_status_t::duplicate_source,
                .binding = std::nullopt,
                .message = "decoded source identity is already registered"};
    }
    if (entries_.size() >= maximum_sources_) {
        return {.status = source_registration_status_t::capacity_exceeded,
                .binding = std::nullopt,
                .message = "decoded source registry capacity is exhausted"};
    }
    const auto binding = candidate->binding;
    entries_.push_back(std::move(candidate));
    return {.status = source_registration_status_t::registered, .binding = binding, .message = {}};
}

auto decoded_source_registry_t::register_generated_source(
    generated_source_registration_t registration) -> source_registration_result_t {
    if (!valid_source(registration.source) || registration.interleaved_stereo.empty() ||
        registration.interleaved_stereo.size() % output_channel_count != 0) {
        return {.status = source_registration_status_t::invalid_request,
                .binding = std::nullopt,
                .message = "generated decoded source registration is incomplete"};
    }
    auto candidate = std::make_shared<entry_t>();
    std::string message;
    if (!candidate->decoder.open_pcm(registration.interleaved_stereo, &message)) {
        return {.status = source_registration_status_t::open_failed,
                .binding = std::nullopt,
                .message = std::move(message)};
    }
    const auto info = candidate->decoder.info();
    if (!info.has_value()) {
        return {.status = source_registration_status_t::open_failed,
                .binding = std::nullopt,
                .message = "generated decoded source metadata is unavailable"};
    }
    candidate->binding = {.asset = {},
                          .source = std::move(registration.source),
                          .source_sample_rate = output_sample_rate,
                          .source_frame_count = info->source_frame_count,
                          .output_sample_rate = output_sample_rate,
                          .output_frame_count = info->output_frame_count};

    const std::scoped_lock lock(mutex_);
    if (std::ranges::any_of(entries_, [&](const auto& entry) {
            return entry->binding.source == candidate->binding.source;
        })) {
        return {.status = source_registration_status_t::duplicate_source,
                .binding = std::nullopt,
                .message = "decoded source identity is already registered"};
    }
    if (entries_.size() >= maximum_sources_) {
        return {.status = source_registration_status_t::capacity_exceeded,
                .binding = std::nullopt,
                .message = "decoded source registry capacity is exhausted"};
    }
    const auto binding = candidate->binding;
    entries_.push_back(std::move(candidate));
    return {.status = source_registration_status_t::registered, .binding = binding, .message = {}};
}

auto decoded_source_registry_t::remove(asset_id_t asset) -> bool {
    const std::scoped_lock lock(mutex_);
    const auto old_size = entries_.size();
    std::erase_if(entries_, [&](const auto& entry) { return entry->binding.asset == asset; });
    return entries_.size() != old_size;
}

void decoded_source_registry_t::clear() {
    const std::scoped_lock lock(mutex_);
    entries_.clear();
}

auto decoded_source_registry_t::bindings() const -> std::vector<decoded_source_binding_t> {
    const std::scoped_lock lock(mutex_);
    std::vector<decoded_source_binding_t> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (entry->binding.asset)
            result.push_back(entry->binding);
    }
    return result;
}

auto decoded_source_registry_t::decode_block(transport_t& transport, session_token_t token,
                                             audio_block_key_t key) -> block_decode_result_t {
    if (!valid_source(key.source)) {
        return invalid_decode();
    }
    std::shared_ptr<entry_t> entry;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = std::ranges::find_if(entries_, [&](const auto& candidate) {
            return candidate->binding.source == key.source;
        });
        if (found == entries_.end()) {
            return invalid_decode();
        }
        entry = *found;
    }
    return transport.decode_block(entry->decoder, token, std::move(key));
}

auto decoded_source_registry_t::analyze_waveform(
    asset_id_t asset, decoded_block_cache_t& block_cache, waveform_cache_t& waveform_cache,
    std::uint32_t frames_per_bin, waveform_analysis_options_t options, std::stop_token stop)
    -> waveform_analysis_result_t {
    if (!asset || frames_per_bin == 0) {
        return {.status = waveform_analysis_status_t::invalid_request};
    }
    std::shared_ptr<entry_t> entry;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = std::ranges::find_if(
            entries_, [&](const auto& candidate) { return candidate->binding.asset == asset; });
        if (found == entries_.end()) {
            return {.status = waveform_analysis_status_t::decoder_unavailable};
        }
        entry = *found;
    }
    return wormedit::audio::analyze_waveform(entry->decoder, block_cache, waveform_cache,
                                             entry->binding.source, frames_per_bin, options, stop);
}

auto decoded_source_registry_t::size() const -> std::size_t {
    const std::scoped_lock lock(mutex_);
    return entries_.size();
}

} // namespace wormedit::audio
