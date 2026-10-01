#pragma once

#include <wormedit/core/preferences.h>

namespace wormedit::sys {

struct program_preferences_runtime_t {
    program_preferences_t preferences;
    std::filesystem::path path;
    std::vector<std::string> warnings;
    bool migrated{};
};

[[nodiscard]] auto load_program_preferences_runtime() -> program_preferences_runtime_t;
// Validation failure keeps previous values. Publication failure keeps accepted new values.
[[nodiscard]] auto apply_program_preferences(program_preferences_runtime_t& runtime,
    program_preferences_t candidate) -> std::string;

} // namespace wormedit::sys
