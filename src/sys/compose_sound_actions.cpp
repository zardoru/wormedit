#include "sys/compose_sound_actions.h"
#include "sys/application_edit_inputs.h"
#include "sys/compose_action_outcome.h"
#include "sys/metadata_commands.h"

#include <wormedit/core/natural_order.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/sound_groups.h>
#include <wormedit/core/sound_import.h>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#endif

#include <algorithm>
#include <array>
#include <filesystem>
#include <numeric>
#include <span>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace wormedit::sys {
namespace {

auto file_name_of(const std::string& path) -> std::string {
    return std::filesystem::path{path}.filename().string();
}

auto probe_audio(const std::filesystem::path& path) -> std::optional<audio_stream_info_t> {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    audio::decoder_t decoder;
    std::string error;
    if (!decoder.open(path, &error))
        return std::nullopt;
    const auto info = decoder.info();
    if (!info)
        return std::nullopt;
    return audio_stream_info_t{.sample_rate = info->source_sample_rate,
                               .channels = info->source_channel_count,
                               .frame_count = info->source_frame_count,
                               .seekable = info->seekable};
#else
    static_cast<void>(path);
    return std::nullopt;
#endif
}

// Asset paths that already back a sample, compared without case folding because
// project paths keep the user's spelling.
auto sample_source_paths(const song_document_t& document) -> std::unordered_set<std::string> {
    std::unordered_set<std::string> paths;
    for (const auto& resource : document.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::sample) == resource.roles.end())
            continue;
        const auto* asset = document.find_audio_asset(resource.asset);
        if (asset != nullptr)
            paths.insert(asset->source_path.lexically_normal().generic_string());
    }
    return paths;
}

struct sound_import_plan_t {
    std::vector<bulk_sound_import_entry_t> entries;
    std::size_t skipped_unreadable{};
    std::size_t skipped_existing{};
};

auto plan_sound_import(const song_document_t& document, bool allocate_bms_identifiers,
                       std::vector<std::string> paths) -> sound_import_plan_t {
    std::ranges::sort(paths, [](const auto& left, const auto& right) {
        return natural_name_less(file_name_of(left), file_name_of(right));
    });
    const auto known = sample_source_paths(document);
    std::unordered_set<std::string> planned;
    sound_import_plan_t plan;
    for (const auto& text : paths) {
        const auto path = std::filesystem::path{text}.lexically_normal();
        std::error_code error;
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error) {
            ++plan.skipped_unreadable;
            continue;
        }
        const auto key = path.generic_string();
        if (known.contains(key) || !planned.insert(key).second) {
            ++plan.skipped_existing;
            continue;
        }
        const auto stream = probe_audio(path);
        if (!stream) {
            ++plan.skipped_unreadable;
            continue;
        }
        auto name = path.filename().generic_string();
        if (name.size() > 255)
            name.clear();
        plan.entries.push_back({.asset = {.source_path = path,
                                          .role = resource_role_t::sample,
                                          .name = std::move(name),
                                          .allocate_bms_identifier = allocate_bms_identifiers,
                                          .audio_stream = stream},
                                .kind = sound_kind_t::keysound,
                                .tags = {}});
    }
    return plan;
}

auto skipped_suffix(const sound_import_plan_t& plan) -> std::string {
    std::string text;
    if (plan.skipped_existing != 0)
        text += ", " + std::to_string(plan.skipped_existing) + " already in the list";
    if (plan.skipped_unreadable != 0)
        text += ", " + std::to_string(plan.skipped_unreadable) + " not readable audio";
    return text;
}

auto sound_count_text(std::size_t count) -> std::string {
    return std::to_string(count) + (count == 1 ? " sound" : " sounds");
}

auto bms_wav_identifier_value(const resource_entry_t& resource) -> std::optional<std::size_t> {
    for (const auto& identity : resource.target_identities)
        if (const auto value = bms_wav_slot_value(identity))
            return value;
    return std::nullopt;
}

struct slot_owner_t {
    std::uint64_t id{};
    std::string name;
    bool ambiguous{};
};

auto slot_owner(const song_document_t& document, std::size_t slot) -> slot_owner_t {
    const auto owner = bms_wav_slot_owner(document, slot);
    const auto entry = std::ranges::find(document.resources, owner.resource, &resource_entry_t::id);
    return {owner.resource.value, entry == document.resources.end() ? "" : entry->name,
            owner.ambiguous};
}

void set_first_error(std::string& reason, std::string next) {
    if (reason.empty())
        reason = std::move(next);
}

