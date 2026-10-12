#include <wormedit/formats/conversion.h>
#include <wormedit/formats/slice_spans.h>

#include <wormedit/core/quantize.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/timing.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <tuple>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

struct conversion_context_t {
    const conversion_request_t& request;
    const conversion_limits_t& limits;
    conversion_result_t& result;
};
auto byte_span(std::string_view bytes) -> std::span<const std::byte> {
    return {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
}

void add_diagnostic(conversion_result_t& result, diagnostic_severity_t severity,
                    conversion_diagnostic_code_t code, conversion_stage_t stage,
                    format_family_t target, std::optional<chart_id_t> chart,
                    std::string message, std::string path = {}, bool loss = false) {
    result.diagnostics.push_back({severity, code, stage, target, chart, loss, std::move(message),
                                  std::move(path)});
}

auto result_has_errors(const conversion_result_t& result) -> bool {
    return std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto selected_chart_ids(conversion_context_t& context, const song_document_t& song)
    -> std::vector<chart_id_t> {
    if (context.request.chart && !context.request.charts.empty()) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request, conversion_stage_t::request,
                       context.request.target, context.request.chart,
                       "Select either one chart or an ordered chart set");
        return {};
    }
    if (!context.request.charts.empty()) {
        std::unordered_set<std::uint64_t> seen;
        for (const auto id : context.request.charts) {
            const auto count = std::ranges::count(song.charts, id, &chart_document_t::id);
            if (!id || !seen.insert(id.value).second || count != 1) {
                add_diagnostic(context.result, diagnostic_severity_t::error,
                               conversion_diagnostic_code_t::invalid_request,
                               conversion_stage_t::request, context.request.target, id,
                               "Chart set contains a duplicate, missing, or ambiguous identity");
                return {};
            }
        }
        return context.request.charts;
    }
    if (context.request.chart) {
        const auto count =
            std::ranges::count(song.charts, *context.request.chart, &chart_document_t::id);
        if (count != 1) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::chart_not_found,
                           conversion_stage_t::request, context.request.target,
                           context.request.chart,
                           count == 0 ? "Conversion chart was not found"
                                      : "Conversion chart identity is ambiguous");
            return {};
        }
        return {*context.request.chart};
    }
    std::vector<chart_id_t> ids;
    ids.reserve(song.charts.size());
    for (const auto& chart : song.charts)
        ids.push_back(chart.id);
    if (ids.empty())
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::chart_not_found, conversion_stage_t::request,
                       context.request.target, std::nullopt,
                       "Conversion requires at least one chart");
    return ids;
}

auto slug(std::string_view source, std::string_view fallback, std::size_t capacity) -> std::string {
    std::string result;
    result.reserve(std::min(source.size(), capacity));
    bool separator{};
    for (const auto byte : source) {
        const auto value = static_cast<unsigned char>(byte);
        if (std::isalnum(value) != 0) {
            if (separator && !result.empty() && result.size() < capacity)
                result.push_back('-');
            separator = false;
            if (result.size() < capacity)
                result.push_back(static_cast<char>(std::tolower(value)));
        } else {
            separator = true;
        }
        if (result.size() >= capacity)
            break;
    }
    while (!result.empty() && result.back() == '-')
        result.pop_back();
    if (result.empty())
        result.assign(fallback.substr(0, capacity));
    return result;
}

auto filename_for(const song_document_t& song, const chart_document_t* chart,
                  std::string_view extension, const conversion_limits_t& limits)
    -> std::optional<std::string> {
    const auto identity = chart ? chart->id.value : song.id.value;
    const auto suffix = std::string{"-"} + std::to_string(identity) + std::string(extension);
    if (suffix.size() >= limits.maximum_filename_bytes)
        return std::nullopt;
    const auto& label = chart ? chart->metadata.name : song.metadata.title;
    const auto fallback = chart ? "chart" : "project";
    return slug(label, fallback, limits.maximum_filename_bytes - suffix.size()) + suffix;
}

auto bms_extension(std::string_view profile) -> std::string_view {
    if (profile.find("pms") != std::string_view::npos)
        return ".pms";
    if (profile.find("bme") != std::string_view::npos)
        return ".bme";
    if (profile.find("bml") != std::string_view::npos)
        return ".bml";
    return ".bms";
}

template <typename Diagnostic>
void append_target_diagnostics(conversion_result_t& result,
                               const std::vector<Diagnostic>& diagnostics,
                               format_family_t target, std::optional<chart_id_t> chart,
                               loss_policy_t loss_policy) {
    for (const auto& diagnostic : diagnostics) {
        const bool loss = diagnostic.severity == diagnostic_severity_t::warning;
        auto severity = diagnostic.severity;
        if (loss && loss_policy == loss_policy_t::allow)
            severity = diagnostic_severity_t::info;
        std::string path;
        if constexpr (requires { diagnostic.path; })
            path = diagnostic.path;
        else if constexpr (requires { diagnostic.field; })
            path = diagnostic.field;
        else if constexpr (requires { diagnostic.json_path; })
            path = diagnostic.json_path;
        add_diagnostic(result, severity, conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::serialize, target, chart, diagnostic.message,
                       std::move(path), loss);
    }
}

