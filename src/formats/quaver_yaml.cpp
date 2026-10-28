#include "quaver_internal.h"
#include "../core/text_util.h"
#include <wormedit/core/numeric_text.h>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/parser.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <numeric>
#include <sstream>

namespace wormedit::quaver_detail {
namespace {
class yaml_budget_t final : public YAML::EventHandler {
public:
    explicit yaml_budget_t(const quaver_parse_options_t& options) : options_(options) {}
    void OnDocumentStart(const YAML::Mark&) override {
        if (++documents_ > 1) fail(code_t::invalid_yaml, "Expected one YAML document");
    }
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark&, YAML::anchor_t) override { account(); }
    void OnAlias(const YAML::Mark&, YAML::anchor_t) override {
        fail(code_t::invalid_yaml, "YAML aliases are outside the bounded Quaver profile");
    }
    void OnScalar(const YAML::Mark&, const std::string&, YAML::anchor_t, const std::string&) override { account(); }
    void OnSequenceStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) override { begin(); }
    void OnSequenceEnd() override { --depth_; }
    void OnMapStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) override { begin(); }
    void OnMapEnd() override { --depth_; }
private:
    void account() {
        if (++nodes_ > options_.max_nodes) fail(code_t::structure_limit, "YAML node limit exceeded");
    }
    void begin() {
        account();
        if (++depth_ > options_.max_depth) fail(code_t::structure_limit, "YAML nesting limit exceeded");
    }
    const quaver_parse_options_t& options_;
    std::size_t nodes_{};
    std::size_t depth_{};
    unsigned documents_{};
};

void validate_mapping_keys(const YAML::Node& node) {
    if (node.IsSequence()) {
        for (const auto& child : node) validate_mapping_keys(child);
    } else if (node.IsMap()) {
        std::set<std::string> keys;
        for (const auto& entry : node) {
            if (!entry.first.IsScalar() || !keys.insert(entry.first.Scalar()).second)
                fail(code_t::invalid_yaml, "Mapping keys must be unique scalars");
            validate_mapping_keys(entry.second);
        }
    }
}

