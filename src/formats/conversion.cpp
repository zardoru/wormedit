#include <wormedit/formats/resource_plan.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/catalogs.h>
#include "../core/text_util.h"
#include "../core/checked_math.h"
#include <wormedit/core/rational.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/slice_spans.h>

#include <wormedit/core/quantize.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/measure_editor.h>
#include "../core/chart_position_storage.h"
#include "../core/id_ceiling.h"
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>
#include "stepmania_storyboard.h"
#include "../core/timing_arithmetic.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <set>
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
    conversion_outcome_t& result;
    const conversion_execution_t& execution;
    [[nodiscard]] auto can_continue() -> bool;
    void advance() const {
        if (execution.progress) ++execution.progress->completed;
    }
};
auto byte_span(std::string_view bytes) -> std::span<const std::byte> {
    return {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
}

void add_diagnostic(conversion_outcome_t& result, diagnostic_severity_t severity,
                    conversion_diagnostic_code_t code, conversion_stage_t stage,
                    format_family_t target, std::optional<chart_id_t> chart,
                    std::string message, std::string path = {}, bool loss = false) {
    result.diagnostics.push_back({severity, code, stage, target, chart, loss, std::move(message),
                                  std::move(path), {}});
}

auto conversion_context_t::can_continue() -> bool {
    if (!execution.stop.stop_requested()) return true;
    if (result.ok())
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::cancelled, conversion_stage_t::serialize,
                       request.target, request.chart, "Conversion cancelled.");
    return false;
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
            const auto count = std::ranges::count(song.read_charts(), id, [](const auto* chart) { return chart->id; });
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
            std::ranges::count(song.read_charts(), *context.request.chart, [](const auto* chart) { return chart->id; });
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
    ids.reserve(song.read_charts().size());
    for (const auto* chart : song.read_charts())
        ids.push_back(chart->id);
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
                result.push_back(static_cast<char>(text_util::ascii_lowercase(static_cast<char>(value))));
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
    return bms_extension_for_profile(profile);
}

template <typename Diagnostic>
void append_target_diagnostics(conversion_outcome_t& result,
                               const std::vector<Diagnostic>& diagnostics,
                               format_family_t target, std::optional<chart_id_t> chart,
                               loss_policy_t loss_policy) {
    for (const auto& diagnostic : diagnostics) {
        bool loss = diagnostic.severity == diagnostic_severity_t::warning;
        if constexpr (std::is_same_v<Diagnostic, osu_mania_write_diagnostic_t>)
            if (diagnostic.code == osu_mania_write_diagnostic_code_t::storyboard_bake) loss = false;
        auto severity = diagnostic.severity;
        if (loss && loss_policy == loss_policy_t::allow)
            severity = diagnostic_severity_t::info;
        std::string path;
        if constexpr (requires { path = diagnostic.location; })
            path = diagnostic.location;
        else if constexpr (requires { diagnostic.location.json_path; })
            path = diagnostic.location.json_path;
        add_diagnostic(result, severity, conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::serialize, target, chart, diagnostic.message,
                       std::move(path), loss);
    }
}

auto selected_chart(const song_document_t& song, chart_id_t id) -> const chart_document_t* {
    const auto found = song.read_chart(id);
    return found == nullptr ? nullptr : &*found;
}

