#include <wormedit/core/rational.h>
#include <wormedit/core/chart_import.h>

#include <wormedit/core/chart_editor.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <system_error>
#include <unordered_map>

namespace wormedit {
namespace {

constexpr std::uintmax_t maximum_full_hash_bytes = 64ULL * 1024 * 1024;
constexpr std::size_t maximum_catalog_entries = 100'000;

auto import_error(std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = diagnostic_code_t::invalid_id,
            .message = std::move(message)};
}

auto resolve_path(const std::filesystem::path& base, const std::filesystem::path& path)
    -> std::filesystem::path {
    if (path.empty()) return {};
    return (path.is_absolute() ? path : base / path).lexically_normal();
}

struct file_identity_t {
    std::uintmax_t size{};
    std::uint64_t hash{};
    auto operator==(const file_identity_t&) const -> bool = default;
};

// Size plus an FNV-1a hash of the content; files above the full-hash limit hash their first
// and last mebibyte so a huge audio file is not read in full for a preview.
auto identify_file(const std::filesystem::path& path) -> std::optional<file_identity_t> {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return std::nullopt;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&](std::uintmax_t from, std::uintmax_t count) {
        std::vector<char> buffer(1 << 16);
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(from));
        while (count > 0 && stream) {
            const auto chunk = static_cast<std::streamsize>(std::min<std::uintmax_t>(count, buffer.size()));
            stream.read(buffer.data(), chunk);
            const auto got = stream.gcount();
            for (std::streamsize index = 0; index < got; ++index)
                hash = (hash ^ static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)])) * 1099511628211ULL;
            if (got <= 0) break;
            count -= static_cast<std::uintmax_t>(got);
        }
    };
    if (size <= maximum_full_hash_bytes) {
        mix(0, size);
    } else {
        constexpr std::uintmax_t edge = 1ULL << 20;
        mix(0, edge);
        mix(size - edge, edge);
    }
    return file_identity_t{size, hash};
}

struct located_asset_t {
    bool visual{};
    asset_id_t id;
    std::filesystem::path path;
};

auto source_asset(const song_document_t& song, asset_id_t id) -> std::optional<located_asset_t> {
    const auto audio = std::ranges::find(song.audio_assets, id, &audio_asset_t::id);
    if (audio != song.audio_assets.end()) return located_asset_t{false, id, audio->source_path};
    const auto visual = std::ranges::find(song.visual_assets, id, &visual_asset_t::id);
    if (visual != song.visual_assets.end()) return located_asset_t{true, id, visual->source_path};
    return std::nullopt;
}

// Looks up files of the target project, resolving and hashing each at most once.
class target_files_t {
  public:
    target_files_t(const song_document_t& target, const std::filesystem::path& base)
        : target_(target), base_(base) {}

    // The target asset holding this file, matched by path first and content second.
    auto find(bool visual, const std::filesystem::path& path) -> std::optional<asset_id_t> {
        const auto count = visual ? target_.visual_assets.size() : target_.audio_assets.size();
        for (std::size_t index = 0; index < count; ++index)
            if (resolved(visual, index) == path)
                return visual ? target_.visual_assets[index].id : target_.audio_assets[index].id;
        const auto wanted = identify_file(path);
        if (!wanted) return std::nullopt;
        for (std::size_t index = 0; index < count; ++index)
            if (identity(visual, index) == wanted)
                return visual ? target_.visual_assets[index].id : target_.audio_assets[index].id;
        return std::nullopt;
    }

  private:
    auto resolved(bool visual, std::size_t index) const -> std::filesystem::path {
        return resolve_path(base_, visual ? target_.visual_assets[index].source_path
                                          : target_.audio_assets[index].source_path);
    }
    auto identity(bool visual, std::size_t index) -> std::optional<file_identity_t> {
        const auto key = std::pair{visual, index};
        const auto found = identities_.find(key);
        if (found != identities_.end()) return found->second;
        return identities_.emplace(key, identify_file(resolved(visual, index))).first->second;
    }

    const song_document_t& target_;
    std::filesystem::path base_;
    std::map<std::pair<bool, std::size_t>, std::optional<file_identity_t>> identities_;
};

auto has_role_overlap(const resource_entry_t& left, const resource_entry_t& right) -> bool {
    return std::ranges::any_of(left.roles, [&](auto role) {
        return std::ranges::find(right.roles, role) != right.roles.end();
    });
}

