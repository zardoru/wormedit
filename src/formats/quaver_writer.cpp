#include "quaver_internal.h"
#include "../core/text_util.h"
#include <wormedit/core/grid.h>
#include <wormedit/core/timing.h>
#include <algorithm>
#include <array>
#include <map>
#include <numeric>

namespace wormedit {
namespace {
using namespace quaver_detail;
auto root_for(const chart_document_t& chart) -> YAML::Node {
    for (const auto& group : chart.metadata.format_groups) {
        if (group.family != format_family_t::quaver) continue;
        for (const auto& [key, value] : group.ordered_fields)
            if (key == "quaver.source") return load(value, {});
    }
    return YAML::Node(YAML::NodeType::Map);
}
void strip_internal(YAML::Node node) {
    if (node.IsSequence()) {
        for (auto child : node) strip_internal(child);
    } else if (node.IsMap()) {
        std::vector<std::string> keys;
        for (auto entry : node) {
            const auto key = entry.first.as<std::string>();
            if (key.starts_with('_')) keys.push_back(key);
            else strip_internal(entry.second);
        }
        for (const auto& key : keys) node.remove(key);
    }
}
void omit_scalar_defaults(YAML::Node node) {
    constexpr std::array defaults{
        std::pair{"StartTime", "0"}, std::pair{"EndTime", "0"}, std::pair{"EditorLayer", "0"},
        std::pair{"QuaVersion", "0"}, std::pair{"Volume", "100"}, std::pair{"HitSound", "1"},
        std::pair{"Hidden", "false"}, std::pair{"LegacyLNRendering", "false"},
        std::pair{"HasScratchKey", "false"}, std::pair{"BPMDoesNotAffectScrollVelocity", "false"},
        std::pair{"UnaffectedByRate", "false"}, std::pair{"Type", "Normal"},
        std::pair{"TimingGroup", "$Default"}, std::pair{"Signature", "Quadruple"}};
    for (const auto& [key, value] : defaults)
        if (node[key] && node[key].IsScalar() && node[key].Scalar() == value) node.remove(key);
}
void omit_defaults(YAML::Node node) {
    if (node.IsSequence()) {
        for (auto child : node) omit_defaults(child);
    } else if (node.IsMap()) {
        std::vector<std::string> empty;
        for (auto entry : node) {
            omit_defaults(entry.second);
            if ((entry.second.IsSequence() || entry.second.IsMap()) && entry.second.size() == 0)
                empty.push_back(entry.first.as<std::string>());
        }
        for (const auto& key : empty) node.remove(key);
        omit_scalar_defaults(node);
    }
}
struct quaver_writer_t {
    const song_document_t& song;
    const chart_document_t& chart;
    const quaver_write_options_t& options;
    YAML::Node root;
    measure_index_t measures;
    measure_lookup_t measure_lookup;
    timing_index_t timing;
    std::vector<lane_definition_t> lanes;
    std::map<asset_id_t, const audio_asset_t*> audio_assets;
    std::map<layer_id_t, std::size_t> layers;
    std::map<resource_id_t, std::size_t> samples;
    std::map<double, double> tempos;
    std::size_t events{};
    bool mine{};
    void account() {
        if (++events > options.max_events) fail(code_t::output_limit, "Output event limit exceeded");
    }
    auto milliseconds(local_position_t position) const -> double {
        const auto row = measure_lookup.row_of(position);
        if (!row) fail(code_t::invalid_document, "Position is outside chart measures");
        const auto value = timing.query_seconds(*row);
        if (!value) fail(code_t::invalid_document, "Position is outside chart timing");
        return to_double(value->value) * 1000;
    }
    auto time(const YAML::Node& node, local_position_t position, bool integer = false,
              const char* key = "StartTime", const char* prefix = "_") const -> double {
        auto value = milliseconds(position);
        if (same_position(node, position, prefix)) {
            const auto retained_time = number(node, key);
            // Retain exact spelling only while the modeled timing still agrees.
            if (std::abs(retained_time - value) < 0.01) value = retained_time;
        }
        if (integer) {
            if (value < INT32_MIN || value > INT32_MAX || std::abs(value - std::round(value)) > 0.01)
                fail(code_t::unsupported_feature, "Hit object/bookmark time is not an integer millisecond");
            value = std::round(value);
        }
        return value;
    }
    void validate() {
        if (!chart.pauses.empty() || !chart.warps.empty() || !chart.bga_events.empty() || !chart.kiai.empty() ||
            !chart.sound_channels.empty() || !song.storyboards.empty() || !chart.sv_recipes.empty() ||
            !chart.volumes.empty() || !chart.sample_sets.empty() || !chart.sections.empty() || chart.bms_control_tree)
            fail(code_t::unsupported_feature, "Quaver cannot encode pauses, warps, BGA, Kiai, sample controls, sections, recipes or sound-channel data");
        if (chart.lane_layouts.size() != 1 || chart.lane_layouts.front().position != local_position_t{chart.measures.front().id, 0})
            fail(code_t::unsupported_feature, "Quaver requires one fixed lane layout");
        const auto& lanes = chart.lane_layouts.front().layout.lanes;
        const auto scratches = std::ranges::count(lanes, lane_role_t::scratch, &lane_definition_t::role);
        if (scratches > 1 || (scratches == 1 && lanes.back().role != lane_role_t::scratch))
            fail(code_t::unsupported_feature, "Quaver requires a single trailing scratch lane");
        const auto keys = lanes.size() - static_cast<std::size_t>(scratches);
        if (keys < 1 || keys > 10) fail(code_t::unsupported_feature, "Quaver supports one to ten keys plus scratch");
        for (const auto& lane : lanes)
            if (lane.role != lane_role_t::playable && lane.role != lane_role_t::scratch)
                fail(code_t::unsupported_feature, "Quaver cannot encode this lane role");
        for (const auto& measure : chart.measures) {
            const auto meter = measure.definition.meter;
            if (meter && (meter->denominator != 4 || (meter->numerator != 3 && meter->numerator != 4)))
                fail(code_t::unsupported_feature, "Quaver supports only 3/4 and 4/4 signatures");
        }
        root["Mode"] = "Keys" + std::to_string(keys);
        root["HasScratchKey"] = scratches != 0;
    }
    auto path_for(const resource_entry_t& resource) const -> std::string {
        const auto found = audio_assets.find(resource.asset);
        if (found == audio_assets.end()) fail(code_t::invalid_document, "Missing audio asset");
        const auto* asset = found->second;
        auto path = text_util::path_utf8(asset->source_path);
        auto source = retained(resource.provenance);
        if (asset->source_path.is_absolute()) {
            if (field<std::string>(source, "_path") != path)
                fail(code_t::unsupported_feature, "Stage absolute audio paths beside the exported map before writing");
            path = field<std::string>(source, "Path", field<std::string>(source, "AudioFile"));
        }
        validate_path(path);
        if (path.empty()) fail(code_t::invalid_path, "Empty resource path");
        return path;
    }
    auto sample(resource_id_t id) const -> std::size_t {
        const auto found = samples.find(id);
        if (found == samples.end()) fail(code_t::invalid_sample, "Sound does not refer to a sample resource");
        return found->second;
    }
    void build_samples() {
        root["CustomAudioSamples"] = YAML::Node(YAML::NodeType::Sequence);
        root.remove("AudioFile");
        unsigned music_count{};
        for (const auto& resource : song.resources) {
            if (!audio_assets.contains(resource.asset)) continue;
            if (resource.has_role(resource_role_t::music)) {
                if (++music_count > 1) fail(code_t::unsupported_feature, "Quaver has only one song audio file");
                root["AudioFile"] = path_for(resource);
                continue;
            }
            if (!resource.has_role(resource_role_t::sample) && !resource.has_role(resource_role_t::bgm)) continue;
            account();
            auto node = retained(resource.provenance);
            node["Path"] = path_for(resource);
            root["CustomAudioSamples"].push_back(node);
            samples.emplace(resource.id, samples.size() + 1);
        }
    }
    void build_visual_assets() {
        for (const auto* key : {"BackgroundFile", "BannerFile"}) {
            const auto witness = root["_visual"][key];
            if (!witness) continue;
            const auto resource = resource_id_t{field<std::uint64_t>(witness, "resource")};
            const auto current = std::ranges::find(song.resources, resource, &resource_entry_t::id);
            if (current == song.resources.end()) {
                root.remove(key);
                continue;
            }
            const auto asset = std::ranges::find(song.visual_assets, current->asset, &visual_asset_t::id);
            if (asset == song.visual_assets.end()) fail(code_t::invalid_document, "Missing Quaver visual asset");
            auto path = text_util::path_utf8(asset->source_path);
            if (asset->source_path.is_absolute()) {
                if (path != field<std::string>(witness, "path"))
                    fail(code_t::unsupported_feature, "Stage changed absolute visual paths beside the exported map");
                path = field<std::string>(root, key);
            }
            validate_path(path);
            root[key] = path;
        }
    }
    void build_metadata() {
        root["Title"] = song.metadata.title;
        root["Artist"] = song.metadata.artist;
        root["DifficultyName"] = chart.metadata.name;
        root["Creator"] = chart.metadata.author;
        std::string tags;
        for (const auto& tag : song.metadata.ordered_search_tags) {
            if (!tags.empty()) tags += ' ';
            tags += tag;
        }
        root["Tags"] = tags;
        const auto& preview = song.effective_preview(chart);
        if (preview) root["SongPreviewTime"] = preview->start_milliseconds;
        else root.remove("SongPreviewTime");
    }
    auto layer_index(layer_id_t id) const -> std::size_t {
        const auto found = layers.find(id);
        if (found == layers.end()) fail(code_t::invalid_document, "Note has an unknown semantic layer");
        return found->second;
    }
    void build_layers() {
        root["EditorLayers"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& layer : chart.semantic_layers) {
            account();
            layers.emplace(layer.id, layers.size());
            if (layer.color.alpha != 255) fail(code_t::unsupported_feature, "Quaver layer colors cannot encode alpha");
            YAML::Node node;
            node["Name"] = layer.name;
            node["Hidden"] = !layer.visible;
            node["ColorRgb"] = std::to_string(layer.color.red) + "," + std::to_string(layer.color.green) + "," + std::to_string(layer.color.blue);
            root["EditorLayers"].push_back(node);
        }
    }
    auto serialize_note(const note_t& note) -> YAML::Node {
        account();
        if (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold && note.kind != note_kind_t::mine)
            fail(code_t::unsupported_feature, "Quaver supports taps, holds and mines");
        if ((note.kind == note_kind_t::hold) != note.end.has_value() || note.branch || note.label || note.sound_channel_event)
            fail(code_t::unsupported_feature, "Invalid hold or unsupported note annotation");
        auto node = retained(note.provenance);
        node["StartTime"] = static_cast<std::int32_t>(time(node, note.position, true));
        if (note.end) node["EndTime"] = static_cast<std::int32_t>(time(node, *note.end, true, "EndTime", "_end_"));
        else node.remove("EndTime");
        const auto lane = std::ranges::find(lanes, note.lane, &lane_definition_t::id);
        if (lane == lanes.end()) fail(code_t::invalid_document, "Note lane is outside the layout");
        node["Lane"] = std::distance(lanes.begin(), lane) + 1;
        node["Type"] = note.kind == note_kind_t::mine ? "Mine" : "Normal";
        mine = mine || note.kind == note_kind_t::mine;
        if (note.semantic_layers.size() > 1) fail(code_t::unsupported_feature, "Quaver notes have exactly one editor layer");
        node["EditorLayer"] = note.semantic_layers.empty() ? 0 : layer_index(note.semantic_layers.front());
        build_note_sample(node, note);
        return node;
    }
    void build_note_sample(YAML::Node node, const note_t& note) {
        if (note.osu_hitsample) {
            const auto& value = *note.osu_hitsample;
            if (value.normal_set != osu_sample_set_t::automatic || value.addition_set != osu_sample_set_t::automatic ||
                value.custom_index != 0 || value.volume != 0 || !value.filename.empty())
                fail(code_t::unsupported_feature, "Quaver HitSound cannot encode osu sample-set overrides");
            node["HitSound"] = value.additions() | (field<int>(node, "HitSound", 1) & 1);
        } else node.remove("HitSound");
        if (node["KeySounds"].IsSequence()) {
            for (auto retained_sample : node["KeySounds"]) {
                if (retained_sample["_resource"])
                    retained_sample["Sample"] = sample(resource_id_t{field<std::uint64_t>(retained_sample, "_resource")});
            }
        }
        if (note.sound) {
            if (!node["KeySounds"] || node["KeySounds"].size() == 0) {
                YAML::Node first;
                first["Sample"] = sample(*note.sound);
                node["KeySounds"] = YAML::Node(YAML::NodeType::Sequence);
                node["KeySounds"].push_back(first);
            } else node["KeySounds"][0]["Sample"] = sample(*note.sound);
        } else node.remove("KeySounds");
    }
    void build_objects() {
        std::vector<std::pair<double, YAML::Node>> notes;
        std::vector<std::pair<double, YAML::Node>> effects;
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                auto node = serialize_note(note);
                notes.emplace_back(number(node, "StartTime"), std::move(node));
            }
            for (const auto& event : measure.bgm_events) {
                account();
                if (!event.sound || event.end || event.branch || !event.semantic_layers.empty())
                    fail(code_t::unsupported_feature, "Quaver sound effects need one point sample");
                auto node = retained(event.provenance);
                node["StartTime"] = time(node, event.position);
                node["Sample"] = sample(*event.sound);
                effects.emplace_back(number(node, "StartTime"), std::move(node));
            }
        }
        sorted_list("HitObjects", notes);
        sorted_list("SoundEffects", effects);
        root["QuaVersion"] = mine ? 1 : 0;
    }
    void sorted_list(const char* key, std::vector<std::pair<double, YAML::Node>>& entries) {
        std::vector<std::size_t> order(entries.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return entries[a].first < entries[b].first; });
        root[key] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto index : order) root[key].push_back(YAML::Clone(entries[index].second));
    }
    void build_bpms() {
        std::vector<std::pair<double, YAML::Node>> entries;
        for (const auto& event : chart.bpms) {
            account();
            if (event.provenance.source_field == "prefix") continue;
            auto node = retained(event.provenance);
            const auto value = to_double(event.beats_per_minute);
            if (!node["_bpm"] || rational(number(node, "_bpm")) != event.beats_per_minute) node["Bpm"] = value;
            node["StartTime"] = time(node, event.position);
            const auto slot = measure_lookup.bucket_for(event.position.measure);
            if (slot && chart.measures[*slot].definition.meter)
                node["Signature"] = chart.measures[*slot].definition.meter->numerator == 3 ? "Triple" : "Quadruple";
            entries.emplace_back(number(node, "StartTime"), std::move(node));
        }
        sorted_list("TimingPoints", entries);
        for (const auto& point : root["TimingPoints"]) tempos[number(point, "StartTime")] = number(point, "Bpm");
        build_signature_points(entries);
    }
    void build_signature_points(std::vector<std::pair<double, YAML::Node>>& entries) {
        unsigned previous = 4;
        for (const auto& measure : chart.measures) {
            if (!measure.definition.meter) continue;
            const auto signature = measure.definition.meter->numerator;
            const auto at = milliseconds({measure.id, 0});
            const auto changed = signature != previous;
            previous = signature;
            if (!changed || tempos.contains(at)) continue;
            account();
            YAML::Node node;
            node["StartTime"] = at;
            node["Bpm"] = bpm_at(at);
            node["Signature"] = signature == 3 ? "Triple" : "Quadruple";
            entries.emplace_back(at, std::move(node));
        }
        sorted_list("TimingPoints", entries);
    }
    auto bpm_at(double at) const -> double {
        if (tempos.empty()) fail(code_t::missing_content, "Quaver requires a BPM anchor");
        const auto after = tempos.upper_bound(at);
        return after == tempos.begin() ? tempos.begin()->second : std::prev(after)->second;
    }
    void build_scrolls() {
        const auto normalized = field<bool>(root, "BPMDoesNotAffectScrollVelocity", chart.source_family != format_family_t::quaver);
        root["BPMDoesNotAffectScrollVelocity"] = normalized;
        std::vector<std::pair<double, YAML::Node>> entries;
        for (const auto& event : chart.scrolls) {
            account();
            auto node = retained(event.provenance);
            const auto at = time(node, event.position);
            const auto value = to_double(event.multiplier);
            const auto unchanged = same_position(node, event.position) &&
                rational(number(node, "_effective", value + 1)) == event.multiplier &&
                (normalized || (number(node, "_tempo", -1) == bpm_at(at) &&
                    number(node, "_base", -1) == number(root["TimingPoints"][0], "Bpm")));
            if (unchanged && field<bool>(node, "_bpm_only")) continue;
            if (unchanged && field<bool>(node, "_initial")) {
                if (normalized) root["InitialScrollVelocity"] = value;
                continue;
            }
            node["StartTime"] = at;
            node["Multiplier"] = value * (normalized ? 1 : number(root["TimingPoints"][0], "Bpm") / bpm_at(at));
            entries.emplace_back(at, std::move(node));
        }
        sorted_list("SliderVelocities", entries);
        entries.clear();
        for (const auto& event : chart.speeds) {
            account();
            if (event.transition_beats.numerator != 0)
                fail(code_t::unsupported_feature, "Quaver scroll speed factors must be instantaneous");
            auto node = retained(event.provenance);
            node["StartTime"] = time(node, event.position);
            node["Multiplier"] = to_double(event.multiplier);
            entries.emplace_back(number(node, "StartTime"), std::move(node));
        }
        sorted_list("ScrollSpeedFactors", entries);
    }
    void build_bookmarks() {
        std::map<std::uint64_t, YAML::Node> retained_nodes;
        for (const auto& node : root["Bookmarks"])
            if (node["_id"]) retained_nodes.emplace(field<std::uint64_t>(node, "_id"), YAML::Clone(node));
        std::vector<std::pair<double, YAML::Node>> entries;
        for (const auto& marker : chart.bookmarks) {
            account();
            auto node = retained_nodes.contains(marker.id.value) ? retained_nodes.at(marker.id.value) : YAML::Node(YAML::NodeType::Map);
            if (marker.anchor == marker_anchor_t::audio_time && marker.audio_milliseconds) node["StartTime"] = *marker.audio_milliseconds;
            else if (marker.position) node["StartTime"] = static_cast<std::int32_t>(time(node, *marker.position, true));
            else fail(code_t::invalid_document, "Bookmark has no time");
            node["Note"] = marker.label;
            entries.emplace_back(number(node, "StartTime"), std::move(node));
        }
        sorted_list("Bookmarks", entries);
    }
    auto run() -> std::string {
        measure_lookup = measure_lookup_t::from(measures);
        for (const auto& asset : song.audio_assets) audio_assets.emplace(asset.id, &asset);
        validate();
        lanes = chart.lane_layouts.front().layout.lanes_in_display_order();
        build_metadata();
        build_samples();
        build_visual_assets();
        build_layers();
        build_bpms();
        build_scrolls();
        build_objects();
        build_bookmarks();
        if (root["TimingGroups"]) {
            auto groups = root["TimingGroups"];
            for (auto entry : groups) entry.second.SetTag("!ScrollGroup");
            const auto global = groups["$Global"];
            if (global && global["ScrollVelocities"].size() == 0 && global["ScrollSpeedFactors"].size() == 0) groups.remove("$Global");
        }
        strip_internal(root);
        omit_defaults(root);
        // Validate the emitted data before publication, including retained samples/groups.
        (void)normalize(YAML::Clone(root), options.max_events);
        const auto yaml = dump(root);
        if (yaml.size() > options.max_output_bytes) fail(code_t::output_limit, "Output byte limit exceeded");
        std::string output;
        for (const auto character : yaml) {
            if (character == '\n') output += '\r';
            output += character;
            if (output.size() > options.max_output_bytes) fail(code_t::output_limit, "Output byte limit exceeded");
        }
        if (output.size() + 2 > options.max_output_bytes) fail(code_t::output_limit, "Output byte limit exceeded");
        output += "\r\n";
        return output;
    }
};
}
auto write_quaver(const song_document_t& song, quaver_write_options_t options) -> quaver_write_outcome_t {
    try {
        const auto charts = song.read_charts();
        const auto* chart = options.chart ? song.read_chart(*options.chart) : (charts.size() == 1 ? charts.front() : nullptr);
        if (!chart || chart->measures.empty()) fail(code_t::missing_chart, "Select one nonempty chart for Quaver export");
        if (diagnostic_queries::has_errors(validate_document(song))) fail(code_t::invalid_document, "Invalid source document");
        const auto index = timing_index_t::from(*chart);
        if (!index.value || !index.ok()) fail(code_t::invalid_document, "Invalid chart timing");
        auto measures = measure_index_t::from(*chart);
        if (!measures) fail(code_t::invalid_document, "Invalid measure geometry");
        quaver_writer_t writer{.song = song, .chart = *chart, .options = options,
            .root = root_for(*chart), .measures = std::move(*measures), .timing = *index.value};
        return {.value = writer.run()};
    } catch (const failure_t& error) {
        return {.diagnostics = {error.diagnostic}};
    } catch (const YAML::Exception& error) {
        return {.diagnostics = {{.code = code_t::invalid_yaml, .message = error.what()}}};
    }
}
}