auto is_valid_staged_relative_path(const std::filesystem::path& staged_path) -> bool {
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
        if (!staged.asset || !is_valid_staged_relative_path(staged.relative_path) ||
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
            for (auto& chart : prepared.charts)
                chart.metadata.overrides.transform_audio_paths([&](std::filesystem::path& reference) {
                    if (reference.lexically_normal() == asset.source_path.lexically_normal()) reference = found->second;
                });
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
        const auto found = prepared.find_chart(chart_id);
        if (found == nullptr) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::chart_not_found,
                           conversion_stage_t::transform, context.request.target, chart_id,
                           "Quantization chart was not found in the detached document");
            return false;
        }
        auto quantized = quantization_plan_t::from(*found, *context.request.quantization);
        for (const auto& diagnostic : quantized.diagnostics) {
            add_diagnostic(context.result, diagnostic.severity,
                           conversion_diagnostic_code_t::transform_failed,
                           conversion_stage_t::transform, context.request.target, chart_id,
                           diagnostic.message);
        }
        if (!quantized.value.chart) {
            return false;
        }
        const auto moved = quantized.value.resolution_moves.size() + quantized.value.quantized_moves.size();
        *found = std::move(*quantized.value.chart);
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
    if (context.request.target != format_family_t::bms && context.request.target != format_family_t::native_project) {
        for (const auto& owner : song.prepared_bms_programs) prepared.charts.push_back(*owner->read_chart());
        prepared.prepared_bms_programs.clear();
    }
    if (!context.request.charts.empty() && context.request.target == format_family_t::stepmania) {
        std::vector<chart_document_t> selected;
        selected.reserve(charts.size());
        for (const auto id : charts) {
            const auto found = prepared.find_chart(id);
            selected.push_back(*found);
        }
        prepared.charts = std::move(selected);
        prepared.chart_order = charts;
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

auto append_slice_resources(song_document_t& song, slice_span_plan_outcome_t& plan,
    shared_slices_t& shared, slice_span_plan_outcome_t& render_plan) -> std::vector<resource_id_t> {
    std::vector<resource_id_t> resources;
    render_plan.value.sources = plan.value.sources;
    for (auto& span : plan.value.spans) {
        const slice_identity_t identity{span.asset.value, span.begin_frame, span.end_frame};
        auto found = shared.find(identity);
        if (found == shared.end()) {
            const auto resource = append_slice_resource(song, span);
            found = shared.emplace(identity, shared_slice_resource_t{resource, span.filename}).first;
            render_plan.value.spans.push_back(span);
        }
        span.filename = found->second.filename;
        resources.push_back(found->second.resource);
    }
    return resources;
}

using slice_bindings_t = std::unordered_map<std::uint64_t, std::optional<resource_id_t>>;
auto slice_bindings(const slice_span_plan_outcome_t& plan, const std::vector<resource_id_t>& resources,
                    slice_span_event_kind_t kind) -> slice_bindings_t {
    slice_bindings_t result;
    for (const auto& mapping : plan.value.mappings) {
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

void apply_slice_bindings(chart_document_t& chart, const slice_span_plan_outcome_t& plan,
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
    const std::unordered_set<std::uint64_t>& retained, conversion_outcome_t& result,
    std::span<chart_document_t> charts = {}) {
    for (auto& asset : assets) {
        if (!retained.contains(asset.id.value)) continue;
        auto extension = asset.source_path.extension().string();
        if (extension.size() > 16 || std::ranges::any_of(extension, [](unsigned char c) {
                return c != '.' && !std::isalnum(c);
            })) extension.clear();
        const auto filename = "retained-" + std::to_string(asset.id.value) + extension;
        result.value.asset_copies.push_back({.asset = asset.id, .relative_path = filename});
        for (auto& chart : charts)
            chart.metadata.overrides.transform_audio_paths([&](std::filesystem::path& reference) {
                if (reference.lexically_normal() == asset.source_path.lexically_normal()) reference = filename;
            });
        asset.source_path = filename;
    }
}

void prepare_retained_slice_assets(song_document_t& song, const std::vector<chart_id_t>& charts,
    const shared_slices_t& shared, const std::unordered_set<std::uint64_t>& silent_resources,
    conversion_outcome_t& result) {
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
    retain_slice_export_assets(song.audio_assets, retained, result, song.charts);
    retain_slice_export_assets(song.visual_assets, retained, result);
    for (auto& resource : song.resources) {
        const auto copy = std::ranges::find(result.value.asset_copies, resource.asset, &conversion_asset_copy_t::asset);
        if (copy != result.value.asset_copies.end()) resource.name = copy->relative_path.generic_string();
    }
}

auto sliced_bgm_timestamp_error(const chart_document_t& chart) -> long double {
    const auto timing = timing_index_t::from(chart);
    const auto measures = measure_index_t::from(chart);
    if (!timing.value || !measures.has_value()) return 0;
    long double maximum{};
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            if (!event.sound) continue;
            const auto time = timing.value->query_seconds(measures.value(), event.position);
            if (!time) continue;
            const auto milliseconds = to_long_double(time->value) * 1000;
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
    // Scratch columns become sampled osu! columns in the same display order.
    // Their typed lane IDs and note bindings remain intact in the detached document.
    for (auto& segment : chart.lane_layouts)
        for (auto& lane : segment.layout.lanes)
            if (lane.role == lane_role_t::scratch) lane.role = lane_role_t::playable;
}

void prepare_bms_osu_layouts(song_document_t& song, const conversion_request_t& request,
                             const std::vector<chart_id_t>& charts) {
    if (request.target != format_family_t::osu_mania) return;
    for (const auto id : charts) {
        auto* chart = song.find_chart(id);
        if (chart && chart->source_family == format_family_t::bms)
            prepare_osu_slice_layout(*chart);
    }
}

auto add_bms_silent_binding(song_document_t& song, const std::vector<chart_id_t>& charts,
    conversion_outcome_t& result, std::unordered_set<std::uint64_t>& silent_resources) -> bool {
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
                    result.value.silent_files.emplace_back(filename);
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
        return song.find_audio_asset(resource.asset) != nullptr;
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
                    bms_write_options_t::from_profile(context.request.target_profile).has_value() ||
                    descriptor.profile.layout.profile_id == context.request.target_profile) &&
                   is_bms_lane_layout_compatible(segment.layout, descriptor.profile.layout);
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

auto has_target_stem_slices(format_family_t target) -> bool {
    return target == format_family_t::osu_mania || target == format_family_t::bms ||
           target == format_family_t::stepmania;
}

auto prepare_slice_exports(conversion_context_t& context, song_document_t& prepared,
                           const std::vector<chart_id_t>& charts) -> bool {
    if (!has_target_stem_slices(context.request.target) && prepared.export_cuts.empty())
        return true;
    auto options = context.request.slice_options;
    shared_slices_t shared;
    for (const auto id : charts) {
        auto chart = prepared.find_chart(id);
        if (!chart || chart->source_family != format_family_t::bmson) continue;
        auto plan = slice_span_plan_payload_t::from(prepared, id, context.request.slice_sources, options);
        if (!plan.ok()) {
            for (const auto& diagnostic : plan.diagnostics)
                add_diagnostic(context.result, diagnostic_severity_t::error,
                               conversion_diagnostic_code_t::transform_failed,
                               conversion_stage_t::transform, context.request.target, id,
                               diagnostic.message);
            return false;
        }
        const auto available = std::numeric_limits<std::uint64_t>::max() -
            std::max(next_slice_asset(prepared), next_slice_resource(prepared));
        if (plan.value.spans.size() > available) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::transform_failed,
                           conversion_stage_t::transform, context.request.target, id,
                           "Generated slice asset/resource ID capacity exceeded");
            return false;
        }
        slice_span_plan_outcome_t render_plan;
        const auto resources = append_slice_resources(prepared, plan, shared, render_plan);
        apply_slice_bindings(*chart, plan, resources);
        if (context.request.target == format_family_t::osu_mania ||
            context.request.target == format_family_t::stepmania)
            prepare_osu_slice_layout(*chart);
        else if (context.request.target == format_family_t::bms && !prepare_bms_slice_layout(context, *chart))
            return false;
        report_slice_timestamp_loss(context, *chart);
        for (const auto& span : plan.value.spans) options.occupied_filenames.push_back(span.filename);
        if (!render_plan.value.spans.empty())
            context.result.value.slice_plans.push_back(std::move(render_plan));
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

auto has_bms_tree(const song_document_t& song, const std::vector<chart_id_t>& charts) -> bool {
    return std::ranges::any_of(charts, [&](const auto id) {
        const auto* chart = selected_chart(song, id);
        const auto* owner = song.find_bms_program(id);
        return (chart && chart->bms_control_tree.has_value()) || (owner &&
            (owner->prepared().source.original_bytes || owner->prepared().program.has_control_flow()));
    });
}

auto make_bms_reimport_options(const conversion_request_t& request, const conversion_output_t& output)
    -> bms_parse_options_t {
    bms_parse_options_t options;
    options.file_extension = output.suggested_filename.substr(
        output.suggested_filename.find_last_of('.'));
    const auto player = bms_write_options_t::from_profile(request.target_profile);
    if (!request.target_profile.empty() && !player)
        options.lane_profile_id = request.target_profile;
    if (request.bms_profile.value_or(player ? player->profile : bms_export_profile_t::beatoraja) ==
        bms_export_profile_t::lr2_compatible)
        options.encoding_override = text_encoding_t::cp932;
    if (request.encoding && *request.encoding != text_encoding_t::ascii &&
        *request.encoding != text_encoding_t::utf8)
        options.encoding_override = request.encoding;
    return options;
}

auto validate_osu_reimport(conversion_context_t& context, const conversion_output_t& output) -> bool {
    if (std::filesystem::path(output.suggested_filename).extension() == ".osb") {
        const auto parsed = parse_osb(output.bytes);
        if (parsed.value && parsed.ok()) return true;
        for (const auto& diagnostic : parsed.diagnostics)
            add_diagnostic(context.result, diagnostic_severity_t::error,
                conversion_diagnostic_code_t::reimport_failed, conversion_stage_t::reimport,
                output.target, output.chart, diagnostic.message, output.suggested_filename);
        return false;
    }
    const auto parsed = parse_osu_mania_bytes(byte_span(output.bytes));
    if (parsed.value && parsed.ok()) return true;
    append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                              loss_policy_t::warn);
    return false;
}

auto validate_reimport(conversion_context_t& context, const conversion_output_t& output,
                       const stepmania_export_policy_outcome_t* stepmania_options,
                       const song_document_t& prepared) -> bool {
    switch (output.target) {
    case format_family_t::native_project: {
        const auto parsed = parse_native_project(output.bytes);
        if (parsed.value && parsed.ok())
            return true;
        append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::bms: {
        auto options = make_bms_reimport_options(context.request, output);
        const auto* chart = output.chart ? prepared.read_chart(*output.chart) : nullptr;
        if (!options.lane_profile_id && chart && !chart->lane_layouts.empty())
            options.authored_lane_profile = lane_input_profile_t{chart->lane_layouts.front().layout, chart->input_profile};
        const auto parsed = parse_bms_bytes(byte_span(output.bytes), std::move(options));
        if (parsed.value.song && parsed.ok())
            return true;
        append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::stepmania: {
        if (!stepmania_options || !(*stepmania_options))
            break;
        const auto& write = **stepmania_options;
        const auto parsed = parse_stepmania_bytes(
            byte_span(output.bytes),
            {.profile = write.profile,
             .source_timing_syntax = write.target_timing_syntax,
             .source_implementation_profile = write.target_implementation_profile});
        if (parsed.value && parsed.ok())
            return true;
        append_target_diagnostics(context.result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::osu_mania:
        if (validate_osu_reimport(context, output)) return true;
        break;
    case format_family_t::bmson: {
        const auto parsed = parse_bmson_bytes(byte_span(output.bytes));
        if (parsed.value && parsed.ok())
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

auto validate_request_policies(conversion_context_t& context, const song_document_t& song,
                               bool allow_native_selection = false) -> bool {
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
    if (!context.request.bms_output_extension.empty() &&
        (context.request.target != format_family_t::bms ||
         !is_bms_extension(context.request.bms_output_extension))) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Output extension does not match the BMS format family");
    }
    if (context.request.target != format_family_t::stepmania && context.request.stepmania) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "StepMania export policy was supplied for a different target");
    }
    if (context.request.target != format_family_t::bms &&
        (context.request.encoding || context.request.bms_profile)) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Output text encoding and BMS player profiles are BMS-family policies");
    }
    if (context.request.target == format_family_t::native_project && !allow_native_selection &&
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
        !has_bms_tree(song, charts)) {
        return true;
    }
    if (context.request.branches != branch_projection_t::active_projection) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request, conversion_stage_t::request,
                       context.request.target, context.request.chart,
                       "The target cannot preserve a BMS branch tree; enable Flatten BMS branches "
                       "on this output group (active projection)");
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
    if (!context.can_continue() || !bytes) {
        return;
    }
    if (context.result.value.outputs.size() >= context.limits.maximum_outputs) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::output_limit, conversion_stage_t::serialize,
                       context.request.target, chart,
                       "Conversion output count exceeds its configured limit");
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
    context.result.value.outputs.push_back(
        {context.request.target, chart, *filename, std::move(*bytes), false});
    context.advance();
}

void write_native_output(conversion_context_t& context, const song_document_t& prepared) {
    if (!context.request.target_profile.empty() && context.request.target_profile != "1") {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, context.request.target, std::nullopt,
                       "Unknown native-project target profile: " + context.request.target_profile);
    }
    if (!context.result.ok()) {
        return;
    }
    const auto written = write_native_project(prepared);
    append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                              std::nullopt, context.request.loss_policy);
    append_conversion_output(context, prepared, std::nullopt, ".wormedit", written.value, nullptr);
}

void write_bms_outputs(conversion_context_t& context, const song_document_t& prepared,
                       const std::vector<chart_id_t>& charts) {
    const auto player = bms_write_options_t::from_profile(context.request.target_profile);
    if (player && context.request.bms_profile && player->profile != *context.request.bms_profile) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
            conversion_diagnostic_code_t::invalid_request, conversion_stage_t::request,
            context.request.target, context.request.chart, "Conflicting BMS player profiles");
        return;
    }
    for (const auto chart_id : charts) {
        if (!context.can_continue()) return;
        auto options = player.value_or(bms_write_options_t{});
        options.profile = context.request.bms_profile.value_or(options.profile);
        options.chart = chart_id;
        options.preserve_unchanged_source = context.request.staged_assets.empty();
        options.branch_policy = context.request.branches == branch_projection_t::preserve_tree
                                    ? bms_branch_policy_t::preserve_tree
                                    : bms_branch_policy_t::flatten_active;
        options.output_encoding = context.request.encoding;
        if (!context.request.target_profile.empty() && !player &&
            selected_chart(prepared, chart_id)->source_family != format_family_t::bmson) {
            options.lane_profile_id = context.request.target_profile;
        }
        const auto written = write_bms(prepared, std::move(options));
        append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                                  chart_id, context.request.loss_policy);
        const auto* chart = selected_chart(prepared, chart_id);
        const auto profile = (context.request.target_profile.empty() || player) && !chart->lane_layouts.empty()
                                 ? chart->lane_layouts.front().layout.profile_id
                                 : context.request.target_profile;
        const auto extension = context.request.bms_output_extension.empty()
                                   ? std::string(bms_extension(profile))
                                   : context.request.bms_output_extension;
        append_conversion_output(context, prepared, chart_id, extension, written.value, chart);
    }
}

