#include "sys/sound_import_staging.h"
#include "sys/compose_sound_actions.h"
#include "ui/frontend.h"
#include <algorithm>

namespace wormedit::sys {
namespace {

auto stage_automatic_sound_import(const song_document_t& document, chart_id_t chart,
                                  std::span<const std::string> paths, std::uint8_t fixed_kind = 0)
    -> compose_action_outcome_t {
    const auto* target = document.find_chart(chart);
    if (!target) return compose_action_outcome_t::blocked(
        "Select a difficulty before adding sounds.", notification_source_t::import_);
    auto prepared = prepare_keysound_import(document, chart, paths, fixed_kind);
    if (prepared.entries.empty()) return compose_action_outcome_t::from(prepared.message,
        prepared.skipped_unreadable ? notification_severity_t::warning : notification_severity_t::info,
        notification_source_t::import_);
    std::vector<wormedit::ui::sound_import_item_t> items;
    std::vector<std::string> source_paths;
    source_paths.reserve(prepared.entries.size());
    for (const auto& entry : prepared.entries)
        source_paths.push_back(entry.asset.source_path.generic_string());
    for (std::size_t index = 0; index < prepared.entries.size(); ++index) {
        const auto& entry = prepared.entries[index];
        items.push_back({source_paths[index].c_str(), entry.asset.name.c_str(),
                         static_cast<std::uint8_t>(entry.kind), fixed_kind != 0});
    }
    std::vector<wormedit::ui::sound_import_group_t> groups;
    for (const auto& group : document.sound_groups)
        groups.push_back({group.name.c_str(), group.id.value, nullptr, 0});
    const bool has_bgm = !target->sound_channels.empty() ||
        std::ranges::any_of(target->measures, [](const measure_chunk_t& measure) {
            return !measure.bgm_events.empty();
        });
    const auto staged = worm_ui_frontend_stage_sound_import(document.id.value, chart.value,
        document.revision, items.data(), items.size(), groups.data(), groups.size(), has_bgm,
        prepared.message.c_str());
    return compose_action_outcome_t::from(staged ? "Review the sounds before importing."
        : "The sound import could not be staged.",
        staged ? notification_severity_t::hint : notification_severity_t::warning,
        notification_source_t::import_, false, staged);
}

auto stage_dropped_sound_slots(const song_document_t& document, chart_id_t chart,
                               std::uint16_t first_slot, std::span<const std::string> paths)
    -> compose_action_outcome_t {
    if (paths.size() != 1) return stage_sound_slot_batch(document, chart, first_slot, paths);
    const auto prepared = prepare_sound_slot_batch(document, chart, first_slot, paths);
    if (!prepared.reason.empty() || prepared.items.size() != 1)
        return stage_sound_slot_batch(document, chart, first_slot, paths);
    const auto& item = prepared.items.front();
    const auto staged = worm_ui_frontend_stage_sound_slot_file_at(document.id.value,
        chart.value, document.revision, first_slot, item.occupied_resource_id, item.path.c_str());
    return compose_action_outcome_t::from(staged ? "Sound staged for the selected WAV slot."
        : "The WAV slots target is no longer active.",
        staged ? notification_severity_t::hint : notification_severity_t::warning,
        notification_source_t::import_, false, staged);
}

} // namespace

auto stage_sound_slot_batch(const song_document_t& document, chart_id_t chart,
                            std::uint16_t first_slot, std::span<const std::string> paths)
    -> compose_action_outcome_t {
    const auto prepared = prepare_sound_slot_batch(document, chart, first_slot, paths);
    if (prepared.items.empty() && !prepared.reason.empty())
        return compose_action_outcome_t::blocked(prepared.reason, notification_source_t::import_);
    std::vector<wormedit::ui::sound_slot_import_item_t> items;
    items.reserve(prepared.items.size());
    for (const auto& item : prepared.items)
        items.push_back({item.path.c_str(), item.name.c_str(), item.occupied_resource_id,
                         item.occupied_name.c_str(), item.ambiguous});
    const auto staged = worm_ui_frontend_stage_sound_slot_batch(document.id.value,
        chart.value, document.revision, first_slot, items.data(), items.size(),
        prepared.reason.c_str());
    return compose_action_outcome_t::from(staged ? "Review the WAV slot assignments before importing."
        : "The WAV slots target is no longer active.",
        staged ? notification_severity_t::hint : notification_severity_t::warning,
        notification_source_t::import_, false, staged);
}

auto stage_sound_import(const song_document_t& document, const workspace_controller_t& workspace,
                        std::span<const std::string> paths, std::uint8_t fixed_kind)
    -> compose_action_outcome_t {
    if (!workspace.selected_chart) return compose_action_outcome_t::blocked(
        "Select a difficulty before adding sounds.", notification_source_t::import_);
    WormUiKeysoundDockRequest dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    if (workspace.active_tab == workspace_tab_t::compose && dock.slots_mode && dock.selected_slot_explicit)
        return stage_sound_slot_batch(document, *workspace.selected_chart, dock.selected_slot, paths);
    return stage_automatic_sound_import(document, *workspace.selected_chart, paths, fixed_kind);
}

auto stage_dropped_sound_import(const song_document_t& document, const sound_drop_batch_t& batch)
    -> compose_action_outcome_t {
    const auto& origin = batch.origin;
    const auto chart = chart_id_t{origin.chart_id};
    if (origin.slots_mode && origin.slot_explicit)
        return stage_dropped_sound_slots(document, chart, origin.first_slot, batch.paths);
    return stage_automatic_sound_import(document, chart, batch.paths);
}

} // namespace wormedit::sys