auto selected_chart(const song_document_t& song, chart_id_t id) -> const chart_document_t* {
    const auto found = std::ranges::find(song.charts, id, &chart_document_t::id);
    return found == song.charts.end() ? nullptr : &*found;
}

auto valid_staged_relative_path(const std::filesystem::path& staged_path) -> bool {
    const auto path = staged_path.generic_string();
    return !staged_path.empty() && path.size() <= 4096 && !staged_path.filename().empty() &&
           !staged_path.is_absolute() && !staged_path.has_root_name() &&
           staged_path.lexically_normal() == staged_path &&
           std::ranges::none_of(staged_path,
                                [](const auto& part) { return part == "." || part == ".."; }) &&
           !std::ranges::any_of(path, [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

auto prepare_staged_assets(conversion_context_t& context, song_document_t& prepared) -> bool {
    std::unordered_map<std::uint64_t, std::size_t> known_assets;
    known_assets.reserve(prepared.audio_assets.size() + prepared.visual_assets.size());
    for (const auto& asset : prepared.audio_assets) {
        ++known_assets[asset.id.value];
    }
    for (const auto& asset : prepared.visual_assets) {
        ++known_assets[asset.id.value];
    }
    std::unordered_map<std::uint64_t, std::filesystem::path> paths_by_id;
    paths_by_id.reserve(context.request.staged_assets.size());
    std::unordered_set<std::string> paths;
    for (const auto& staged : context.request.staged_assets) {
        const auto path = staged.relative_path.generic_string();
        if (!staged.asset || !valid_staged_relative_path(staged.relative_path) ||
            known_assets[staged.asset.value] != 1 ||
            !paths_by_id.emplace(staged.asset.value, staged.relative_path).second ||
            !paths.insert(path).second) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::invalid_request,
                           conversion_stage_t::request, context.request.target,
                           context.request.chart,
                           "Staged asset mapping has an invalid, duplicate, or unknown path");
            return false;
        }
    }
    for (auto& asset : prepared.audio_assets) {
        if (const auto found = paths_by_id.find(asset.id.value); found != paths_by_id.end()) {
            asset.source_path = found->second;
        }
    }
    for (auto& asset : prepared.visual_assets) {
        if (const auto found = paths_by_id.find(asset.id.value); found != paths_by_id.end()) {
            asset.source_path = found->second;
        }
    }
    for (auto& resource : prepared.resources) {
        if (const auto found = paths_by_id.find(resource.asset.value); found != paths_by_id.end()) {
            resource.name = found->second.generic_string();
        }
    }
    return true;
}

auto apply_quantization(conversion_context_t& context, song_document_t& prepared,
                        const std::vector<chart_id_t>& charts) -> bool {
    if (!context.request.quantization->objects.empty() && charts.size() != 1) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request, conversion_stage_t::transform,
                       context.request.target, std::nullopt,
                       "Object-scoped quantization requires one selected chart");
        return false;
    }
    for (const auto chart_id : charts) {
        const auto found = std::ranges::find(prepared.charts, chart_id, &chart_document_t::id);
        if (found == prepared.charts.end()) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::chart_not_found,
                           conversion_stage_t::transform, context.request.target, chart_id,
                           "Quantization chart was not found in the detached document");
            return false;
        }
        auto quantized = quantize_chart_exact(*found, *context.request.quantization);
        for (const auto& diagnostic : quantized.diagnostics) {
            add_diagnostic(context.result, diagnostic.severity,
                           conversion_diagnostic_code_t::transform_failed,
                           conversion_stage_t::transform, context.request.target, chart_id,
                           diagnostic.message);
        }
        if (!quantized) {
            return false;
        }
        const auto moved = quantized.resolution_moves.size() + quantized.quantized_moves.size();
        *found = std::move(*quantized.chart);
        add_diagnostic(context.result, diagnostic_severity_t::info,
                       conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::transform, context.request.target, chart_id,
                       "Quantization prepared " + std::to_string(moved) + " exact movement(s)");
    }
    return true;
}

auto prepare_document(conversion_context_t& context, const song_document_t& song,
                      const std::vector<chart_id_t>& charts) -> std::optional<song_document_t> {
    auto prepared = song;
    if (!context.request.charts.empty() && context.request.target == format_family_t::stepmania) {
        std::vector<chart_document_t> selected;
        selected.reserve(charts.size());
        for (const auto id : charts) {
            const auto found = std::ranges::find(prepared.charts, id, &chart_document_t::id);
            selected.push_back(*found);
        }
        prepared.charts = std::move(selected);
    }
    if (!context.request.staged_assets.empty() && !prepare_staged_assets(context, prepared)) {
        return std::nullopt;
    }
    if (!context.request.quantization)
        return prepared;
    if (!apply_quantization(context, prepared, charts)) {
        return std::nullopt;
    }
    return prepared;
}

