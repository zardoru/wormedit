#include "sys/preferences_runtime.h"
#include "sys/platform_shortcut_profile.h"

#include <exception>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace wormedit::sys {

auto load_program_preferences_runtime() -> program_preferences_runtime_t {
    program_preferences_runtime_t runtime;
    try {
        const auto shortcut_path = prepare_platform_shortcut_profile_path();
        if (!shortcut_path || shortcut_path->empty()) {
            runtime.warnings.push_back("Program preferences directory unavailable: " +
                                       (shortcut_path ? std::string{} : shortcut_path.error()));
            return runtime;
        }
        runtime.path = shortcut_path->parent_path() / "preferences.json";
        auto loaded = load_program_preferences_for_startup(runtime.path,
            runtime.path.parent_path() / "external-players.conf");
        auto& contents = loaded.contents ? *loaded.contents : loaded.contents.error();
        runtime.preferences = std::move(contents.preferences);
        runtime.warnings = std::move(contents.warnings);
        runtime.migrated = loaded.migrated;
        load_recent_files(runtime);
    } catch (const std::exception&) {
        runtime.warnings.push_back("Program preferences could not be loaded; defaults retained.");
    }
    return runtime;
}

auto apply_program_preferences(program_preferences_runtime_t& runtime,
    program_preferences_t candidate) -> std::string {
    const auto serialized = serialize_program_preferences(candidate);
    if (!serialized) return "Program preferences contain invalid or oversized values.";
    runtime.preferences = std::move(candidate);
    if (runtime.path.empty()) return "Program preferences changed, but their directory is unavailable.";
    const auto saved = create_file_atomic(runtime.path, *serialized, maximum_preferences_bytes);
    if (!saved) return "Program preferences changed, but saving failed: " + saved.error();
    return {};
}
namespace {
auto normalized_recent_path(const std::filesystem::path& path) -> std::string {
    if (path.empty()) return {};
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) return {};
    auto normalized = std::filesystem::weakly_canonical(absolute, error);
    const auto text = (error ? absolute.lexically_normal() : normalized).string();
    if (text.size() >= sizeof(WormUiWorkspaceAction{}.project_path) || text.find('\0') != std::string::npos) return {};
    return text;
}
void prepend_recent(program_preferences_runtime_t& runtime, std::string path) {
    std::erase_if(runtime.recent_files, [&](const auto& entry) { return entry.path == path; });
    const auto label = std::filesystem::path{path}.filename().string();
    runtime.recent_files.insert(runtime.recent_files.begin(), {std::move(path), label});
    if (runtime.recent_files.size() > 100) runtime.recent_files.resize(100);
}
}

void load_recent_files(program_preferences_runtime_t& runtime) {
    runtime.recent_files.clear();
    if (runtime.path.empty()) return;
    const auto path = runtime.path.parent_path() / "recent-files.json";
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return;
    if (std::filesystem::file_size(path, error) > maximum_preferences_bytes || error) {
        runtime.warnings.push_back("Recent files history is unreadable or oversized.");
        return;
    }
    std::ifstream input{path};
    const auto json = nlohmann::json::parse(input, nullptr, false);
    if (!json.is_array() || json.size() > 100) {
        runtime.warnings.push_back("Recent files history is invalid; ignored.");
        return;
    }
    for (auto it = json.rbegin(); it != json.rend(); ++it) {
        if (!it->is_string()) continue;
        auto normalized = normalized_recent_path(it->get<std::string>());
        if (!normalized.empty()) prepend_recent(runtime, std::move(normalized));
    }
}

auto remember_recent_file(program_preferences_runtime_t& runtime,
                          const std::filesystem::path& path) -> std::string {
    auto normalized = normalized_recent_path(path);
    if (normalized.empty()) return "Recent file path could not be recorded.";
    prepend_recent(runtime, std::move(normalized));
    if (runtime.path.empty()) return "Recent files changed, but their directory is unavailable.";
    auto json = nlohmann::json::array();
    for (const auto& entry : runtime.recent_files) json.push_back(entry.path);
    const auto saved = create_file_atomic(runtime.path.parent_path() / "recent-files.json",
                                       json.dump(2), maximum_preferences_bytes);
    return saved ? std::string{} : "Could not save recent files: " + saved.error();
}

void build_recent_files_view(const program_preferences_runtime_t& runtime, WormUiWorkspaceView& view) {
    view.recent_file_count = std::min({runtime.recent_files.size(),
        static_cast<std::size_t>(runtime.preferences.general.recent_files_count), std::size(view.recent_files)});
    for (std::size_t index = 0; index < view.recent_file_count; ++index)
        view.recent_files[index] = {runtime.recent_files[index].label.c_str(), runtime.recent_files[index].path.c_str()};
}
} // namespace wormedit::sys