auto resolve_stepmania_options(conversion_context_t& context)
    -> std::optional<stepmania_export_policy_outcome_t> {
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
    auto resolved = stepmania_write_options_t::from(*context.request.stepmania);
    if (!resolved) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, context.request.target, context.request.chart,
                       resolved.error().message);
        return std::nullopt;
    }
    return resolved;
}

void append_native_report(conversion_context_t& context, osbx_osb_report_t report) {
    if (report.classification != osbx_osb_class_t::exact)
        add_diagnostic(context.result, diagnostic_severity_t::warning,
            conversion_diagnostic_code_t::target_diagnostic, conversion_stage_t::serialize,
            format_family_t::stepmania, context.request.chart,
            "Storyboard " + report.node_id + ": " + report.reason, report.node_id);
    context.result.value.stepmania_storyboard_report.push_back(std::move(report));
}
auto native_scene(const storyboard_document_t& storyboard) -> const osbx_document_t& {
    const auto prepared = osbx_lua_prepared_scope_t::find(*storyboard.osbx);
    return prepared ? *prepared : *storyboard.osbx;
}
void refuse_native_scene(conversion_context_t& context, const storyboard_document_t& storyboard,
    std::string reason) {
    for (const auto& node : native_scene(storyboard).nodes) {
        if (node.kind == osbx_node_kind_t::group) continue;
        append_native_report(context, {.node_id = std::to_string(storyboard.id.value) + "/" + node.id,
            .reason = reason});
    }
}

