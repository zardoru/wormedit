#include "chart_object_ids.h"
#include "resource_assignment_internal.h"
#include <wormedit/core/sound_groups.h>
#include <wormedit/core/bms36.h>
#include "text_util.h"
#include <wormedit/core/sound_import.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace wormedit {
namespace {


auto validate_batch(const bulk_sound_import_action_t& action, document_command_result_t& result)
    -> bool {
    if (action.entries.empty() || action.entries.size() > maximum_bulk_sound_import) {
        reject(result, diagnostic_code_t::invalid_id, "sound import needs between 1 and 1295 files");
        return false;
    }
    std::unordered_set<std::string> paths;
    for (const auto& entry : action.entries) {
        if (!paths.insert(entry.asset.source_path.lexically_normal().generic_string()).second) {
            reject(result, diagnostic_code_t::invalid_id, "sound import lists the same source file twice");
            return false;
        }
    }
    return true;
}

struct bms_id_gap_t { std::size_t begin; std::size_t length; };

auto requested_bms_indices(const bulk_sound_import_action_t& action) -> std::vector<std::size_t> {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < action.entries.size(); ++index)
        if (action.entries[index].asset.allocate_bms_identifier) result.push_back(index);
    return result;
}

auto free_bms_gaps(const song_document_t& document) -> std::vector<bms_id_gap_t> {
    const auto used = bms_occupied(document, "WAV", 36);
    auto gaps = std::vector<bms_id_gap_t>{};
    for (std::size_t value = 1; value < used.size();) {
        if (used[value]) { ++value; continue; }
        const auto begin = value;
        while (value < used.size() && !used[value]) ++value;
        gaps.push_back({begin, value - begin});
    }
    std::ranges::sort(gaps, [](const auto& left, const auto& right) {
        return left.length != right.length ? left.length > right.length : left.begin < right.begin;
    });
    return gaps;
}

auto free_bms_gaps(const std::vector<bool>& used) -> std::vector<bms_id_gap_t> {
    auto gaps = std::vector<bms_id_gap_t>{};
    for (std::size_t value = 1; value < used.size();) {
        if (used[value]) { ++value; continue; }
        const auto begin = value;
        while (value < used.size() && !used[value]) ++value;
        gaps.push_back({begin, value - begin});
    }
    std::ranges::sort(gaps, [](const auto& left, const auto& right) {
        return left.length != right.length ? left.length > right.length : left.begin < right.begin;
    });
    return gaps;
}

auto split_bms_ids(const std::vector<bms_id_gap_t>& gaps, std::size_t count)
    -> std::vector<std::string> {
    constexpr auto base = std::size_t{36};
    constexpr auto alphabet = bms36_digits;
    auto ids = std::vector<std::string>{};
    auto remaining = count;
    for (const auto& gap : gaps) {
        const auto take = std::min(remaining, gap.length);
        for (std::size_t offset = 0; offset < take; ++offset) {
            const auto value = gap.begin + offset;
            ids.emplace_back(std::string{alphabet[value / base], alphabet[value % base]});
        }
        remaining -= take;
        if (remaining == 0) break;
    }
    return ids;
}

auto assign_contiguous_group_ids(std::vector<bool>& used, const bulk_sound_import_action_t& action,
                                 const bulk_sound_import_group_t& group,
                                 std::vector<std::optional<std::string>>& ids,
                                 document_command_result_t& result) -> bool {
    constexpr auto base = std::size_t{36};
    constexpr auto alphabet = bms36_digits;
    const auto count = static_cast<std::size_t>(std::ranges::count_if(
        group.entry_indexes, [&](const auto index) {
            return action.entries[index].asset.allocate_bms_identifier;
        }));
    if (count == 0) return true;
    const auto gaps = free_bms_gaps(used);
    auto best = gaps.end();
    for (auto gap = gaps.begin(); gap != gaps.end(); ++gap) {
        if (gap->length < count) continue;
        if (best == gaps.end() || gap->begin < best->begin) best = gap;
    }
    if (best == gaps.end()) {
        reject(result, diagnostic_code_t::invalid_id, "not enough contiguous BMS WAV IDs for a sound group");
        return false;
    }
    const auto begin = best->begin;
    auto offset = std::size_t{};
    for (const auto index : group.entry_indexes) {
        if (!action.entries[index].asset.allocate_bms_identifier) continue;
        const auto value = begin + offset++;
        ids[index] = std::string{alphabet[value / base], alphabet[value % base]};
        used[value] = true;
    }
    return true;
}

struct group_run_request_t {
    std::size_t group_index{};
    std::size_t count{};
};

