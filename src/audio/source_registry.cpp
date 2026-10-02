#include <wormedit/audio/source_registry.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

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

enum class probe_state_t { pending, probing, ready, failed };

struct decoded_source_registry_t::entry_t {
    asset_id_t asset;
    audio_source_identity_t source;
    std::filesystem::path path;
    bool generated{};

    // Guarded by the registry mutex.
    probe_state_t state{probe_state_t::pending};
    decoded_source_binding_t binding;
    std::string failure;

    // Guarded by `lifecycle`; the decoder is only opened, closed, or leased there.
    std::mutex lifecycle;
    decoder_t decoder;
    std::size_t in_use{};
    std::uint64_t last_used{};
    bool open{};
    bool streaming{}; // long source: its decoder stays open and is not counted (set when probed)
    bool counted{};   // contributes to the registry's open-decoder count
    bool detached{};  // removed from the registry; close once the last lease ends
};

// Keeps one entry's decoder open for the duration of a decode or analysis.
class decoded_source_registry_t::decoder_lease_t {
  public:
    decoder_lease_t() = default;
    decoder_lease_t(decoded_source_registry_t* registry, std::shared_ptr<entry_t> entry)
        : registry_(registry), entry_(std::move(entry)) {}
    decoder_lease_t(const decoder_lease_t&) = delete;
    auto operator=(const decoder_lease_t&) -> decoder_lease_t& = delete;
    decoder_lease_t(decoder_lease_t&& other) noexcept
        : registry_(other.registry_), entry_(std::move(other.entry_)) {}
    auto operator=(decoder_lease_t&&) -> decoder_lease_t& = delete;
    ~decoder_lease_t() {
        if (!entry_) return;
        {
            const std::scoped_lock lock(entry_->lifecycle);
            --entry_->in_use;
            entry_->last_used = registry_->use_tick_.fetch_add(1) + 1;
            if (entry_->detached && entry_->in_use == 0 && entry_->open && !entry_->generated) {
                entry_->decoder.close();
                entry_->open = false;
            }
        }
        registry_->trim_idle_decoders();
    }

    [[nodiscard]] explicit operator bool() const noexcept { return entry_ != nullptr; }
    [[nodiscard]] auto decoder() const -> decoder_t& { return entry_->decoder; }
    [[nodiscard]] auto source() const -> const audio_source_identity_t& { return entry_->source; }

  private:
    decoded_source_registry_t* registry_{};
    std::shared_ptr<entry_t> entry_;
};

auto decoded_source_registry_t::add_entry(std::shared_ptr<entry_t> candidate)
    -> std::optional<source_registration_result_t> {
    const std::scoped_lock lock(mutex_);
    if (!candidate->generated &&
        std::ranges::any_of(entries_, [&](const auto& entry) {
            return !entry->generated && entry->asset == candidate->asset;
        })) {
        return source_registration_result_t{.status = source_registration_status_t::duplicate_asset,
                                            .binding = std::nullopt,
                                            .message = "audio asset already has a decoded source"};
    }
    if (std::ranges::any_of(entries_, [&](const auto& entry) {
            return entry->source == candidate->source;
        })) {
        return source_registration_result_t{
            .status = source_registration_status_t::duplicate_source,
            .binding = std::nullopt,
            .message = "decoded source identity is already registered"};
    }
    if (entries_.size() >= maximum_sources_) {
        return source_registration_result_t{
            .status = source_registration_status_t::capacity_exceeded,
            .binding = std::nullopt,
            .message = "decoded source registry capacity is exhausted"};
    }
    entries_.push_back(std::move(candidate));
    return std::nullopt;
}

auto decoded_source_registry_t::register_source_pending(
    decoded_source_registration_t registration) -> source_registration_result_t {
    if (!registration.asset || registration.path.empty() || !valid_source(registration.source)) {
        return {.status = source_registration_status_t::invalid_request,
                .binding = std::nullopt,
                .message = "decoded source registration is incomplete"};
    }
    auto candidate = std::make_shared<entry_t>();
    candidate->asset = registration.asset;
    candidate->source = std::move(registration.source);
    candidate->path = std::move(registration.path);
    if (auto rejected = add_entry(std::move(candidate))) {
        return *std::move(rejected);
    }
    return {.status = source_registration_status_t::pending,
            .binding = std::nullopt,
            .message = {}};
}

