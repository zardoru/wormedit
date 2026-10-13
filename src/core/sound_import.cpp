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


struct object_id_scan_t {
    std::uint64_t largest{};
    std::size_t count{};
};

auto scan_chart_objects(const chart_document_t& chart) -> object_id_scan_t {
    object_id_scan_t scan;
    const auto visit = [&](const auto& values) {
        for (const auto& value : values) {
            scan.largest = std::max(scan.largest, value.id.value);
            ++scan.count;
        }
    };
    visit(chart.bgm_source_lanes); visit(chart.lane_layouts); visit(chart.bpms);
    visit(chart.pauses); visit(chart.warps); visit(chart.scrolls); visit(chart.speeds);
    visit(chart.kiai); visit(chart.timing_anchors); visit(chart.bookmarks);
    visit(chart.sections); visit(chart.bga_events); visit(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) visit(channel.events);
    for (const auto& measure : chart.measures) {
        visit(measure.notes);
        visit(measure.bgm_events);
    }
    return scan;
}

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

auto used_bms_wav_ids(const song_document_t& document) -> std::vector<bool> {
    constexpr auto base = std::size_t{36};
    auto used = std::vector<bool>(base * base, false);
    for (const auto& resource : document.resources)
        for (const auto& identity : resource.target_identities) {
            if (identity.family != format_family_t::bms || identity.namespace_name != "WAV" ||
                identity.identifier.size() != 2) continue;
            const auto high = bms36_value(static_cast<unsigned char>(identity.identifier[0]));
            const auto low = bms36_value(static_cast<unsigned char>(identity.identifier[1]));
            if (high >= 0 && low >= 0)
                used[static_cast<std::size_t>(high) * base + static_cast<std::size_t>(low)] = true;
        }
    return used;
}

auto requested_bms_indices(const bulk_sound_import_action_t& action) -> std::vector<std::size_t> {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < action.entries.size(); ++index)
        if (action.entries[index].asset.allocate_bms_identifier) result.push_back(index);
    return result;
}

auto free_bms_gaps(const song_document_t& document) -> std::vector<bms_id_gap_t> {
    const auto used = used_bms_wav_ids(document);
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
    auto used = used_bms_wav_ids(document);
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
                 object_id_scan_t& ids, document_command_result_t& result) -> bool {
    if (ids.count > default_maximum_chart_objects - 2 ||
        ids.largest > std::numeric_limits<std::uint64_t>::max() - 2) {
        reject(result, diagnostic_code_t::invalid_id, "chart cannot hold another stem (object IDs exhausted)");
        return false;
    }
    resource.target_identities.push_back(
        {format_family_t::bmson, std::to_string(source_index), "sound_channel"});
    const object_id_t channel_id{++ids.largest};
    const object_id_t lane_id{++ids.largest};
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

auto import_entry(document_history_t& draft, const bulk_sound_import_entry_t& entry,
                  chart_id_t stem_chart, object_id_scan_t& ids,
                  const std::optional<std::string>& bms_identifier,
                  document_command_result_t& result) -> bool {
    auto asset = entry.asset;
    asset.role = resource_role_t::sample;
    const auto imported = apply_resource_asset_import(draft, draft.document.revision, asset);
    if (!imported.applied) {
        result.diagnostics.insert(result.diagnostics.end(), imported.diagnostics.begin(),
                                  imported.diagnostics.end());
        return false;
    }
    const auto resource_id = draft.document.resources.back().id;
    if (bms_identifier) {
        auto& imported_resource = draft.document.resources.back();
        std::erase_if(imported_resource.target_identities, [](const auto& identity) {
            return identity.family == format_family_t::bms && identity.namespace_name == "WAV";
        });
        imported_resource.target_identities.push_back(
            {format_family_t::bms, *bms_identifier, "WAV"});
    }
    const auto metadata = apply_resource_sound_metadata(
        draft, draft.document.revision,
        {.resource = resource_id, .kind = entry.kind, .tags = entry.tags});
    if (!metadata.diagnostics.empty()) {
        result.diagnostics.insert(result.diagnostics.end(), metadata.diagnostics.begin(),
                                  metadata.diagnostics.end());
        return false;
    }
    if (entry.kind != sound_kind_t::stem) return true;
    auto& resource = draft.document.resources.back();
    if (std::ranges::find(resource.roles, resource_role_t::bgm) == resource.roles.end())
        resource.roles.push_back(resource_role_t::bgm);
    if (!stem_chart) return true;
    // Each command replaces the draft document, so resolve the chart afresh.
    const auto chart = std::ranges::find(draft.document.charts, stem_chart, &chart_document_t::id);
    return chart != draft.document.charts.end() &&
           append_stem(*chart, resource, next_channel_source_index(draft.document, *chart), ids,
                       result);
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
    if (name.empty() || !text_util::valid_bounded_text(name, maximum_sound_group_name_bytes)) {
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
    const auto folded = text_util::ascii_fold(std::string(name));
    for (const auto& prior : prior_names) {
        if (text_util::ascii_fold(std::string(prior)) == folded) {
            reject(result, diagnostic_code_t::invalid_id, "sound group name is already in use");
            return false;
        }
    }
    for (const auto& current : song.sound_groups) {
        if (group.existing_group == current.id) continue;
        if (text_util::ascii_fold(std::string(current.name)) == folded) {
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
    auto next_id = std::uint64_t{1};
    for (const auto& group : song.sound_groups) {
        if (group.id.value == std::numeric_limits<std::uint64_t>::max()) {
            reject(result, diagnostic_code_t::invalid_id, "sound group IDs are exhausted");
            return std::nullopt;
        }
        next_id = std::max(next_id, group.id.value + 1);
    }
    if (new_group_count != 0 && new_group_count - 1 >
            std::numeric_limits<std::uint64_t>::max() - next_id) {
        reject(result, diagnostic_code_t::invalid_id, "sound group IDs are exhausted");
        return std::nullopt;
    }
    return next_id;
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

auto apply_bulk_sound_import(document_history_t& history, std::uint64_t expected_revision,
                             const bulk_sound_import_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "sound import document revision is stale");
        return result;
    }
    if (!validate_batch(action, result)) return result;
    const auto identifiers = assigned_bms_ids(history.document, action, result);
    if (!identifiers) return result;
    auto draft = make_document_history(history.document, 1, false);
    const auto first_imported_resource = draft.document.resources.size();
    chart_id_t stem_chart{};
    object_id_scan_t ids;
    if (action.chart) {
        const auto found = std::ranges::find(draft.document.charts, action.chart,
                                             &chart_document_t::id);
        if (found == draft.document.charts.end()) {
            reject(result, diagnostic_code_t::invalid_id, "sound import chart is unknown");
            return result;
        }
        if (found->source_family == format_family_t::bmson) {
            stem_chart = found->id;
            ids = scan_chart_objects(*found);
        }
    }
    for (std::size_t index = 0; index < action.entries.size(); ++index) {
        if (!import_entry(draft, action.entries[index], stem_chart, ids, (*identifiers)[index], result)) return result;
    }
    if (!apply_import_groups(draft.document, action, first_imported_resource, result)) return result;
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(draft.document));
    return transaction.commit(action.groups.empty() ? "Import sounds" : "Import sounds and groups");
}

} // namespace wormedit