struct native_activation_t {
    signed_rational_t beat;
    std::int64_t time_ms{};
};
auto native_activation(const song_document_t& song, const osbx_osb_span_t& span)
    -> result_t<native_activation_t, std::string> {
    const auto charts = song.read_charts();
    if (charts.empty()) return std::unexpected("Native storyboard activation needs a chart");
    const auto timing = timing_index_t::from(*charts.front());
    if (!timing.value || timing.value->rows.empty()) return std::unexpected("Native storyboard song timing is unavailable");
    const auto& first = timing.value->rows.front();
    const auto earliest = std::min(static_cast<long double>(span.begin_time_ms) / 1000,
        to_long_double(first.before_delays_seconds)) - 1;
    constexpr auto clock_limit_seconds = stepmania_storyboard_options_t::maximum_music_time_ms / 1000 - 1;
    if (!std::isfinite(earliest) || earliest < -clock_limit_seconds || earliest > clock_limit_seconds)
        return std::unexpected("Native storyboard activation is outside its bounded music clock");
    const auto query = timing.value->row_at({static_cast<std::int64_t>(std::floor(earliest * 1000)), 1000},
        {.extrapolate_before_chart = true});
    if (!query) return std::unexpected("Native storyboard activation has no unambiguous preroll position");
    const auto beat = timing_arithmetic::multiply(*query.absolute_row, {4, charts.front()->divisions_per_standard_measure});
    if (!beat) return std::unexpected("Native storyboard activation beat exceeds exact bounds");
    const auto integer = static_cast<std::int64_t>(std::floor(to_long_double(*beat)));
    const auto seconds = to_long_double(first.before_delays_seconds) +
        static_cast<long double>(integer) * 60 / to_long_double(first.effective_bpm);
    if (!std::isfinite(seconds) || seconds < -clock_limit_seconds || seconds > clock_limit_seconds)
        return std::unexpected("Native storyboard activation duration is outside its bounded music clock");
    return native_activation_t{.beat = {integer, 1},
        .time_ms = static_cast<std::int64_t>(std::floor(seconds * 1000)) - 1};
}
void mark_native_layer_loss(conversion_context_t& context, const storyboard_document_t& storyboard,
    bool foreground, std::string reason) {
    for (const auto& node : native_scene(storyboard).nodes) {
        if ((node.layer == osbx_layer_t::foreground) != foreground) continue;
        const auto id = std::to_string(storyboard.id.value) + "/" + node.id;
        const auto report = std::ranges::find(context.result.value.stepmania_storyboard_report, id, &osbx_osb_report_t::node_id);
        if (report != context.result.value.stepmania_storyboard_report.end()) {
            report->classification = osbx_osb_class_t::unsupported;
            report->reason = reason;
        }
    }
    add_diagnostic(context.result, diagnostic_severity_t::warning, conversion_diagnostic_code_t::target_diagnostic,
        conversion_stage_t::serialize, format_family_t::stepmania, context.request.chart, std::move(reason));
}
struct native_bundle_t {
    std::array<std::string, 2> layers;
    std::set<std::filesystem::path> images;
    std::vector<conversion_artifact_t> files;
    std::size_t bytes{};
};
void append_notitg_files(conversion_context_t& context, const song_document_t& song,
    const storyboard_document_t& storyboard, native_bundle_t& bundle,
    const stepmania_storyboard_export_t& emitted) {
    if (has_stepmania_storyboard_hook(song, true)) return;
    std::size_t bytes{};
    for (const auto& file : emitted.files) bytes += file.bytes.size();
    const auto limit = context.limits.maximum_total_output_bytes / 2;
    if (bytes + 256 > limit - std::min(bundle.bytes, limit)) {
        mark_native_layer_loss(context, storyboard, true, "NotITG XML/Lua bundle exceeds aggregate byte budget; chart kept unchanged");
        return;
    }
    bundle.bytes += bytes + 256;
    for (const auto& file : emitted.files) {
        if (!file.actor_reference.empty()) {
            const auto relative = std::filesystem::path{file.actor_reference}.lexically_relative("wormedit-storyboard");
            bundle.layers[1] += "    <Layer File=\"" + relative.generic_string() + "\" />\n";
        }
        bundle.files.push_back({.relative_path = file.relative_path, .bytes = file.bytes});
        for (const auto& image : file.images) bundle.images.emplace(image);
    }
}
void append_native_scene(conversion_context_t& context, const song_document_t& song,
    const storyboard_document_t& storyboard, native_bundle_t& bundle, native_activation_t activation) {
    const auto& request = context.request;
    const auto notitg = request.stepmania->implementation_profile == stepmania_implementation_profile_t::notitg_v4;
    const auto emitted = emit_stepmania_storyboard(*storyboard.osbx,
        {.playback_span = *request.osbx_playback_span, .activation_time_ms = activation.time_ms,
         .conditional_layer = request.stepmania->conditional_layer,
         .implementation_profile = request.stepmania->implementation_profile,
         .relative_directory = notitg ? "wormedit-storyboard/scene-" + std::to_string(storyboard.id.value) : "wormedit-storyboard",
         .image_sizes = request.osbx_image_sizes,
         .maximum_output_bytes = std::min(std::size_t{8U*1024U*1024U}, context.limits.maximum_total_output_bytes / 2)});
    const auto conflict = [&](const osbx_osb_report_t& report) {
        const auto& scene = native_scene(storyboard);
        const auto found = std::ranges::find(scene.nodes, report.node_id, &osbx_node_t::id);
        return found != scene.nodes.end() && has_stepmania_storyboard_hook(song, found->layer == osbx_layer_t::foreground);
    };
    for (auto report : emitted.value.report) {
        if (conflict(report)) { report.classification = osbx_osb_class_t::unsupported; report.reason = "Retained source owns this BGCHANGES/FGCHANGES hook; chart kept unchanged"; }
        report.node_id = std::to_string(storyboard.id.value) + "/" + report.node_id;
        const auto failure = std::ranges::find(request.stepmania_storyboard_report, report.node_id, &osbx_osb_report_t::node_id);
        if (failure != request.stepmania_storyboard_report.end()) report = *failure;
        append_native_report(context, std::move(report));
    }
    if (notitg) {
        append_notitg_files(context, song, storyboard, bundle, emitted.value);
        return;
    }
    for (const auto& file : emitted.value.files) {
        if (has_stepmania_storyboard_hook(song, file.foreground)) continue;
        auto& layer = bundle.layers[file.foreground ? 1 : 0];
        if (file.bytes.size() + 64 > context.limits.maximum_total_output_bytes / 2 -
                std::min(layer.size(), context.limits.maximum_total_output_bytes / 2)) {
            mark_native_layer_loss(context, storyboard, file.foreground, "Native actor bundle exceeds aggregate byte budget; chart kept unchanged");
            continue;
        }
        layer += "layers[#layers + 1] = (function()\n" + file.bytes + "\nend)()\n";
        for (const auto& image : file.images) bundle.images.emplace(image);
    }
}
void finish_notitg_bundle(conversion_context_t& context, native_bundle_t& bundle,
    stepmania_write_options_t& options, native_activation_t activation) {
    if (!bundle.layers[1].empty()) {
        const auto keeper = (static_cast<double>(stepmania_storyboard_options_t::maximum_music_time_ms) - activation.time_ms) / 1000 + 1;
        auto xml = "<ActorFrame OnCommand=\"sleep," + std::to_string(keeper) + "\">\n  <children>\n";
        xml += bundle.layers[1] + "  </children>\n</ActorFrame>\n";
        bundle.files.push_back({.relative_path = "wormedit-storyboard/main.xml", .bytes = std::move(xml)});
        options.storyboard_changes.push_back({.start_beat = activation.beat,
            .actor_reference = "wormedit-storyboard/main.xml", .foreground = true});
    }
    context.result.value.storyboard_files = std::move(bundle.files);
    context.result.value.storyboard_images.assign(bundle.images.begin(), bundle.images.end());
}
auto has_conditional_storyboard_drawables(const song_document_t& song) -> bool {
    return std::ranges::any_of(song.storyboards, [](const auto& storyboard) {
        if (!storyboard.osbx) return false;
        return std::ranges::any_of(native_scene(storyboard).nodes, [](const auto& node) {
            if (node.kind != osbx_node_kind_t::sprite && node.kind != osbx_node_kind_t::animation) return false;
            const auto layer = static_cast<std::size_t>(node.layer);
            return layer < osbx_layer_names.size() &&
                !is_storyboard_layer_visible(osbx_layer_names[layer], storyboard_conditional_layer_t::neither);
        });
    });
}
void append_etterna_conditional_advisory(conversion_context_t& context, const song_document_t& song) {
    const auto& policy = *context.request.stepmania;
    if (policy.implementation_profile != stepmania_implementation_profile_t::etterna_0_75_1 ||
        policy.storyboard != stepmania_storyboard_mode_t::actors || !has_conditional_storyboard_drawables(song)) return;
    const auto pass = osbx_layer_names[static_cast<std::size_t>(osbx_layer_t::pass)];
    const auto fail = osbx_layer_names[static_cast<std::size_t>(osbx_layer_t::fail)];
    const auto name = is_storyboard_layer_visible(pass, policy.conditional_layer) ? pass :
        is_storyboard_layer_visible(fail, policy.conditional_layer) ? fail : std::string_view{"Neither"};
    add_diagnostic(context.result, diagnostic_severity_t::warning, conversion_diagnostic_code_t::target_diagnostic,
        conversion_stage_t::serialize, format_family_t::stepmania, context.request.chart,
        "Etterna storyboard conditional state " + std::string{name} +
            " is fixed for export; no gameplay-trigger interactivity is inferred.");
}
void prepare_native_storyboard(conversion_context_t& context, const song_document_t& song,
    stepmania_write_options_t& options) {
    const auto& request = context.request;
    if (!request.stepmania || request.stepmania->storyboard == stepmania_storyboard_mode_t::none) return;
    append_etterna_conditional_advisory(context, song);
    std::string refusal;
    if (request.stepmania->storyboard == stepmania_storyboard_mode_t::video)
        refusal = "StepMania video route is not yet available; no video was exported";
    else if (options.profile != stepmania_profile_t::ssc &&
             options.target_implementation_profile != stepmania_implementation_profile_t::notitg_v4)
        refusal = "Native actors require SM5.1/ITGmania/Etterna SSC or the conservative NotITG SM dialect";
    else if (!request.osbx_playback_span) refusal = "Native actor export needs a prepared song playback domain";
    auto activation = refusal.empty() ? native_activation(song, *request.osbx_playback_span)
        : result_t<native_activation_t, std::string>{std::unexpected(refusal)};
    native_bundle_t bundle;
    for (const auto& storyboard : song.storyboards) {
        if (!storyboard.osbx) continue;
        if (!activation) refuse_native_scene(context, storyboard, activation.error());
        else append_native_scene(context, song, storyboard, bundle, *activation);
    }
    if (!activation) return;
    if (options.target_implementation_profile == stepmania_implementation_profile_t::notitg_v4) {
        finish_notitg_bundle(context, bundle, options, *activation);
        return;
    }
    const auto lifetime = (static_cast<double>(request.osbx_playback_span->end_time_ms) - activation->time_ms) / 1000 + 1;
    for (std::size_t layer = 0; layer < bundle.layers.size(); ++layer) {
        if (bundle.layers[layer].empty()) continue;
        const auto reference = std::string{"wormedit-storyboard/"} + (layer == 1 ? "foreground" : "background");
        auto text = "local layers = {}\n" + std::move(bundle.layers[layer]);
        text += "return Def.ActorFrame{\n  OnCommand = function(self)\n    self:stoptweening()\n    self:sleep(" + std::to_string(lifetime) + ")\n  end,\n  unpack(layers)\n}\n";
        context.result.value.storyboard_files.push_back({.relative_path = reference + "/default.lua", .bytes = std::move(text)});
        options.storyboard_changes.push_back({.start_beat = activation->beat, .actor_reference = reference, .foreground = layer == 1});
    }
    context.result.value.storyboard_images.assign(bundle.images.begin(), bundle.images.end());
}
void drop_native_artifacts(conversion_context_t& context) {
    context.result.value.storyboard_files.clear();
    context.result.value.storyboard_images.clear();
    for (auto& report : context.result.value.stepmania_storyboard_report) {
        report.classification = osbx_osb_class_t::unsupported;
        report.reason = "Native storyboard exceeds package byte budget; chart kept unchanged";
    }
    add_diagnostic(context.result, diagnostic_severity_t::warning, conversion_diagnostic_code_t::target_diagnostic,
        conversion_stage_t::serialize, format_family_t::stepmania, context.request.chart,
        "Native storyboard exceeds package byte budget; chart kept unchanged");
}

