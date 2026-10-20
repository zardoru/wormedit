#pragma once
#include <wormedit/audio/preview_bridge.h>
#include <wormedit/audio/source_registry.h>
#include <filesystem>
#include <map>
#include <utility>

namespace wormedit::audio {
struct hitsound_environment_t {
    std::filesystem::path map_directory;
    std::filesystem::path skin_directory;
    std::filesystem::path bundle_directory;
    auto operator==(const hitsound_environment_t&) const -> bool = default;
};

struct hitsound_paths_t {
    std::vector<std::filesystem::path> paths;
    float gain{1};
    bool fallback{};
    [[nodiscard]] static auto from(const osu_hitsample_t& sample,
        const hitsound_environment_t& environment, bool custom_normal = false,
        osu_sample_context_t context = {}) -> hitsound_paths_t;
};

struct hitsound_binding_t {
    preview_object_source_binding_t binding;
    float gain{1};
};

class hitsound_resolver_t {
  public:
    hitsound_environment_t environment;
    [[nodiscard]] auto bindings(const song_document_t& song, const chart_document_t& chart,
        decoded_source_registry_t& registry, std::uint64_t generation) -> std::vector<hitsound_binding_t>;
    [[nodiscard]] auto take_fallback_notice() -> bool { return std::exchange(fallback_pending_, false); }
  private:
    std::uint64_t generation_{};
    hitsound_environment_t registered_environment_;
    std::map<std::filesystem::path, preview_voice_source_t> voices_;
    bool fallback_pending_{};
    bool fallback_reported_{};
    [[nodiscard]] auto voice(const std::filesystem::path&, decoded_source_registry_t&)
        -> std::optional<preview_voice_source_t>;
    [[nodiscard]] auto binding_for(const note_t&, decoded_source_registry_t&, osu_sample_context_t,
                                   bool custom_normal) -> hitsound_binding_t;
};
}
