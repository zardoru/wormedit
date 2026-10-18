#include "sys/audio_source_registration.h"
#include "sys/audio_source_registration_internal.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/decoder.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <system_error>
#include <unordered_set>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <string>
#include <utility>

namespace wormedit::sys {

namespace {

auto asset_registration(const song_document_t& document, const audio_asset_t& asset,
                        std::filesystem::path resolved_path, std::uint64_t generation)
    -> audio::decoded_source_registration_t {
    return {
        .asset = asset.id,
        .path = resolved_path,
        .source =
            {
                "project:" + std::to_string(document.id.value) +
                    ":asset:" + std::to_string(asset.id.value),
                "binding:" + std::to_string(generation) + ":" + resolved_path.generic_string(),
            },
    };
}

// Probe order follows registration order: the song (music-role resources) first, then
// keysounds by first use from the start of the chart, then anything unreferenced.
auto probe_order(const song_document_t& document) -> std::vector<const audio_asset_t*> {
    std::vector<asset_id_t> ids;
    for (const auto& resource : document.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::music) != resource.roles.end())
            ids.push_back(resource.asset);
    }
    for (const auto asset : first_use_asset_order(document)) ids.push_back(asset);
    std::vector<const audio_asset_t*> order;
    std::unordered_set<std::uint64_t> seen;
    for (const auto id : ids) {
        const auto found = document.find_audio_asset(id);
        if (found != nullptr && seen.insert(id.value).second)
            order.push_back(&*found);
    }
    return order;
}

auto register_assets(const song_document_t& document, const std::filesystem::path& asset_base,
                     std::uint64_t generation, audio::decoded_source_registry_t& sources,
                     bool defer) -> std::size_t {
    // Every authored asset plus the three generated metronome accents must fit.
    sources.ensure_source_capacity(document.audio_assets.size() + 3);
    std::size_t unavailable{};
    for (const auto* asset : probe_order(document)) {
        const auto resolved_path = resolve_asset_path(*asset, asset_base);
        if (!resolved_path ||
            !register_one_asset(document, *asset, *resolved_path, generation, sources, defer))
            ++unavailable;
    }
    return unavailable;
}

} // namespace

// Resolves an asset's path, or nullopt when it has none usable.
auto resolve_asset_path(const audio_asset_t& asset, const std::filesystem::path& asset_base)
    -> std::optional<std::filesystem::path> {
    if (asset.source_path.empty() || (!asset.source_path.is_absolute() && asset_base.empty())) {
        return std::nullopt;
    }
    return asset.source_path.is_absolute()
               ? asset.source_path
               : (asset_base / asset.source_path).lexically_normal();
}

// Registers one asset; false when the registry rejects it.
auto register_one_asset(const song_document_t& document, const audio_asset_t& asset,
                        std::filesystem::path resolved_path, std::uint64_t generation,
                        audio::decoded_source_registry_t& sources, bool defer) -> bool {
    auto registration = asset_registration(document, asset, std::move(resolved_path), generation);
    return defer ? sources.create_pending_source(std::move(registration)).has_value()
                 : sources.create_source(std::move(registration)).has_value();
}


auto register_audition_sources(
    const song_document_t& document, const std::filesystem::path& asset_base,
    std::uint64_t generation, audio::decoded_source_registry_t& sources)
    -> std::size_t {
    const auto unavailable = register_assets(document, asset_base, generation, sources, false);
    return unavailable;
}

auto register_audition_sources_deferred(
    const song_document_t& document, const std::filesystem::path& asset_base,
    std::uint64_t generation, audio::decoded_source_registry_t& sources)
    -> std::size_t {
    const auto unavailable = register_assets(document, asset_base, generation, sources, true);
    return unavailable;
}

void start_audition_source_probes(audio::decoded_source_registry_t& sources,
                                  task_scheduler_t& scheduler, std::size_t parallelism) {
    const auto tasks = std::min(parallelism, sources.pending_count());
    if (tasks == 0) return;
    task_graph_t graph{tasks};
    for (std::size_t index = 0; index < tasks; ++index) {
        const auto added = graph.create_task(
            {}, [&sources](std::span<const task_outcome_t>, std::stop_token stop) {
                while (sources.probe_next_pending(stop)) {
                }
                return task_value_t::from(std::make_shared<const int>(0));
            });
        if (!added) return;
    }
    static_cast<void>(scheduler.begin(std::move(graph)));
}