// Span resources are local to the detached export document.
auto next_slice_resource(const song_document_t& song) -> std::uint64_t {
    std::uint64_t next{};
    for (const auto& resource : song.resources) next = std::max(next, resource.id.value);
    return next;
}

auto next_slice_asset(const song_document_t& song) -> std::uint64_t {
    std::uint64_t next{};
    for (const auto& asset : song.audio_assets) next = std::max(next, asset.id.value);
    for (const auto& asset : song.visual_assets) next = std::max(next, asset.id.value);
    for (const auto& asset : song.midi_assets) next = std::max(next, asset.id.value);
    return next;
}

using slice_identity_t = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
struct shared_slice_resource_t { resource_id_t resource; std::string filename; };
using shared_slices_t = std::map<slice_identity_t, shared_slice_resource_t>;

auto append_slice_resource(song_document_t& song, const slice_span_t& span) -> resource_id_t {
    const auto asset = asset_id_t{next_slice_asset(song) + 1};
    song.audio_assets.push_back({asset, span.filename,
        {slice_span_sample_rate, 2, span.end_frame - span.begin_frame, true}, {}});
    resource_entry_t entry;
    entry.id = resource_id_t{next_slice_resource(song) + 1};
    entry.asset = asset;
    entry.name = span.filename;
    entry.roles = {resource_role_t::sample, resource_role_t::bgm};
    entry.sound_kind = sound_kind_t::keysound;
    const auto id = entry.id;
    song.resources.push_back(std::move(entry));
    return id;
}

auto append_slice_resources(song_document_t& song, slice_span_plan_t& plan,
    shared_slices_t& shared, slice_span_plan_t& render_plan) -> std::vector<resource_id_t> {
    std::vector<resource_id_t> resources;
    render_plan.sources = plan.sources;
    for (auto& span : plan.spans) {
        const slice_identity_t identity{span.asset.value, span.begin_frame, span.end_frame};
        auto found = shared.find(identity);
        if (found == shared.end()) {
            const auto resource = append_slice_resource(song, span);
            found = shared.emplace(identity, shared_slice_resource_t{resource, span.filename}).first;
            render_plan.spans.push_back(span);
        }
        span.filename = found->second.filename;
        resources.push_back(found->second.resource);
    }
    return resources;
}

using slice_bindings_t = std::unordered_map<std::uint64_t, std::optional<resource_id_t>>;
auto slice_bindings(const slice_span_plan_t& plan, const std::vector<resource_id_t>& resources,
                    slice_span_event_kind_t kind) -> slice_bindings_t {
    slice_bindings_t result;
    for (const auto& mapping : plan.mappings) {
        if (mapping.kind != kind) continue;
        result.emplace(mapping.event.value, mapping.span_index
            ? std::optional{resources.at(*mapping.span_index)} : std::nullopt);
    }
    return result;
}

template <typename Event>
void bind_slice_event(Event& event, const slice_bindings_t& bindings) {
    event.sound = bindings.at(event.id.value);
    event.sound_channel_event.reset();
}

void apply_slice_bindings(chart_document_t& chart, const slice_span_plan_t& plan,
                         const std::vector<resource_id_t>& resources) {
    const auto notes = slice_bindings(plan, resources, slice_span_event_kind_t::note);
    const auto bgm = slice_bindings(plan, resources, slice_span_event_kind_t::bgm_event);
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) bind_slice_event(note, notes);
        for (auto& event : measure.bgm_events) bind_slice_event(event, bgm);
        // An EOF-silent autoplay mapping emits no sample; gameplay notes remain.
        std::erase_if(measure.bgm_events, [](const auto& event) { return !event.sound; });
    }
    chart.sound_channels.clear();
    for (auto& lane : chart.bgm_source_lanes) lane.stem_channel.reset();
}

auto referenced_slice_export_resources(const song_document_t& song,
    const std::vector<chart_id_t>& charts) -> std::unordered_set<std::uint64_t> {
    std::unordered_set<std::uint64_t> ids;
    for (const auto& resource : song.resources)
        if (std::ranges::find(resource.roles, resource_role_t::music) != resource.roles.end())
            ids.insert(resource.id.value);
    for (const auto id : charts) {
        const auto* chart = selected_chart(song, id);
        for (const auto& measure : chart->measures) {
            for (const auto& note : measure.notes) if (note.sound) ids.insert(note.sound->value);
            for (const auto& event : measure.bgm_events) if (event.sound) ids.insert(event.sound->value);
        }
        for (const auto& event : chart->bga_events) ids.insert(event.resource.value);
    }
    return ids;
}

template <typename Asset>
void retain_slice_export_assets(std::vector<Asset>& assets,
    const std::unordered_set<std::uint64_t>& retained, conversion_result_t& result) {
    for (auto& asset : assets) {
        if (!retained.contains(asset.id.value)) continue;
        auto extension = asset.source_path.extension().string();
        if (extension.size() > 16 || std::ranges::any_of(extension, [](unsigned char c) {
                return c != '.' && !std::isalnum(c);
            })) extension.clear();
        const auto filename = "retained-" + std::to_string(asset.id.value) + extension;
        result.asset_copies.push_back({asset.id, filename});
        asset.source_path = filename;
    }
}

