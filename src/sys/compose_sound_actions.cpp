#include "sys/compose_sound_actions.h"

#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/natural_order.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/sound_import.h>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#endif

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace wormedit::sys {
namespace {

auto folded_name(const std::string& path) -> std::string {
    return std::filesystem::path{path}.filename().string();
}

auto probe_audio(const std::filesystem::path& path) -> std::optional<audio_stream_info_t> {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    audio::decoder_t decoder;
    std::string error;
    if (!decoder.open(path, &error)) return std::nullopt;
    const auto info = decoder.info();
    if (!info) return std::nullopt;
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
        const auto asset = std::ranges::find(document.audio_assets, resource.asset,
                                             &audio_asset_t::id);
        if (asset != document.audio_assets.end())
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
        return natural_name_less(folded_name(left), folded_name(right));
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
        if (name.size() > 255) name.clear();
        plan.entries.push_back(
            {.asset = {.source_path = path, .role = resource_role_t::sample,
                       .name = std::move(name), .allocate_bms_identifier = allocate_bms_identifiers,
                       .audio_stream = stream},
             .kind = sound_kind_t::keysound, .tags = {}});
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

auto imported_bms_id_ranges(const song_document_t& document, std::size_t count) -> std::size_t {
    if (count == 0 || count > document.resources.size()) return 0;
    constexpr auto base = std::size_t{36};
    const auto digit = [base](unsigned char value) -> int {
        if (value >= 'a' && value <= 'z')
            value = static_cast<unsigned char>(value - ('a' - 'A'));
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'Z') return value - 'A' + 10;
        return -1;
    };
    auto ranges = std::size_t{};
    auto previous = std::optional<std::size_t>{};
    const auto begin = document.resources.size() - count;
    for (std::size_t index = begin; index < document.resources.size(); ++index) {
        const auto& identities = document.resources[index].target_identities;
        const auto identity = std::ranges::find_if(identities, [](const auto& item) {
            return item.family == format_family_t::bms && item.namespace_name == "WAV";
        });
        if (identity == identities.end() || identity->identifier.size() != 2) continue;
        const auto high = digit(static_cast<unsigned char>(identity->identifier[0]));
        const auto low = digit(static_cast<unsigned char>(identity->identifier[1]));
        if (high < 0 || low < 0) continue;
        const auto value = static_cast<std::size_t>(high) * base + static_cast<std::size_t>(low);
        if (!previous || value != *previous + 1) ++ranges;
        previous = value;
    }
    return ranges;
}

} // namespace

auto import_keysound_files(document_history_t& history, workspace_controller_t& workspace,
                           chart_id_t chart, std::span<const std::string> paths) -> std::string {
    const auto target = std::ranges::find(history.document.charts, chart, &chart_document_t::id);
    if (target == history.document.charts.end()) return "Select a difficulty before adding sounds.";
    if (paths.empty()) return "No files were dropped.";
    if (paths.size() > maximum_bulk_sound_import)
        return "Drop at most " + std::to_string(maximum_bulk_sound_import) + " sounds at once.";
    auto plan = plan_sound_import(history.document, target->source_family == format_family_t::bms,
                                  {paths.begin(), paths.end()});
    if (plan.entries.empty())
        return "No sounds were added" + skipped_suffix(plan) + ".";
    const auto count = plan.entries.size();
    const auto split_ids = target->source_family == format_family_t::bms &&
        count <= bms_identifier_capacity(history.document, "WAV").total -
                     bms_identifier_capacity(history.document, "WAV").used &&
        !find_bms_identifier_run(history.document, "WAV", count).has_value();
    const auto applied = apply_bulk_sound_import(history, history.document.revision,
                                                 {.chart = {}, .entries = std::move(plan.entries)});
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    if (!applied.applied) return "Sound import made no document change.";
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Sounds added, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    auto message = "Added " + sound_count_text(count) + skipped_suffix(plan);
    if (split_ids) message += ". Group split across " +
        std::to_string(imported_bms_id_ranges(history.document, count)) +
        " ID ranges; Renumber by name can rejoin it";
    return message + ".";
}

namespace {

auto sound_ids(const WormUiWorkspaceAction& action) -> std::vector<resource_id_t> {
    std::vector<resource_id_t> ids;
    if (!action.compose_sound_resource_ids) return ids;
    ids.reserve(action.compose_sound_resource_count);
    for (std::size_t index = 0; index < action.compose_sound_resource_count; ++index)
        ids.push_back(resource_id_t{action.compose_sound_resource_ids[index]});
    return ids;
}

auto finish_sound_edit(workspace_controller_t& workspace, document_history_t& history,
                       const document_command_result_t& applied, std::string success,
                       const char* unchanged) -> std::string {
    if (!applied.diagnostics.empty()) return applied.diagnostics.front().message;
    if (!applied.applied) return unchanged;
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound)
        return rebound.diagnostics.empty() ? "Sounds updated, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    return success;
}

auto delete_sounds(const WormUiWorkspaceAction& action, document_history_t& history,
                   workspace_controller_t& workspace) -> std::string {
    const auto ids = sound_ids(action);
    const auto applied = apply_resource_deletions(history, action.compose_input_document_revision,
                                                  ids);
    return finish_sound_edit(workspace, history, applied,
                             "Removed " + sound_count_text(ids.size()) + " from the list.",
                             "No sounds were removed.");
}

auto change_sound_layers(const WormUiWorkspaceAction& action, document_history_t& history,
                         workspace_controller_t& workspace, chart_id_t chart) -> std::string {
    const auto ids = sound_ids(action);
    const auto clear = action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS;
    const auto add = action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER;
    const auto revision = action.compose_input_document_revision;
    const auto applied = clear
        ? apply_sound_layer_defaults(history, revision, {.chart = chart, .resources = ids,
                                                         .layers = {}})
        : apply_sound_layer_membership(history, revision,
              {.chart = chart, .resources = ids,
               .layer = layer_id_t{action.compose_semantic_layer_id}, .member = add,
               .overwrite_note_layers = action.compose_sound_overwrite_note_layers});
    const auto subject = sound_count_text(ids.size());
    return finish_sound_edit(workspace, history, applied,
                             clear ? "Cleared the layers of " + subject + "."
                             : add ? "Added the layer to " + subject + "."
                                   : "Removed the layer from " + subject + ".",
                             "The sounds already had that layer state.");
}

auto request_audition(const WormUiWorkspaceAction& action, const document_history_t& history,
                      application_command_state_t& application_state) -> std::string {
    const resource_id_t resource{action.compose_resource_id};
    const auto found = std::ranges::find(history.document.resources, resource,
                                         &resource_entry_t::id);
    if (!resource || found == history.document.resources.end())
        return "That sound is no longer in the list.";
    if (found->missing) return "That sound's file is missing.";
    application_state.pending_sound_audition = resource;
    return "Playing " + (found->name.empty() ? std::string{"sound"} : found->name) + ".";
}

auto import_picked_files(const WormUiWorkspaceAction& action, document_history_t& history,
                         workspace_controller_t& workspace, chart_id_t chart) -> std::string {
    std::vector<std::string> paths;
    for (std::size_t index = 0; index < action.compose_import_path_count; ++index)
        if (action.compose_import_paths && action.compose_import_paths[index])
            paths.emplace_back(action.compose_import_paths[index]);
    return import_keysound_files(history, workspace, chart, paths);
}

} // namespace

auto dispatch_compose_sound_action(const WormUiWorkspaceAction& action,
                                   document_history_t& history, workspace_controller_t& workspace,
                                   chart_id_t chart, application_command_state_t& application_state)
    -> std::optional<std::string> {
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES:
        return import_picked_files(action, history, workspace, chart);
    case WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS: return delete_sounds(action, history, workspace);
    case WORM_UI_COMPOSE_ACTION_AUDITION_SOUND:
        return request_audition(action, history, application_state);
    case WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER:
    case WORM_UI_COMPOSE_ACTION_REMOVE_SOUND_LAYER:
    case WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS:
        return change_sound_layers(action, history, workspace, chart);
    default: return std::nullopt;
    }
}

} // namespace wormedit::sys