// Ignore unmatched YAML properties like Quaver.API. In particular, source files
// cannot inject the private provenance fields used to recognize unchanged edits.
void keep_fields(YAML::Node node, std::initializer_list<std::string_view> known) {
    if (!node.IsMap()) return;
    std::vector<std::string> unknown;
    for (const auto& entry : node) {
        const auto key = entry.first.as<std::string>();
        if (std::ranges::find(known, key) == known.end()) unknown.push_back(key);
    }
    for (const auto& key : unknown) node.remove(key);
}
void sanitize_group(YAML::Node group) {
    keep_fields(group, {"InitialScrollVelocity", "ScrollVelocities", "ScrollSpeedFactors", "ColorRgb", "Hidden"});
    for (const auto* key : {"ScrollVelocities", "ScrollSpeedFactors"})
        if (group[key].IsSequence()) for (auto node : group[key]) keep_fields(node, {"StartTime", "Multiplier"});
}
void sanitize_record(YAML::Node node, std::string_view key) {
    if (key == "TimingPoints") keep_fields(node, {"StartTime", "Bpm", "Signature", "Hidden"});
    else if (key == "SoundEffects") keep_fields(node, {"StartTime", "Sample", "Volume"});
    else if (key == "CustomAudioSamples") keep_fields(node, {"Path", "UnaffectedByRate"});
    else if (key == "Bookmarks") keep_fields(node, {"StartTime", "Note", "ColorRgb"});
    else if (key == "EditorLayers") keep_fields(node, {"Name", "Hidden", "ColorRgb"});
    else if (key == "HitObjects") {
        keep_fields(node, {"StartTime", "Lane", "EndTime", "HitSound", "Type", "KeySounds", "EditorLayer", "TimingGroup"});
        if (node["KeySounds"].IsSequence())
            for (auto sample : node["KeySounds"]) keep_fields(sample, {"Sample", "Volume"});
    }
}
void sanitize(YAML::Node root) {
    keep_fields(root, {"QuaVersion", "AudioFile", "SongPreviewTime", "BackgroundFile", "BannerFile",
        "MapId", "MapSetId", "Mode", "Title", "Artist", "Source", "Tags", "Creator", "DifficultyName",
        "Description", "Genre", "LegacyLNRendering", "BPMDoesNotAffectScrollVelocity", "InitialScrollVelocity",
        "HasScratchKey", "EditorLayers", "Bookmarks", "CustomAudioSamples", "SoundEffects", "TimingPoints",
        "SliderVelocities", "ScrollSpeedFactors", "HitObjects", "TimingGroups"});
    for (const auto* key : {"TimingPoints", "HitObjects", "SoundEffects", "CustomAudioSamples", "Bookmarks", "EditorLayers"}) {
        if (!root[key].IsSequence()) continue;
        for (auto node : root[key]) sanitize_record(node, key);
    }
    for (const auto* key : {"SliderVelocities", "ScrollSpeedFactors"})
        if (root[key].IsSequence()) for (auto node : root[key]) keep_fields(node, {"StartTime", "Multiplier"});
    if (root["TimingGroups"].IsMap()) for (auto entry : root["TimingGroups"]) sanitize_group(entry.second);
}
auto records(YAML::Node parent, const char* key, std::size_t& budget) -> YAML::Node {
    auto list = parent[key];
    if (!list || list.IsNull()) { parent[key] = YAML::Node(YAML::NodeType::Sequence); list = parent[key]; }
    if (!list.IsSequence()) fail(code_t::invalid_value, "Expected a sequence", key);
    if (list.size() > budget) fail(code_t::structure_limit, "Event limit exceeded", key);
    budget -= list.size();
    for (const auto& node : list)
        if (!node.IsMap()) fail(code_t::invalid_value, "Expected a record mapping", key);
    return list;
}
void sort_records(YAML::Node parent, const char* key) {
    // yaml-cpp Node assignment changes shared graph data. Sort indices, never
    // Nodes, so moving a handle cannot rewrite another source record.
    const auto source = YAML::Clone(parent[key]);
    std::vector<std::size_t> order(source.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        return number(source[a], "StartTime") < number(source[b], "StartTime");
    });
    parent[key] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto index : order) parent[key].push_back(YAML::Clone(source[index]));
}
void normalize_track(YAML::Node root, const char* key, std::size_t& budget) {
    for (auto node : records(root, key, budget)) {
        node["StartTime"] = number(node, "StartTime");
        node["Multiplier"] = number(node, "Multiplier");
    }
    sort_records(root, key);
}
void normalize_sample(YAML::Node node, std::size_t count, bool effect) {
    const auto index = field<int>(node, "Sample");
    if (index < 1 || static_cast<std::size_t>(index) > count)
        fail(code_t::invalid_sample, "Sample index must be in 1..CustomAudioSamples.Count");
    auto volume = field<int>(node, "Volume");
    if (volume == 0) volume = 100;
    if (volume < 1 || (effect && volume > 100))
        fail(code_t::invalid_sample, "Invalid sample volume");
    node["Volume"] = volume;
}
auto hitsound(const YAML::Node& node) -> int {
    const auto text = field<std::string>(node, "HitSound", "Normal");
    int numeric{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), numeric);
    if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()) {
        if (numeric < 0 || numeric > 15) fail(code_t::invalid_value, "Invalid HitSound flags");
        return numeric == 0 ? 1 : numeric;
    }
    int result{};
    std::istringstream input(text);
    std::string token;
    while (std::getline(input, token, ',')) {
        const auto name = text_util::trim(token, text_util::ascii_whitespace);
        if (name == "Normal") result |= 1;
        else if (name == "Whistle") result |= 2;
        else if (name == "Finish") result |= 4;
        else if (name == "Clap") result |= 8;
        else fail(code_t::invalid_value, "Invalid HitSound flag");
    }
    return result;
}
void normalize_note(YAML::Node node, const YAML::Node& root, std::size_t& budget) {
    const auto start = field<std::int32_t>(node, "StartTime");
    const auto end = field<std::int32_t>(node, "EndTime");
    if (end != 0 && end <= start) fail(code_t::invalid_hold, "EndTime must be greater than StartTime");
    const auto lane = field<unsigned>(node, "Lane");
    const auto lanes = key_count(root) + (field<bool>(root, "HasScratchKey") ? 1U : 0U);
    if (lane < 1 || lane > lanes) fail(code_t::invalid_value, "Lane is outside Mode's layout");
    const auto type = field<std::string>(node, "Type", "Normal");
    if (type != "Normal" && type != "Mine") fail(code_t::invalid_value, "Unknown hit object Type");
    if (type == "Mine" && end != 0) fail(code_t::invalid_hold, "Mines cannot have a long-note tail");
    if (type == "Mine" && field<int>(root, "QuaVersion") < 1)
        fail(code_t::invalid_version, "Mines require QuaVersion 1");
    const auto layer = field<int>(node, "EditorLayer");
    if (layer < 0 || (layer != 0 && static_cast<std::size_t>(layer) >= root["EditorLayers"].size()))
        fail(code_t::invalid_value, "EditorLayer is outside the layer table");
    const auto group = field<std::string>(node, "TimingGroup", "$Default");
    if (group != "$Default" && !root["TimingGroups"][group])
        fail(code_t::invalid_value, "Unknown note timing group");
    node["HitSound"] = hitsound(node);
    node["StartTime"] = start;
    for (auto sample : records(node, "KeySounds", budget))
        normalize_sample(sample, root["CustomAudioSamples"].size(), false);
}
void validate_root_scalars(const YAML::Node& root) {
    for (const auto* key : {"Title", "Artist", "Source", "Tags", "Creator", "DifficultyName", "Description", "Genre"})
        (void)field<std::string>(root, key);
    for (const auto* key : {"LegacyLNRendering", "BPMDoesNotAffectScrollVelocity", "HasScratchKey"})
        (void)field<bool>(root, key);
    for (const auto* key : {"SongPreviewTime", "MapId", "MapSetId"})
        (void)field<std::int32_t>(root, key);
}
void normalize_groups(YAML::Node root, std::size_t& budget) {
    if (!root["TimingGroups"] || root["TimingGroups"].IsNull()) root["TimingGroups"] = YAML::Node(YAML::NodeType::Map);
    auto groups = root["TimingGroups"];
    if (!groups.IsMap()) fail(code_t::invalid_value, "TimingGroups must be a mapping");
    if (!groups["$Global"]) groups["$Global"] = YAML::Node(YAML::NodeType::Map);
    for (auto entry : groups) {
        auto group = entry.second;
        if (!group.IsMap() || entry.first.as<std::string>() == "$Default")
            fail(code_t::invalid_value, "Invalid timing group");
        (void)field<bool>(group, "Hidden");
        group["InitialScrollVelocity"] = number(group, "InitialScrollVelocity", 1);
        (void)color(field<std::string>(group, "ColorRgb", "255,255,255"));
        normalize_track(group, "ScrollVelocities", budget);
        normalize_track(group, "ScrollSpeedFactors", budget);
    }
}
}