void prepare_retained_slice_assets(song_document_t& song, const std::vector<chart_id_t>& charts,
    const shared_slices_t& shared, const std::unordered_set<std::uint64_t>& silent_resources,
    conversion_result_t& result) {
    const auto resources = referenced_slice_export_resources(song, charts);
    auto generated = silent_resources;
    for (const auto& [identity, item] : shared) {
        (void)identity;
        generated.insert(item.resource.value);
    }
    std::unordered_set<std::uint64_t> retained;
    for (const auto& resource : song.resources)
        if (resources.contains(resource.id.value) && !generated.contains(resource.id.value))
            retained.insert(resource.asset.value);
    retain_slice_export_assets(song.audio_assets, retained, result);
    retain_slice_export_assets(song.visual_assets, retained, result);
    for (auto& resource : song.resources) {
        const auto copy = std::ranges::find(result.asset_copies, resource.asset, &conversion_asset_copy_t::asset);
        if (copy != result.asset_copies.end()) resource.name = copy->relative_path.generic_string();
    }
}

auto sliced_bgm_timestamp_error(const chart_document_t& chart) -> long double {
    const auto timing = build_timing_index(chart);
    const auto measures = build_measure_index(chart);
    if (!timing || !std::holds_alternative<measure_index_t>(measures)) return 0;
    long double maximum{};
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            if (!event.sound) continue;
            const auto time = timing_at(*timing.index, std::get<measure_index_t>(measures), event.position);
            if (!time) continue;
            const auto milliseconds = static_cast<long double>(time.seconds->numerator) * 1000 /
                time.seconds->denominator;
            maximum = std::max(maximum, std::abs(std::round(milliseconds) - milliseconds));
        }
    }
    return maximum;
}

void report_slice_timestamp_loss(conversion_context_t& context, const chart_document_t& chart) {
    if (context.request.target != format_family_t::osu_mania ||
        context.request.target_profile == "lazer-v128")
        return;
    const auto error = sliced_bgm_timestamp_error(chart);
    if (error < 1e-9L) return;
    const auto severity = context.request.loss_policy == loss_policy_t::allow
                              ? diagnostic_severity_t::info
                              : diagnostic_severity_t::warning;
    add_diagnostic(
        context.result, severity, conversion_diagnostic_code_t::target_diagnostic,
        conversion_stage_t::transform, context.request.target, chart.id,
        "Stable-v14 autoplay sample timestamps round to integer milliseconds; maximum offset " +
            std::to_string(static_cast<double>(error)) + " ms (at most 0.5 ms)",
        "Events.Sample", true);
}

void prepare_osu_slice_layout(chart_document_t& chart) {
    // BMSON scratch columns become sampled osu! columns in the same display order.
    // Their typed lane IDs and note bindings remain intact in the detached document.
    for (auto& segment : chart.lane_layouts)
        for (auto& lane : segment.layout.lanes)
            if (lane.role == lane_role_t::scratch) lane.role = lane_role_t::playable;
}

auto add_bms_silent_binding(song_document_t& song, const std::vector<chart_id_t>& charts,
    conversion_result_t& result, std::unordered_set<std::uint64_t>& silent_resources) -> bool {
    std::optional<resource_id_t> silence;
    for (auto& chart : song.charts) {
        if (chart.source_family != format_family_t::bmson ||
            std::ranges::find(charts, chart.id) == charts.end()) continue;
        for (auto& measure : chart.measures) {
            for (auto& note : measure.notes) {
                if (note.sound) continue;
                if (!silence) {
                    if (std::max(next_slice_asset(song), next_slice_resource(song)) ==
                        std::numeric_limits<std::uint64_t>::max()) return false;
                    const auto filename = "conversion-silence.wav";
                    silence = append_slice_resource(song, {.begin_frame = 0, .end_frame = 1,
                        .filename = filename});
                    result.silent_files.emplace_back(filename);
                    silent_resources.insert(silence->value);
                }
                note.sound = silence;
            }
        }
    }
    return true;
}

auto prepare_bms_slice_resources(conversion_context_t& context, song_document_t& song,
                                 const std::vector<chart_id_t>& charts,
                                 std::unordered_set<std::uint64_t>& silent_resources) -> bool {
    if (context.request.target != format_family_t::bms)
        return true;
    if (!add_bms_silent_binding(song, charts, context.result, silent_resources)) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::transform_failed,
                       conversion_stage_t::transform, context.request.target, {},
                       "Generated silence asset/resource ID capacity exceeded");
        return false;
    }
    const auto used = referenced_slice_export_resources(song, charts);
    std::erase_if(song.resources, [&](const auto& resource) { return !used.contains(resource.id.value); });
    const auto count = std::ranges::count_if(song.resources, [&](const auto& resource) {
        return std::ranges::find(song.audio_assets, resource.asset, &audio_asset_t::id) != song.audio_assets.end();
    });
    constexpr std::size_t capacity = 1295;
    if (static_cast<std::size_t>(count) <= capacity) return true;
    add_diagnostic(context.result, diagnostic_severity_t::error,
                   conversion_diagnostic_code_t::transform_failed, conversion_stage_t::transform,
                   context.request.target, {},
                   "needs " + std::to_string(count) + " sounds, BMS holds " +
                       std::to_string(capacity));
    return false;
}