auto sorted_slot_paths(std::span<const std::string> paths) -> std::vector<std::string> {
    auto sorted = std::vector<std::string>{paths.begin(), paths.end()};
    std::ranges::sort(sorted, [](const auto& left, const auto& right) {
        const auto left_path = std::filesystem::path{left}.lexically_normal();
        const auto right_path = std::filesystem::path{right}.lexically_normal();
        const auto left_name = left_path.filename().generic_string();
        const auto right_name = right_path.filename().generic_string();
        if (natural_name_less(left_name, right_name))
            return true;
        if (natural_name_less(right_name, left_name))
            return false;
        return left_path.generic_string() < right_path.generic_string();
    });
    return sorted;
}

auto prepare_slot_item(const song_document_t& document, std::size_t slot, const std::string& source,
                       std::unordered_set<std::string>& seen, prepared_sound_slot_batch_t& prepared)
    -> prepared_sound_slot_import_item_t {
    auto path = std::filesystem::path{source}.lexically_normal();
    const auto key = path.generic_string();
    std::error_code error;
    const auto regular =
        path.is_absolute() && std::filesystem::is_regular_file(path, error) && !error;
    const auto duplicate = !seen.insert(key).second;
    const auto stream = probe_audio(path);
    if (!regular)
        set_first_error(prepared.reason, "Audio file is not readable: " + key);
    if (duplicate)
        set_first_error(prepared.reason, "The selection contains the same file more than once.");
    if (!stream)
        set_first_error(prepared.reason, "File is not decodable audio: " + key);
    auto name = path.filename().generic_string();
    if (name.empty() || name.size() > 255)
        set_first_error(prepared.reason, "A selected filename cannot be used as a sound name.");
    const auto owner = slot_owner(document, slot);
    if (owner.ambiguous)
        set_first_error(prepared.reason,
                        "A destination WAV slot is assigned to more than one sound.");
    return {.path = key,
            .name = std::move(name),
            .occupied_resource_id = owner.id,
            .occupied_name = owner.name,
            .ambiguous = owner.ambiguous};
}

auto imported_bms_id_ranges(const song_document_t& document, std::size_t first_resource,
                            std::span<const std::size_t> entry_indexes) -> std::size_t {
    auto ranges = std::size_t{};
    auto previous = std::optional<std::size_t>{};
    for (const auto entry_index : entry_indexes) {
        const auto resource_index = first_resource + entry_index;
        if (resource_index >= document.resources.size())
            continue;
        const auto value = bms_wav_identifier_value(document.resources[resource_index]);
        if (!value)
            continue;
        if (!previous || *value != *previous + 1)
            ++ranges;
        previous = *value;
    }
    return ranges;
}

auto imported_bms_id_ranges(const song_document_t& document, std::size_t count) -> std::size_t {
    if (count == 0 || count > document.resources.size())
        return 0;
    std::vector<std::size_t> entries(count);
    std::iota(entries.begin(), entries.end(), 0);
    return imported_bms_id_ranges(document, document.resources.size() - count, entries);
}

auto ungrouped_entry_indexes(std::size_t entry_count,
                             std::span<const bulk_sound_import_group_t> groups)
    -> std::vector<std::size_t> {
    std::vector<bool> grouped(entry_count);
    for (const auto& group : groups)
        for (const auto index : group.entry_indexes)
            if (index < grouped.size())
                grouped[index] = true;
    std::vector<std::size_t> ungrouped;
    for (std::size_t index = 0; index < grouped.size(); ++index)
        if (!grouped[index])
            ungrouped.push_back(index);
    return ungrouped;
}

} // namespace

auto import_keysound_feedback(document_history_t& history, workspace_controller_t& workspace,
                              chart_id_t chart, std::span<const std::string> paths,
                              sound_kind_t kind = sound_kind_t::keysound)
    -> compose_action_outcome_t {
    const auto* target = history.document.find_chart(chart);
    if (target == nullptr)
        return compose_action_outcome_t::blocked("Select a difficulty before adding sounds.");
    if (paths.empty())
        return compose_action_outcome_t::from("No files were dropped.",
                                              notification_severity_t::info);
    if (paths.size() > maximum_bulk_sound_import)
        return compose_action_outcome_t::blocked(
            "Drop at most " + std::to_string(maximum_bulk_sound_import) + " sounds at once.");
    auto plan = plan_sound_import(history.document, target->source_family == format_family_t::bms,
                                  {paths.begin(), paths.end()});
    if (plan.entries.empty())
        return compose_action_outcome_t::from("No sounds were added" + skipped_suffix(plan) + ".",
                                              plan.skipped_unreadable
                                                  ? notification_severity_t::warning
                                                  : notification_severity_t::info);
    for (auto& entry : plan.entries)
        entry.kind = kind;
    const auto count = plan.entries.size();
    const auto split_ids = target->source_family == format_family_t::bms &&
                           count <= bms_identifier_capacity(history.document, "WAV").total -
                                        bms_identifier_capacity(history.document, "WAV").used &&
                           !find_bms_identifier_run(history.document, "WAV", count).has_value();
    auto applied = apply_bulk_sound_import(
        history, {.chart = kind == sound_kind_t::stem ? chart : chart_id_t{},
                  .entries = std::move(plan.entries)});
    if (!applied.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{
                .rebind_failure = "Sounds added, but workspace rebinding failed.",
                .unknown_severity = notification_severity_t::info,
                .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only});
    if (!applied.applied)
        return compose_action_outcome_t::from("Sound import made no document change.",
                                              notification_severity_t::info);
    auto message = "Added " + sound_count_text(count) + skipped_suffix(plan);
    if (split_ids)
        message += ". Group split across " +
                   std::to_string(imported_bms_id_ranges(history.document, count)) +
                   " ID ranges; Renumber by name can rejoin it";
    message += ".";
    auto outcome = compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .rebind_failure = "Sounds added, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        std::move(message));
    if (plan.skipped_unreadable)
        outcome.severity = notification_severity_t::warning;
    return outcome;
}