auto write_stepmania_output(conversion_context_t& context, const song_document_t& prepared)
    -> std::optional<stepmania_export_policy_outcome_t> {
    auto resolved = resolve_stepmania_options(context);
    if (!resolved.has_value()) {
        return std::nullopt;
    }
    auto options = resolved->value();
    options.chart = context.request.chart;
    options.allow_timing_approximation = true;
    prepare_native_storyboard(context, prepared, options);
    auto written = write_stepmania(prepared, options);
    std::size_t artifact_bytes{};
    for (const auto& file : context.result.value.storyboard_files) artifact_bytes += file.bytes.size();
    const auto native_writer_limit = !written.value && !options.storyboard_changes.empty() &&
        std::ranges::any_of(written.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == stepmania_write_diagnostic_code_t::output_limit;
        });
    if (native_writer_limit || (written.value && artifact_bytes > context.limits.maximum_total_output_bytes -
            std::min(written.value->size(), context.limits.maximum_total_output_bytes))) {
        drop_native_artifacts(context);
        options.storyboard_changes.clear();
        written = write_stepmania(prepared, options);
    }
    append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                              context.request.chart, context.request.loss_policy);
    const auto extension =
        context.request.stepmania->file_profile == stepmania_profile_t::sm ? ".sm" : ".ssc";
    append_conversion_output(
        context, prepared, context.request.chart, extension, written.value,
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

auto append_osbx_output(conversion_context_t& context, const song_document_t& prepared) -> bool {
    const auto media = storyboard_media_plan_t::from(prepared, {}, context.execution.stop);
    if (!context.can_continue()) return false;
    if (!media) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
            conversion_diagnostic_code_t::output_limit, conversion_stage_t::serialize,
            context.request.target, std::nullopt, media.error());
        return false;
    }
    const auto written = write_osu_storyboard(prepared,
        {.osbx = {.image_sizes = context.request.osbx_image_sizes,
                  .playback_span = context.request.osbx_playback_span}});
    append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                              std::nullopt, context.request.loss_policy);
    if (!written.value) {
        if (written.diagnostics.empty())
            add_diagnostic(context.result, diagnostic_severity_t::error,
                conversion_diagnostic_code_t::target_diagnostic, conversion_stage_t::serialize,
                context.request.target, std::nullopt, "OSBX conversion returned no storyboard");
        return false;
    }
    append_conversion_output(context, prepared, std::nullopt, ".osb", written.value, nullptr);
    std::set<std::filesystem::path> images;
    for (const auto& image : media->images) {
        auto file = image.file;
        std::ranges::replace(file, '\\', '/');
        images.insert(text_util::path_from_utf8(file).lexically_normal());
    }
    context.result.value.storyboard_images.assign(images.begin(), images.end());
    context.result.value.storyboard_samples = media->samples;
    return true;
}

void write_osu_outputs(conversion_context_t& context, const song_document_t& prepared,
                       const std::vector<chart_id_t>& charts) {
    const auto profile = osu_profile(context);
    if (!context.request.target_profile.empty() && !profile.has_value()) {
        return;
    }
    const bool osbx = std::ranges::any_of(prepared.storyboards, [](const auto& storyboard) {
        return storyboard.osbx.has_value();
    });
    if (osbx && !append_osbx_output(context, prepared)) return;
    for (const auto chart_id : charts) {
        if (!context.can_continue()) return;
        const auto written = write_osu_mania(prepared, {.profile = profile, .chart = chart_id,
            .max_rows_per_measure = std::numeric_limits<std::size_t>::max(),
            .embed_storyboard = !osbx});
        append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                                  chart_id, context.request.loss_policy);
        append_conversion_output(context, prepared, chart_id, ".osu", written.value,
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
        if (!context.can_continue()) return;
        const auto written = write_bmson(prepared, {.chart = chart_id});
        append_target_diagnostics(context.result, written.diagnostics, context.request.target,
                                  chart_id, context.request.loss_policy);
        append_conversion_output(context, prepared, chart_id, ".bmson", written.value,
                                 selected_chart(prepared, chart_id));
    }
}

auto serialize_conversion(conversion_context_t& context, const song_document_t& prepared,
                          const std::vector<chart_id_t>& charts)
    -> std::optional<stepmania_export_policy_outcome_t> {
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
                                 const stepmania_export_policy_outcome_t* stepmania_options,
                                 const song_document_t& prepared)
    -> bool {
    if (!context.result.ok()) {
        context.result.value.outputs.clear();
        return false;
    }
    if (context.request.loss_policy == loss_policy_t::reject &&
        std::ranges::any_of(context.result.diagnostics, &conversion_diagnostic_t::loss)) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::loss_rejected, conversion_stage_t::serialize,
                       context.request.target, context.request.chart,
                       "Conversion produced a loss diagnostic under the reject policy");
        context.result.value.outputs.clear();
        return false;
    }
    if (context.result.value.outputs.empty()) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::serialize, context.request.target, context.request.chart,
                       "Target writer produced no conversion output");
        return false;
    }
    std::size_t total_bytes{};
    for (const auto& file : context.result.value.storyboard_files) total_bytes += file.bytes.size();
    for (const auto& output : context.result.value.outputs) {
        if (output.bytes.size() > context.limits.maximum_total_output_bytes - total_bytes) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::output_limit,
                           conversion_stage_t::serialize, context.request.target, output.chart,
                           "Conversion output set exceeds its configured byte limit");
            context.result.value.outputs.clear();
            return false;
        }
        total_bytes += output.bytes.size();
    }
    for (auto& output : context.result.value.outputs) {
        if (!context.can_continue() || !validate_reimport(context, output, stepmania_options, prepared)) {
            context.result.value.outputs.clear();
            return false;
        }
        output.reimport_validated = true;
        context.advance();
    }
    return true;
}

} // namespace

namespace {
class storyboard_media_builder_t {
    osbx_validation_options_t options_;
    std::stop_token stop_;
    std::size_t remaining_, remaining_bytes_, remaining_nodes_, remaining_source_images_;
    storyboard_media_plan_t plan_;
    std::map<std::pair<std::string,std::string>, std::size_t> images_;
    std::set<std::filesystem::path> samples_;
    auto add_image(const osbx_image_t& image) -> std::size_t {
        const auto key = std::pair{image.id, image.file};
        if (const auto found = images_.find(key); found != images_.end()) return found->second;
        const auto index = plan_.images.size();
        images_.emplace(key, index); plan_.images.push_back(image);
        return index;
    }
    auto path_storage_cost(const osbx_node_t& node,
        const std::map<std::string_view, const osbx_image_t*>& images) const -> std::optional<std::size_t> {
        const auto overhead = sizeof(osbx_image_t) * 2 + sizeof(std::size_t) * 8;
        if (overhead > remaining_bytes_) return std::nullopt;
        const auto maximum_text = (remaining_bytes_ - overhead) / sizeof(std::filesystem::path::value_type);
        std::size_t text = 10; // Maximum uint32 decimal suffix.
        if (text > maximum_text) return std::nullopt;
        if (node.image) {
            if (!checked_math::account_size(text, images.at(*node.image)->file.size(), maximum_text) ||
                !checked_math::account_size(text, node.image->size(), maximum_text)) return std::nullopt;
        } else if (node.sample && !checked_math::account_size(text, node.sample->path.size(), maximum_text)) return std::nullopt;
        return overhead + text * sizeof(std::filesystem::path::value_type);
    }
    auto charge(const osbx_document_t& scene) -> bool {
        if (scene.nodes.size() > remaining_nodes_ || scene.images.size() > remaining_source_images_) return false;
        remaining_nodes_ -= scene.nodes.size(); remaining_source_images_ -= scene.images.size();
        std::map<std::string_view, const osbx_image_t*> images;
        for (const auto& image : scene.images) images.emplace(image.id, &image);
        for (const auto& node : scene.nodes) {
            if (stop_.stop_requested()) return false;
            const auto count = node.animation ? std::size_t{node.animation->frame_count} * 2 + 1 :
                (node.image || node.sample ? std::size_t{2} : std::size_t{0});
            if (count > remaining_) return false;
            if (count == 0) continue;
            const auto bytes = path_storage_cost(node, images);
            if (!bytes || count > remaining_bytes_ / *bytes) return false;
            remaining_ -= count; remaining_bytes_ -= count * *bytes;
        }
        return true;
    }
    auto append_animation(const osbx_image_t& image, const storyboard_animation_t& metadata)
        -> std::optional<std::string> {
        storyboard_animation_assets_t animation{.image = image};
        storyboard_frame_arena_t arena;
        for (std::uint32_t i = 0; i < metadata.frame_count; ++i) {
            if (stop_.stop_requested()) return "OSBX media preparation cancelled";
            arena.reset();
            const auto file = storyboard_animation_t::frame_path(image.file, i, arena);
            if (!file) return "OSBX Animation frame path exceeds its bounds";
            animation.frames.push_back(add_image({.id = image.id, .file = std::string{*file}}));
        }
        plan_.animations.push_back(std::move(animation));
        return std::nullopt;
    }
    void append_static_images(const osbx_document_t& scene) {
        std::set<std::string_view> referenced;
        for (const auto& node : scene.nodes)
            if (node.kind == osbx_node_kind_t::sprite && node.image) referenced.insert(*node.image);
        // Preserve the predecessor's static image/receipt ordering.
        for (const auto& image : scene.images)
            if (referenced.contains(image.id)) add_image(image);
    }
    auto append_scene(const osbx_document_t& scene) -> std::optional<std::string> {
        append_static_images(scene);
        std::map<std::string_view, const osbx_image_t*> images;
        for (const auto& image : scene.images) images.emplace(image.id, &image);
        for (const auto& node : scene.nodes) {
            if (stop_.stop_requested()) return "OSBX media preparation cancelled";
            if (node.kind == osbx_node_kind_t::animation) {
                if (!node.animation || !node.image) return "OSBX Animation metadata is incomplete";
                if (auto failure = append_animation(*images.at(*node.image), *node.animation)) return failure;
            } else if (node.kind == osbx_node_kind_t::sample) {
                if (!node.sample) return "OSBX Sample metadata is incomplete";
                auto path = node.sample->path; std::ranges::replace(path, '\\', '/');
                samples_.insert(text_util::path_from_utf8(path).lexically_normal());
            }
        }
        return std::nullopt;
    }
  public:
    explicit storyboard_media_builder_t(osbx_validation_options_t options, std::stop_token stop)
        : options_(options), stop_(stop), remaining_(options.maximum_images), remaining_bytes_(options.maximum_expanded_storage_bytes),
          remaining_nodes_(options.maximum_nodes), remaining_source_images_(options.maximum_images) {}
    auto build(const song_document_t& song) -> result_t<storyboard_media_plan_t, std::string> {
        if (song.storyboards.size() > options_.maximum_nodes) return std::unexpected("OSBX media scene count exceeds its bounds");
        std::vector<const osbx_document_t*> scenes;
        for (const auto& storyboard : song.storyboards) {
            if (stop_.stop_requested()) return std::unexpected("OSBX media preparation cancelled");
            if (!storyboard.osbx) continue;
            const auto prepared = osbx_lua_prepared_scope_t::find(*storyboard.osbx);
            if (has_osbx_lua_links(*storyboard.osbx) && !prepared) return std::unexpected("OSBX media needs the current prepared Lua scene");
            const auto* scene = prepared ? prepared.get() : &*storyboard.osbx;
            if (!validate_osbx_document(*scene, options_).value) return std::unexpected("Invalid OSBX scene before media preparation");
            if (!charge(*scene)) return std::unexpected(stop_.stop_requested() ?
                "OSBX media preparation cancelled" : "OSBX media enumeration exceeds its aggregate count/work/storage bounds");
            scenes.push_back(scene);
        }
        // ALL scene counts are admitted before generating or allocating frames.
        for (const auto* scene : scenes)
            if (auto failure = append_scene(*scene)) return std::unexpected(*failure);
        plan_.samples.assign(samples_.begin(), samples_.end());
        plan_.logical_storage_bytes = options_.maximum_expanded_storage_bytes - remaining_bytes_;
        return std::move(plan_);
    }
};
} // namespace