auto compatible_bms_layout(const lane_layout_t& source, const lane_layout_t& target) -> bool {
    if (source.lanes.size() != target.lanes.size()) return false;
    return std::ranges::all_of(source.lanes, [&](const auto& lane) {
        const auto matching = [&](const auto& other) { return other.role == lane.role && other.stage == lane.stage; };
        return std::ranges::count_if(source.lanes, matching) == std::ranges::count_if(target.lanes, matching);
    });
}

void map_bms_slice_lanes(lane_layout_t& source, const lane_layout_t& target) {
    std::map<std::pair<stage_group_t, lane_role_t>, std::size_t> next;
    for (auto& lane : source.lanes) {
        std::vector<const lane_definition_t*> matches;
        for (const auto& candidate : target.lanes)
            if (candidate.role == lane.role && candidate.stage == lane.stage) matches.push_back(&candidate);
        std::ranges::sort(matches, {}, [](const auto* value) { return value->display_order; });
        lane.bms_channels = matches.at(next[{lane.stage, lane.role}]++)->bms_channels;
    }
    source.profile_id = target.profile_id;
}

auto prepare_bms_slice_layout(conversion_context_t& context, chart_document_t& chart) -> bool {
    for (auto& segment : chart.lane_layouts) {
        const auto profiles = built_in_bms_lane_profiles();
        const auto found = std::ranges::find_if(profiles, [&](const auto& descriptor) {
            return (context.request.target_profile.empty() ||
                    descriptor.profile.layout.profile_id == context.request.target_profile) &&
                   compatible_bms_layout(segment.layout, descriptor.profile.layout);
        });
        if (found == profiles.end()) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::transform_failed,
                           conversion_stage_t::transform, context.request.target, chart.id,
                           "BMSON lane layout has no compatible BMS channel profile");
            return false;
        }
        map_bms_slice_lanes(segment.layout, found->profile.layout);
    }
    return true;
}

auto target_slices_stems(format_family_t target) -> bool {
    return target == format_family_t::osu_mania || target == format_family_t::bms ||
           target == format_family_t::stepmania;
}

auto prepare_slice_exports(conversion_context_t& context, song_document_t& prepared,
                           const std::vector<chart_id_t>& charts) -> bool {
    if (!target_slices_stems(context.request.target))
        return true;
    auto options = context.request.slice_options;
    shared_slices_t shared;
    for (const auto id : charts) {
        auto chart = std::ranges::find(prepared.charts, id, &chart_document_t::id);
        if (chart->source_family != format_family_t::bmson) continue;
        auto plan = build_slice_span_plan(prepared, id, context.request.slice_sources, options);
        if (!plan) {
            for (const auto& diagnostic : plan.diagnostics)
                add_diagnostic(context.result, diagnostic_severity_t::error,
                               conversion_diagnostic_code_t::transform_failed,
                               conversion_stage_t::transform, context.request.target, id,
                               diagnostic.message);
            return false;
        }
        const auto available = std::numeric_limits<std::uint64_t>::max() -
            std::max(next_slice_asset(prepared), next_slice_resource(prepared));
        if (plan.spans.size() > available) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::transform_failed,
                           conversion_stage_t::transform, context.request.target, id,
                           "Generated slice asset/resource ID capacity exceeded");
            return false;
        }
        slice_span_plan_t render_plan;
        const auto resources = append_slice_resources(prepared, plan, shared, render_plan);
        apply_slice_bindings(*chart, plan, resources);
        if (context.request.target == format_family_t::osu_mania ||
            context.request.target == format_family_t::stepmania)
            prepare_osu_slice_layout(*chart);
        else if (!prepare_bms_slice_layout(context, *chart))
            return false;
        report_slice_timestamp_loss(context, *chart);
        for (const auto& span : plan.spans) options.occupied_filenames.push_back(span.filename);
        if (!render_plan.spans.empty())
            context.result.slice_plans.push_back(std::move(render_plan));
    }
    if (std::ranges::any_of(charts, [&](const auto id) {
            return selected_chart(prepared, id)->source_family == format_family_t::bmson;
        })) {
        std::unordered_set<std::uint64_t> silent_resources;
        if (!prepare_bms_slice_resources(context, prepared, charts, silent_resources))
            return false;
        prepare_retained_slice_assets(prepared, charts, shared, silent_resources, context.result);
    }
    return true;
}