auto import_keysound_files(document_history_t& history, workspace_controller_t& workspace,
                           chart_id_t chart, std::span<const std::string> paths,
                           notification_sink_t notifications) -> std::string {
    const document_edit_inputs_t document{history, workspace, notifications};
    auto outcome = import_keysound_feedback(history, workspace, chart, paths);
    outcome.source = notification_source_t::import_;
    auto finished = finish_compose_action(document, notifications, std::move(outcome));
    return std::move(finished.message);
}

namespace {

auto sound_ids(const WormUiWorkspaceAction& action) -> std::vector<resource_id_t> {
    std::vector<resource_id_t> ids;
    if (!action.compose_sound_resource_ids)
        return ids;
    ids.reserve(action.compose_sound_resource_count);
    for (std::size_t index = 0; index < action.compose_sound_resource_count; ++index)
        ids.push_back(resource_id_t{action.compose_sound_resource_ids[index]});
    return ids;
}

auto assign_picked_slot(const WormUiWorkspaceAction& action, document_history_t& history,
                        workspace_controller_t& workspace, chart_id_t chart_id)
    -> compose_action_outcome_t {
    const auto* chart = history.document.find_chart(chart_id);
    if (chart == nullptr || chart->source_family != format_family_t::bms)
        return compose_action_outcome_t::blocked(
            "Direct sound slots are available only for a BMS chart.");
    if (!chart_stamp_t{.document = {.project = history.document.id,
                                    .revision = action.compose_input_document_revision},
                       .chart = chart_id_t{action.compose_input_chart_id}}
             .is_current(history.document, chart_id))
        return compose_action_outcome_t::blocked(stale_action_message("Sound slot assignment"));
    if (action.compose_sound_slot == 0 || action.compose_sound_slot >= 1296)
        return compose_action_outcome_t::blocked("Choose a sound slot from 01 through ZZ.");
    if (action.compose_import_path_count != 1 || !action.compose_import_paths ||
        !action.compose_import_paths[0])
        return compose_action_outcome_t::blocked(
            "Assign exactly one audio file to the selected slot.");
    const auto path = std::filesystem::path{action.compose_import_paths[0]}.lexically_normal();
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error)
        return compose_action_outcome_t::blocked("The selected audio file is not readable.");
    const auto stream = probe_audio(path);
    if (!stream)
        return compose_action_outcome_t::blocked("The selected file is not decodable audio.");
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const auto number = action.compose_sound_slot;
    const auto identifier = std::string{digits[number / 36], digits[number % 36]};
    auto name = path.filename().generic_string();
    if (name.size() > 255)
        name.clear();
    auto applied = apply_bms_wav_slot_assignment(
        history, {.identifier = identifier,
                  .imported_file = {.source_path = path,
                                    .role = resource_role_t::sample,
                                    .name = std::move(name),
                                    .audio_stream = stream},
                  .confirm_replacement = action.compose_sound_slot_confirmed});
    return compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .unchanged = "Sound slot assignment made no change.",
            .rebind_failure = "Sounds updated, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        "Assigned audio to WAV " + identifier + ".");
}

