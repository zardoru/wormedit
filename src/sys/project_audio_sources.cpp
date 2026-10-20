#include "sys/project_audio_sources.h"
#include "sys/audio_source_registration_internal.h"
#include <algorithm>
#include <system_error>
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#if defined(WORMEDIT_HAS_PROJECT_AUDIO_OUTPUTS)
#include "sys/chart_preview_state.h"
#endif
#if defined(WORMEDIT_HAS_PROJECT_AUDIO_WAVEFORMS)
#include "sys/waveform_analysis_runtime.h"
#endif
namespace wormedit::sys {
namespace {
struct audition_source_reconciliation_context_t {
    const song_document_t& document;
    project_audio_sources_t& owner;
};

using resolved_inventory_t = std::vector<audition_asset_entry_t>;

// Resolved paths (empty when unusable), so a Save As that only moves the base is not a change.
auto resolved_inventory(const song_document_t& document, const std::filesystem::path& asset_base)
    -> resolved_inventory_t {
    resolved_inventory_t inventory;
    inventory.reserve(document.audio_assets.size());
    for (const auto& asset : document.audio_assets) {
        inventory.emplace_back(asset.id,
                               resolve_asset_path(asset, asset_base).value_or(std::filesystem::path{}));
    }
    return inventory;
}

auto same_file(const std::filesystem::path& left, const std::filesystem::path& right) -> bool {
    std::error_code error;
    return std::filesystem::equivalent(left, right, error) && !error;
}

struct source_reconciliation_plan_t {
    std::vector<audition_asset_entry_t> rebase;  // same file under a new path
    std::vector<audition_asset_entry_t> add;     // register (new, or replacing a changed file)
    std::vector<asset_id_t> remove;              // drop the old registration
    std::size_t unresolved{};
    [[nodiscard]] auto changes_sources() const -> bool { return !add.empty() || !remove.empty(); }
};

void plan_asset(const audio::decoded_source_registry_t& sources, const audition_asset_entry_t& next,
                source_reconciliation_plan_t& plan) {
    const auto& [id, path] = next;
    const auto registered = sources.source_path(id);
    if (path.empty()) ++plan.unresolved;
    if (registered && *registered == path) return;
    if (registered && !path.empty() && same_file(*registered, path)) {
        plan.rebase.push_back(next);
        return;
    }
    if (registered) plan.remove.push_back(id);
    if (!path.empty()) plan.add.push_back(next);
}

auto plan_source_reconciliation(const audition_source_reconciliation_context_t& context,
                                const resolved_inventory_t& next_inventory)
    -> source_reconciliation_plan_t {
    source_reconciliation_plan_t plan;
    for (const auto& next : next_inventory) plan_asset(context.owner.sources, next, plan);
    for (const auto& [id, path] : context.owner.asset_inventory) {
        const auto kept = std::ranges::find(next_inventory, id, &audition_asset_entry_t::first);
        if (kept == next_inventory.end()) plan.remove.push_back(id);
    }
    return plan;
}

auto apply_source_plan(const audition_source_reconciliation_context_t& context,
                       const source_reconciliation_plan_t& plan) -> std::size_t {
    for (const auto asset : plan.remove) static_cast<void>(context.owner.sources.remove(asset));
    context.owner.sources.ensure_source_capacity(context.document.audio_assets.size() + 3);
    std::size_t unavailable = plan.unresolved;
    for (const auto& [id, path] : plan.add) {
        const auto found = context.document.find_audio_asset(id);
        if (found == nullptr ||
            !register_one_asset(context.document, *found, path, context.owner.generation, context.owner.sources,
                                context.owner.probe_scheduler != nullptr))
            ++unavailable;
    }
    return unavailable;
}

// Same project, some assets added/removed/changed: keep every unchanged binding (identity and
// decoded data) and only touch what differs. Unchanged files are never re-probed.
auto reconcile_in_place(audition_source_reconciliation_context_t& context,
                        resolved_inventory_t next_inventory, bool stop_playback)
    -> std::optional<std::size_t> {
    const auto plan = plan_source_reconciliation(context, next_inventory);
    for (const auto& [id, path] : plan.rebase) static_cast<void>(context.owner.sources.rebase_source(id, path));
    context.owner.asset_inventory = std::move(next_inventory);
    if (!plan.changes_sources()) return std::nullopt;
    context.owner.reset_dependents(stop_playback);
    if (context.owner.resident_loader != nullptr) context.owner.resident_loader->cancel();
    ++context.owner.generation;
    const auto unavailable = apply_source_plan(context, plan);
    if (context.owner.probe_scheduler != nullptr) {
        start_audition_source_probes(context.owner.sources, *context.owner.probe_scheduler);
    }
    if (context.owner.resident_loader != nullptr && context.owner.probe_scheduler != nullptr) {
        context.owner.resident_loader->start(context.document, context.owner.asset_base, context.owner.generation,
                                       context.owner.sources);
    }
    return unavailable;
}

} // namespace

auto project_audio_sources_t::reconcile(
    const song_document_t& document, const std::filesystem::path& base, bool stop_playback)
    -> std::optional<std::size_t> {
    asset_base = base;
    reconciled_revision = document.revision;
    audition_source_reconciliation_context_t context{
        document, *this};
    auto next_inventory = resolved_inventory(context.document, context.owner.asset_base);
    if (context.owner.source_project == context.document.id) {
        if (context.owner.asset_inventory == next_inventory) return std::nullopt;
        return reconcile_in_place(context, std::move(next_inventory), stop_playback);
    }
    context.owner.reset_dependents(stop_playback);
    if (context.owner.resident_loader != nullptr) context.owner.resident_loader->cancel();
    context.owner.sources.clear();
    ++context.owner.generation;
    const auto register_sources = context.owner.probe_scheduler != nullptr
                                      ? register_audition_sources_deferred
                                      : register_audition_sources;
    const auto unavailable = register_sources(
        context.document, context.owner.asset_base, context.owner.generation, context.owner.sources);
    if (metronome) metronome->register_into(sources, generation);
    if (context.owner.probe_scheduler != nullptr) {
        start_audition_source_probes(context.owner.sources, *context.owner.probe_scheduler);
    }
    if (context.owner.resident_loader != nullptr && context.owner.probe_scheduler != nullptr) {
        context.owner.resident_loader->start(context.document, context.owner.asset_base, context.owner.generation,
                                       context.owner.sources);
    }
    context.owner.source_project = context.document.id;
    context.owner.asset_inventory = std::move(next_inventory);
    return unavailable;
}

void project_audio_sources_t::reset_dependents(bool stop_playback) {
#if defined(WORMEDIT_HAS_PROJECT_AUDIO_OUTPUTS)
    if (stop_playback) {
        if (audition_output) static_cast<void>(audition_output->stop());
        if (preview_output) static_cast<void>(preview_output->stop());
        if (preview) preview->invalidate();
    }
#if defined(WORMEDIT_HAS_PROJECT_AUDIO_WAVEFORMS)
    if (waveform) waveform->reset_sources();
#endif
#else
    static_cast<void>(stop_playback);
#endif
}
auto project_audio_sources_t::reconcile_if_needed(const song_document_t& document)
    -> std::optional<std::size_t> {
    if (source_project == document.id && reconciled_revision == document.revision)
        return std::nullopt;
    return reconcile(document, asset_base);
}
void project_audio_sources_t::ensure_current(const song_document_t& document) {
    const auto unavailable = reconcile_if_needed(document);
    if (unavailable && *unavailable != 0 && notifications)
        notifications->post(notification_severity_t::warning, notification_source_t::audio,
            std::to_string(*unavailable) + " audio asset(s) are unavailable for preview.");
}
auto project_audio_sources_t::bindings(const song_document_t& document)
    -> std::vector<audio::decoded_source_binding_t> {
    ensure_current(document);
    return sources.bindings();
}
auto project_audio_sources_t::resolve_preview(const song_document_t& document,
    const chart_document_t& chart, const preview_schedule_t& schedule)
    -> audio::preview_bindings_resolve_outcome_t {
    ensure_current(document);
    hitsounds.environment.map_directory = asset_base;
    const auto registered = sources.bindings();
    const auto extras = hitsounds.bindings(document, chart, sources, generation);
    auto result = audio::preview_bindings_resolution_t::from(document, chart.id, schedule,
        registered, {.skip_unavailable_preview_sources = true, .hitsounds = extras});
    if (hitsounds.take_fallback_notice() && notifications)
        notifications->post(notification_severity_t::warning, notification_source_t::audio,
            "Some osu! hitsamples were unavailable; default samples were used where possible.");
    return result;
}
auto project_audio_sources_t::binding(const song_document_t& document, asset_id_t asset)
    -> std::optional<audio::decoded_source_binding_t> {
    ensure_current(document);
    return sources.binding(asset);
}
} // namespace wormedit::sys
#endif