auto storyboard_media_plan_t::from(const song_document_t& song, osbx_validation_options_t options, std::stop_token stop)
    -> result_t<storyboard_media_plan_t, std::string> {
    try { return storyboard_media_builder_t{options, stop}.build(song); }
    catch (const std::bad_alloc&) { return std::unexpected("OSBX media planning allocation failed"); }
}

auto conversion_result_t::from(conversion_outcome_t&& outcome) -> conversion_result_t {
    return {.outputs = std::move(outcome.value.outputs),
        .storyboard_images = std::move(outcome.value.storyboard_images),
        .storyboard_samples = std::move(outcome.value.storyboard_samples),
        .storyboard_files = std::move(outcome.value.storyboard_files),
        .stepmania_storyboard_report = std::move(outcome.value.stepmania_storyboard_report),
        .slice_plans = std::move(outcome.value.slice_plans),
        .cut_audio_plans = std::move(outcome.value.cut_audio_plans),
        .asset_copies = std::move(outcome.value.asset_copies), .silent_files = std::move(outcome.value.silent_files),
        .diagnostics = std::move(outcome.diagnostics)};
}

auto conversion_result_t::has_errors() const -> bool {
    return diagnostic_queries::has_errors(diagnostics);
}

conversion_result_t::operator bool() const noexcept {
    return !outputs.empty() && !has_errors();
}

auto is_conversion_needs_slice_spans(const song_document_t& song, const conversion_request_t& request) -> bool {
    if (!has_target_stem_slices(request.target) && song.export_cuts.empty()) return false;
    return std::ranges::any_of(song.charts, [&](const auto& chart) {
        const bool selected = request.chart ? chart.id == *request.chart :
            request.charts.empty() || std::ranges::find(request.charts, chart.id) != request.charts.end();
        return selected && chart.source_family == format_family_t::bmson;
    });
}

namespace {
auto quantize_cut_chart(conversion_context_t& context, chart_document_t& chart) -> bool {
    // 192 rows per 4/4 measure is the traditional StepMania compatibility grid.
    // Reuse quantization's collision and hold validation on the detached export.
    auto quantized = quantization_plan_t::from(chart, {.divisions = 192, .allow_resolution_promotion = true});
    for (const auto& diagnostic : quantized.diagnostics)
        add_diagnostic(context.result, diagnostic.severity, conversion_diagnostic_code_t::transform_failed,
            conversion_stage_t::transform, context.request.target, chart.id, diagnostic.message);
    if (!quantized.value.chart) return false;
    if (!quantized.value.quantized_moves.empty())
        add_diagnostic(context.result, diagnostic_severity_t::warning, conversion_diagnostic_code_t::target_diagnostic,
            conversion_stage_t::transform, context.request.target, chart.id,
            "Export-cut positions were rounded to the StepMania grid (192 rows per measure)", {}, true);
    chart = std::move(*quantized.value.chart);
    return true;
}
auto standardize_cut_chart(conversion_context_t& context, song_document_t& song, chart_document_t& chart) -> bool {
    std::set<measure_id_t> existing;
    for (const auto& measure : chart.measures) existing.insert(measure.id);
    const auto diagnostics = rebar_standard_measures(chart,
        {.maximum_measures = 65'536, .maximum_rows_per_measure = std::numeric_limits<std::uint64_t>::max()});
    for (const auto& diagnostic : diagnostics)
        add_diagnostic(context.result, diagnostic.severity, conversion_diagnostic_code_t::transform_failed,
            conversion_stage_t::transform, context.request.target, chart.id, diagnostic.message);
    if (diagnostic_queries::has_errors(diagnostics)) return false;
    auto identities = id_ceiling_t<measure_id_t>::from(song);
    std::map<measure_id_t, measure_id_t> replacements;
    for (auto& measure : chart.measures) {
        if (existing.contains(measure.id)) continue;
        const auto allocated = identities.next();
        if (!allocated) return false;
        identities.reserve(*allocated);
        replacements.emplace(measure.id, *allocated);
        measure.id = *allocated;
    }
    for_each_chart_position(chart, [&](local_position_t& position, measure_chart_end_policy_t) {
        const auto replacement = replacements.find(position.measure);
        if (replacement != replacements.end()) position.measure = replacement->second;
    });
    return quantize_cut_chart(context, chart);
}
auto standardize_cut_bars(conversion_context_t& context, song_document_t& song,
                          const std::vector<chart_id_t>& charts) -> bool {
    if (context.request.target != format_family_t::stepmania) return true;
    for (const auto id : charts) {
        auto* chart = song.find_chart(id);
        if (!chart || !standardize_cut_chart(context, song, *chart)) return false;
    }
    return true;
}
auto prepare_export_cuts(conversion_context_t& context, song_document_t& prepared,
                         const std::vector<chart_id_t>& charts) -> bool {
    if (prepared.export_cuts.empty()) return true;
    auto transformed = export_cut_transform_t::from(prepared, charts, context.request.export_cut_sources, context.request.audio);
    for (const auto& diagnostic : transformed.diagnostics) {
        add_diagnostic(context.result, diagnostic.severity,
            diagnostic.code == diagnostic_code_t::dropped_feature
                ? conversion_diagnostic_code_t::target_diagnostic : conversion_diagnostic_code_t::transform_failed,
            conversion_stage_t::transform, context.request.target, diagnostic.location.chart,
            diagnostic.message);
        context.result.diagnostics.back().object = diagnostic.location.object;
    }
    if (!transformed.ok() || !transformed.value) return false;
    context.result.value.cut_audio_plans = std::move(transformed.value->audio_plans);
    prepared = std::move(transformed.value->document);
    return standardize_cut_bars(context, prepared, charts);
}
}

namespace {
struct prepared_conversion_t { song_document_t document; std::vector<chart_id_t> charts; };
// osu! finds skin-named samples only beside the chart, under their exact names.
auto prepare_osu_skin_samples(conversion_context_t& context, song_document_t& prepared) -> bool {
    if (context.request.target != format_family_t::osu_mania) return true;
    std::unordered_set<std::string> filenames;
    for (const auto& found : build_osu_skin_sample_assets(prepared)) {
        if (std::ranges::contains(context.result.value.asset_copies, found.asset, &conversion_asset_copy_t::asset))
            continue;
        const auto filename = found.export_filename();
        if (found.missing) {
            add_diagnostic(context.result, diagnostic_severity_t::warning,
                           conversion_diagnostic_code_t::target_diagnostic, conversion_stage_t::transform,
                           context.request.target, std::nullopt,
                           "Missing osu! hitsound sample was not exported: " + filename, {}, true);
            continue;
        }
        if (!filenames.insert(filename).second) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::transform_failed, conversion_stage_t::transform,
                           context.request.target, std::nullopt,
                           "Two sounds would both export as the osu! hitsound sample " + filename);
            return false;
        }
        context.result.value.asset_copies.push_back({.asset = found.asset, .relative_path = filename});
        prepared.find_audio_asset(found.asset)->source_path = filename;
        for (auto& resource : prepared.resources)
            if (resource.asset == found.asset) resource.name = filename;
    }
    return true;
}

template<class Event> auto typed_sample_request(chart_id_t chart, const Event& event)
    -> std::optional<resource_plan_request_t> {
    if (!event.sound || (!event.sample_reference && event.provenance.source_field != typed_lua_osu_sample_origin)) return {};
    resource_plan_request_t request{.chart = chart, .event = event.id, .resource = *event.sound};
    if (event.sample_reference) {
        if (const auto* bms = std::get_if<bms_wav_reference_t>(&event.sample_reference->reference)) {
            request.explicit_bms_index = bms->index;
            request.source_chart = bms->source_chart.value_or(chart);
        } else request.expected_hash = std::get<audio_file_reference_t>(event.sample_reference->reference).content_hash;
    }
    if (event.sound_channel_event) request.compatibility_key = "retained:" + std::to_string(event.sound_channel_event->value);
    return request;
}
auto typed_sample_requests(const song_document_t& song, std::span<const chart_id_t> charts)
    -> std::vector<resource_plan_request_t> {
    std::vector<resource_plan_request_t> requests;
    for (const auto id : charts) {
        const auto* chart = song.read_chart(id);
        if (!chart) continue;
        for (const auto& measure : chart->measures) {
            for (const auto& note : measure.notes)
                if (auto request = typed_sample_request(id, note)) requests.push_back(std::move(*request));
            for (const auto& event : measure.bgm_events)
                if (auto request = typed_sample_request(id, event)) requests.push_back(std::move(*request));
        }
    }
    return requests;
}

// Apply only to the detached conversion document. Preserve one resource per
// promised explicit WAV identity; ordinary compatible references may reuse.
struct typed_sample_plan_applier_t {
    song_document_t& song;
    format_family_t target;
    std::map<std::pair<resource_id_t, std::string>, resource_id_t> assigned;
    std::map<resource_id_t, std::string> first_identifiers;