auto assign_picked_slot_batch(const WormUiWorkspaceAction& action, document_history_t& history,
                              workspace_controller_t& workspace, chart_id_t chart_id)
    -> compose_action_outcome_t {
    const auto* chart = history.document.find_chart(chart_id);
    if (chart == nullptr || chart->source_family != format_family_t::bms)
        return compose_action_outcome_t::blocked(
            "Direct sound slots are available only for a BMS chart.");
    if (!chart_stamp_t{.document = {.project = history.document.id,
                                    .revision = action.compose_input_document_revision},
                       .chart = chart_id_t{action.compose_input_chart_id}}
             .is_current(history.document, chart_id))
        return compose_action_outcome_t::blocked(stale_action_message("Sound slot assignment"));
    if (action.compose_sound_slot == 0 || action.compose_sound_slot >= 1296)
        return compose_action_outcome_t::blocked("Choose a sound slot from 01 through ZZ.");
    if (!action.sound_import_items || action.sound_import_item_count == 0 ||
        action.sound_import_item_count > maximum_bulk_sound_import)
        return compose_action_outcome_t::blocked(
            "Choose between 1 and 1295 audio files for the WAV slots.");

    auto files = std::vector<import_resource_asset_action_t>{};
    files.reserve(action.sound_import_item_count);
    for (std::size_t index = 0; index < action.sound_import_item_count; ++index) {
        const auto& item = action.sound_import_items[index];
        if (!item.path || !item.name ||
            item.kind != static_cast<std::uint8_t>(sound_kind_t::keysound))
            return compose_action_outcome_t::blocked(
                "The reviewed WAV slot list contains an invalid item.");
        const auto path = std::filesystem::path{item.path}.lexically_normal();
        std::error_code error;
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error)
            return compose_action_outcome_t::blocked(
                "A reviewed audio file is no longer readable: " + path.generic_string());
        const auto stream = probe_audio(path);
        if (!stream)
            return compose_action_outcome_t::blocked(
                "A reviewed file is no longer decodable audio: " + path.generic_string());
        const auto name = std::string{item.name};
        if (name.empty() || name.size() > 255)
            return compose_action_outcome_t::blocked("Sound names must contain 1 to 255 bytes.");
        files.push_back({.source_path = path,
                         .role = resource_role_t::sample,
                         .name = name,
                         .audio_stream = stream});
    }
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const auto number = action.compose_sound_slot;
    const auto identifier = std::string{digits[number / 36], digits[number % 36]};
    auto applied = apply_bms_wav_slot_batch_assignment(
        history, {.first_identifier = identifier,
                  .imported_files = std::move(files),
                  .confirm_replacements = action.compose_sound_slot_confirmed});
    return compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .unchanged = "WAV slot batch made no document change.",
            .rebind_failure = "Sounds updated, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        "Assigned " + sound_count_text(action.sound_import_item_count) + " starting at WAV " +
            identifier + ".");
}

auto delete_sounds(const WormUiWorkspaceAction& action, document_history_t& history,
                   workspace_controller_t& workspace) -> compose_action_outcome_t {
    const auto ids = sound_ids(action);
    if (auto rejected = metadata_command_detail::stale_rejection(
            history, action.compose_input_document_revision, "Resource deletion"))
        return compose_action_outcome_t::from(rejected->diagnostics.front(),
                                              notification_source_t::edit,
                                              notification_severity_t::info);
    auto applied = apply_resource_deletions(history, ids);
    return compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .unchanged = "No sounds were removed.",
            .rebind_failure = "Sounds updated, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        "Removed " + sound_count_text(ids.size()) + " from the list.");
}

auto change_sound_layers(const WormUiWorkspaceAction& action, document_history_t& history,
                         workspace_controller_t& workspace, chart_id_t chart)
    -> compose_action_outcome_t {
    const auto ids = sound_ids(action);
    const auto clear = action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS;
    const auto add = action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER;
    auto applied =
        clear
            ? apply_sound_layer_defaults(history, {.chart = chart, .resources = ids, .layers = {}})
            : apply_sound_layer_membership(
                  history, {.chart = chart,
                            .resources = ids,
                            .layer = layer_id_t{action.compose_semantic_layer_id},
                            .member = add,
                            .overwrite_note_layers = action.compose_sound_overwrite_note_layers});
    const auto subject = sound_count_text(ids.size());
    return compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .unchanged = "The sounds already had that layer state.",
            .rebind_failure = "Sounds updated, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        clear ? "Cleared the layers of " + subject + "."
        : add ? "Added the layer to " + subject + "."
              : "Removed the layer from " + subject + ".");
}