auto load(std::string_view text, const quaver_parse_options_t& options) -> YAML::Node {
    if (text.size() > options.max_source_bytes) fail(code_t::source_limit, "Source byte limit exceeded");
    if (!text_util::is_valid_bounded_text(text, options.max_source_bytes))
        fail(code_t::invalid_encoding, "Expected UTF-8 without embedded NUL");
    std::istringstream input{std::string(text)};
    YAML::Parser parser(input);
    yaml_budget_t budget(options);
    while (parser.HandleNextDocument(budget)) {}
    auto root = YAML::Load(std::string(text));
    if (!root.IsMap()) fail(code_t::invalid_yaml, "Expected a Quaver mapping");
    validate_mapping_keys(root);
    return root;
}
auto key_count(const YAML::Node& root) -> unsigned {
    const auto mode = field<std::string>(root, "Mode");
    constexpr std::array names{"Keys1", "Keys2", "Keys3", "Keys4", "Keys5", "Keys6", "Keys7", "Keys8", "Keys9", "Keys10"};
    const auto found = std::ranges::find(names, mode);
    if (found == names.end()) fail(code_t::invalid_mode, "Mode must be Keys1 through Keys10");
    return static_cast<unsigned>(std::distance(names.begin(), found) + 1);
}
auto rational(double value) -> signed_rational_t {
    if (value == 0) return {};
    const auto result = rationalized_value_t::from(std::abs(value), {.maximum_denominator = 1'000'000'000,
        .maximum_absolute_error = 1e-9L});
    if (!result || result->value.numerator > INT64_MAX)
        fail(code_t::invalid_value, "Number cannot be represented on the bounded rational grid");
    return {static_cast<std::int64_t>(result->value.numerator) * (value < 0 ? -1 : 1), result->value.denominator};
}
auto color(std::string_view text) -> color_rgba_t {
    std::istringstream input{std::string(text)};
    std::array<unsigned, 3> channels{};
    std::string token;
    for (auto& channel : channels) {
        if (!std::getline(input, token, ',')) fail(code_t::invalid_value, "Expected RGB color");
        unsigned parsed_channel{};
        const auto valid = parse_integer(text_util::trim(token, text_util::ascii_whitespace), parsed_channel);
        const auto value = valid ? std::optional{parsed_channel} : std::nullopt;
        if (!value || *value > 255) fail(code_t::invalid_value, "Invalid RGB channel");
        channel = *value;
    }
    if (std::getline(input, token, ',')) fail(code_t::invalid_value, "Too many RGB channels");
    return {.red = static_cast<std::uint8_t>(channels[0]), .green = static_cast<std::uint8_t>(channels[1]),
            .blue = static_cast<std::uint8_t>(channels[2])};
}
void validate_path(std::string_view text) {
    if (text.empty()) return;
    const auto path = text_util::path_from_utf8(text);
    if (path.is_absolute() || text.find(':') != std::string_view::npos ||
        text.find('\\') != std::string_view::npos || text.find('\n') != std::string_view::npos ||
        text.find('\r') != std::string_view::npos)
        fail(code_t::invalid_path, "Expected a safe relative resource path");
    for (const auto& part : path)
        if (part == "..") fail(code_t::invalid_path, "Resource path escapes the map folder");
}
auto dump(const YAML::Node& node) -> std::string {
    YAML::Emitter emitter;
    emitter << node;
    if (!emitter.good()) fail(code_t::invalid_yaml, "YAML serialization failed");
    return emitter.c_str();
}
auto normalize(YAML::Node root, std::size_t budget) -> YAML::Node {
    sanitize(root);
    validate_root_scalars(root);
    const auto version = field<int>(root, "QuaVersion");
    if (version < 0 || version > 1) fail(code_t::invalid_version, "Unsupported QuaVersion");
    (void)key_count(root);
    for (const auto* key : {"AudioFile", "BackgroundFile", "BannerFile"}) validate_path(field<std::string>(root, key));
    for (auto node : records(root, "CustomAudioSamples", budget)) {
        const auto path = field<std::string>(node, "Path");
        if (path.empty()) fail(code_t::invalid_path, "Sample Path cannot be empty");
        validate_path(path);
        (void)field<bool>(node, "UnaffectedByRate");
    }
    for (auto node : records(root, "EditorLayers", budget))
        (void)color(field<std::string>(node, "ColorRgb", "255,255,255"));
    for (auto node : records(root, "Bookmarks", budget)) {
        node["StartTime"] = field<std::int32_t>(node, "StartTime");
        (void)color(field<std::string>(node, "ColorRgb", "255,255,0"));
    }
    sort_records(root, "Bookmarks");
    for (auto node : records(root, "TimingPoints", budget)) {
        node["StartTime"] = number(node, "StartTime");
        (void)field<bool>(node, "Hidden");
        if (number(node, "Bpm") <= 0) fail(code_t::invalid_value, "Bpm must be positive");
        const auto signature = field<std::string>(node, "Signature", "Quadruple");
        if (signature != "Quadruple" && signature != "Triple" && signature != "0" && signature != "4" && signature != "3")
            fail(code_t::invalid_value, "Unsupported time signature");
    }
    sort_records(root, "TimingPoints");
    normalize_groups(root, budget);
    normalize_track(root, "SliderVelocities", budget);
    normalize_track(root, "ScrollSpeedFactors", budget);
    root["InitialScrollVelocity"] = number(root, "InitialScrollVelocity", 1);
    for (auto node : records(root, "SoundEffects", budget)) {
        node["StartTime"] = number(node, "StartTime");
        normalize_sample(node, root["CustomAudioSamples"].size(), true);
    }
    sort_records(root, "SoundEffects");
    for (auto node : records(root, "HitObjects", budget)) normalize_note(node, root, budget);
    sort_records(root, "HitObjects");
    if (root["TimingPoints"].size() == 0 || root["HitObjects"].size() == 0)
        fail(code_t::missing_content, "Quaver requires timing points and hit objects");
    return root;
}
}