    auto resource_for(const resource_plan_binding_t& binding) -> result_t<resource_id_t, error_t<failure_code_t>> {
        const auto key = std::pair{binding.target_resource, binding.identifier};
        if (const auto previous = assigned.find(key); previous != assigned.end()) return previous->second;
        auto id = binding.target_resource;
        const auto source = std::ranges::find(song.resources, id, &resource_entry_t::id);
        if (source == song.resources.end()) return std::unexpected(error_t{failure_code_t::not_found, "Planned sound disappeared"});
        if (target == format_family_t::bms && first_identifiers.contains(id) && first_identifiers.at(id) != binding.identifier) {
            const auto next = id_ceiling_t<resource_id_t>::from(song).next();
            if (!next) return std::unexpected(error_t{failure_code_t::capacity_exceeded, "Planned resource IDs exhausted"});
            auto copy = *source; copy.id = *next;
            song.resources.push_back(std::move(copy));
            id = *next;
        }
        first_identifiers.emplace(binding.target_resource, binding.identifier);
        auto resource = std::ranges::find(song.resources, id, &resource_entry_t::id);
        resource->name = binding.filename;
        if (target == format_family_t::bms) {
            std::erase_if(resource->target_identities, [](const auto& identity) { return bms_wav_slot_value(identity).has_value(); });
            resource->target_identities.push_back({.family = format_family_t::bms,
                .identifier = binding.identifier, .namespace_name = "WAV"});
        }
        assigned.emplace(key, id);
        return id;
    }
    auto apply(const resource_plan_binding_t& binding) -> result_t<void, error_t<failure_code_t>> {
        const auto resource = resource_for(binding);
        if (!resource) return std::unexpected(resource.error());
        auto* chart = song.find_chart(binding.chart);
        if (!chart) return std::unexpected(error_t{failure_code_t::not_found, "Planned difficulty disappeared"});
        const auto update = [&](auto& event) {
            if (event.id == binding.event) {
                event.sound = *resource;
                event.sample_reference.reset(); // Promise consumed in this detached export.
            }
        };
        for (auto& measure : chart->measures) {
            for (auto& note : measure.notes) update(note);
            for (auto& event : measure.bgm_events) update(event);
        }
        if (auto* asset = song.find_audio_asset(binding.asset)) asset->source_path = binding.filename;
        return {};
    }
};
auto apply_typed_sample_plan(song_document_t& song, const resource_plan_t& plan, format_family_t target)
    -> result_t<void, error_t<failure_code_t>> {
    typed_sample_plan_applier_t applier{.song = song, .target = target};
    for (const auto& binding : plan.bindings)
        if (const auto result = applier.apply(binding); !result) return result;
    return {};
}

template<class Event> auto lower_typed_osu_sample(song_document_t& song, const song_document_t& source, Event& event,
    const std::optional<osu_sample_context_index_t>& context) -> result_t<void, error_t<failure_code_t>> {
    if (!event.osu_hitsample || event.provenance.source_field != typed_lua_osu_sample_origin) return {};
    if (!context) return std::unexpected(error_t{failure_code_t::invalid_data, "Typed osu! sample context is unavailable"});
    const auto resolved = resolve_osu_sample_resources(source, *event.osu_hitsample, context->at(event.position), event.sound);
    if (!resolved) return std::unexpected(resolved.error());
    if (resolved->size() != 1 || resolved->front().second != 100)
        return std::unexpected(error_t{failure_code_t::unsupported,
            "This typed osu! descriptor needs multiple voices or scaled audio; the target requires one unit-gain registered sound"});
    const auto id = resolved->front().first;
    const auto original = std::ranges::find(source.resources, id, &resource_entry_t::id);
    const auto prepared = std::ranges::find(song.resources, id, &resource_entry_t::id);
    if (original == source.resources.end() || prepared == song.resources.end() ||
        original->asset != prepared->asset || event.sound_channel_event)
        return std::unexpected(error_t{failure_code_t::unsupported, "Typed osu! source is incompatible with the prepared slice/clip identity"});
    event.sound = id;
    event.osu_hitsample.reset(); // Only the detached single-voice target candidate.
    return {};
}
auto lower_chart_typed_osu_samples(song_document_t& song, const song_document_t& source, chart_document_t& chart)
    -> result_t<void, error_t<failure_code_t>> {
    const auto context = osu_sample_context_index_t::from(chart);
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes)
            if (const auto result = lower_typed_osu_sample(song, source, note, context); !result) return result;
        for (auto& event : measure.bgm_events)
            if (const auto result = lower_typed_osu_sample(song, source, event, context); !result) return result;
    }
    return {};
}
auto lower_typed_osu_samples(conversion_context_t& context, song_document_t& song,
    std::span<const chart_id_t> charts, const song_document_t& source) -> bool {
    if (context.request.target == format_family_t::osu_mania) return true;
    for (const auto id : charts) {
        auto* chart = song.find_chart(id);
        if (!chart) continue;
        if (const auto result = lower_chart_typed_osu_samples(song, source, *chart); !result) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                conversion_diagnostic_code_t::unsupported_target, conversion_stage_t::transform,
                context.request.target, id, result.error().message);
            return false;
        }
    }
    return true;
}