auto contains_bms_tree(const song_document_t& song, const std::vector<chart_id_t>& charts) -> bool {
    return std::ranges::any_of(charts, [&](const auto id) {
        const auto* chart = selected_chart(song, id);
        return chart && chart->bms_control_tree.has_value();
    });
}

auto validate_reimport(conversion_context_t& context, const conversion_output_t& output,
                       const stepmania_export_policy_result_t* stepmania_options) -> bool {
    switch (output.target) {
    case format_family_t::native_project: {
        const auto parsed = parse_native_project(output.bytes);
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::bms: {
        bms_parse_options_t options;
        options.file_extension = output.suggested_filename.substr(
            output.suggested_filename.find_last_of('.'));
        if (!context.request.target_profile.empty())
            options.lane_profile_id = context.request.target_profile;
        if (context.request.encoding && *context.request.encoding != text_encoding_t::ascii &&
            *context.request.encoding != text_encoding_t::utf8)
            options.encoding_override = context.request.encoding;
        const auto parsed = parse_bms_bytes(byte_span(output.bytes), std::move(options));
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::stepmania: {
        if (!stepmania_options || !stepmania_options->options)
            break;
        const auto& write = *stepmania_options->options;
        const auto parsed = parse_stepmania_bytes(
            byte_span(output.bytes),
            {.profile = write.profile,
             .source_timing_syntax = write.target_timing_syntax,
             .source_implementation_profile = write.target_implementation_profile});
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::osu_mania: {
        const auto parsed = parse_osu_mania_bytes(byte_span(output.bytes));
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::bmson: {
        const auto parsed = parse_bmson_bytes(byte_span(output.bytes));
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::midi:
    case format_family_t::quaver:
        break;
    }
    add_diagnostic(context.result, diagnostic_severity_t::error,
                   conversion_diagnostic_code_t::reimport_failed, conversion_stage_t::reimport,
                   output.target, output.chart,
                   "Serialized conversion output did not pass bounded reimport validation",
                   output.suggested_filename);
    return false;
}

auto validate_request_policies(conversion_context_t& context, const song_document_t& song) -> bool {
    const auto reject = [&](conversion_diagnostic_code_t code, std::string message) {
        add_diagnostic(context.result, diagnostic_severity_t::error, code,
                       conversion_stage_t::request, context.request.target, context.request.chart,
                       std::move(message));
        return false;
    };
    if (context.limits.maximum_outputs == 0 || context.limits.maximum_total_output_bytes == 0 ||
        context.limits.maximum_filename_bytes < 16) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Conversion limits must permit outputs, bytes, and bounded filenames");
    }
    if (context.request.target != format_family_t::stepmania && context.request.stepmania) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "StepMania export policy was supplied for a different target");
    }
    if (context.request.target != format_family_t::bms && context.request.encoding) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Output text encoding is currently a BMS-family policy");
    }
    if (context.request.target == format_family_t::native_project &&
        (context.request.chart || !context.request.charts.empty())) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Native project conversion preserves the whole project; chart selection "
                      "is not applicable");
    }
    if (!context.request.staged_assets.empty() && context.request.target != format_family_t::bms &&
        context.request.target != format_family_t::bmson &&
        context.request.target != format_family_t::osu_mania &&
        context.request.target != format_family_t::stepmania) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "This target does not yet serialize staged resource paths");
    }
    if (context.request.staged_assets.size() >
        song.audio_assets.size() + song.visual_assets.size()) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Staged asset mapping exceeds the document asset count");
    }
    if (context.request.target == format_family_t::midi ||
        context.request.target == format_family_t::quaver) {
        return reject(conversion_diagnostic_code_t::unsupported_target,
                      "The selected conversion target has no paired writer yet");
    }
    return true;
}

auto validate_selected_target(conversion_context_t& context, const song_document_t& song,
                              const std::vector<chart_id_t>& charts) -> bool {
    if (charts.size() > context.limits.maximum_outputs &&
        context.request.target != format_family_t::stepmania &&
        context.request.target != format_family_t::native_project) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::output_limit, conversion_stage_t::request,
                       context.request.target, context.request.chart,
                       "Conversion output count exceeds its configured limit");
        return false;
    }
    if (context.request.target == format_family_t::bms ||
        context.request.target == format_family_t::native_project ||
        !contains_bms_tree(song, charts)) {
        return true;
    }
    if (context.request.branches != branch_projection_t::active_projection) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request, conversion_stage_t::request,
                       context.request.target, context.request.chart,
                       "The target cannot preserve a BMS branch tree; select active projection "
                       "explicitly");
        return false;
    }
    const auto severity = context.request.loss_policy == loss_policy_t::allow
                              ? diagnostic_severity_t::info
                              : diagnostic_severity_t::warning;
    add_diagnostic(context.result, severity, conversion_diagnostic_code_t::target_diagnostic,
                   conversion_stage_t::transform, context.request.target, context.request.chart,
                   "BMS control flow will be flattened to its active materialized projection",
                   "branches", true);
    return true;
}

