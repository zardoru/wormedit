#include <wormedit/audio/resident_samples.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <utility>

namespace wormedit::audio {
namespace {

constexpr auto bytes_per_frame = static_cast<std::size_t>(output_channel_count) * sizeof(float);

auto arena_ceiling(std::size_t budget, std::size_t chunk) -> std::size_t {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    constexpr auto half = maximum / 2;
    if (budget > half || chunk > half - budget)
        return maximum;
    return (budget + chunk) * 2;
}

auto arena_options_for(const resident_sample_options_t& options) -> arena_options_t {
    const auto chunk = std::max<std::size_t>(options.chunk_bytes, 1);
    return {.chunk_bytes = chunk,
            // Live payload is capped by the budget; the rest of the headroom holds
            // stale spans until compaction and the compacted copy's first chunk.
            .maximum_bytes = arena_ceiling(options.budget_bytes, chunk),
            .maximum_objects = 1U << 20U};
}

} // namespace

auto resident_snapshot_t::find(const audio_source_identity_t& source) const noexcept
    -> const entry_t* {
    const auto found = entries_.find(source.source_identity);
    if (found == entries_.end() || found->second.asset_identity != source.asset_identity)
        return nullptr;
    return &found->second;
}

auto resident_snapshot_t::block(const audio_block_key_t& key) const noexcept
    -> std::span<const float> {
    const auto* entry = find(key.source);
    if (entry == nullptr || key.block_index > entry->frame_count / maximum_read_frames)
        return {};
    const auto first = static_cast<std::size_t>(key.block_index) * maximum_read_frames;
    if (first >= entry->frame_count)
        return {};
    const auto frames = std::min(maximum_read_frames, entry->frame_count - first);
    return {entry->data + first * output_channel_count, frames * output_channel_count};
}

resident_sample_store_t::resident_sample_store_t(resident_sample_options_t options)
    : options_(options),
      arena_(arena_options_for(options)),
      current_(std::make_shared<const resident_snapshot_t>()) {}

void resident_sample_store_t::set_listener(listener_t listener) {
    const std::scoped_lock lock(mutex_);
    listener_ = std::move(listener);
}

void resident_sample_store_t::notify(const resident_snapshot_ptr_t& snapshot) const {
    listener_t listener;
    {
        const std::scoped_lock lock(mutex_);
        listener = listener_;
    }
    if (listener)
        listener(snapshot);
}

auto resident_sample_store_t::snapshot() const -> resident_snapshot_ptr_t {
    const std::scoped_lock lock(mutex_);
    return current_;
}

auto resident_sample_store_t::set_budget_bytes(std::size_t budget_bytes) -> bool {
    const std::scoped_lock lock(mutex_);
    if (options_.budget_bytes == budget_bytes)
        return false;
    options_.budget_bytes = budget_bytes;
    arena_.grow_maximum_bytes(arena_options_for(options_).maximum_bytes);
    return true;
}

auto resident_sample_store_t::options() const -> resident_sample_options_t {
    const std::scoped_lock lock(mutex_);
    return options_;
}

auto resident_sample_store_t::publish_locked() -> resident_snapshot_ptr_t {
    auto next = std::make_shared<resident_snapshot_t>();
    next->entries_.reserve(bindings_.size());
    for (const auto& [source_identity, binding] : bindings_) {
        const auto content = contents_.find(binding.content_key);
        if (content == contents_.end())
            continue;
        const auto& span = content->second.span;
        next->entries_.emplace(
            source_identity,
            resident_snapshot_t::entry_t{
                .asset_identity = binding.asset_identity,
                .chunk = span.chunk,
                .data = reinterpret_cast<const float*>(span.bytes.data()),
                .frame_count = content->second.frame_count});
    }
    current_ = std::move(next);
    return current_;
}

auto resident_sample_store_t::reconcile(std::span<const resident_request_t> wanted)
    -> std::vector<std::size_t> {
    std::vector<std::size_t> missing;
    resident_snapshot_ptr_t published;
    {
        const std::scoped_lock lock(mutex_);
        bindings_.clear();
        std::unordered_map<std::string, bool> keep;
        for (std::size_t index = 0; index < wanted.size(); ++index) {
            const auto& request = wanted[index];
            bindings_[request.source.source_identity] = {request.source.asset_identity,
                                                         request.content_key};
            keep.emplace(request.content_key, true);
            if (!contents_.contains(request.content_key))
                missing.push_back(index);
        }
        for (auto it = contents_.begin(); it != contents_.end();) {
            if (keep.contains(it->first)) {
                ++it;
                continue;
            }
            arena_.mark_stale(it->second.span.bytes.size());
            it = contents_.erase(it);
        }
        published = publish_locked();
    }
    notify(published);
    return missing;
}

auto resident_sample_store_t::reserve(const resident_request_t& request)
    -> std::pair<resident_load_status_t, append_arena_span_t> {
    const std::scoped_lock lock(mutex_);
    const auto wanted = bindings_.find(request.source.source_identity);
    if (wanted == bindings_.end() || wanted->second.content_key != request.content_key)
        return {resident_load_status_t::stopped, {}};
    if (contents_.contains(request.content_key))
        return {resident_load_status_t::already_resident, {}};
    if (request.frame_count == 0 || request.frame_count > options_.maximum_frames_per_sample)
        return {resident_load_status_t::too_long, {}};
    const auto bytes = static_cast<std::size_t>(request.frame_count) * bytes_per_frame;
    const auto live_bytes = arena_.live_bytes();
    if (live_bytes > options_.budget_bytes || bytes > options_.budget_bytes - live_bytes) {
        ++over_budget_count_;
        return {resident_load_status_t::over_budget, {}};
    }
    auto appended = arena_.append_span(bytes, alignof(float));
    if (!appended) {
        ++failed_count_;
        return {resident_load_status_t::decode_failed, {}};
    }
    return {resident_load_status_t::loaded, std::move(*appended)};
}

void resident_sample_store_t::discard(const append_arena_span_t& span) {
    const std::scoped_lock lock(mutex_);
    arena_.mark_stale(span.bytes.size());
    ++failed_count_;
}

auto resident_sample_store_t::commit(const resident_request_t& request, append_arena_span_t span,
                                     std::size_t frames) -> resident_load_status_t {
    resident_snapshot_ptr_t published;
    {
        const std::scoped_lock lock(mutex_);
        const auto wanted = bindings_.find(request.source.source_identity);
        if (wanted == bindings_.end() || wanted->second.content_key != request.content_key ||
            contents_.contains(request.content_key)) {
            arena_.mark_stale(span.bytes.size());
            return resident_load_status_t::stopped;
        }
        contents_.emplace(request.content_key,
                          content_t{.span = std::move(span),
                                    .frame_count = frames,
                                    .order = next_order_++});
        published = publish_locked();
    }
    notify(published);
    return resident_load_status_t::loaded;
}

auto resident_sample_store_t::load(const resident_request_t& request, decoder_t& decoder,
                                   std::stop_token stop) -> resident_load_status_t {
    if (stop.stop_requested())
        return resident_load_status_t::stopped;
    auto [status, span] = reserve(request);
    if (status != resident_load_status_t::loaded)
        return status;
    const auto frames = static_cast<std::size_t>(request.frame_count);
    auto* output = reinterpret_cast<float*>(span.bytes.data());
    {
        const std::scoped_lock lock(mutex_);
        ++decode_count_;
    }
    for (std::size_t done = 0; done < frames;) {
        if (stop.stop_requested()) {
            discard(span);
            return resident_load_status_t::stopped;
        }
        const auto wanted = std::min(maximum_read_frames, frames - done);
        const auto read = decoder.read_at(
            done, std::span<float>{output + done * output_channel_count,
                                   wanted * output_channel_count},
            wanted);
        if (read.frames_read != wanted) {
            discard(span);
            return resident_load_status_t::decode_failed;
        }
        done += wanted;
    }
    return commit(request, std::move(span), frames);
}

auto resident_sample_store_t::compact_locked() -> bool {
    if (arena_.stale_bytes() == 0 || arena_.stale_ratio() < options_.stale_threshold)
        return false;
    std::vector<std::string> keys;
    std::vector<append_arena_span_t> spans;
    keys.reserve(contents_.size());
    spans.reserve(contents_.size());
    for (const auto& [key, content] : contents_) {
        keys.push_back(key);
        spans.push_back(content.span);
    }
    // Copy in load order so playback walks the new arena forward.
    std::vector<std::size_t> order(spans.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, [&](std::size_t left, std::size_t right) {
        return contents_.at(keys[left]).order < contents_.at(keys[right]).order;
    });
    auto compacted = arena_.build_compaction(spans, order);
    if (!compacted)
        return false;
    for (std::size_t index = 0; index < keys.size(); ++index)
        contents_.at(keys[index]).span = std::move(compacted->remap[index]);
    arena_ = std::move(compacted->arena);
    ++compactions_;
    return true;
}

auto resident_sample_store_t::compact_if_needed() -> bool {
    resident_snapshot_ptr_t published;
    {
        const std::scoped_lock lock(mutex_);
        if (!compact_locked())
            return false;
        published = publish_locked();
    }
    notify(published);
    return true;
}

void resident_sample_store_t::clear() {
    resident_snapshot_ptr_t published;
    {
        const std::scoped_lock lock(mutex_);
        bindings_.clear();
        contents_.clear();
        arena_ = append_arena_t{arena_options_for(options_)};
        published = publish_locked();
    }
    notify(published);
}

auto resident_sample_store_t::stats() const -> resident_sample_stats_t {
    const std::scoped_lock lock(mutex_);
    return {.resident_samples = contents_.size(),
            .live_bytes = arena_.live_bytes(),
            .stale_bytes = arena_.stale_bytes(),
            .committed_bytes = arena_.statistics().committed_bytes,
            .decode_count = decode_count_,
            .compactions = compactions_,
            .over_budget_count = over_budget_count_,
            .failed_count = failed_count_};
}

} // namespace wormedit::audio