auto decoded_source_registry_t::register_source(decoded_source_registration_t registration)
    -> source_registration_result_t {
    const auto source = registration.source;
    auto pending = register_source_pending(std::move(registration));
    if (pending.status != source_registration_status_t::pending) {
        return pending;
    }
    std::shared_ptr<entry_t> entry;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = std::ranges::find_if(
            entries_, [&](const auto& candidate) { return candidate->source == source; });
        if (found == entries_.end()) {
            return {.status = source_registration_status_t::invalid_request,
                    .binding = std::nullopt,
                    .message = "audio source was removed during registration"};
        }
        entry = *found;
    }
    auto probed = probe_entry(entry, false);
    if (!probed) {
        const std::scoped_lock lock(mutex_);
        std::erase(entries_, entry);
    }
    return probed;
}

auto decoded_source_registry_t::probe_entry(const std::shared_ptr<entry_t>& entry,
                                            bool report_failure) const
    -> source_registration_result_t {
    const auto settled = [&]() -> std::optional<source_registration_result_t> {
        const std::scoped_lock lock(mutex_);
        if (entry->state == probe_state_t::ready) {
            return source_registration_result_t{.status = source_registration_status_t::registered,
                                                .binding = entry->binding,
                                                .message = {}};
        }
        if (entry->state == probe_state_t::failed) {
            return source_registration_result_t{.status = source_registration_status_t::open_failed,
                                                .binding = std::nullopt,
                                                .message = entry->failure};
        }
        return std::nullopt;
    };
    if (auto done = settled()) return *std::move(done);

    // Concurrent probes of one entry serialize here; the loser finds it settled.
    const std::scoped_lock lifecycle(entry->lifecycle);
    if (auto done = settled()) return *std::move(done);

    std::string message;
    std::optional<stream_info_t> info;
    const auto was_open = entry->open;
    if (entry->open || entry->decoder.open(entry->path, &message, decoder_index_t::skip)) {
        entry->open = true;
        info = entry->decoder.info();
        if (!was_open && entry->in_use == 0) {
            entry->decoder.close();
            entry->open = false;
        }
    }
    if (!info.has_value() || info->source_sample_rate == 0 || info->output_frame_count == 0) {
        if (message.empty()) message = "decoded source did not expose valid stream geometry";
        const std::scoped_lock lock(mutex_);
        entry->state = probe_state_t::failed;
        entry->failure = message;
        if (report_failure && std::ranges::find(entries_, entry) != entries_.end()) {
            probe_failures_.push_back({entry->asset, entry->path, message});
        }
        return {.status = source_registration_status_t::open_failed,
                .binding = std::nullopt,
                .message = std::move(message)};
    }
    const std::scoped_lock lock(mutex_);
    entry->binding = {
        .asset = entry->asset,
        .source = entry->source,
        .source_sample_rate = info->source_sample_rate,
        .source_frame_count = info->source_frame_count == 0
                                  ? std::optional<std::uint64_t>{}
                                  : std::optional{info->source_frame_count},
        .output_sample_rate = info->output_sample_rate,
        .output_frame_count = info->output_frame_count,
    };
    entry->state = probe_state_t::ready;
    // Reopening a long compressed source rescans it, which a refill cannot afford.
    entry->streaming = info->output_frame_count >= long_source_frames;
    return {.status = source_registration_status_t::registered,
            .binding = entry->binding,
            .message = {}};
}

auto decoded_source_registry_t::probe(asset_id_t asset) -> source_registration_result_t {
    const auto entry = asset ? find_entry(asset) : nullptr;
    if (!entry) {
        return {.status = source_registration_status_t::invalid_request,
                .binding = std::nullopt,
                .message = "audio asset is not registered"};
    }
    return probe_entry(entry);
}

auto decoded_source_registry_t::probe_next_pending(std::stop_token stop) -> bool {
    std::shared_ptr<entry_t> claimed;
    {
        const std::scoped_lock lock(mutex_);
        if (stop.stop_requested()) return false;
        const auto found = std::ranges::find_if(entries_, [](const auto& entry) {
            return entry->state == probe_state_t::pending;
        });
        if (found == entries_.end()) return false;
        (*found)->state = probe_state_t::probing;
        claimed = *found;
    }
    static_cast<void>(probe_entry(claimed));
    return true;
}

void decoded_source_registry_t::await_pending() const {
    std::vector<std::shared_ptr<entry_t>> unsettled;
    {
        const std::scoped_lock lock(mutex_);
        for (const auto& entry : entries_) {
            if (entry->state == probe_state_t::pending || entry->state == probe_state_t::probing) {
                unsettled.push_back(entry);
            }
        }
    }
    for (const auto& entry : unsettled) {
        static_cast<void>(probe_entry(entry));
    }
}