auto has_rendered_typed_source(const conversion_payload_t& converted, asset_id_t asset) -> bool {
    if (std::ranges::any_of(converted.cut_audio_plans, [asset](const auto& plan) { return plan.source.asset == asset; })) return true;
    for (const auto& plan : converted.slice_plans)
        if (std::ranges::any_of(plan.value.spans, [asset](const auto& span) { return span.asset == asset; })) return true;
    return false;
}
template<class Event> auto typed_source_was_rendered(const Event& event, const conversion_payload_t& converted) -> bool {
    if (!event.sample_reference) return false;
    return event.sound != event.sample_reference->bound_sound ||
        (std::holds_alternative<audio_file_reference_t>(event.sample_reference->reference) &&
         has_rendered_typed_source(converted, event.sample_reference->bound_asset));
}
auto validate_typed_source_preparation(conversion_context_t& context, const song_document_t& prepared,
    std::span<const chart_id_t> charts) -> bool {
    for (const auto id : charts) {
        const auto* chart = prepared.read_chart(id);
        if (!chart) continue;
        const auto rendered = [&](const auto& event) { return typed_source_was_rendered(event, context.result.value); };
        for (const auto& measure : chart->measures) {
            if (!std::ranges::any_of(measure.notes, rendered) && !std::ranges::any_of(measure.bgm_events, rendered)) continue;
            add_diagnostic(context.result, diagnostic_severity_t::error,
                conversion_diagnostic_code_t::unsupported_target, conversion_stage_t::transform,
                context.request.target, id,
                "Typed source identity/hash cannot be verified against rendered slice or cut output; resolve the source explicitly before this transform");
            return false;
        }
    }
    return true;
}

template<class Event> auto map_typed_osu_custom_path(const song_document_t& source,
    const song_document_t& prepared, Event& event) -> result_t<void, error_t<failure_code_t>> {
    if (!event.osu_hitsample || event.sound || event.provenance.source_field != typed_lua_osu_sample_origin ||
        event.osu_hitsample->filename.empty()) return {};
    auto normal = *event.osu_hitsample;
    normal.whistle = false; normal.finish = false; normal.clap = false;
    const auto resolved = resolve_osu_sample_resources(source, normal);
    if (!resolved) return std::unexpected(resolved.error());
    if (resolved->size() != 1) return std::unexpected(error_t{failure_code_t::unsupported, "Custom normal sample is not one registered source"});
    const auto resource = std::ranges::find(prepared.resources, resolved->front().first, &resource_entry_t::id);
    const auto* asset = resource == prepared.resources.end() ? nullptr : prepared.find_audio_asset(resource->asset);
    if (!asset) return std::unexpected(error_t{failure_code_t::not_found, "Prepared custom sample asset is unavailable"});
    event.osu_hitsample->filename = text_util::path_utf8(asset->source_path);
    return {};
}
auto map_chart_typed_osu_custom_paths(const song_document_t& source, song_document_t& prepared,
    chart_document_t& chart) -> result_t<void, error_t<failure_code_t>> {
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes)
            if (const auto result = map_typed_osu_custom_path(source, prepared, note); !result) return result;
        for (auto& event : measure.bgm_events)
            if (const auto result = map_typed_osu_custom_path(source, prepared, event); !result) return result;
    }
    return {};
}
auto map_typed_osu_custom_paths(conversion_context_t& context, const song_document_t& source,
    song_document_t& prepared, std::span<const chart_id_t> charts) -> bool {
    if (context.request.target != format_family_t::osu_mania) return true;
    for (const auto id : charts) {
        auto* chart = prepared.find_chart(id);
        if (!chart) continue;
        if (const auto result = map_chart_typed_osu_custom_paths(source, prepared, *chart); !result) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                conversion_diagnostic_code_t::transform_failed, conversion_stage_t::transform,
                context.request.target, id, result.error().message);
            return false;
        }
    }
    return true;
}

auto prepare_typed_samples(conversion_context_t& context, song_document_t& prepared,
    std::span<const chart_id_t> charts, const song_document_t& source_profiles) -> bool {
    if (context.request.target != format_family_t::bms && context.request.target != format_family_t::bmson &&
        context.request.target != format_family_t::osu_mania) return true;
    if (!validate_typed_source_preparation(context, prepared, charts) ||
        !map_typed_osu_custom_paths(context, source_profiles, prepared, charts) ||
        !lower_typed_osu_samples(context, prepared, charts, source_profiles)) return false;
    const auto requests = typed_sample_requests(prepared, charts);
    if (requests.empty()) return true;
    const auto plan = resource_plan_t::from(prepared, context.request.target, requests, &source_profiles);
    if (!plan.value) {
        for (const auto& diagnostic : plan.diagnostics)
            add_diagnostic(context.result, diagnostic_severity_t::error,
                conversion_diagnostic_code_t::transform_failed, conversion_stage_t::transform,
                context.request.target, diagnostic.location.chart, diagnostic.message);
        return false;
    }
    const auto applied = apply_typed_sample_plan(prepared, *plan.value, context.request.target);
    if (!applied) {
        add_diagnostic(context.result, diagnostic_severity_t::error,
            conversion_diagnostic_code_t::transform_failed, conversion_stage_t::transform,
            context.request.target, std::nullopt, applied.error().message);
        return false;
    }
    for (const auto& binding : plan.value->bindings) {
        auto existing = std::ranges::find(context.result.value.asset_copies, binding.asset, &conversion_asset_copy_t::asset);
        if (existing != context.result.value.asset_copies.end() && (existing->relative_path != binding.filename || (existing->expected_hash && binding.expected_hash &&
            !text_util::ascii_iequal(*existing->expected_hash, *binding.expected_hash)))) {
            add_diagnostic(context.result, diagnostic_severity_t::error,
                conversion_diagnostic_code_t::transform_failed, conversion_stage_t::transform,
                context.request.target, binding.chart, "Typed audio filename conflicts with an existing output copy");
            return false;
        }
        if (existing == context.result.value.asset_copies.end())
            context.result.value.asset_copies.push_back({.asset = binding.asset, .relative_path = binding.filename,
                .expected_hash = binding.expected_hash});
        else if (binding.expected_hash) existing->expected_hash = binding.expected_hash;
    }
    return true;
}

auto prepare_conversion(conversion_context_t& context, const song_document_t& song,
                        bool diagnostics_only = false) -> std::optional<prepared_conversion_t> {
    auto normalized = song;
    normalize_sample_references(normalized);
    if (!context.can_continue() || !validate_request_policies(context, normalized, diagnostics_only)) {
        return {};
    }
    const auto charts = selected_chart_ids(context, normalized);
    if (!context.result.ok() || !validate_selected_target(context, normalized, charts)) {
        return {};
    }
    if (context.execution.progress) {
        context.execution.progress->phase.store(job_phase_t::preparing);
        context.execution.progress->completed.store(0);
        const auto count = context.request.target == format_family_t::native_project ||
                                   context.request.target == format_family_t::stepmania ? 1 : charts.size();
        context.execution.progress->total.store(count * 2);
    }
    auto prepared = prepare_document(context, normalized, charts);
    if (!prepared || !context.result.ok()) {
        context.result.value.outputs.clear();
        return {};
    }
    prepare_bms_osu_layouts(*prepared, context.request, charts);
    if (!prepare_slice_exports(context, *prepared, charts) || !prepare_export_cuts(context, *prepared, charts) ||
        !prepare_osu_skin_samples(context, *prepared) || !prepare_typed_samples(context, *prepared, charts, normalized)) {
        context.result.value = {};
        return {};
    }
    if (!context.can_continue()) {
        context.result.value = {};
        return {};
    }
    return prepared_conversion_t{std::move(*prepared), charts};
}
}

auto diagnose_conversion_preparation(const song_document_t& song, const conversion_request_t& request,
    conversion_execution_t execution) -> std::vector<conversion_diagnostic_t> {
    conversion_outcome_t result;
    const conversion_limits_t limits;
    conversion_context_t context{request, limits, result, execution};
    (void)prepare_conversion(context, song, true);
    return std::move(result.diagnostics);
}

auto conversion_result_t::from(const song_document_t& song, const conversion_request_t& request,
                      conversion_limits_t limits, conversion_execution_t execution) -> conversion_outcome_t {
    conversion_outcome_t result{};
    const auto prepared_links = std::ranges::all_of(request.osbx_prepared, [&](const auto& scene) { return scene.stamp.is_current(song); })
        ? std::span<const osbx_lua_prepared_scene_t>{request.osbx_prepared} : std::span<const osbx_lua_prepared_scene_t>{};
    const osbx_lua_prepared_scope_t link_scope{prepared_links};
    conversion_context_t context{request, limits, result, execution};
    auto prepared = prepare_conversion(context, song);
    if (!prepared) return result;
    auto stepmania_options = serialize_conversion(context, prepared->document, prepared->charts);
    if (!validate_conversion_outputs(context, stepmania_options ? &*stepmania_options : nullptr, prepared->document)) {
        result.value = {};
        return result;
    }
    return result;
}

auto convert_document(const song_document_t& song, const conversion_request_t& request,
    conversion_limits_t limits, conversion_execution_t execution) -> conversion_result_t {
    return conversion_result_t::from(conversion_result_t::from(song, request, limits, execution));
}

} // namespace wormedit