auto drain_audition_probe_failures(audio::decoded_source_registry_t& sources)
    -> std::optional<notification_post_t> {
    const auto failures = sources.take_probe_failures();
    if (failures.empty()) return std::nullopt;
    auto line = std::to_string(failures.size()) + " audio asset(s) could not be opened (" +
                failures.front().path.filename().string() +
                (failures.size() > 1 ? ", ..." : "") + "): " + failures.front().message;
    return notification_post_t{.severity = notification_severity_t::error,
                               .source = notification_source_t::audio, .message = std::move(line)};
}

auto first_use_asset_order(const song_document_t& document) -> std::vector<asset_id_t> {
    std::unordered_map<std::uint64_t, asset_id_t> asset_by_resource;
    for (const auto& resource : document.resources) {
        asset_by_resource.emplace(resource.id.value, resource.asset);
    }
    std::vector<asset_id_t> order;
    std::unordered_set<std::uint64_t> seen;
    const auto add = [&](asset_id_t asset) {
        if (seen.insert(asset.value).second) order.push_back(asset);
    };
    if (!document.charts.empty()) {
        for (const auto& measure : document.charts.front().measures) {
            std::vector<std::pair<std::uint64_t, resource_id_t>> uses;
            for (const auto& note : measure.notes) {
                if (note.sound) uses.emplace_back(note.position.row, *note.sound);
            }
            for (const auto& event : measure.bgm_events) {
                if (event.sound) uses.emplace_back(event.position.row, *event.sound);
            }
            std::ranges::stable_sort(uses, {}, &decltype(uses)::value_type::first);
            for (const auto& [row, resource] : uses) {
                if (const auto found = asset_by_resource.find(resource.value);
                    found != asset_by_resource.end()) {
                    add(found->second);
                }
            }
        }
    }
    for (const auto& asset : document.audio_assets) add(asset.id);
    return order;
}

struct resident_sample_loader_t::run_state_t {
    std::mutex control_mutex;
    std::uint64_t budget_version{};
    std::uint64_t processed_budget_version{};
    audio::decoded_source_registry_t* sources{};
    std::vector<std::pair<asset_id_t, audio::resident_request_t>> skipped;
    std::atomic<std::size_t> total{};
    std::atomic<std::size_t> done{};
    std::atomic<bool> running{true};
    std::atomic<std::size_t> over_budget{};
    std::atomic<std::size_t> failed{};
    std::atomic<bool> reported{};
    std::mutex priority_mutex;
    std::vector<asset_id_t> priority;
};

namespace {

struct resident_candidate_t {
    asset_id_t asset;
    std::filesystem::path path;
};

auto resident_content_key(const std::filesystem::path& path) -> std::optional<std::string> {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return std::nullopt;
    return path.generic_string() + "|" + std::to_string(size) + "|" +
           std::to_string(static_cast<long long>(modified.time_since_epoch().count()));
}

struct resident_wanted_t {
    asset_id_t asset;
    audio::resident_request_t request;
};

auto resident_requests(const std::vector<resident_candidate_t>& candidates,
                       audio::decoded_source_registry_t& sources,
                       const audio::resident_sample_options_t& options, std::stop_token stop)
    -> std::vector<resident_wanted_t> {
    std::vector<resident_wanted_t> wanted;
    for (const auto& candidate : candidates) {
        if (stop.stop_requested()) break;
        const auto probed = sources.open_source_binding(candidate.asset);
        const auto key = resident_content_key(candidate.path);
        if (!probed || !key ||
            probed->output_frame_count > options.maximum_frames_per_sample) {
            continue;
        }
        wanted.push_back({candidate.asset,
                          {.source = probed->source,
                           .content_key = *key,
                           .frame_count = probed->output_frame_count}});
    }
    return wanted;
}

// Picks the next unloaded request: the earliest asset in the priority list that is
// still pending, otherwise the next one in first-use order.
auto next_pending_index(const std::vector<resident_wanted_t>& wanted,
                        const std::vector<std::size_t>& missing, std::vector<bool>& taken,
                        const std::vector<asset_id_t>& priority) -> std::optional<std::size_t> {
    for (const auto asset : priority) {
        for (std::size_t slot = 0; slot < missing.size(); ++slot) {
            if (!taken[slot] && wanted[missing[slot]].asset == asset) {
                taken[slot] = true;
                return missing[slot];
            }
        }
    }
    const auto free = std::ranges::find(taken, false);
    if (free == taken.end()) return std::nullopt;
    *free = true;
    return missing[static_cast<std::size_t>(free - taken.begin())];
}

void count_resident_status(operation_state_t<audio::resident_load_phase_t> status,
                           std::atomic<std::size_t>& over_budget,
                           std::atomic<std::size_t>& failed) {
    if (status == wormedit::failure_code_t::capacity_exceeded) ++over_budget;
    if (status == wormedit::failure_code_t::decode_failed) ++failed;
}

} // namespace