// Resource and asset IDs the selected charts refer to.
struct chart_references_t {
    std::set<std::uint64_t> resources;
    std::set<std::uint64_t> clip_assets;
};

void collect_references(const chart_document_t& chart, chart_references_t& references) {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            if (note.sound) references.resources.insert(note.sound->value);
        for (const auto& event : measure.bgm_events)
            if (event.sound) references.resources.insert(event.sound->value);
    }
    for (const auto& event : chart.bga_events) references.resources.insert(event.resource.value);
    for (const auto& entry : chart.sound_layer_defaults) references.resources.insert(entry.resource.value);
    for (const auto& channel : chart.sound_channels) references.clip_assets.insert(channel.source_clip.source.value);
}

struct import_plan_t {
    chart_import_proposal_t proposal;
    std::vector<const chart_document_t*> charts;
    // Source asset -> existing target asset, for matched files (resources and clips).
    std::map<std::uint64_t, asset_id_t> reused_assets;
    chart_references_t references;
};

auto chart_note_count(const chart_document_t& chart) -> std::size_t {
    std::size_t total{};
    for (const auto& measure : chart.measures) total += measure.notes.size();
    return total;
}

auto describe(const chart_document_t& chart) -> chart_import_difficulty_t {
    return {.chart = chart.id, .name = chart.metadata.name, .family = chart.source_family,
            .measures = chart.measures.size(), .notes = chart_note_count(chart)};
}

auto first_bpm(const chart_document_t& chart) -> std::optional<signed_rational_t> {
    std::optional<signed_rational_t> found;
    std::optional<std::pair<std::size_t, std::uint64_t>> best;
    for (const auto& event : chart.bpms) {
        const auto measure = std::ranges::find(chart.measures, event.position.measure, &measure_chunk_t::id);
        const auto key = std::pair{static_cast<std::size_t>(measure - chart.measures.begin()), event.position.row};
        if (!best || key < *best) { best = key; found = event.beats_per_minute; }
    }
    return found;
}

