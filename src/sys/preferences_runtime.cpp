#include "sys/preferences_runtime.h"
#include "sys/platform_shortcut_profile.h"

#include <exception>

namespace wormedit::sys {

auto load_program_preferences_runtime() -> program_preferences_runtime_t {
    program_preferences_runtime_t runtime;
    try {
        const auto shortcut_path = prepare_platform_shortcut_profile_path();
        if (!shortcut_path) {
            runtime.warnings.push_back("Program preferences directory unavailable: " + shortcut_path.error);
            return runtime;
        }
        runtime.path = shortcut_path.path.parent_path() / "preferences.json";
        auto loaded = load_program_preferences_for_startup(runtime.path,
            runtime.path.parent_path() / "external-players.conf");
        runtime.preferences = std::move(loaded.contents.preferences);
        runtime.warnings = std::move(loaded.contents.warnings);
        runtime.migrated = loaded.migrated;
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
    const auto saved = save_file_atomic(runtime.path, *serialized, maximum_preferences_bytes);
    if (!saved) return "Program preferences changed, but saving failed: " + saved.error;
    return {};
}
} // namespace wormedit::sys
