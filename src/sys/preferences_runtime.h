#pragma once

#include <wormedit/core/preferences.h>

#include "ui/frontend.h"

namespace wormedit::sys {

struct recent_file_t {
    std::string path;
    std::string label;
};

struct program_preferences_runtime_t {
    program_preferences_t preferences;
    std::filesystem::path path;
    std::vector<std::string> warnings;
    bool migrated{};
    std::vector<recent_file_t> recent_files;
};

void load_recent_files(program_preferences_runtime_t& runtime);
[[nodiscard]] auto remember_recent_file(program_preferences_runtime_t& runtime,
    const std::filesystem::path& path) -> std::string;
void build_recent_files_view(const program_preferences_runtime_t& runtime, wormedit::ui::workspace_view_t& view);

[[nodiscard]] auto load_program_preferences_runtime() -> program_preferences_runtime_t;
// Validation failure keeps previous values. Publication failure keeps accepted new values.
[[nodiscard]] auto apply_program_preferences(program_preferences_runtime_t& runtime,
    program_preferences_t candidate) -> std::string;

} // namespace wormedit::sys