auto assign_grouped_bms_ids(const song_document_t& document,
                            const bulk_sound_import_action_t& action,
                            const std::vector<std::size_t>& allocated,
                            const std::vector<bool>& grouped,
                            std::vector<std::optional<std::string>>& ids,
                            document_command_result_t& result) -> bool {
    auto used = bms_occupied(document, "WAV", 36);
    auto requests = std::vector<group_run_request_t>{};
    for (std::size_t index = 0; index < action.groups.size(); ++index) {
        const auto count = static_cast<std::size_t>(std::ranges::count_if(
            action.groups[index].entry_indexes, [&](const auto entry_index) {
                return action.entries[entry_index].asset.allocate_bms_identifier;
            }));
        if (count != 0) requests.push_back({index, count});
    }
    std::ranges::sort(requests, [](const auto& left, const auto& right) {
        return left.count != right.count ? left.count > right.count
                                         : left.group_index < right.group_index;
    });
    for (const auto& request : requests)
        if (!assign_contiguous_group_ids(used, action, action.groups[request.group_index], ids,
                                         result)) return false;
    auto ungrouped = std::vector<std::size_t>{};
    for (const auto index : allocated)
        if (!grouped[index]) ungrouped.push_back(index);
    const auto remaining = split_bms_ids(free_bms_gaps(used), ungrouped.size());
    for (std::size_t index = 0; index < ungrouped.size(); ++index)
        ids[ungrouped[index]] = remaining[index];
    return true;
}

auto group_entry_membership(const bulk_sound_import_action_t& action,
                            document_command_result_t& result) -> std::optional<std::vector<bool>> {
    auto grouped = std::vector<bool>(action.entries.size(), false);
    for (const auto& group : action.groups) {
        if (group.entry_indexes.empty()) {
            reject(result, diagnostic_code_t::invalid_id, "sound import group has no members");
            return std::nullopt;
        }
        for (const auto index : group.entry_indexes) {
            if (index >= action.entries.size() || grouped[index]) {
                reject(result, diagnostic_code_t::invalid_id, "sound group import has an invalid or repeated member");
                return std::nullopt;
            }
            grouped[index] = true;
        }
    }
    return grouped;
}

auto assigned_bms_ids(const song_document_t& document, const bulk_sound_import_action_t& action,
                      document_command_result_t& result)
    -> std::optional<std::vector<std::optional<std::string>>> {
    auto ids = std::vector<std::optional<std::string>>(action.entries.size());
    const auto allocated = requested_bms_indices(action);
    const auto grouped_entries = group_entry_membership(action, result);
    if (!grouped_entries) return std::nullopt;
    if (allocated.empty()) return ids;
    const auto capacity = bms_identifier_capacity(document, "WAV");
    if (allocated.size() > capacity.total - capacity.used) {
        reject(result, diagnostic_code_t::invalid_id, "BMS WAV namespace has " + std::to_string(capacity.total - capacity.used) +
                           " IDs left; import needs " + std::to_string(allocated.size()));
        return std::nullopt;
    }
    if (!action.groups.empty()) {
        if (!assign_grouped_bms_ids(document, action, allocated, *grouped_entries,
                                    ids, result)) return std::nullopt;
        return ids;
    }
    if (const auto run = find_bms_identifier_run(document, "WAV", allocated.size())) {
        for (std::size_t offset = 0; offset < allocated.size(); ++offset)
            ids[allocated[offset]] = (*run)[offset];
        return ids;
    }

    const auto ids_in_order = split_bms_ids(free_bms_gaps(document), allocated.size());
    for (std::size_t offset = 0; offset < allocated.size(); ++offset)
        ids[allocated[offset]] = ids_in_order[offset];
    return ids;
}

// The BMSON writer pairs a retained channel with its resource through the
// "sound_channel" identity and the "$.sound_channels[N]" event provenance, so a
// new stem takes the next unused N like an imported one.
auto next_channel_source_index(const song_document_t& document, const chart_document_t& chart)
    -> std::size_t {
    std::size_t next = chart.sound_channels.size();
    for (const auto& resource : document.resources) {
        for (const auto& identity : resource.target_identities) {
            if (identity.family != format_family_t::bmson || identity.namespace_name != "sound_channel")
                continue;
            std::size_t value{};
            const auto* end = identity.identifier.data() + identity.identifier.size();
            const auto parsed = std::from_chars(identity.identifier.data(), end, value);
            if (parsed.ec == std::errc{} && parsed.ptr == end) next = std::max(next, value + 1);
        }
    }
    return next;
}