namespace {

void preload_long_source_heads(const std::vector<resident_candidate_t>& candidates,
                               audio::decoded_source_registry_t& sources,
                               const audio::resident_sample_options_t& options,
                               audio::decoded_block_cache_t& cache, std::uint64_t head_frames,
                               std::stop_token stop) {
    constexpr std::size_t maximum_heads = 4;
    std::size_t started{};
    for (const auto& candidate : candidates) {
        if (stop.stop_requested() || started == maximum_heads) return;
        const auto binding = sources.binding(candidate.asset);
        if (!binding || binding->output_frame_count <= options.maximum_frames_per_sample) continue;
        ++started;
        static_cast<void>(sources.preload_head(candidate.asset, cache, head_frames, stop));
    }
}

void run_resident_loads(const std::vector<resident_wanted_t>& wanted,
                        audio::decoded_source_registry_t& sources,
                        audio::resident_sample_store_t& store, resident_sample_loader_t::run_state_t& state,
                        std::stop_token stop) {
    std::vector<audio::resident_request_t> requests;
    requests.reserve(wanted.size());
    for (const auto& entry : wanted) requests.push_back(entry.request);
    if (stop.stop_requested()) return;
    const auto missing = store.reconcile(requests);
    state.total = missing.size();
    std::vector<bool> taken(missing.size(), false);
    while (!stop.stop_requested()) {
        std::vector<asset_id_t> priority;
        {
            const std::scoped_lock lock(state.priority_mutex);
            priority = state.priority;
        }
        const auto next = next_pending_index(wanted, missing, taken, priority);
        if (!next) break;
        const auto status =
            sources.load_resident(wanted[*next].asset, store, requests[*next], stop);
        count_resident_status(status, state.over_budget, state.failed);
        if (status == wormedit::failure_code_t::capacity_exceeded)
            state.skipped.emplace_back(wanted[*next].asset, requests[*next]);
        ++state.done;
    }
    static_cast<void>(store.compact_if_needed());
}

} // namespace

namespace {
void retry_skipped_samples(audio::resident_sample_store_t& store,
                           resident_sample_loader_t::run_state_t& state, std::stop_token stop) {
    auto skipped = std::move(state.skipped);
    state.skipped.clear();
    state.over_budget = 0;
    state.reported = false;
    for (auto& [asset, request] : skipped) {
        if (stop.stop_requested()) return;
        const auto status = state.sources->load_resident(asset, store, request, stop);
        count_resident_status(status, state.over_budget, state.failed);
        if (status == wormedit::failure_code_t::capacity_exceeded)
            state.skipped.emplace_back(asset, std::move(request));
    }
}
void finish_resident_pass(audio::resident_sample_store_t& store,
                          resident_sample_loader_t::run_state_t& state,
                          std::stop_token stop, bool retry) {
    std::uint64_t observed{};
    while (!stop.stop_requested()) {
        {
            const std::scoped_lock lock(state.control_mutex);
            observed = state.budget_version;
        }
        if (retry || observed != 0) retry_skipped_samples(store, state, stop);
        const std::scoped_lock lock(state.control_mutex);
        if (state.budget_version == observed) {
            state.processed_budget_version = observed;
            state.running = false;
            return;
        }
        retry = true;
    }
    state.running = false;
}
} // namespace

resident_sample_loader_t::resident_sample_loader_t(audio::resident_sample_store_t& store,
                                                   task_scheduler_t& scheduler)
    : store_(store), scheduler_(scheduler) {}

resident_sample_loader_t::~resident_sample_loader_t() {
    cancel();
}

void resident_sample_loader_t::cancel() {
    stop_.request_stop();
    if (state_) state_->running = false;
}

void resident_sample_loader_t::set_budget_bytes(std::size_t bytes) {
    const auto previous = store_.options().budget_bytes;
    (void)store_.set_budget_bytes(bytes);
    if (!state_ || stop_.stop_requested()) return;
    const auto state = state_;
    {
        const std::scoped_lock lock(state->control_mutex);
        if (bytes > previous) ++state->budget_version;
        if (state->budget_version == state->processed_budget_version || state->running.exchange(true)) return;
    }
    const auto stop = stop_.get_token();
    task_graph_t graph{1};
    const auto added = graph.create_task({},
        [&store = store_, state, stop, pass = pass_mutex_](std::span<const task_outcome_t>, std::stop_token) {
            const std::scoped_lock lock(*pass);
            if (!stop.stop_requested()) finish_resident_pass(store, *state, stop, true);
            return task_value_t::from(std::make_shared<const int>(0));
        });
    if (!added || !scheduler_.begin(std::move(graph))) state->running = false;
}

