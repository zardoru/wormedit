#include "sys/project_save_paths.h"

#include <filesystem>
#include <optional>
#include <system_error>

namespace wormedit::sys {
namespace {

auto absolute_base(const std::filesystem::path& base) -> std::optional<std::filesystem::path> {
    if (base.empty()) {
        return std::nullopt;
    }
    std::error_code error;
    const auto absolute = std::filesystem::absolute(base, error);
    return error ? std::nullopt : std::optional{absolute.lexically_normal()};
}

void rebase_path(std::filesystem::path& path,
                 const std::filesystem::path& old_base,
                 const std::filesystem::path& new_base) {
    if (path.empty() || path.is_absolute()) {
        return;
    }
    const auto resolved = (old_base / path).lexically_normal();
    const auto relative = resolved.lexically_relative(new_base);
    path = relative.empty() ? resolved : relative;
}

} // namespace

void rebase_project_paths(song_document_t& document,
                          const std::filesystem::path& old_base,
                          const std::filesystem::path& new_base) {
    const auto source = absolute_base(old_base);
    const auto destination = absolute_base(new_base);
    if (!source || !destination || *source == *destination) {
        return;
    }
    for (auto& asset : document.audio_assets) {
        rebase_path(asset.source_path, *source, *destination);
        if (asset.waveform_cache_path) {
            rebase_path(*asset.waveform_cache_path, *source, *destination);
        }
    }
    for (auto& asset : document.visual_assets) {
        rebase_path(asset.source_path, *source, *destination);
    }
    for (auto& asset : document.midi_assets) {
        rebase_path(asset.source_path, *source, *destination);
    }
    for (auto& recipe : document.generator_recipes) {
        rebase_path(recipe.source_path, *source, *destination);
    }
}

} // namespace wormedit::sys
