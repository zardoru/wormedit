#include "quaver_internal.h"
#include "../core/text_util.h"
#include <wormedit/core/song_preview.h>
#include <algorithm>
#include <map>
#include <sstream>

namespace wormedit {
namespace {
using namespace quaver_detail;
struct measure_time_t {
    double begin{};
    double end{};
    double bpm{};
    std::uint64_t rows{};
};
struct quaver_reader_t {
    YAML::Node root;
    const quaver_parse_options_t& options;
    song_document_t song;
    format_detail::id_source_t ids;
    std::vector<measure_time_t> geometry;
    std::vector<resource_id_t> samples;
    std::map<measure_id_t, std::size_t> measure_slots;
    std::map<double, double> tempos;
    std::vector<quaver_diagnostic_t> diagnostics;

    auto chart() -> chart_document_t& { return song.charts.front(); }
    void warn(code_t code, std::string message) {
        diagnostics.push_back({.severity = diagnostic_severity_t::warning, .code = code, .message = std::move(message)});
    }
    auto position(double time) const -> local_position_t {
        const auto after = std::upper_bound(geometry.begin(), geometry.end(), time,
            [](double value, const auto& entry) { return value < entry.begin; });
        if (after == geometry.begin()) fail(code_t::invalid_value, "Time precedes the chart geometry");
        const auto index = static_cast<std::size_t>(std::distance(geometry.begin(), after) - 1);
        const auto& span = geometry[index];
        const auto row = nearest_row((time - span.begin) * span.rows / (span.end - span.begin),
            span.rows + 1, row_clamp_policy_t::reject_outside);
        if (!row) fail(code_t::invalid_value, "Time lies outside chart geometry");
        if (*row == span.rows && index + 1 < geometry.size()) return {chart_ref().measures[index + 1].id, 0};
        return {chart_ref().measures[index].id, *row};
    }
    auto chart_ref() const -> const chart_document_t& { return song.charts.front(); }
    auto timed_source(YAML::Node node, local_position_t at) -> source_provenance_t {
        auto copy = YAML::Clone(node);
        retain_position(copy, at);
        return provenance(copy);
    }
    auto extent() const -> std::pair<double, double> {
        double minimum = std::min(0.0, number(root["TimingPoints"][0], "StartTime"));
        double maximum = minimum;
        for (const auto* key : {"HitObjects", "TimingPoints", "SliderVelocities", "ScrollSpeedFactors", "SoundEffects", "Bookmarks"}) {
            for (const auto& node : root[key]) {
                minimum = std::min(minimum, number(node, "StartTime"));
                maximum = std::max({maximum, number(node, "StartTime"), number(node, "EndTime")});
            }
        }
        return {minimum, maximum};
    }
    void append_measure(double begin, double end, double bpm, unsigned signature) {
        if (geometry.size() >= options.max_measures) fail(code_t::measure_limit, "Derived measure limit exceeded");
        constexpr std::uint64_t divisions = 960'000;
        const auto ideal = (end - begin) * bpm / 240'000.0 * divisions;
        const auto row = nearest_row(ideal, UINT32_MAX, row_clamp_policy_t::reject_outside);
        if (!row || *row == 0) fail(code_t::invalid_value, "Timing interval is too short or too long");
        measure_chunk_t measure;
        measure.id = ids.take<measure_id_t>();
        measure.definition = {.length = rows_to_fraction(*row, divisions)};
        if (*row == divisions * signature / 4) measure.definition.meter = meter_t{signature, 4};
        measure_slots.emplace(measure.id, geometry.size());
        chart().measures.push_back(std::move(measure));
        const auto realized_bpm = static_cast<double>(*row) * 240'000.0 / (divisions * (end - begin));
        geometry.push_back({begin, end, realized_bpm, *row});
        if (std::abs(ideal - static_cast<double>(*row)) > 1e-6)
            warn(code_t::quantization, "Partial timing measure uses an adjusted BPM to preserve its duration");
    }
    void build_geometry() {
        const auto [minimum, maximum] = extent();
        const auto points = root["TimingPoints"];
        double begin = minimum;
        double bpm = number(points[0], "Bpm");
        unsigned signature = 4;
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto point = points[index];
            const auto time = number(point, "StartTime");
            if (time > begin) append_span(begin, time, bpm, signature);
            begin = time;
            bpm = number(point, "Bpm");
            const auto name = field<std::string>(point, "Signature", "Quadruple");
            signature = name == "Triple" || name == "3" ? 3U : 4U;
        }
        const auto tail = std::max(maximum, begin) + 60'000.0 / bpm * signature;
        append_span(begin, tail, bpm, signature);
        chart().beat_zero_audio_offset_seconds = rational(minimum / 1000.0);
    }
    void append_span(double begin, double end, double bpm, unsigned signature) {
        const auto duration = 60'000.0 / bpm * signature;
        while (begin < end - 1e-8) {
            const auto next = std::min(end, begin + duration);
            append_measure(begin, next, bpm, signature);
            begin = next;
        }
    }
    void build_layout() {
        auto& target = chart();
        const auto keys = key_count(root);
        const auto scratch = field<bool>(root, "HasScratchKey");
        lane_layout_t layout{.profile_id = "quaver-" + std::to_string(keys) + (scratch ? "+scratch" : ""),
            .display_name = "Quaver " + std::to_string(keys) + "K" + (scratch ? "+scratch" : "")};
        for (unsigned column = 0; column < keys + (scratch ? 1U : 0U); ++column) {
            auto lane = lane_definition_t::from(ids.take<lane_id_t>().value, column, stage_group_t::left);
            if (scratch && column == keys) lane.role = lane_role_t::scratch;
            layout.lanes.push_back(std::move(lane));
        }
        target.lane_layouts.push_back({.id = ids.take<object_id_t>(), .position = {target.measures.front().id, 0}, .layout = std::move(layout)});
    }
    auto add_audio(std::string path, resource_role_t role, YAML::Node source) -> resource_id_t {
        const auto asset = ids.take<asset_id_t>();
        const auto resource = ids.take<resource_id_t>();
        auto file = text_util::path_from_utf8(path);
        if (options.asset_base_directory) file = *options.asset_base_directory / file;
        std::error_code error;
        const auto exists = options.asset_base_directory && std::filesystem::is_regular_file(file, error);
        song.audio_assets.push_back({.id = asset, .source_path = file});
        auto retained_source = YAML::Clone(source);
        retained_source["_path"] = text_util::path_utf8(file);
        song.resources.push_back({.id = resource, .name = std::move(path), .asset = asset,
            .roles = {role}, .missing = !exists, .provenance = provenance(retained_source)});
        return resource;
    }
    void build_resources() {
        for (auto sample : root["CustomAudioSamples"])
            samples.push_back(add_audio(field<std::string>(sample, "Path"), resource_role_t::sample, sample));
        const auto audio = field<std::string>(root, "AudioFile");
        if (!audio.empty()) {
            YAML::Node source;
            source["AudioFile"] = audio;
            (void)add_audio(audio, resource_role_t::music, source);
        }
        for (const auto* key : {"BackgroundFile", "BannerFile"}) {
            const auto path = field<std::string>(root, key);
            if (path.empty()) continue;
            const auto asset = ids.take<asset_id_t>();
            auto file = text_util::path_from_utf8(path);
            if (options.asset_base_directory) file = *options.asset_base_directory / file;
            const auto resource = ids.take<resource_id_t>();
            root["_visual"][key]["asset"] = asset.value;
            root["_visual"][key]["resource"] = resource.value;
            root["_visual"][key]["path"] = text_util::path_utf8(file);
            song.visual_assets.push_back({.id = asset, .source_path = std::move(file)});
            song.resources.push_back({.id = resource, .name = path, .asset = asset,
                .roles = {resource_role_t::bga}, .missing = true});
        }
    }
    void build_layers() {
        for (auto layer : root["EditorLayers"]) {
            chart().semantic_layers.push_back({.id = ids.take<layer_id_t>(),
                .name = field<std::string>(layer, "Name"),
                .color = color(field<std::string>(layer, "ColorRgb", "255,255,255")),
                .visible = !field<bool>(layer, "Hidden")});
        }
    }
    void build_notes() {
        const auto& lanes = chart().lane_layouts.front().layout.lanes;
        for (auto node : root["HitObjects"]) {
            note_t note{.id = ids.take<object_id_t>(), .position = position(number(node, "StartTime")),
                .lane = lanes[field<unsigned>(node, "Lane") - 1].id};
            auto source = YAML::Clone(node);
            retain_position(source, note.position);
            for (auto sample : source["KeySounds"])
                sample["_resource"] = samples[field<std::size_t>(sample, "Sample") - 1].value;
            if (const auto end = number(node, "EndTime"); end != 0) {
                note.kind = note_kind_t::hold;
                note.end = position(end);
                retain_position(source, *note.end, "_end_");
            }
            if (field<std::string>(node, "Type", "Normal") == "Mine") note.kind = note_kind_t::mine;
            const auto flags = field<int>(node, "HitSound", 1);
            note.osu_hitsample = osu_hitsample_t{.whistle = (flags & 2) != 0, .finish = (flags & 4) != 0, .clap = (flags & 8) != 0};
            if (node["KeySounds"].size() != 0) note.sound = samples[field<std::size_t>(node["KeySounds"][0], "Sample") - 1];
            if (!chart().semantic_layers.empty()) note.semantic_layers.push_back(chart().semantic_layers[field<std::size_t>(node, "EditorLayer")].id);
            note.provenance = provenance(source);
            auto& measure = chart().measures[measure_slots.at(note.position.measure)];
            measure.notes.push_back(std::move(note));
        }
    }
    void build_timing() {
        for (auto node : root["TimingPoints"]) {
            const auto at = position(number(node, "StartTime"));
            auto source = YAML::Clone(node);
            retain_position(source, at);
            const auto index = measure_slots.at(at.measure);
            const auto bpm = geometry[index].bpm;
            source["_bpm"] = bpm;
            chart().bpms.push_back({.id = ids.take<object_id_t>(), .position = at,
                .beats_per_minute = rational(bpm), .provenance = provenance(source)});
        }
        // Prefix measures use the first timing point's BPM, as Quaver does.
        if (chart().bpms.front().position != local_position_t{chart().measures.front().id, 0})
            chart().bpms.insert(chart().bpms.begin(), {.id = ids.take<object_id_t>(),
                .position = {chart().measures.front().id, 0}, .beats_per_minute = rational(geometry.front().bpm),
                .provenance = {.family = format_family_t::quaver, .source_field = "prefix"}});
        std::map<measure_id_t, bool> anchored;
        for (const auto& event : chart().bpms) anchored[event.position.measure] = true;
        for (std::size_t i = 0; i < geometry.size(); ++i) {
            const local_position_t at{chart().measures[i].id, 0};
            if (!anchored.contains(at.measure) && std::abs(geometry[i].bpm - geometry[i == 0 ? 0 : i - 1].bpm) > 1e-8)
                chart().bpms.push_back({.id = ids.take<object_id_t>(), .position = at,
                    .beats_per_minute = rational(geometry[i].bpm)});
        }
    }
    auto bpm_at(double time) const -> double {
        const auto after = tempos.upper_bound(time);
        return after == tempos.begin() ? tempos.begin()->second : std::prev(after)->second;
    }
    void build_scrolls() {
        const auto normalized = field<bool>(root, "BPMDoesNotAffectScrollVelocity");
        const auto base = number(root["TimingPoints"][0], "Bpm");
        for (const auto& node : root["TimingPoints"]) tempos[number(node, "StartTime")] = number(node, "Bpm");
        std::map<double, YAML::Node> points;
        YAML::Node initial;
        initial["StartTime"] = geometry.front().begin;
        initial["Multiplier"] = normalized ? number(root, "InitialScrollVelocity", 1) : 1;
        initial["_initial"] = true;
        points[geometry.front().begin] = initial;
        if (!normalized) for (const auto& bpm : root["TimingPoints"]) {
            YAML::Node node;
            node["StartTime"] = number(bpm, "StartTime");
            node["_bpm_only"] = true;
            points[number(bpm, "StartTime")] = node;
        }
        for (const auto& node : root["SliderVelocities"]) points[number(node, "StartTime")] = YAML::Clone(node);
        double raw = normalized ? number(root, "InitialScrollVelocity", 1) : 1;
        for (auto& [time, node] : points) {
            if (node["Multiplier"]) raw = number(node, "Multiplier");
            const auto value = raw * (normalized ? 1 : bpm_at(time) / base);
            const auto at = position(time);
            retain_position(node, at);
            node["_effective"] = value;
            node["_tempo"] = bpm_at(time);
            node["_base"] = base;
            chart().scrolls.push_back({.id = ids.take<object_id_t>(), .position = at,
                .multiplier = rational(value), .provenance = provenance(node)});
        }
        for (const auto& node : root["ScrollSpeedFactors"]) {
            const auto at = position(number(node, "StartTime"));
            chart().speeds.push_back({.id = ids.take<object_id_t>(), .position = at,
                .multiplier = rational(number(node, "Multiplier")), .provenance = timed_source(node, at)});
        }
    }
    void build_markers_and_effects() {
        for (auto node : root["Bookmarks"]) {
            const auto id = ids.take<object_id_t>();
            node["_id"] = id.value;
            chart().bookmarks.push_back({.id = id, .label = field<std::string>(node, "Note"),
                .anchor = marker_anchor_t::audio_time, .audio_milliseconds = field<std::int32_t>(node, "StartTime")});
        }
        if (root["SoundEffects"].size() == 0) return;
        const auto lane = ids.take<object_id_t>();
        chart().bgm_source_lanes.push_back({.id = lane, .name = "Quaver sound effects"});
        for (const auto& node : root["SoundEffects"]) {
            const auto at = position(number(node, "StartTime"));
            chart().measures[measure_slots.at(at.measure)].bgm_events.push_back({
                    .id = ids.take<object_id_t>(), .position = at, .source_lane = lane,
                    .sound = samples[field<std::size_t>(node, "Sample") - 1], .provenance = timed_source(node, at)});
        }
    }
    auto run() -> quaver_parse_outcome_t {
        song.id = ids.take<song_id_t>();
        song.metadata.title = field<std::string>(root, "Title");
        song.metadata.artist = field<std::string>(root, "Artist");
        song.metadata.preview = song_preview_t::from_osu_preview_time(field<std::string>(root, "SongPreviewTime", "-1"));
        std::istringstream tags(field<std::string>(root, "Tags"));
        for (std::string tag; tags >> tag;) song.metadata.ordered_search_tags.push_back(tag);
        song.charts.push_back({.id = ids.take<chart_id_t>(),
            .metadata = {.name = field<std::string>(root, "DifficultyName"), .author = field<std::string>(root, "Creator")},
            .source_family = format_family_t::quaver, .divisions_per_standard_measure = 960'000});
        build_geometry();
        build_layout();
        build_resources();
        build_layers();
        build_notes();
        build_timing();
        build_scrolls();
        build_markers_and_effects();
        chart().metadata.format_group_for(format_family_t::quaver).ordered_fields.emplace_back("quaver.source", dump(root));
        if (root["TimingGroups"].size() > 1 || root["TimingGroups"]["$Global"]["ScrollVelocities"].size() != 0 ||
            root["TimingGroups"]["$Global"]["ScrollSpeedFactors"].size() != 0)
            warn(code_t::retained_feature, "Timing groups are retained for export; per-note scroll preview is not modeled");
        song.ensure_chart_order();
        if (diagnostic_queries::has_errors(validate_document(song)))
            fail(code_t::invalid_document, "Quaver data cannot form a valid editor document");
        return {.value = std::move(song), .diagnostics = std::move(diagnostics)};
    }
};
}
auto parse_quaver_bytes(std::span<const std::byte> bytes, quaver_parse_options_t options) -> quaver_parse_outcome_t {
    try {
        const auto text = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        auto root = quaver_detail::normalize(quaver_detail::load(text, options), options.max_events);
        return quaver_reader_t{.root = std::move(root), .options = options}.run();
    } catch (const quaver_detail::failure_t& error) {
        return {.diagnostics = {error.diagnostic}};
    } catch (const YAML::Exception& error) {
        return {.diagnostics = {{.code = quaver_diagnostic_code_t::invalid_yaml, .message = error.what()}}};
    }
}
}
