#pragma once
#include <wormedit/audio/offline_mix.h>
#include <wormedit/audio/audition_resolver.h>
#include <wormedit/core/document.h>
#include <memory>

namespace wormedit::sys {
struct offline_preview_paths_t {
    std::filesystem::path project, skin, bundle;
    auto operator==(const offline_preview_paths_t&) const -> bool = default;
};
// Worker-owned source registry/schedule/bindings; both offline exporters use
// the same authored mix setup. No live transport/device or monitoring levels.
class offline_preview_mix_t {
  public:
    [[nodiscard]] static auto from(const song_document_t&, chart_id_t,
        const offline_preview_paths_t&, bool include_samples = true, bool include_music = true)
        -> result_t<std::unique_ptr<offline_preview_mix_t>, error_t<failure_code_t>>;
    [[nodiscard]] auto end_frame() const -> std::uint64_t { return end_frame_; }
    [[nodiscard]] auto render(std::uint64_t begin, std::uint64_t end,
        audio::offline_mix_options_t options, std::stop_token stop)
        -> result_t<audio::offline_mix_t, error_t<failure_code_t>>;
    [[nodiscard]] auto render_to(std::uint64_t begin, std::uint64_t end,
        const audio::offline_mix_sink_t&, std::stop_token)
        -> result_t<void, error_t<failure_code_t>>;
  private:
    audio::decoded_source_registry_t sources_;
    preview_schedule_t schedule_;
    audio::preview_bindings_resolution_t bindings_;
    std::uint64_t end_frame_{};
    [[nodiscard]] auto prepare(const song_document_t&, chart_id_t,
        const offline_preview_paths_t&, bool, bool) -> result_t<void, error_t<failure_code_t>>;
    [[nodiscard]] auto include_music(const song_document_t&, const chart_document_t&)
        -> result_t<void, error_t<failure_code_t>>;
};
}
