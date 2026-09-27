#pragma once

#include <wormedit/audio/transport.h>
#include <wormedit/audio/waveform.h>
#include <wormedit/core/document.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace wormedit::audio {

struct decoded_source_binding_t {
    asset_id_t asset;
    audio_source_identity_t source;
    std::uint32_t source_sample_rate{};
    std::optional<std::uint64_t> source_frame_count;
    std::uint32_t output_sample_rate{};
    std::uint64_t output_frame_count{};

    auto operator==(const decoded_source_binding_t&) const -> bool = default;
};

struct decoded_source_registration_t {
    asset_id_t asset;
    std::filesystem::path path;
    // Must identify immutable content or include a caller-owned revision. A
    // path alone is not sufficient when a file can be replaced in place.
    audio_source_identity_t source;
};

struct generated_source_registration_t {
    audio_source_identity_t source;
    std::vector<float> interleaved_stereo;
};

enum class source_registration_status_t {
    registered,
    invalid_request,
    duplicate_asset,
    duplicate_source,
    capacity_exceeded,
    open_failed,
};

struct source_registration_result_t {
    source_registration_status_t status{source_registration_status_t::invalid_request};
    std::optional<decoded_source_binding_t> binding;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == source_registration_status_t::registered && binding.has_value();
    }
};

// Owns the normalized decoders behind immutable runtime source identities.
// Registration is a control-thread operation, while decode_block() may safely
// overlap registry queries or removal because each in-flight decoder entry is
// retained until its final caller releases it.
class decoded_source_registry_t {
  public:
    explicit decoded_source_registry_t(std::size_t maximum_sources = 1'024)
        : maximum_sources_(maximum_sources) {}

    [[nodiscard]] auto register_source(decoded_source_registration_t registration)
        -> source_registration_result_t;
    [[nodiscard]] auto register_generated_source(generated_source_registration_t registration)
        -> source_registration_result_t;
    [[nodiscard]] auto remove(asset_id_t asset) -> bool;
    void clear();

    [[nodiscard]] auto bindings() const -> std::vector<decoded_source_binding_t>;
    [[nodiscard]] auto binding(asset_id_t asset) const -> std::optional<decoded_source_binding_t>;
    [[nodiscard]] auto decode_block(transport_t& transport, session_token_t token,
                                    audio_block_key_t key) -> block_decode_result_t;
    [[nodiscard]] auto analyze_waveform(asset_id_t asset, decoded_block_cache_t& block_cache,
                                        waveform_cache_t& waveform_cache,
                                        std::uint32_t frames_per_bin,
                                        waveform_analysis_options_t options = {},
                                        std::stop_token stop = {}) -> waveform_analysis_result_t;
    [[nodiscard]] auto size() const -> std::size_t;

  private:
    struct entry_t;

    std::size_t maximum_sources_{};
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<entry_t>> entries_;
};

} // namespace wormedit::audio
