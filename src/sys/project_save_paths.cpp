#include "sys/project_save_paths.h"
#include <wormedit/core/resource_file_copy.h>

#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

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

auto resolved_asset_path(const std::filesystem::path& path, const std::filesystem::path& base)
    -> std::filesystem::path {
    const auto absolute = path.is_absolute() ? path : base / path;
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : resolved;
}

auto is_inside_project(const std::filesystem::path& relative) -> bool {
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

struct project_save_assets_t {
    project_save_report_t& report;
    std::filesystem::path directory;
    std::stop_token stop;

    auto copy_path(asset_id_t id, std::filesystem::path& path) -> bool {
        if (path.empty()) return false;
        const auto source = resolved_asset_path(path, directory);
        const auto relative = source.lexically_relative(directory);
        if (is_inside_project(relative)) {
            report.relinks.emplace(source, relative);
            return std::exchange(path, relative) != relative;
        }
        if (const auto found = report.relinks.find(source); found != report.relinks.end()) {
            path = found->second;
            return true;
        }
        const auto copied = create_project_asset_copy(
            {.project_directory = directory, .source_path = source, .asset = id,
             .reuse_identical_files = true}, stop);
        if (!copied) {
            report.copy_errors.push_back({copied.error().code,
                "Could not copy " + source.string() + ": " + copied.error().message});
            return false;
        }
        report.relinks.emplace(source, copied->relative_path);
        path = copied->relative_path;
        if (copied->status == project_asset_copy_phase_t::copied)
            report.copied_files.push_back(copied->relative_path);
        return true;
    }

    auto copy_assets(song_document_t& document) -> bool {
        bool changed = false;
        for (auto& asset : document.audio_assets) changed |= copy_path(asset.id, asset.source_path);
        for (auto& asset : document.visual_assets) changed |= copy_path(asset.id, asset.source_path);
        for (auto& asset : document.midi_assets) changed |= copy_path(asset.id, asset.source_path);
        for (auto& recipe : document.generator_recipes)
            changed |= copy_path(asset_id_t{recipe.id.value}, recipe.source_path);
        return changed;
    }
};

void relink_saved_path(std::filesystem::path& path, const std::filesystem::path& directory,
                       const project_save_report_t& report) {
    if (path.empty()) return;
    const auto source = resolved_asset_path(path, directory);
    if (const auto found = report.relinks.find(source); found != report.relinks.end())
        path = found->second;
}

void publish_saved_paths(song_document_t& document, const std::filesystem::path& old_base,
                          const project_save_report_t& report) {
    const auto directory = resolved_asset_path(report.destination.parent_path(), {});
    rebase_project_paths(document, old_base, directory);
    for (auto& asset : document.audio_assets) relink_saved_path(asset.source_path, directory, report);
    for (auto& asset : document.visual_assets) relink_saved_path(asset.source_path, directory, report);
    for (auto& asset : document.midi_assets) relink_saved_path(asset.source_path, directory, report);
    for (auto& recipe : document.generator_recipes) relink_saved_path(recipe.source_path, directory, report);
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

auto project_portability_notice_t::warning_message() noexcept -> std::string_view {
    return "Some assets are linked outside the project folder. Save or Save As copies them into "
           "assets/ and relinks the project. If a copy fails, the original link is retained.";
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

auto create_project_save(const project_save_request_t& request, std::stop_token stop)
    -> project_save_report_t {
    std::error_code error;
    project_save_report_t report{
        .destination = std::filesystem::absolute(request.destination, error).lexically_normal()};
    if (error) {
        report.storage.diagnostics.push_back({.code = native_project_storage_diagnostic_code_t::invalid_destination,
            .message = "Could not resolve project destination: " + error.message(), .path = request.destination});
        return report;
    }
    auto document = request.document;
    rebase_project_paths(document, request.source_base, report.destination.parent_path());
    report.storage = create_native_project_file(report.destination, document, request.storage_options);
    if (!report.storage.value) return report;
    project_save_assets_t assets{report, resolved_asset_path(report.destination.parent_path(), {}), stop};
    if (!assets.copy_assets(document)) return report;
    const auto linked = create_native_project_file(report.destination, document, request.storage_options);
    if (linked.value) report.storage = linked;
    else {
        report.relinks.clear();
        report.copy_errors.push_back({failure_code_t::publication_failed,
            "Could not save copied-asset links in " + report.destination.string() + ": " +
            (linked.diagnostics.empty() ? "Project write failed." : linked.diagnostics.front().message)});
    }
    return report;
}

void project_save_report_t::publish_paths(document_history_t& history,
                                         const std::filesystem::path& old_base) const {
    if (!storage.value) return;
    publish_saved_paths(history.document, old_base, *this);
    for (auto& entry : history.undo) {
        publish_saved_paths(entry.before, old_base, *this);
        publish_saved_paths(entry.after, old_base, *this);
    }
    for (auto& entry : history.redo) {
        publish_saved_paths(entry.before, old_base, *this);
        publish_saved_paths(entry.after, old_base, *this);
    }
}

auto project_save_report_t::status_message() const -> std::string {
    if (!storage.value)
        return storage.diagnostics.empty() ? "Could not save the project." : storage.diagnostics.front().message;
    auto message = "Project saved to " + destination.string() + ".";
    if (!copied_files.empty()) {
        message += " Copied:";
        for (const auto& path : copied_files) message += " " + path.generic_string();
        message += ".";
    }
    for (const auto& error : copy_errors) message += " " + error.message;
    if (!storage.diagnostics.empty()) message += " " + storage.diagnostics.front().message;
    return message;
}

} // namespace wormedit::sys