void append_conversion_output(conversion_context_t& context, const song_document_t& prepared,
                              std::optional<chart_id_t> chart, std::string extension,
                              std::optional<std::string> bytes,
                              const chart_document_t* chart_document) {
    if (!bytes) {
        return;
    }
    const auto filename = filename_for(prepared, chart_document, extension, context.limits);
    if (!filename) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::output_limit, conversion_stage_t::serialize,
                       context.request.target, chart,
                       "Conversion filename cannot fit its configured byte limit");
        return;
    }
    context.result.outputs.push_back(
        {context.request.target, chart, *filename, std::move(*bytes), false});
}

void write_native_output(conversion_context_t& context, const song_document_t& prepared) {
    if (!context.request.target_profile.empty() && context.request.target_profile != "1") {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, context.request.target, std::nullopt,
                       "Unknown native-project target profile: " + context.request.target_profile);
    }
    if (result_has_errors(context.result)) {
        return;
    }
    const auto written = write_native_project(prepared);
    append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                              std::nullopt, context.request.loss_policy);
    append_conversion_output(context, prepared, std::nullopt, ".wormedit", written.bytes, nullptr);
}

void write_bms_outputs(conversion_context_t& context, const song_document_t& prepared,
                       const std::vector<chart_id_t>& charts) {
    for (const auto chart_id : charts) {
        bms_write_options_t options;
        options.chart = chart_id;
        options.preserve_unchanged_source = context.request.staged_assets.empty();
        options.branch_policy = context.request.branches == branch_projection_t::preserve_tree
                                    ? bms_branch_policy_t::preserve_tree
                                    : bms_branch_policy_t::flatten_active;
        options.output_encoding = context.request.encoding;
        if (!context.request.target_profile.empty() &&
            selected_chart(prepared, chart_id)->source_family != format_family_t::bmson) {
            options.lane_profile_id = context.request.target_profile;
        }
        const auto written = write_bms(prepared, std::move(options));
        append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                                  chart_id, context.request.loss_policy);
        const auto* chart = selected_chart(prepared, chart_id);
        const auto profile = context.request.target_profile.empty() &&
                                     chart->source_family == format_family_t::bmson &&
                                     !chart->lane_layouts.empty()
                                 ? chart->lane_layouts.front().layout.profile_id
                                 : context.request.target_profile;
        append_conversion_output(context, prepared, chart_id, std::string(bms_extension(profile)),
                                 written.bytes, chart);
    }
}

auto resolve_stepmania_options(conversion_context_t& context)
    -> std::optional<stepmania_export_policy_result_t> {
    if (!context.request.target_profile.empty()) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, context.request.target, context.request.chart,
                       "StepMania target profiles use the typed export policy");
        return std::nullopt;
    }
    if (!context.request.stepmania) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request, conversion_stage_t::request,
                       context.request.target, context.request.chart,
                       "StepMania conversion requires explicit file, syntax, and implementation "
                       "choices");
        return std::nullopt;
    }
    auto resolved = resolve_stepmania_export_policy(*context.request.stepmania);
    if (!resolved) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, context.request.target, context.request.chart,
                       resolved.message);
        return std::nullopt;
    }
    return resolved;
}

auto write_stepmania_output(conversion_context_t& context, const song_document_t& prepared)
    -> std::optional<stepmania_export_policy_result_t> {
    auto resolved = resolve_stepmania_options(context);
    if (!resolved.has_value()) {
        return std::nullopt;
    }
    auto options = *resolved->options;
    options.chart = context.request.chart;
    const auto written = write_stepmania(prepared, std::move(options));
    append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                              context.request.chart, context.request.loss_policy);
    const auto extension =
        context.request.stepmania->file_profile == stepmania_profile_t::sm ? ".sm" : ".ssc";
    append_conversion_output(
        context, prepared, context.request.chart, extension, written.text,
        context.request.chart ? selected_chart(prepared, *context.request.chart) : nullptr);
    return resolved;
}

auto osu_profile(conversion_context_t& context) -> std::optional<osu_mania_file_profile_t> {
    if (context.request.target_profile.empty()) {
        return std::nullopt;
    }
    if (context.request.target_profile == "stable-v14") {
        return osu_mania_file_profile_t::stable_v14;
    }
    if (context.request.target_profile == "lazer-v128") {
        return osu_mania_file_profile_t::lazer_v128;
    }
    add_diagnostic(context.result, diagnostic_severity_t::error,
                   conversion_diagnostic_code_t::unknown_target_profile,
                   conversion_stage_t::request, context.request.target, context.request.chart,
                   "Unknown osu!mania target profile: " + context.request.target_profile);
    return std::nullopt;
}

void write_osu_outputs(conversion_context_t& context, const song_document_t& prepared,
                       const std::vector<chart_id_t>& charts) {
    const auto profile = osu_profile(context);
    if (!context.request.target_profile.empty() && !profile.has_value()) {
        return;
    }
    for (const auto chart_id : charts) {
        const auto written = write_osu_mania(prepared, {.profile = profile, .chart = chart_id});
        append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                                  chart_id, context.request.loss_policy);
        append_conversion_output(context, prepared, chart_id, ".osu", written.text,
                                 selected_chart(prepared, chart_id));
    }
}