// Adds an empty BMSON channel and its explicitly linked presentation lane.
auto append_stem(chart_document_t& chart, resource_entry_t& resource, std::size_t source_index,
                 id_ceiling_t<object_id_t>& ids, document_command_result_t& result) -> bool {
    if (ids.count > default_maximum_chart_objects - 2 ||
        ids.maximum > std::numeric_limits<std::uint64_t>::max() - 2) {
        reject(result, diagnostic_code_t::invalid_id, "chart cannot hold another stem (object IDs exhausted)");
        return false;
    }
    resource.target_identities.push_back(
        {format_family_t::bmson, std::to_string(source_index), "sound_channel"});
    const object_id_t channel_id{++ids.maximum};
    const object_id_t lane_id{++ids.maximum};
    ids.count += 2;
    std::size_t order = 0;
    for (const auto& lane : chart.bgm_source_lanes) order = std::max(order, lane.source_order + 1);
    chart.bgm_source_lanes.push_back({.id = lane_id, .name = resource.name,
                                      .source_order = order, .stem_channel = channel_id});
    chart.sound_channels.push_back(
        {.id = channel_id,
         .name = resource.name,
         .source_clip = {.id = clip_id_t{channel_id.value}, .source = resource.asset},
         .events = {}});
    return true;
}

auto stage_imported_resource_details(document_command_transaction_t& transaction,
    const bulk_sound_import_entry_t& entry, chart_id_t stem_chart, id_ceiling_t<object_id_t>& ids,
    const std::optional<std::string>& bms_identifier, document_command_result_t& result)
    -> attempt_t<document_edit_effect_t> {
    return transaction.stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        auto& resource = document.resources.back();
        if (bms_identifier) {
            std::erase_if(resource.target_identities, [](const auto& identity) {
                return identity.family == format_family_t::bms && identity.namespace_name == "WAV";
            });
            resource.target_identities.push_back({format_family_t::bms, *bms_identifier, "WAV"});
        }
        if (entry.kind == sound_kind_t::stem) {
            if (std::ranges::find(resource.roles, resource_role_t::bgm) == resource.roles.end()) resource.roles.push_back(resource_role_t::bgm);
            const auto* chart = document.find_chart(stem_chart);
            if (stem_chart && (!chart || !append_stem(*document.find_chart(stem_chart), resource,
                next_channel_source_index(document, *chart), ids, result))) return {.diagnostics = result.diagnostics};
        }
        return {.value = document_edit_effect_t{.changed = true}};
    });
}

auto import_entry(document_command_transaction_t& transaction,
    const bulk_sound_import_entry_t& entry, chart_id_t stem_chart,
    id_ceiling_t<object_id_t>& ids, const std::optional<std::string>& bms_identifier,
    std::optional<bms_program_target_key_t> selection, document_command_result_t& result) -> bool {
    auto asset = entry.asset;
    asset.role = resource_role_t::sample;
    asset.allocate_bms_identifier = false;
    const auto imported = stage_resource_asset_import(transaction, asset, selection);
    if (!imported.value || !imported.ok()) { result.diagnostics = imported.diagnostics; return false; }
    const auto resource_id = transaction.current_document().resources.back().id;
    const auto metadata = stage_resource_sound_metadata(transaction,
        {.resource = resource_id, .kind = entry.kind, .tags = entry.tags});
    if (!metadata.value || !metadata.ok()) { result.diagnostics = metadata.diagnostics; return false; }
    if (bms_identifier && selection) {
        const auto target = transaction.bms_target(selection->difficulty);
        if (!target) { result.diagnostics.push_back(error(diagnostic_code_t::invalid_id, target.error().message)); return false; }
        const auto declared = stage_bms_resource_declaration(transaction, *target,
            {.identifier = *bms_identifier, .resource = resource_id});
        if (!declared.value || !declared.ok()) { result.diagnostics = declared.diagnostics; return false; }
    }
    const auto staged = stage_imported_resource_details(transaction, entry, stem_chart, ids,
        selection ? std::nullopt : bms_identifier, result);
    if (!staged.value || !staged.ok()) { result.diagnostics = staged.diagnostics; return false; }
    return true;
}

auto trim_group_name(std::string text) -> std::string {
    return std::string(text_util::trim(text, text_util::ascii_whitespace));
}

auto validate_import_group_name(const song_document_t& song,
                                const bulk_sound_import_group_t& group,
                                const std::vector<std::string>& prior_names,
                                std::unordered_set<std::uint64_t>& existing_ids,
                                std::string& name, document_command_result_t& result) -> bool {
    name = trim_group_name(group.name);
    if (name.empty() || !text_util::is_valid_bounded_text(name, maximum_sound_group_name_bytes)) {
        reject(result, diagnostic_code_t::invalid_id, "sound group name must contain 1 to 64 bytes");
        return false;
    }
    if (group.existing_group && !existing_ids.insert(group.existing_group->value).second) {
        reject(result, diagnostic_code_t::invalid_id, "sound import refers to the same existing group more than once");
        return false;
    }
    if (group.existing_group && std::ranges::find(song.sound_groups, *group.existing_group,
                                                   &sound_group_t::id) == song.sound_groups.end()) {
        reject(result, diagnostic_code_t::invalid_id, "sound import refers to an unknown existing group");
        return false;
    }
    for (const auto& prior : prior_names) {
        if (text_util::ascii_iequal(prior, name)) {
            reject(result, diagnostic_code_t::invalid_id, "sound group name is already in use");
            return false;
        }
    }
    for (const auto& current : song.sound_groups) {
        if (group.existing_group == current.id) continue;
        if (text_util::ascii_iequal(current.name, name)) {
            reject(result, diagnostic_code_t::invalid_id, "sound group name is already in use");
            return false;
        }
    }
    return true;
}