void resident_sample_loader_t::prioritize(std::vector<asset_id_t> assets) {
    priority_hint_ = assets;
    if (!state_) return;
    const std::scoped_lock lock(state_->priority_mutex);
    state_->priority = std::move(assets);
}

namespace {

auto grouped_count(std::size_t value) -> std::string {
    auto digits = std::to_string(value);
    for (auto position = static_cast<std::ptrdiff_t>(digits.size()) - 3; position > 0;
         position -= 3) {
        digits.insert(static_cast<std::size_t>(position), ",");
    }
    return digits;
}

} // namespace

auto resident_sample_loader_t::drain_progress() -> std::optional<notification_post_t> {
    const auto current = progress();
    if (current.total == 0) return std::nullopt;
    const auto shown = current.running ? current.done : current.total;
    if (shown == last_progress_) return std::nullopt;
    last_progress_ = shown;
    if (current.running) {
        return notification_post_t{.severity = notification_severity_t::hint,
                                   .source = notification_source_t::audio,
                                   .message = "Loading keysounds " + grouped_count(current.done) + "/" +
                                       grouped_count(current.total)};
    }
    return notification_post_t{.severity = notification_severity_t::hint,
                               .source = notification_source_t::audio,
                               .message = "Loaded " + grouped_count(current.total) +
                                   " keysound(s) into memory."};
}

auto resident_sample_loader_t::progress() const -> progress_t {
    if (!state_) return {};
    return {state_->total.load(), state_->done.load(), state_->running.load()};
}

auto resident_sample_loader_t::drain_report() -> std::optional<notification_post_t> {
    if (!state_ || state_->running.load() || state_->reported.exchange(true)) return std::nullopt;
    const auto over_budget = state_->over_budget.load();
    const auto failed = state_->failed.load();
    if (over_budget == 0 && failed == 0) return std::nullopt;
    std::string line;
    if (over_budget != 0) {
        line += std::to_string(over_budget) +
                " keysound(s) exceed the resident memory budget and keep streaming from disk. Raise Keysound memory in Options to load them.";
    }
    if (failed != 0) {
        if (!line.empty()) line += " ";
        line += std::to_string(failed) + " keysound(s) could not be decoded and keep streaming.";
    }
    return notification_post_t{.severity = notification_severity_t::warning,
                               .source = notification_source_t::audio, .message = std::move(line)};
}

void resident_sample_loader_t::start(const song_document_t& document,
                                     const std::filesystem::path& asset_base,
                                     std::uint64_t, audio::decoded_source_registry_t& sources) {
    cancel();
    stop_ = std::stop_source{};
    state_ = std::make_shared<run_state_t>();
    state_->priority = priority_hint_;
    state_->sources = &sources;
    std::vector<resident_candidate_t> candidates;
    for (const auto asset : first_use_asset_order(document)) {
        const auto found = document.find_audio_asset(asset);
        if (found == nullptr) continue;
        if (const auto path = resolve_asset_path(*found, asset_base)) {
            candidates.push_back({asset, *path});
        }
    }
    auto state = state_;
    auto stop = stop_.get_token();
    task_graph_t graph{1};
    const auto added = graph.create_task(
        {}, [&store = store_, state, stop, candidates = std::move(candidates),
             &sources, head_cache = head_cache_, head_frames = head_frames_, pass = pass_mutex_](
                std::span<const task_outcome_t>, std::stop_token) {
            const std::scoped_lock pass_lock(*pass);
            if (stop.stop_requested())
                return task_value_t::from(std::make_shared<const int>(0));
            const auto wanted = resident_requests(candidates, sources, store.options(), stop);
            // The song head comes first so playback can start before any keysound loads.
            if (head_cache != nullptr) {
                preload_long_source_heads(candidates, sources, store.options(), *head_cache,
                                          head_frames, stop);
            }
            run_resident_loads(wanted, sources, store, *state, stop);
            finish_resident_pass(store, *state, stop, false);
            return task_value_t::from(std::make_shared<const int>(0));
        });
    if (!added || !scheduler_.begin(std::move(graph))) state->running = false;
}

namespace {

} // namespace

} // namespace wormedit::sys

#endif