auto decoded_source_registry_t::pending_count() const -> std::size_t {
    const std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(std::ranges::count_if(entries_, [](const auto& entry) {
        return entry->state == probe_state_t::pending || entry->state == probe_state_t::probing;
    }));
}

auto decoded_source_registry_t::take_probe_failures() -> std::vector<source_probe_failure_t> {
    const std::scoped_lock lock(mutex_);
    return std::exchange(probe_failures_, {});
}

auto decoded_source_registry_t::open_decoder_count() const -> std::size_t {
    return open_decoders_.load();
}

auto decoded_source_registry_t::streaming_decoder_count() const -> std::size_t {
    std::vector<std::shared_ptr<entry_t>> snapshot;
    {
        const std::scoped_lock lock(mutex_);
        snapshot = entries_;
    }
    return static_cast<std::size_t>(std::ranges::count_if(snapshot, [](const auto& entry) {
        const std::scoped_lock lifecycle(entry->lifecycle);
        return entry->streaming && entry->open;
    }));
}

auto decoded_source_registry_t::find_entry(asset_id_t asset) const -> std::shared_ptr<entry_t> {
    const std::scoped_lock lock(mutex_);
    const auto found = std::ranges::find_if(entries_, [asset](const auto& entry) {
        return !entry->generated && entry->asset == asset;
    });
    return found == entries_.end() ? nullptr : *found;
}

auto decoded_source_registry_t::acquire_lease(const std::shared_ptr<entry_t>& entry)
    -> decoder_lease_t {
    if (!entry->generated && !probe_entry(entry)) return {};
    {
        const std::scoped_lock lifecycle(entry->lifecycle);
        if (!entry->open) {
            std::string message;
            if (!entry->decoder.open(entry->path, &message)) return {};
            entry->open = true;
            if (!entry->detached && !entry->streaming) {
                entry->counted = true;
                ++open_decoders_;
            }
        }
        ++entry->in_use;
        entry->last_used = use_tick_.fetch_add(1) + 1;
    }
    return {this, entry};
}

void decoded_source_registry_t::trim_idle_decoders() {
    while (open_decoders_.load() > maximum_open_decoders_) {
        std::vector<std::shared_ptr<entry_t>> snapshot;
        {
            const std::scoped_lock lock(mutex_);
            snapshot = entries_;
        }
        std::shared_ptr<entry_t> oldest;
        std::uint64_t oldest_tick = std::numeric_limits<std::uint64_t>::max();
        for (const auto& entry : snapshot) {
            std::unique_lock lifecycle(entry->lifecycle, std::try_to_lock);
            if (!lifecycle.owns_lock() || !entry->counted || !entry->open || entry->in_use != 0) {
                continue;
            }
            if (entry->last_used <= oldest_tick) {
                oldest_tick = entry->last_used;
                oldest = entry;
            }
        }
        if (!oldest) return;
        const std::scoped_lock lifecycle(oldest->lifecycle);
        if (oldest->in_use != 0 || !oldest->open || !oldest->counted) continue;
        oldest->decoder.close();
        oldest->open = false;
        oldest->counted = false;
        --open_decoders_;
    }
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
    candidate->generated = true;
    candidate->open = true;
    candidate->source = std::move(registration.source);
    candidate->state = probe_state_t::ready;
    candidate->binding = {.asset = {},
                          .source = candidate->source,
                          .source_sample_rate = output_sample_rate,
                          .source_frame_count = info->source_frame_count,
                          .output_sample_rate = output_sample_rate,
                          .output_frame_count = info->output_frame_count};
    const auto binding = candidate->binding;
    if (auto rejected = add_entry(std::move(candidate))) {
        return *std::move(rejected);
    }
    return {.status = source_registration_status_t::registered, .binding = binding, .message = {}};
}

void decoded_source_registry_t::detach(const std::shared_ptr<entry_t>& entry) {
    const std::scoped_lock lifecycle(entry->lifecycle);
    entry->detached = true;
    if (entry->counted) {
        entry->counted = false;
        --open_decoders_;
    }
    if (entry->in_use == 0 && entry->open && !entry->generated) {
        entry->decoder.close();
        entry->open = false;
    }
}

auto decoded_source_registry_t::remove(asset_id_t asset) -> bool {
    std::vector<std::shared_ptr<entry_t>> removed;
    {
        const std::scoped_lock lock(mutex_);
        for (auto& entry : entries_) {
            if (entry->asset == asset) removed.push_back(entry);
        }
        std::erase_if(entries_, [&](const auto& entry) { return entry->asset == asset; });
    }
    for (const auto& entry : removed) detach(entry);
    return !removed.empty();
}

