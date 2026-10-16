#include <wormedit/core/shortcut_profile_startup.h>

#include <algorithm>
#include <string>
#include <utility>

namespace wormedit {
namespace {

auto make_notice(diagnostic_severity_t severity, diagnostic_code_t code, std::string message)
    -> diagnostic_t {
    return {.severity = severity, .code = code, .message = std::move(message)};
}

void bound_diagnostics(diagnostic_list_t& diagnostics) {
    constexpr auto maximum_count = maximum_shortcut_profile_startup_diagnostics;
    constexpr auto maximum_bytes = maximum_shortcut_profile_startup_diagnostic_bytes;
    const bool omitted = diagnostics.size() > maximum_count;
    if (omitted) {
        diagnostics.resize(maximum_count - 1);
        diagnostics.push_back(make_notice(diagnostic_severity_t::warning,
                                          diagnostic_code_t::unsupported_feature,
                                          "Additional shortcut profile diagnostics were omitted."));
    }
    for (auto& diagnostic : diagnostics) {
        if (diagnostic.message.size() > maximum_bytes) {
            diagnostic.message.resize(maximum_bytes - 3);
            diagnostic.message += "...";
        }
    }
}

auto fallback_diagnostic(diagnostic_severity_t severity, std::string message) -> diagnostic_list_t {
    return {make_notice(severity, diagnostic_code_t::invalid_binding, std::move(message))};
}

} // namespace

auto apply_startup_shortcut_profile(const std::filesystem::path& path, command_registry_t& registry)
    -> shortcut_profile_startup_outcome_t {
    shortcut_profile_startup_outcome_t result{.value = failure_code_t::unavailable};
    auto loaded = open_shortcut_profile(path);
    if (loaded) {
        auto applied = apply_shortcut_profile(registry, *loaded);
        result.diagnostics = std::move(applied.diagnostics);
        result.value = applied.value
                           ? operation_state_t{shortcut_profile_startup_phase_t::applied}
                           : failure_code_t::invalid_data;
        if (!applied.value && result.diagnostics.empty()) {
            result.diagnostics =
                fallback_diagnostic(diagnostic_severity_t::warning,
                                    "Shortcut preferences could not be applied and were ignored.");
        }
    } else {
        switch (loaded.error().value.code) {
        case failure_code_t::not_found:
            result.value = shortcut_profile_startup_phase_t::missing;
            return result;
        case failure_code_t::invalid_data:
            result.value = failure_code_t::invalid_data;
            result.diagnostics = std::move(loaded.error().diagnostics);
            if (result.diagnostics.empty()) {
                result.diagnostics =
                    fallback_diagnostic(diagnostic_severity_t::warning,
                                        "Shortcut preferences are invalid and were ignored.");
            }
            break;
        case failure_code_t::capacity_exceeded:
            result.value = failure_code_t::invalid_data;
            result.diagnostics =
                fallback_diagnostic(diagnostic_severity_t::warning,
                                    "Shortcut preferences exceed the supported size and were ignored.");
            break;
        case failure_code_t::io_failed:
            result.value = failure_code_t::unavailable;
            result.diagnostics =
                fallback_diagnostic(diagnostic_severity_t::warning,
                                    "Shortcut preferences could not be read and were ignored.");
            break;
        default:
            break;
        }
    }
    bound_diagnostics(result.diagnostics);
    return result;
}

} // namespace wormedit