void write_bmson_outputs(conversion_context_t& context, const song_document_t& prepared,
                         const std::vector<chart_id_t>& charts) {
    if (!context.request.target_profile.empty() && context.request.target_profile != "1.0") {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, context.request.target, context.request.chart,
                       "Unknown BMSON target profile: " + context.request.target_profile);
        return;
    }
    for (const auto chart_id : charts) {
        const auto written = write_bmson(prepared, {.chart = chart_id});
        append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                                  chart_id, context.request.loss_policy);
        append_conversion_output(context, prepared, chart_id, ".bmson", written.json,
                                 selected_chart(prepared, chart_id));
    }
}

auto serialize_conversion(conversion_context_t& context, const song_document_t& prepared,
                          const std::vector<chart_id_t>& charts)
    -> std::optional<stepmania_export_policy_result_t> {
    switch (context.request.target) {
    case format_family_t::native_project:
        write_native_output(context, prepared);
        break;
    case format_family_t::bms:
        write_bms_outputs(context, prepared, charts);
        break;
    case format_family_t::stepmania:
        return write_stepmania_output(context, prepared);
    case format_family_t::osu_mania:
        write_osu_outputs(context, prepared, charts);
        break;
    case format_family_t::bmson:
        write_bmson_outputs(context, prepared, charts);
        break;
    case format_family_t::midi:
    case format_family_t::quaver:
        break;
    }
    return std::nullopt;
}

auto validate_conversion_outputs(conversion_context_t& context,
                                 const stepmania_export_policy_result_t* stepmania_options)
    -> bool {
    if (result_has_errors(context.result)) {
        context.result.outputs.clear();
        return false;
    }
    if (context.request.loss_policy == loss_policy_t::reject &&
        std::ranges::any_of(context.result.diagnostics, &conversion_diagnostic_t::loss)) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::loss_rejected, conversion_stage_t::serialize,
                       context.request.target, context.request.chart,
                       "Conversion produced a loss diagnostic under the reject policy");
        context.result.outputs.clear();
        return false;
    }
    if (context.result.outputs.empty()) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::serialize, context.request.target, context.request.chart,
                       "Target writer produced no conversion output");
        return false;
    }
    std::size_t total_bytes{};
    for (const auto& output : context.result.outputs) {
        if (output.bytes.size() > context.limits.maximum_total_output_bytes - total_bytes) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::output_limit,
                           conversion_stage_t::serialize, context.request.target, output.chart,
                           "Conversion output set exceeds its configured byte limit");
            context.result.outputs.clear();
            return false;
        }
        total_bytes += output.bytes.size();
    }
    for (auto& output : context.result.outputs) {
        if (!validate_reimport(context, output, stepmania_options)) {
            context.result.outputs.clear();
            return false;
        }
        output.reimport_validated = true;
    }
    return true;
}

} // namespace

auto conversion_result_t::has_errors() const -> bool {
    return result_has_errors(*this);
}

conversion_result_t::operator bool() const noexcept {
    return !outputs.empty() && !std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
               return diagnostic.severity == diagnostic_severity_t::error;
           });
}

auto plan_conversion_slice_spans(const song_document_t& song, chart_id_t chart,
    std::span<const slice_span_source_t> sources, const slice_span_options_t& options) -> slice_span_plan_t {
    return build_slice_span_plan(song, chart, sources, options);
}

auto conversion_needs_slice_spans(const song_document_t& song, const conversion_request_t& request) -> bool {
    if (!target_slices_stems(request.target)) return false;
    return std::ranges::any_of(song.charts, [&](const auto& chart) {
        const bool selected = request.chart ? chart.id == *request.chart :
            request.charts.empty() || std::ranges::find(request.charts, chart.id) != request.charts.end();
        return selected && chart.source_family == format_family_t::bmson;
    });
}

auto convert_document(const song_document_t& song, const conversion_request_t& request,
                      conversion_limits_t limits) -> conversion_result_t {
    conversion_result_t result;
    conversion_context_t context{request, limits, result};
    if (!validate_request_policies(context, song)) {
        return result;
    }
    const auto charts = selected_chart_ids(context, song);
    if (result_has_errors(result) || !validate_selected_target(context, song, charts)) {
        return result;
    }
    auto prepared = prepare_document(context, song, charts);
    if (!prepared || result_has_errors(result)) {
        result.outputs.clear();
        return result;
    }
    if (!prepare_slice_exports(context, *prepared, charts)) {
        result.slice_plans.clear();
        result.asset_copies.clear();
        result.silent_files.clear();
        return result;
    }
    auto stepmania_options = serialize_conversion(context, *prepared, charts);
    if (!validate_conversion_outputs(context, stepmania_options ? &*stepmania_options : nullptr)) {
        result.slice_plans.clear();
        result.asset_copies.clear();
        result.silent_files.clear();
        return result;
    }
    return result;
}

} // namespace wormedit