void decoded_source_registry_t::clear() {
    std::vector<std::shared_ptr<entry_t>> removed;
    {
        const std::scoped_lock lock(mutex_);
        removed.swap(entries_);
        probe_failures_.clear();
    }
    for (const auto& entry : removed) detach(entry);
}

auto decoded_source_registry_t::bindings() const -> std::vector<decoded_source_binding_t> {
    const std::scoped_lock lock(mutex_);
    std::vector<decoded_source_binding_t> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) {
        if (!entry->generated && entry->state == probe_state_t::ready)
            result.push_back(entry->binding);
    }
    return result;
}

auto decoded_source_registry_t::binding(asset_id_t asset) const
    -> std::optional<decoded_source_binding_t> {
    if (!asset)
        return std::nullopt;
    const std::scoped_lock lock(mutex_);
    const auto found = std::ranges::find_if(entries_, [asset](const auto& entry) {
        return !entry->generated && entry->asset == asset;
    });
    if (found == entries_.end() || (*found)->state != probe_state_t::ready) return std::nullopt;
    return (*found)->binding;
}

auto decoded_source_registry_t::decode_block(transport_t& transport, session_token_t token,
                                             audio_block_key_t key) -> block_decode_result_t {
    if (!valid_source(key.source)) {
        return invalid_decode();
    }
    if (const auto resident = transport.resident_block_hit(token, key)) {
        return *resident;
    }
    std::shared_ptr<entry_t> entry;
    {
        const std::scoped_lock lock(mutex_);
        const auto found = std::ranges::find_if(entries_, [&](const auto& candidate) {
            return candidate->source == key.source;
        });
        if (found == entries_.end()) {
            return invalid_decode();
        }
        entry = *found;
    }
    // A source that was never probed is probed here, so a request for it waits for
    // its geometry instead of failing.
    const auto lease = acquire_lease(entry);
    if (!lease) {
        return invalid_decode();
    }
    return transport.decode_block(lease.decoder(), token, std::move(key));
}

auto decoded_source_registry_t::preload_head(asset_id_t asset, decoded_block_cache_t& block_cache,
                                             std::uint64_t frames, std::stop_token stop)
    -> std::size_t {
    const auto entry = find_entry(asset);
    if (!entry) return 0;
    const auto lease = acquire_lease(entry);
    if (!lease) return 0;
    const auto info = lease.decoder().info();
    if (!info) return 0;
    // A private session: nothing is published to any transport.
    decode_session_t session;
    const auto token = session.start();
    if (!token) return 0;
    const auto limit = std::min(frames, info->output_frame_count);
    const auto blocks = (limit + maximum_read_frames - 1) / maximum_read_frames;
    std::size_t cached{};
    for (std::uint64_t block = 0; block < blocks && !stop.stop_requested(); ++block) {
        const auto result = decode_and_cache_block(lease.decoder(), block_cache, session, *token,
                                                   {.source = entry->source, .block_index = block});
        if (result.status != block_decode_status_t::decoded &&
            result.status != block_decode_status_t::cache_hit) {
            break;
        }
        ++cached;
        static_cast<void>(session.take_ready(*token));
    }
    return cached;
}

auto decoded_source_registry_t::load_resident(asset_id_t asset, resident_sample_store_t& store,
                                              const resident_request_t& request,
                                              std::stop_token stop) -> resident_load_status_t {
    const auto entry = find_entry(asset);
    if (!entry) return resident_load_status_t::decode_failed;
    const auto lease = acquire_lease(entry);
    if (!lease) return resident_load_status_t::decode_failed;
    return store.load(request, lease.decoder(), stop);
}

auto decoded_source_registry_t::analyze_waveform(
    asset_id_t asset, decoded_block_cache_t& block_cache, waveform_cache_t& waveform_cache,
    std::uint32_t frames_per_bin, waveform_analysis_options_t options, std::stop_token stop)
    -> waveform_analysis_result_t {
    if (!asset || frames_per_bin == 0) {
        return {.status = waveform_analysis_status_t::invalid_request};
    }
    const auto entry = find_entry(asset);
    if (!entry) {
        return {.status = waveform_analysis_status_t::decoder_unavailable};
    }
    const auto lease = acquire_lease(entry);
    if (!lease) {
        return {.status = waveform_analysis_status_t::decoder_unavailable};
    }
    return wormedit::audio::analyze_waveform(lease.decoder(), block_cache, waveform_cache,
                                             lease.source(), frames_per_bin, options, stop);
}

auto decoded_source_registry_t::size() const -> std::size_t {
    const std::scoped_lock lock(mutex_);
    return entries_.size();
}

} // namespace wormedit::audio