auto group_sounds(const WormUiWorkspaceAction& action, document_history_t& history,
                  workspace_controller_t& workspace) -> compose_action_outcome_t {
    if (!action.compose_sound_group_assignments || action.compose_sound_group_assignment_count == 0)
        return compose_action_outcome_t::blocked("Choose at least one sound group.");
    auto next_group = std::uint64_t{1};
    for (const auto& group : history.document.sound_groups)
        next_group = std::max(next_group, group.id.value + 1);
    auto edits = std::vector<sound_group_edit_action_t>{};
    edits.reserve(action.compose_sound_group_assignment_count);
    for (std::size_t index = 0; index < action.compose_sound_group_assignment_count; ++index) {
        const auto& assignment = action.compose_sound_group_assignments[index];
        if (!assignment.sound_ids || assignment.sound_count == 0)
            return compose_action_outcome_t::blocked("A sound group has no selected members.");
        auto ids = std::vector<resource_id_t>{};
        ids.reserve(assignment.sound_count);
        for (std::size_t member = 0; member < assignment.sound_count; ++member)
            ids.push_back(resource_id_t{assignment.sound_ids[member]});
        if (assignment.group_id != 0) {
            const auto group =
                std::ranges::find(history.document.sound_groups,
                                  sound_group_id_t{assignment.group_id}, &sound_group_t::id);
            if (group == history.document.sound_groups.end())
                return compose_action_outcome_t::blocked(
                    "The selected sound group no longer exists.");
            if (assignment.name && assignment.name[0] != '\0' && assignment.name != group->name)
                edits.emplace_back(
                    rename_sound_group_action_t{.group = group->id, .name = assignment.name});
            edits.emplace_back(add_sounds_to_group_action_t{
                .group = sound_group_id_t{assignment.group_id}, .sounds = std::move(ids)});
        } else {
            if (!assignment.name)
                return compose_action_outcome_t::blocked("A new sound group needs a name.");
            edits.emplace_back(create_sound_group_action_t{.group = sound_group_id_t{next_group++},
                                                           .name = assignment.name,
                                                           .sounds = std::move(ids)});
        }
    }
    if (auto rejected = metadata_command_detail::stale_rejection(
            history, action.compose_input_document_revision, "Sound group edit"))
        return compose_action_outcome_t::from(rejected->diagnostics.front(),
                                              notification_source_t::edit,
                                              notification_severity_t::info);
    auto applied =
        apply_sound_group_edits(history, edits, action.compose_sound_group_keep_ids_together);
    return compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .unchanged = "The selected sounds were already grouped.",
            .rebind_failure = "Sounds updated, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        "Grouped selected sounds.");
}

auto ungroup_sounds(const WormUiWorkspaceAction& action, document_history_t& history,
                    workspace_controller_t& workspace) -> compose_action_outcome_t {
    const auto ids = sound_ids(action);
    if (auto rejected = metadata_command_detail::stale_rejection(
            history, action.compose_input_document_revision, "Sound group edit"))
        return compose_action_outcome_t::from(rejected->diagnostics.front(),
                                              notification_source_t::edit,
                                              notification_severity_t::info);
    auto applied = apply_sound_group_edit(history, ungroup_sounds_action_t{.sounds = ids});
    return compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .unchanged = "The selected sounds were already ungrouped.",
            .rebind_failure = "Sounds updated, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        "Ungrouped selected sounds.");
}

auto renumber_dock_ids(const WormUiWorkspaceAction& action, document_history_t& history,
                       workspace_controller_t& workspace, chart_id_t chart_id)
    -> compose_action_outcome_t {
    const auto* chart = history.document.find_chart(chart_id);
    if (chart == nullptr || chart->source_family != format_family_t::bms)
        return compose_action_outcome_t::blocked(
            "BMS identifiers are available only for a BMS chart.");
    if (!action.compose_renumber_all && action.compose_sound_resource_count == 0)
        return compose_action_outcome_t::blocked("Select sounds or choose all before renumbering.");
    std::vector<resource_id_t> ids;
    if (!action.compose_renumber_all)
        ids = sound_ids(action);
    const auto name_space = action.compose_renumber_bga ? "BMP" : "WAV";
    if (auto rejected = metadata_command_detail::stale_rejection(
            history, action.compose_input_document_revision, "BMS identifier renumber"))
        return compose_action_outcome_t::from(rejected->diagnostics.front(),
                                              notification_source_t::edit,
                                              notification_severity_t::info);
    auto applied = apply_bms_identifier_renumber(
        history, {.name_space = name_space, .resources = std::move(ids)});
    return compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .unchanged = "BMS IDs are already in name order.",
            .rebind_failure = "Sounds updated, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        std::string{"Renumbered "} + name_space + " IDs by name.");
}

auto request_audition(const WormUiWorkspaceAction& action, const document_history_t& history,
                      application_command_state_t& application_state) -> compose_action_outcome_t {
    const resource_id_t resource{action.compose_resource_id};
    const auto found =
        std::ranges::find(history.document.resources, resource, &resource_entry_t::id);
    if (!resource || found == history.document.resources.end())
        return compose_action_outcome_t::blocked("That sound is no longer in the list.");
    if (found->missing)
        return compose_action_outcome_t::blocked("That sound's file is missing.");
    application_state.project.sound_selection.pending_sound_audition = resource;
    return compose_action_outcome_t::from(
        "Playing " + (found->name.empty() ? std::string{"sound"} : found->name) + ".",
        notification_severity_t::info);
}

