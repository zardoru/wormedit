#pragma once

#include <wormedit/audio/transport.h>
#include <wormedit/audio/waveform.h>
#include <wormedit/core/document.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
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
    // register_source_pending() accepted the source; its geometry is not probed yet.
    pending,
};

struct source_registration_result_t {
    source_registration_status_t status{source_registration_status_t::invalid_request};
    std::optional<decoded_source_binding_t> binding;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == source_registration_status_t::registered && binding.has_value();
    }
};

// One failed geometry probe, reported once through take_probe_failures().
struct source_probe_failure_t {
    asset_id_t asset;
    std::filesystem::path path;
    std::string message;
};

// Owns the normalized decoders behind immutable runtime source identities.
// Registration is a control-thread operation, while decode_block() may safely
// overlap registry queries or removal because each in-flight decoder entry is
// retained until its final caller releases it.
//
// Sources registered with register_source_pending() cost no I/O: they become
// visible in bindings() once probe(), probe_next_pending() or a decode/analysis
// request opens the file, reads its geometry, and closes it again. Decoders stay
// open only while a decode or analysis uses them; at most maximum_open_decoders
// idle decoders are kept afterwards (generated PCM sources are never closed).
class decoded_source_registry_t {
  public:
    explicit decoded_source_registry_t(std::size_t maximum_sources = 1'024,
                                       std::size_t maximum_open_decoders = 8)
        : maximum_sources_(maximum_sources), maximum_open_decoders_(maximum_open_decoders) {}

    // Registers and probes synchronously; a source that cannot be opened is not kept.
    [[nodiscard]] auto register_source(decoded_source_registration_t registration)
        -> source_registration_result_t;
    // Registers without touching the file. Returns `pending` or a rejection status.
    [[nodiscard]] auto register_source_pending(decoded_source_registration_t registration)
        -> source_registration_result_t;
    // Probes one source (no-op if already probed). `registered` carries the binding.
    [[nodiscard]] auto probe(asset_id_t asset) -> source_registration_result_t;
    // Claims and probes the oldest not-yet-probed source. Safe to call from several
    // threads at once; returns false when nothing is left to claim or stop was requested.
    auto probe_next_pending(std::stop_token stop = {}) -> bool;
    // Completes every not-yet-probed source on the calling thread (waiting for probes that
    // other threads are running). Call before building a schedule from bindings().
    void await_pending() const;
    [[nodiscard]] auto pending_count() const -> std::size_t;
    [[nodiscard]] auto take_probe_failures() -> std::vector<source_probe_failure_t>;
    [[nodiscard]] auto open_decoder_count() const -> std::size_t;
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
    class decoder_lease_t;

    [[nodiscard]] auto find_entry(asset_id_t asset) const -> std::shared_ptr<entry_t>;
    [[nodiscard]] auto probe_entry(const std::shared_ptr<entry_t>& entry,
                                   bool report_failure = true) const
        -> source_registration_result_t;
    [[nodiscard]] auto acquire_lease(const std::shared_ptr<entry_t>& entry) -> decoder_lease_t;
    void detach(const std::shared_ptr<entry_t>& entry);
    [[nodiscard]] auto add_entry(std::shared_ptr<entry_t> candidate)
        -> std::optional<source_registration_result_t>;
    void trim_idle_decoders();

    std::size_t maximum_sources_{};
    std::size_t maximum_open_decoders_{};
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<entry_t>> entries_;
    mutable std::vector<source_probe_failure_t> probe_failures_;
    std::atomic<std::size_t> open_decoders_{};
    std::atomic<std::uint64_t> use_tick_{};
};

} // namespace wormedit::audio
