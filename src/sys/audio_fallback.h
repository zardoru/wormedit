#pragma once

#include "core/text_util.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace wormedit::sys {

// How an authored audio path resolves on disk (docs/references/bms-audio-fallback.md).
enum class audio_fallback_outcome_t {
    exact,      // the authored path exists as written
    fallback,   // one other file with the same stem and a .wav/.ogg/.flac extension exists
    ambiguous,  // several candidates (extensions or case variants); the user must choose
    missing,    // nothing usable, or the path is unsafe to search
};

struct audio_fallback_t {
    audio_fallback_outcome_t outcome{audio_fallback_outcome_t::missing};
    // The authored path for exact; the single candidate for fallback.
    std::filesystem::path chosen;
    // Every candidate, relative to the asset base, preferred extension first. Empty for exact.
    std::vector<std::filesystem::path> candidates;
};

namespace audio_fallback_detail {
using text_util::ascii_lowercase;

inline auto extension_rank(const std::string& folded_extension) -> int {
    constexpr std::array<const char*, 3> extensions{".wav", ".ogg", ".flac"};
    for (std::size_t index = 0; index < extensions.size(); ++index)
        if (folded_extension == extensions[index]) return static_cast<int>(index);
    return -1;
}

inline auto is_file(const std::filesystem::path& path) -> bool {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

} // namespace audio_fallback_detail

// Resolves `authored` (relative to `asset_base`) without changing it: the exact path first, then
// files in the same directory with the same stem (case-insensitive) and a .wav, .ogg or .flac
// extension (case-insensitive). Never recurses and never leaves the project directory; absolute
// or ".." paths are only checked exactly.
// Directory listings reused across many lookups in one pass.
using audio_directory_cache_t = std::map<std::filesystem::path, std::vector<std::filesystem::path>>;

inline auto resolve_audio_fallback(const std::filesystem::path& authored,
                                   const std::filesystem::path& asset_base,
                                   audio_directory_cache_t* cache = nullptr) -> audio_fallback_t {
    using namespace audio_fallback_detail;
    audio_fallback_t result;
    if (authored.empty() || authored.filename().empty()) return result;
    const auto resolved = authored.is_absolute() ? authored : asset_base / authored;
    if (is_file(resolved)) {
        result.outcome = audio_fallback_outcome_t::exact;
        result.chosen = authored;
        return result;
    }
    const auto unsafe = authored.is_absolute() || asset_base.empty() ||
        std::ranges::any_of(authored, [](const auto& part) { return part == ".."; });
    if (unsafe) return result;
    std::error_code error;
    const auto directory = resolved.parent_path();
    const auto stem = ascii_lowercase(authored.stem().string());
    struct found_t { int rank; std::string name; };
    std::vector<found_t> found;
    audio_directory_cache_t local;
    auto& listings = cache ? *cache : local;
    auto listing = listings.find(directory);
    if (listing == listings.end()) {
        std::vector<std::filesystem::path> names;
        for (const auto& entry : std::filesystem::directory_iterator(directory, error))
            names.push_back(entry.path());
        listing = listings.emplace(directory, std::move(names)).first;
    }
    for (const auto& path : listing->second) {
        const auto rank = extension_rank(text_util::lowercase_extension(path));
        if (rank < 0 || !text_util::ascii_iequal(path.stem().string(), stem) || !is_file(path)) continue;
        found.push_back({rank, path.filename().string()});
    }
    std::ranges::sort(found, [](const auto& left, const auto& right) {
        return left.rank != right.rank ? left.rank < right.rank : left.name < right.name;
    });
    for (const auto& item : found) result.candidates.push_back(authored.parent_path() / item.name);
    if (result.candidates.size() == 1) {
        result.outcome = audio_fallback_outcome_t::fallback;
        result.chosen = result.candidates.front();
    } else if (!result.candidates.empty()) {
        result.outcome = audio_fallback_outcome_t::ambiguous;
    }
    return result;
}

} // namespace wormedit::sys