auto import_picked_files(const WormUiWorkspaceAction& action, document_history_t& history,
                         workspace_controller_t& workspace, chart_id_t chart)
    -> compose_action_outcome_t {
    std::vector<std::string> paths;
    for (std::size_t index = 0; index < action.compose_import_path_count; ++index)
        if (action.compose_import_paths && action.compose_import_paths[index])
            paths.emplace_back(action.compose_import_paths[index]);
    return import_keysound_feedback(history, workspace, chart, paths,
                                    action.sound_import_kind ==
                                            static_cast<std::uint8_t>(sound_kind_t::stem)
                                        ? sound_kind_t::stem
                                        : sound_kind_t::keysound);
}

auto confirmed_import_entries(const WormUiWorkspaceAction& action, const song_document_t& document,
                              bool bms_chart, std::string& reason)
    -> std::optional<std::vector<bulk_sound_import_entry_t>> {
    if (!action.sound_import_items || action.sound_import_item_count == 0 ||
        action.sound_import_item_count > maximum_bulk_sound_import) {
        reason = "Sound import needs between 1 and 1295 files.";
        return std::nullopt;
    }
    const auto known = sample_source_paths(document);
    auto planned = std::unordered_set<std::string>{};
    auto entries = std::vector<bulk_sound_import_entry_t>{};
    entries.reserve(action.sound_import_item_count);
    constexpr auto minimum_kind = std::uint8_t{1};
    constexpr auto maximum_kind = std::uint8_t{3};
    for (std::size_t index = 0; index < action.sound_import_item_count; ++index) {
        const auto& item = action.sound_import_items[index];
        if (!item.path || !item.name || item.kind < minimum_kind || item.kind > maximum_kind) {
            reason = "The reviewed sound list contains an invalid item.";
            return std::nullopt;
        }
        const auto path = std::filesystem::path{item.path}.lexically_normal();
        std::error_code error;
        if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error) {
            reason = "A reviewed audio file is no longer readable: " + path.generic_string();
            return std::nullopt;
        }
        const auto key = path.generic_string();
        if (known.contains(key) || !planned.insert(key).second) {
            reason = "A reviewed audio file is already in the sound list or appears twice.";
            return std::nullopt;
        }
        const auto stream = probe_audio(path);
        if (!stream) {
            reason = "A reviewed file is no longer decodable audio: " + key;
            return std::nullopt;
        }
        auto name = std::string{item.name};
        if (name.empty() || name.size() > 255) {
            reason = "Sound names must contain 1 to 255 bytes.";
            return std::nullopt;
        }
        entries.push_back({.asset = {.source_path = path,
                                     .role = resource_role_t::sample,
                                     .name = std::move(name),
                                     .allocate_bms_identifier = bms_chart,
                                     .audio_stream = stream},
                           .kind = static_cast<sound_kind_t>(item.kind)});
    }
    return entries;
}

auto confirmed_import_groups(const WormUiWorkspaceAction& action, std::size_t entry_count,
                             std::string& reason)
    -> std::optional<std::vector<bulk_sound_import_group_t>> {
    auto groups = std::vector<bulk_sound_import_group_t>{};
    if (action.sound_import_group_count != 0 && !action.sound_import_groups) {
        reason = "The reviewed group list is invalid.";
        return std::nullopt;
    }
    groups.reserve(action.sound_import_group_count);
    for (std::size_t index = 0; index < action.sound_import_group_count; ++index) {
        const auto& group = action.sound_import_groups[index];
        if (!group.name || !group.member_indices || group.member_count == 0) {
            reason = "A reviewed group has no valid members.";
            return std::nullopt;
        }
        if (std::ranges::any_of(
                std::span{group.member_indices, group.member_count},
                [entry_count](const auto member) { return member >= entry_count; })) {
            reason = "A reviewed group contains an invalid file index.";
            return std::nullopt;
        }
        auto row = bulk_sound_import_group_t{.name = group.name};
        if (group.existing_group_id != 0)
            row.existing_group = sound_group_id_t{group.existing_group_id};
        row.entry_indexes.assign(group.member_indices, group.member_indices + group.member_count);
        groups.push_back(std::move(row));
    }
    return groups;
}

