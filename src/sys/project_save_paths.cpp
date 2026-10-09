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

auto nonportable_project_asset_count(const song_document_t& document) -> std::size_t {
    const auto external = [](const std::filesystem::path& path) {
        if (path.empty()) return false;
        const auto text = path.generic_string();
        if (path.is_absolute() || (text.size() > 2 && text[1] == ':') || text.starts_with("\\")) return true;
        const auto normalized = path.lexically_normal();
        return normalized.begin() != normalized.end() && *normalized.begin() == "..";
    };
    std::size_t count{};
    const auto scan = [&](const auto& assets) {
        for (const auto& asset : assets) count += external(asset.source_path);
    };
    scan(document.audio_assets);
    scan(document.visual_assets);
    scan(document.midi_assets);
    scan(document.generator_recipes);
    return count;
}

auto project_portability_notice_t::update(const song_document_t& document,
                                          std::uint64_t session_generation) -> bool {
    const bool new_project = session_generation_ != session_generation || !revision_;
    if (!new_project && revision_ == document.revision) return false;
    const auto count = nonportable_project_asset_count(document);
    const bool warn = count != 0 && (new_project || count != external_count_);
    revision_ = document.revision;
    session_generation_ = session_generation;
    external_count_ = count;
    return warn;
}

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