auto rational_text(const signed_rational_t& value) -> std::string {
    if (value.denominator == 1) return std::to_string(value.numerator);
    const auto approximate = to_double(value);
    auto text = std::to_string(approximate);
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

void compare_timing(const chart_document_t& reference, const chart_document_t& imported,
                    std::vector<chart_import_timing_difference_t>& out) {
    const auto add = [&](std::string message) {
        out.push_back({imported.id, imported.metadata.name + ": " + std::move(message)});
    };
    const auto left = first_bpm(reference);
    const auto right = first_bpm(imported);
    if (left && right && !(*left == *right))
        add("starts at " + rational_text(*right) + " BPM, the song's chart starts at " + rational_text(*left));
    if (!(reference.beat_zero_audio_offset_seconds == imported.beat_zero_audio_offset_seconds))
        add("audio offset is " + rational_text(imported.beat_zero_audio_offset_seconds) +
            " s, the song's chart uses " + rational_text(reference.beat_zero_audio_offset_seconds) + " s");
    if (reference.divisions_per_standard_measure != imported.divisions_per_standard_measure)
        add("uses " + std::to_string(imported.divisions_per_standard_measure) +
            " divisions per measure, the song's chart uses " +
            std::to_string(reference.divisions_per_standard_measure));
}

void plan_resource(const song_document_t& target, const song_document_t& source,
                   const resource_entry_t& entry, target_files_t& files,
                   const chart_import_paths_t& paths, import_plan_t& plan) {
    chart_import_resource_t item{.source = entry.id, .name = entry.name};
    const auto asset = source_asset(source, entry.asset);
    if (asset) item.path = resolve_path(paths.source_base, asset->path);
    std::error_code error;
    // The file on disk decides; an importer flags entries missing before paths are resolved.
    const bool exists = asset && std::filesystem::is_regular_file(item.path, error);
    if (!exists) {
        item.action = chart_import_resource_action_t::missing;
        plan.proposal.resources.push_back(std::move(item));
        return;
    }
    const auto matched = files.find(asset->visual, item.path);
    if (matched) {
        plan.reused_assets[entry.asset.value] = *matched;
        const auto existing = std::ranges::find_if(target.resources, [&](const auto& candidate) {
            return candidate.asset == *matched && !candidate.missing && has_role_overlap(candidate, entry);
        });
        if (existing != target.resources.end()) {
            item.action = chart_import_resource_action_t::reuse;
            item.existing = existing->id;
        }
    }
    plan.proposal.resources.push_back(std::move(item));
}

auto make_plan(const song_document_t& target, const song_document_t& source,
               const chart_import_paths_t& paths, std::span<const chart_id_t> selected) -> import_plan_t {
    import_plan_t plan;
    plan.proposal.difficulties = list_chart_import_difficulties(source);
    for (const auto id : selected) {
        const auto chart = std::ranges::find(source.charts, id, &chart_document_t::id);
        if (chart == source.charts.end()) {
            plan.proposal.diagnostics.push_back(import_error("the chart to import is not in the source file"));
            return plan;
        }
        if (std::ranges::find(plan.charts, &*chart) != plan.charts.end()) continue;
        plan.charts.push_back(&*chart);
        collect_references(*chart, plan.references);
        if (!target.charts.empty()) compare_timing(target.charts.front(), *chart, plan.proposal.timing_differences);
    }
    target_files_t files{target, paths.target_base};
    for (const auto& entry : source.resources)
        if (plan.references.resources.contains(entry.id.value))
            plan_resource(target, source, entry, files, paths, plan);
    for (const auto asset_value : plan.references.clip_assets) {
        const auto asset = source_asset(source, asset_id_t{asset_value});
        if (!asset || plan.reused_assets.contains(asset_value)) continue;
        if (const auto matched = files.find(asset->visual, resolve_path(paths.source_base, asset->path)))
            plan.reused_assets[asset_value] = *matched;
    }
    return plan;
}

// Source chart IDs the plan refers to, mapped onto the project after resources are merged.
struct merge_maps_t {
    std::unordered_map<std::uint64_t, resource_id_t> resources;
    std::unordered_map<std::uint64_t, asset_id_t> assets;
};

auto next_resource_id(const song_document_t& document) -> std::optional<resource_id_t> {
    std::uint64_t largest{};
    for (const auto& resource : document.resources) largest = std::max(largest, resource.id.value);
    if (largest == std::numeric_limits<std::uint64_t>::max() || document.resources.size() >= maximum_catalog_entries)
        return std::nullopt;
    return resource_id_t{largest + 1};
}

// Copies a source asset into the target under a fresh ID and an absolute path.
auto add_asset(song_document_t& target, const located_asset_t& asset, const song_document_t& source,
               const std::filesystem::path& absolute) -> std::optional<asset_id_t> {
    const auto id = next_resource_asset_id(target);
    if (!id) return std::nullopt;
    if (asset.visual) {
        auto copy = *std::ranges::find(source.visual_assets, asset.id, &visual_asset_t::id);
        copy.id = *id;
        copy.source_path = absolute;
        target.visual_assets.push_back(std::move(copy));
    } else {
        auto copy = *std::ranges::find(source.audio_assets, asset.id, &audio_asset_t::id);
        copy.id = *id;
        copy.source_path = absolute;
        copy.waveform_cache_path.reset();
        target.audio_assets.push_back(std::move(copy));
    }
    return id;
}

auto merge_asset(song_document_t& target, const song_document_t& source, const import_plan_t& plan,
                 const chart_import_paths_t& paths, asset_id_t source_id, merge_maps_t& maps)
    -> std::optional<asset_id_t> {
    if (const auto done = maps.assets.find(source_id.value); done != maps.assets.end()) return done->second;
    const auto asset = source_asset(source, source_id);
    if (!asset) return std::nullopt;
    const auto reused = plan.reused_assets.find(source_id.value);
    const auto id = reused != plan.reused_assets.end()
        ? std::optional{reused->second}
        : add_asset(target, *asset, source, resolve_path(paths.source_base, asset->path));
    if (id) maps.assets[source_id.value] = *id;
    return id;
}

auto merge_resources(song_document_t& target, const song_document_t& source, const import_plan_t& plan,
                     const chart_import_paths_t& paths, merge_maps_t& maps) -> bool {
    for (const auto& item : plan.proposal.resources) {
        if (item.existing) {
            maps.resources[item.source.value] = *item.existing;
            continue;
        }
        const auto entry = std::ranges::find(source.resources, item.source, &resource_entry_t::id);
        const auto asset = merge_asset(target, source, plan, paths, entry->asset, maps);
        const auto id = next_resource_id(target);
        if (!asset || !id) return false;
        auto copy = *entry;
        copy.id = *id;
        copy.asset = *asset;
        copy.target_identities.clear();
        copy.sound_group.reset();
        copy.missing = item.action == chart_import_resource_action_t::missing;
        target.resources.push_back(std::move(copy));
        maps.resources[item.source.value] = *id;
    }
    for (const auto asset_value : plan.references.clip_assets)
        if (!merge_asset(target, source, plan, paths, asset_id_t{asset_value}, maps)) return false;
    return true;
}

void remap_references(chart_document_t& chart, const merge_maps_t& maps) {
    const auto resource = [&](std::optional<resource_id_t>& id) {
        if (id) id = maps.resources.at(id->value);
    };
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) resource(note.sound);
        for (auto& event : measure.bgm_events) resource(event.sound);
    }
    for (auto& event : chart.bga_events) event.resource = maps.resources.at(event.resource.value);
    for (auto& entry : chart.sound_layer_defaults) entry.resource = maps.resources.at(entry.resource.value);
    for (auto& channel : chart.sound_channels)
        channel.source_clip.source = maps.assets.at(channel.source_clip.source.value);
}