auto validated_import_group_names(const song_document_t& song,
                                  const bulk_sound_import_action_t& action,
                                  document_command_result_t& result)
    -> std::optional<std::vector<std::string>> {
    auto names = std::vector<std::string>{};
    names.reserve(action.groups.size());
    auto existing_ids = std::unordered_set<std::uint64_t>{};
    for (const auto& group : action.groups) {
        auto name = std::string{};
        if (!validate_import_group_name(song, group, names, existing_ids, name, result))
            return std::nullopt;
        names.push_back(std::move(name));
    }
    return names;
}

auto next_sound_group_id(const song_document_t& song,
                         const bulk_sound_import_action_t& action,
                         document_command_result_t& result) -> std::optional<std::uint64_t> {
    const auto new_group_count = static_cast<std::uint64_t>(std::ranges::count_if(
        action.groups, [](const auto& group) { return !group.existing_group; }));
    if (new_group_count == 0) return std::uint64_t{1};
    const auto next = id_ceiling_t<sound_group_id_t>::from(song).next(
        static_cast<std::size_t>(new_group_count));
    if (!next) {
        reject(result, diagnostic_code_t::invalid_id, "sound group IDs are exhausted");
        return std::nullopt;
    }
    return next->value;
}

void apply_import_group_rows(song_document_t& song, const bulk_sound_import_action_t& action,
                             const std::vector<std::string>& names,
                             std::size_t first_imported_resource, std::uint64_t next_id) {
    for (std::size_t group_index = 0; group_index < action.groups.size(); ++group_index) {
        const auto& group = action.groups[group_index];
        const auto id = group.existing_group ? *group.existing_group : sound_group_id_t{next_id++};
        const auto existing = std::ranges::find(song.sound_groups, id, &sound_group_t::id);
        if (existing == song.sound_groups.end())
            song.sound_groups.push_back({.id = id, .name = names[group_index]});
        else
            existing->name = names[group_index];
        for (const auto entry_index : group.entry_indexes)
            song.resources[first_imported_resource + entry_index].sound_group = id;
    }
}

auto apply_import_groups(song_document_t& song, const bulk_sound_import_action_t& action,
                         std::size_t first_imported_resource,
                         document_command_result_t& result) -> bool {
    if (action.groups.empty()) return true;
    const auto names = validated_import_group_names(song, action, result);
    if (!names) return false;
    const auto next_id = next_sound_group_id(song, action, result);
    if (!next_id) return false;
    apply_import_group_rows(song, action, *names, first_imported_resource, *next_id);
    return true;
}

} // namespace

auto apply_bulk_sound_import(document_history_t& history,
                             const bulk_sound_import_action_t& action)
    -> document_command_result_t {
    document_command_transaction_t transaction(history);
    auto staged = transaction.stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        document_command_result_t result{.revision = document.revision};
        if (!validate_batch(action, result)) return {.diagnostics = std::move(result.diagnostics)};
        const auto identifiers = assigned_bms_ids(document, action, result);
        if (!identifiers) return {.diagnostics = std::move(result.diagnostics)};
        if (transaction.has_bms_program(action.chart) && !history.bms_edit_target &&
            std::ranges::any_of(*identifiers, [](const auto& value) { return value.has_value(); }))
            return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                "Select a scoped Chart before importing WAV declarations.")}};
        const auto first_resource = document.resources.size();
        chart_id_t stem_chart{};
        id_ceiling_t<object_id_t> ids;
        if (action.chart) {
            const auto* chart = document.read_chart(action.chart);
            if (!chart) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Sound import chart is unknown.")}};
            if (chart->source_family == format_family_t::bmson) { stem_chart = chart->id; ids = id_ceiling_t<object_id_t>::from(*chart); }
        }
        for (std::size_t index = 0; index < action.entries.size(); ++index)
            if (!import_entry(transaction, action.entries[index], stem_chart, ids, (*identifiers)[index],
                history.bms_edit_target, result)) return {.diagnostics = std::move(result.diagnostics)};
        if (!apply_import_groups(document, action, first_resource, result)) return {.diagnostics = std::move(result.diagnostics)};
        return {.value = document_edit_effect_t{.changed = true,
            .command_label = action.groups.empty() ? "Import sounds" : "Import sounds and groups"}};
    });
    return transaction.finish_execution(std::move(staged));
}

} // namespace wormedit