auto confirm_import(const WormUiWorkspaceAction& action, document_history_t& history,
                    workspace_controller_t& workspace, chart_id_t chart)
    -> compose_action_outcome_t {
    const chart_stamp_t stamp{.document = {.project = song_id_t{action.sound_import_project_id},
                                           .revision = action.compose_input_document_revision},
                              .chart = chart_id_t{action.compose_input_chart_id}};
    if (!stamp.is_current(history.document, chart))
        return compose_action_outcome_t::blocked(stale_action_message("Sound import"));
    const auto* target = history.document.find_chart(chart);
    if (target == nullptr)
        return compose_action_outcome_t::blocked("Select a difficulty before adding sounds.");
    auto reason = std::string{};
    auto entries = confirmed_import_entries(action, history.document,
                                            target->source_family == format_family_t::bms, reason);
    if (!entries)
        return compose_action_outcome_t::blocked(reason);
    auto groups = confirmed_import_groups(action, entries->size(), reason);
    if (!groups)
        return compose_action_outcome_t::blocked(reason);
    const bool bms_chart = target->source_family == format_family_t::bms;
    const bool has_groups = !groups->empty();
    const auto imported_resource_begin = history.document.resources.size();
    const auto loose_entries = ungrouped_entry_indexes(entries->size(), *groups);
    auto import = bulk_sound_import_action_t{
        .chart = chart, .entries = std::move(*entries), .groups = std::move(*groups)};
    auto applied = apply_bulk_sound_import(history, import);
    if (!applied.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{
                .rebind_failure = "Sounds added, but workspace rebinding failed.",
                .unknown_severity = notification_severity_t::info,
                .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only});
    if (!applied.applied)
        return compose_action_outcome_t::from("Sound import made no document change.",
                                              notification_severity_t::info);
    auto message = "Added " + sound_count_text(action.sound_import_item_count);
    const auto range_count =
        bms_chart ? imported_bms_id_ranges(history.document, imported_resource_begin, loose_entries)
                  : 0;
    if (range_count > 1) {
        message += has_groups ? ". Ungrouped files split across " : ". Group split across ";
        message += std::to_string(range_count) + " ID ranges; Renumber by name can rejoin them";
    }
    message += ".";
    auto outcome = compose_action_outcome_t::from(
        std::move(applied),
        compose_action_description_t{
            .rebind_failure = "Sounds added, but workspace rebinding failed.",
            .unknown_severity = notification_severity_t::info,
            .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
        std::move(message));
    return outcome;
}

} // namespace

auto prepare_keysound_import(const song_document_t& document, chart_id_t chart,
                             std::span<const std::string> paths, std::uint8_t fixed_kind)
    -> prepared_keysound_import_t {
    const auto* target = document.find_chart(chart);
    if (target == nullptr)
        return {.message = "Select a difficulty before adding sounds."};
    auto plan = plan_sound_import(document, target->source_family == format_family_t::bms,
                                  {paths.begin(), paths.end()});
    if (fixed_kind == static_cast<std::uint8_t>(sound_kind_t::stem))
        for (auto& entry : plan.entries)
            entry.kind = sound_kind_t::stem;
    auto message = "Prepared " + sound_count_text(plan.entries.size()) + skipped_suffix(plan);
    if (plan.entries.size() > maximum_bulk_sound_import)
        message += ". This exceeds the 1295-file limit; remove files before confirming";
    return {.entries = std::move(plan.entries),
            .skipped_unreadable = plan.skipped_unreadable,
            .skipped_existing = plan.skipped_existing,
            .message = std::move(message)};
}

auto prepare_sound_slot_batch(const song_document_t& document, chart_id_t chart,
                              std::uint16_t first_slot, std::span<const std::string> paths)
    -> prepared_sound_slot_batch_t {
    auto prepared = prepared_sound_slot_batch_t{};
    const auto* target = document.find_chart(chart);
    if (target == nullptr || target->source_family != format_family_t::bms)
        set_first_error(prepared.reason, "Direct sound slots are available only for a BMS chart.");
    if (first_slot == 0 || first_slot > 1295)
        set_first_error(prepared.reason, "Choose a sound slot from 01 through ZZ.");
    if (paths.empty() || paths.size() > maximum_bulk_sound_import)
        set_first_error(prepared.reason,
                        "Choose between 1 and 1295 audio files for the WAV slots.");
    if (first_slot > 0 && first_slot <= 1295 && !paths.empty() &&
        (paths.size() > 1295 || paths.size() - 1 > 1295 - first_slot))
        set_first_error(prepared.reason,
                        "The selected files exceed the remaining WAV slots through ZZ.");
    const auto sorted = sorted_slot_paths(paths);
    auto seen = std::unordered_set<std::string>{};
    prepared.items.reserve(sorted.size());
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        const auto destination = static_cast<std::size_t>(first_slot) + index;
        prepared.items.push_back(
            prepare_slot_item(document, destination, sorted[index], seen, prepared));
    }
    return prepared;
}