auto highest_chart_id(const song_document_t& document) -> std::uint64_t {
    std::uint64_t largest{};
    for (const auto& chart : document.charts) largest = std::max(largest, chart.id.value);
    return largest;
}

// Fresh identities come from the duplicate-chart command, which renumbers every ID of a chart
// above everything already in the document (and the history).
auto import_one_chart(document_history_t& scratch, const chart_document_t& source_chart,
                      const merge_maps_t& maps) -> std::optional<chart_document_t> {
    auto staged = source_chart;
    remap_references(staged, maps);
    const auto name = staged.metadata.name;
    staged.id = chart_id_t{highest_chart_id(scratch.document) + 1};
    const auto staged_id = staged.id;
    scratch.document.charts.push_back(std::move(staged));
    const auto duplicated = apply_duplicate_chart_command(scratch, scratch.document.revision, staged_id);
    if (!duplicated || !duplicated.chart) return std::nullopt;
    auto chart = *std::ranges::find(scratch.document.charts, *duplicated.chart, &chart_document_t::id);
    chart.metadata.name = name;
    return chart;
}

} // namespace

auto chart_import_proposal_t::count(chart_import_resource_action_t action) const -> std::size_t {
    return static_cast<std::size_t>(std::ranges::count(resources, action, &chart_import_resource_t::action));
}

auto list_chart_import_difficulties(const song_document_t& source) -> std::vector<chart_import_difficulty_t> {
    std::vector<chart_import_difficulty_t> result;
    for (const auto& chart : source.charts) result.push_back(describe(chart));
    return result;
}

auto plan_chart_import(const song_document_t& target, const song_document_t& source,
                       const chart_import_paths_t& paths, std::span<const chart_id_t> selected)
    -> chart_import_proposal_t {
    return make_plan(target, source, paths, selected).proposal;
}

auto apply_chart_import(document_history_t& history, std::uint64_t expected_revision,
                        const song_document_t& source, const chart_import_paths_t& paths,
                        std::span<const chart_id_t> selected) -> chart_import_result_t {
    chart_import_result_t result{.command = {.revision = history.document.revision, .diagnostics = {}}, .charts = {}};
    const auto reject = [&](std::string message) {
        result.command.diagnostics.push_back(import_error(std::move(message)));
        return result;
    };
    if (expected_revision != history.document.revision) return reject("chart import document revision is stale");
    if (selected.empty()) return reject("choose at least one difficulty to import");
    const auto plan = make_plan(history.document, source, paths, selected);
    if (!plan.proposal.diagnostics.empty()) {
        result.command.diagnostics = plan.proposal.diagnostics;
        return result;
    }
    auto proposed = history.document;
    merge_maps_t maps;
    if (!merge_resources(proposed, source, plan, paths, maps)) return reject("the resource catalog is full");
    auto scratch = make_document_history(proposed, 1, false);
    std::vector<chart_document_t> imported;
    for (const auto* chart : plan.charts) {
        auto added = import_one_chart(scratch, *chart, maps);
        if (!added) return reject("a chart could not be given fresh identities");
        imported.push_back(std::move(*added));
    }
    for (auto& chart : imported) {
        result.charts.push_back(chart.id);
        proposed.charts.push_back(std::move(chart));
    }
    result.command = commit_document_command(history, std::move(proposed),
                                             selected.size() == 1 ? "Import chart" : "Import charts");
    if (!result.command.applied) result.charts.clear();
    return result;
}

} // namespace wormedit
