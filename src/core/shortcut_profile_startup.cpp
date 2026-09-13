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

auto load_shortcut_profile_for_startup(const std::filesystem::path& path,
                                       command_registry_t& registry)
    -> shortcut_profile_startup_result_t {
    shortcut_profile_startup_result_t result;
    const auto loaded = load_shortcut_profile(path);
    switch (loaded.status) {
    case shortcut_profile_load_status_t::missing:
        result.status = shortcut_profile_startup_status_t::missing;
        return result;
    case shortcut_profile_load_status_t::invalid:
        result.status = shortcut_profile_startup_status_t::rejected;
        result.diagnostics = loaded.diagnostics;
        if (result.diagnostics.empty()) {
            result.diagnostics =
                fallback_diagnostic(diagnostic_severity_t::warning,
                                    "Shortcut preferences are invalid and were ignored.");
        }
        break;
    case shortcut_profile_load_status_t::resource_limit:
        result.status = shortcut_profile_startup_status_t::rejected;
        result.diagnostics =
            fallback_diagnostic(diagnostic_severity_t::warning,
                                "Shortcut preferences exceed the supported size and were ignored.");
        break;
    case shortcut_profile_load_status_t::io_error:
        result.status = shortcut_profile_startup_status_t::unavailable;
        result.diagnostics =
            fallback_diagnostic(diagnostic_severity_t::warning,
                                "Shortcut preferences could not be read and were ignored.");
        break;
    case shortcut_profile_load_status_t::loaded:
        if (!loaded.profile) {
            result.status = shortcut_profile_startup_status_t::rejected;
            result.diagnostics =
                fallback_diagnostic(diagnostic_severity_t::warning,
                                    "Shortcut preferences could not be applied and were ignored.");
            break;
        }
        {
            auto applied = apply_shortcut_profile(registry, *loaded.profile);
            result.diagnostics = std::move(applied.diagnostics);
            result.status = applied.applied ? shortcut_profile_startup_status_t::applied
                                            : shortcut_profile_startup_status_t::rejected;
            if (!applied.applied && result.diagnostics.empty()) {
                result.diagnostics = fallback_diagnostic(
                    diagnostic_severity_t::warning,
                    "Shortcut preferences could not be applied and were ignored.");
            }
        }
        break;
    }
    bound_diagnostics(result.diagnostics);
    return result;
}

} // namespace wormedit