auto dispatch_sound_properties_action(const WormUiWorkspaceAction& action,
                                      document_history_t& history,
                                      workspace_controller_t& workspace,
                                      notification_sink_t notifications) -> bool {
    if (!action.sound_properties_apply)
        return false;
    const document_edit_inputs_t document{history, workspace, notifications};
    if (workspace.active_tab != workspace_tab_t::audio_slices || !workspace.selected_chart ||
        action.sound_properties_project_id != history.document.id.value ||
        action.sound_properties_chart_id != workspace.selected_chart->value ||
        action.sound_properties_document_revision != history.document.revision) {
        static_cast<void>(finish_compose_action(
            document, notifications,
            compose_action_outcome_t::blocked(stale_action_message("Sound properties"))));
        return true;
    }
    const auto resource = std::ranges::find(history.document.resources,
                                            resource_id_t{action.sound_properties_resource_id},
                                            &resource_entry_t::id);
    const auto name_end = std::find(std::begin(action.sound_properties_name),
                                    std::end(action.sound_properties_name), '\0');
    if (resource == history.document.resources.end() ||
        name_end == std::end(action.sound_properties_name)) {
        static_cast<void>(finish_compose_action(
            document, notifications,
            compose_action_outcome_t::blocked("The sound properties draft is invalid.")));
        return true;
    }
    auto applied = apply_resource_sound_metadata(
        history, {.resource = resource->id,
                  .kind = static_cast<sound_kind_t>(action.sound_properties_kind),
                  .tags = resource->tags,
                  .name = std::string(std::begin(action.sound_properties_name), name_end)});
    static_cast<void>(finish_compose_action(
        document, notifications,
        compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{
                .unchanged = "Sound properties are unchanged.",
                .rebind_failure = "Sounds updated, but workspace rebinding failed.",
                .unknown_severity = notification_severity_t::info,
                .rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only},
            "Sound properties updated.")));
    return true;
}

auto dispatch_sound_edit_outcome(const WormUiWorkspaceAction& action, document_history_t& history,
                                 workspace_controller_t& workspace, chart_id_t chart,
                                 application_command_state_t& application_state)
    -> std::optional<compose_action_outcome_t> {

    const auto source =
        action.compose_action == WORM_UI_COMPOSE_ACTION_AUDITION_SOUND
            ? notification_source_t::audio
        : action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES ||
                action.compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT ||
                action.compose_action == WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT ||
                action.compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH
            ? notification_source_t::import_
            : notification_source_t::edit;
    std::optional<compose_action_outcome_t> outcome;
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES:
        outcome = import_picked_files(action, history, workspace, chart);
        break;
    case WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT:
        outcome = confirm_import(action, history, workspace, chart);
        break;
    case WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT:
        outcome = assign_picked_slot(action, history, workspace, chart);
        break;
    case WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH:
        outcome = assign_picked_slot_batch(action, history, workspace, chart);
        break;
    case WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS:
        outcome = delete_sounds(action, history, workspace);
        break;
    case WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS:
        outcome = group_sounds(action, history, workspace);
        break;
    case WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS:
        outcome = ungroup_sounds(action, history, workspace);
        break;
    case WORM_UI_COMPOSE_ACTION_AUDITION_SOUND:
        outcome = request_audition(action, history, application_state);
        break;
    case WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER:
    case WORM_UI_COMPOSE_ACTION_REMOVE_SOUND_LAYER:
    case WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS:
        if (auto rejected = metadata_command_detail::stale_rejection(
                history, action.compose_input_document_revision, "Sound layer edit"))
            return compose_action_outcome_t::from(rejected->diagnostics.front(),
                                                  notification_source_t::edit,
                                                  notification_severity_t::info);
        outcome = change_sound_layers(action, history, workspace, chart);
        break;
    case WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS:
        outcome = renumber_dock_ids(action, history, workspace, chart);
        break;
    default:
        return std::nullopt;
    }
    outcome->source = source;
    return outcome;
}

auto dispatch_compose_sound_action(const WormUiWorkspaceAction& action, document_history_t& history,
                                   workspace_controller_t& workspace, chart_id_t chart,
                                   application_command_state_t& application_state,
                                   notification_sink_t notifications)
    -> std::optional<std::string> {
    auto outcome =
        dispatch_sound_edit_outcome(action, history, workspace, chart, application_state);
    if (!outcome)
        return std::nullopt;
    const document_edit_inputs_t document{history, workspace, notifications};
    auto finished = finish_compose_action(document, notifications, std::move(*outcome));
    return std::move(finished.message);
}

} // namespace wormedit::sys
