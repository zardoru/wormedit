#include <wormedit/core/startup_session.h>

#include <ranges>
#include <utility>

namespace wormedit {

auto prepare_native_project_startup(song_document_t fallback,
                                    native_project_startup_request_t request)
    -> native_project_startup_result_t {
    native_project_startup_result_t result{.document = std::move(fallback)};
    result.recovery_scan_configured =
        !request.primary_path.empty() || !request.recovery_entries.empty();
    if (!result.recovery_scan_configured)
        return result;

    result.recovery_scan = scan_native_project_recovery_candidates(
        {.primary_path = std::move(request.primary_path),
         .caller_supplied_entries = std::move(request.recovery_entries)},
        std::move(request.scan_options));
    const auto primary =
        std::ranges::find_if(result.recovery_scan.candidates, [](const auto& item) {
            return item.kind == native_project_recovery_candidate_kind_t::primary;
        });
    if (primary == result.recovery_scan.candidates.end() ||
        primary->status != native_project_recovery_candidate_status_t::valid ||
        !primary->document || !primary->version)
        return result;

    result.document = *primary->document;
    result.primary_version = primary->version;
    result.loaded_primary = true;
    return result;
}

} // namespace wormedit
