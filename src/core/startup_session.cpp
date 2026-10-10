#include <wormedit/core/startup_session.h>

#include <ranges>
#include <system_error>
#include <utility>

namespace wormedit {
namespace {

// A crash after a valid primary save should leave a `.bak` for recovery. An existing backup,
// newer or not, is left alone: publishing never replaces a recovery artifact.
void backup_crashed_primary(native_project_startup_result_t& result,
                            const native_project_recovery_candidate_t& primary,
                            const std::filesystem::path& primary_path) {
    const auto backup = native_project_adjacent_backup_path(primary_path);
    const auto present = std::ranges::find_if(result.recovery_scan.candidates, [&](const auto& item) {
        return item.kind == native_project_recovery_candidate_kind_t::adjacent_backup &&
               item.status != native_project_recovery_candidate_status_t::missing;
    });
    std::error_code error;
    if (present != result.recovery_scan.candidates.end() || std::filesystem::exists(backup, error))
        return;
    native_project_recovery_metadata_t metadata;
    metadata.intended_project_path = primary_path;
    metadata.last_explicit_save_revision = primary.version->revision;
    result.created_backup_from_primary =
        publish_native_project_recovery_atomic(backup, *primary.document, metadata).published;
}

} // namespace

auto prepare_native_project_startup(song_document_t fallback,
                                    native_project_startup_request_t request)
    -> native_project_startup_result_t {
    native_project_startup_result_t result{.document = std::move(fallback)};
    result.recovery_scan_configured = !request.primary_path.empty() ||
                                      !request.recovery_entries.empty() ||
                                      !request.recovery_directories.empty();
    std::error_code marker_error;
    if (!request.crash_marker_path.empty())
        result.previous_run_crashed =
            std::filesystem::is_regular_file(request.crash_marker_path, marker_error);
    if (!result.recovery_scan_configured)
        return result;

    const auto primary_path = request.primary_path;
    result.recovery_scan = scan_native_project_recovery_candidates(
        {.primary_path = std::move(request.primary_path),
         .caller_supplied_entries = std::move(request.recovery_entries),
         .caller_supplied_directories = std::move(request.recovery_directories)},
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
    if (result.previous_run_crashed) backup_crashed_primary(result, *primary, primary_path);
    return result;
}

} // namespace wormedit
