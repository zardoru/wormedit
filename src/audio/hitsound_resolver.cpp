#include <wormedit/audio/hitsound_resolver.h>
#include <wormedit/audio/decoder.h>
#include <wormedit/formats/osu_mania.h>
#include <array>
#include <limits>
#include <unordered_set>

namespace wormedit::audio {
namespace {
auto existing_sample(const std::filesystem::path& directory, const std::string& name)
    -> std::optional<std::filesystem::path> {
    if (directory.empty()) return {};
    for (const auto* extension : {".wav", ".ogg", ".mp3"}) {
        const auto path = directory / (name + extension);
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) return path;
    }
    return {};
}
auto safe_custom_path(const hitsound_environment_t& environment, std::string_view filename)
    -> std::optional<std::filesystem::path> {
    const auto relative = std::filesystem::path{filename};
    if (relative.empty() || relative.is_absolute() || environment.map_directory.empty()) return {};
    for (const auto& part : relative) if (part == "..") return {};
    const auto path = environment.map_directory / relative;
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) ? std::optional{path} : std::nullopt;
}
auto resolve_sample(const hitsound_environment_t& environment, osu_sample_set_t set,
                    const char* kind, std::int64_t index) -> std::optional<std::filesystem::path> {
    constexpr std::array sets{"normal", "normal", "soft", "drum"};
    const auto name = std::string{sets[static_cast<std::size_t>(set)]} + "-" + kind;
    if (const auto map = existing_sample(environment.map_directory,
            name + (index > 1 ? std::to_string(index) : ""))) return map;
    if (const auto skin = existing_sample(environment.skin_directory, name)) return skin;
    return existing_sample(environment.bundle_directory, name);
}
}

auto hitsound_paths_t::from(const osu_hitsample_t& sample, const hitsound_environment_t& environment,
                            bool custom_normal, osu_sample_context_t context) -> hitsound_paths_t {
    hitsound_paths_t result{.gain = static_cast<float>(sample.volume == 0 ? context.volume : sample.volume) / 100};
    if (!sample.is_valid()) return result;
    const auto normal = sample.normal_set == osu_sample_set_t::automatic ? context.normal_set : sample.normal_set;
    const auto addition = sample.addition_set == osu_sample_set_t::automatic ? normal : sample.addition_set;
    const auto custom_index = sample.custom_index == 0 ? context.custom_index : sample.custom_index;
    if (!custom_normal) {
        auto path = safe_custom_path(environment, sample.filename);
        if (!sample.filename.empty() && !path) result.fallback = true;
        if (!path) path = resolve_sample(environment, normal, "hitnormal", custom_index);
        if (path) result.paths.push_back(*path);
        else result.fallback = true;
    }
    const std::array enabled{sample.whistle, sample.finish, sample.clap};
    constexpr std::array kinds{"hitwhistle", "hitfinish", "hitclap"};
    for (std::size_t index = 0; index < enabled.size(); ++index) {
        if (!enabled[index]) continue;
        const auto path = resolve_sample(environment, addition, kinds[index], custom_index);
        if (path) result.paths.push_back(*path);
        else result.fallback = true;
    }
    return result;
}

auto hitsound_resolver_t::voice(const std::filesystem::path& path, decoded_source_registry_t& registry)
    -> std::optional<preview_voice_source_t> {
    if (const auto found = voices_.find(path); found != voices_.end()) return found->second;
    if (voices_.size() >= 128) return {};
    decoder_t decoder;
    if (!decoder.open(path)) return {};
    const auto info = decoder.info();
    if (!info || !info->output_frame_count || info->output_frame_count > output_sample_rate * 5) return {};
    std::vector<float> pcm(static_cast<std::size_t>(info->output_frame_count) * 2);
    const auto read = decoder.read_frames(pcm, static_cast<std::size_t>(info->output_frame_count));
    if (read.frames_read != info->output_frame_count) return {};
    auto registered = registry.create_generated_source({
        .source = {"internal:hitsound:" + path.generic_string(), "binding:" + std::to_string(generation_)},
        .interleaved_stereo = std::move(pcm)});
    if (!registered) return {};
    preview_voice_source_t voice{.source = registered->source, .frame_count = registered->output_frame_count};
    voices_.emplace(path, voice);
    return voice;
}

auto hitsound_resolver_t::binding_for(const note_t& note, decoded_source_registry_t& registry,
    osu_sample_context_t context, bool custom_normal) -> hitsound_binding_t {
    const auto resolved = hitsound_paths_t::from(*note.osu_hitsample, environment, custom_normal, context);
    bool fallback = resolved.fallback;
    preview_object_source_binding_t binding{.object = note.id};
    for (const auto& path : resolved.paths) {
        auto source = voice(path, registry);
        if (source) binding.voices.push_back(*source);
        else fallback = true;
    }
    if (fallback && !fallback_reported_) { fallback_pending_ = true; fallback_reported_ = true; }
    return {.binding = std::move(binding), .gain = resolved.gain};
}

auto hitsound_resolver_t::bindings(const song_document_t& song, const chart_document_t& chart,
    decoded_source_registry_t& registry, std::uint64_t generation,
    std::optional<object_id_t> object) -> std::vector<hitsound_binding_t> {
    if (generation_ != generation) {
        voices_.clear();
        generation_ = generation;
    }
    if (registered_environment_ != environment) {
        registered_environment_ = environment;
        fallback_reported_ = false;
    }
    registry.ensure_source_capacity(registry.bindings().size() + 128);
    std::vector<hitsound_binding_t> result;
    const auto context = osu_sample_context_index_t::from(chart);
    std::unordered_set<std::uint64_t> available;
    std::unordered_set<std::uint64_t> assets;
    for (const auto& binding : registry.bindings()) assets.insert(binding.asset.value);
    for (const auto& resource : song.resources)
        if (assets.contains(resource.asset.value)) available.insert(resource.id.value);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!note.osu_hitsample || (object && note.id != *object)) continue;
            result.push_back(binding_for(note, registry,
                context ? context->at(note.position) : osu_sample_context_t{},
                note.sound && available.contains(note.sound->value)));
        }
    }
    return result;
}
}
