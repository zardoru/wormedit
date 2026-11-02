#include <wormedit/core/metadata_inheritance.h>
#include <wormedit/formats/conversion_publication.h>
#include "../core/text_util.h"
#include "../core/checked_math.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/rational.h>

#include <algorithm>
#include <set>
#include <map>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace wormedit {
namespace {

auto is_valid_relative(const std::filesystem::path& path) -> bool {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path != path.lexically_normal())
        return false;
    for (const auto& part : path) {
        if (part.empty() || part == "." || part == "..")
            return false;
        const auto bytes = part.string();
        if (std::ranges::any_of(bytes, [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
            return false;
    }
    return true;
}

auto is_safe_existing_components(const std::filesystem::path& root,
                              const std::filesystem::path& relative, bool final_must_be_regular,
                              std::string& error_message) -> bool {
    namespace fs = std::filesystem;
    auto current = root;
    std::error_code error;
    const auto root_status = fs::symlink_status(current, error);
    if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status)) {
        error_message = "Project directory is unavailable or is a symlink.";
        return false;
    }
    for (auto it = relative.begin(); it != relative.end(); ++it) {
        current /= *it;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            if (final_must_be_regular) {
                error_message = "Staged asset file is missing.";
                return false;
            }
            return true;
        }
        if (error || fs::is_symlink(status)) {
            error_message = "Output path contains an unreadable or symlink component.";
            return false;
        }
        if (std::next(it) == relative.end()) {
            if (!fs::is_regular_file(status)) {
                error_message = "Output path has an incompatible existing entry.";
                return false;
            }
        } else if (!fs::is_directory(status)) {
            error_message = "Output path contains a non-directory component.";
            return false;
        }
    }
    return true;
}

auto append_conversion_files(const std::filesystem::path& project_directory,
                            const output_group_t& group, const conversion_request_t& request,
                            const conversion_result_t& converted,
                            conversion_publication_plan_payload_t& plan,
                            std::set<std::filesystem::path>& destinations)
    -> std::optional<std::string> {
    namespace fs = std::filesystem;
    const bool file_path = group.destination_kind == output_destination_kind_t::file;
    const auto output_directory =
        file_path ? group.relative_output_path.parent_path() : group.relative_output_path;
    for (std::size_t index = 0; index < converted.outputs.size(); ++index) {
        const auto& output = converted.outputs[index];
        const fs::path suggested{output.suggested_filename};
        if (output.target != request.target || !output.reimport_validated ||
            !is_valid_relative(suggested) || suggested.has_parent_path() ||
            (output.chart && !group.charts.empty() &&
             std::ranges::find(group.charts, *output.chart) == group.charts.end())) {
            return "Converted output has an invalid target, chart, or filename.";
        }
        const auto relative = file_path && (output.chart || converted.outputs.size() == 1)
                                  ? group.relative_output_path : output_directory / suggested;
        std::string error;
        if (!is_valid_relative(relative) || !destinations.insert(relative).second ||
            !is_safe_existing_components(project_directory, relative, false, error)) {
            return error.empty() ? "Output filenames collide or are invalid." : error;
        }
        std::error_code fs_error;
        if (fs::exists(project_directory / relative, fs_error) && !request.overwrite) {
            return "Output file already exists and overwrite is disabled.";
        }
        if (fs_error) {
            return "Could not inspect output destination.";
        }
        plan.files.push_back({index, relative});
    }
    return std::nullopt;
}

auto append_generated_file(const std::filesystem::path& name,
    const std::filesystem::path& project_directory, const std::filesystem::path& directory,
    bool overwrite, conversion_publication_plan_payload_t& plan,
    std::set<std::filesystem::path>& destinations, bool typed_nested = false) -> std::optional<std::string> {
    const auto relative = directory / name;
    std::string error;
    if (!is_valid_relative(name) || (typed_nested ? !conversion_publication_plan_payload_t::can_track_path(name) : name.has_parent_path()) ||
        !destinations.insert(relative).second ||
        !is_safe_existing_components(project_directory, relative, false, error))
        return error.empty() ? "Generated asset filenames collide or are invalid." : error;
    std::error_code fs_error;
    if (std::filesystem::exists(project_directory / relative, fs_error) && !overwrite)
        return "Generated asset exists and overwrite is disabled.";
    if (fs_error) return "Could not inspect generated asset destination.";
    plan.generated_files.push_back(relative);
    return std::nullopt;
}

auto validate_declared_copy_promises(const conversion_result_t& converted) -> std::optional<std::string> {
    std::map<asset_id_t, std::string> hashes;
    for (const auto& copy : converted.asset_copies) {
        if (!copy.expected_hash) continue;
        if (!copy.asset || !text_util::is_sha256_digest(*copy.expected_hash)) return "Invalid frozen audio copy receipt.";
        const auto hash = text_util::ascii_lowercase(*copy.expected_hash);
        const auto [found, inserted] = hashes.emplace(copy.asset, hash);
        if (!inserted && found->second != hash) return "One audio asset has conflicting frozen copy hashes.";
    }
    for (const auto& copy : converted.asset_copies)
        if (const auto found = hashes.find(copy.asset); found != hashes.end() &&
            (!copy.expected_hash || !text_util::ascii_iequal(found->second, *copy.expected_hash)))
            return "An audio copy alias omits or changes its frozen content promise.";
    return {};
}

auto append_generated_copies(const std::filesystem::path& project_directory,
    const std::filesystem::path& directory, const conversion_request_t& request,
    const conversion_result_t& converted, conversion_publication_plan_payload_t& plan,
    std::set<std::filesystem::path>& destinations) -> std::optional<std::string> {
    for (const auto& copy : converted.asset_copies) {
        const auto staged = copy.expected_hash && std::ranges::any_of(request.staged_assets, [&](const auto& item) {
            return item.asset == copy.asset && item.relative_path == copy.relative_path;
        });
        if (!staged)
            if (auto failure = append_generated_file(copy.relative_path, project_directory, directory,
                    request.overwrite, plan, destinations, copy.expected_hash.has_value())) return failure;
    }
    return std::nullopt;
}

auto append_generated_files(const std::filesystem::path& project_directory,
    const output_group_t& group, const conversion_request_t& request,
    const conversion_result_t& converted, conversion_publication_plan_payload_t& plan,
    std::set<std::filesystem::path>& destinations) -> std::optional<std::string> {
    const auto directory = group.destination_kind == output_destination_kind_t::file
        ? group.relative_output_path.parent_path() : group.relative_output_path;
    for (const auto& slice_plan : converted.slice_plans)
        for (const auto& span : slice_plan.value.spans)
            if (auto failure = append_generated_file(span.filename, project_directory, directory,
                    request.overwrite, plan, destinations)) return failure;
    for (const auto& cut : converted.cut_audio_plans)
        if (auto failure = append_generated_file(cut.filename, project_directory, directory,
                request.overwrite, plan, destinations)) return failure;
    for (const auto& name : converted.silent_files)
        if (auto failure = append_generated_file(name, project_directory, directory,
                request.overwrite, plan, destinations)) return failure;
    if (auto failure = validate_declared_copy_promises(converted)) return failure;
    return append_generated_copies(project_directory, directory, request, converted, plan, destinations);
}

auto validate_pending_assets(const conversion_request_t& request,
                             std::span<const project_asset_copy_request_t> pending)
    -> std::optional<std::string> {
    std::set<asset_id_t> seen;
    for (const auto& copy : pending) {
        const auto matching = std::ranges::count_if(request.staged_assets, [&](const auto& staged) {
            return staged.asset == copy.asset &&
                   staged.relative_path == std::filesystem::path{"assets"} / copy.destination_filename();
        });
        if (!seen.insert(copy.asset).second || matching != 1)
            return "Pending asset must have one matching staged mapping.";
    }
    return std::nullopt;
}

auto append_staged_assets(const std::filesystem::path& project_directory,
                          const output_group_t& group, const conversion_request_t& request,
                          conversion_publication_plan_payload_t& plan,
                          std::set<std::filesystem::path>& destinations,
                          std::span<const project_asset_copy_request_t> pending_assets)
    -> std::optional<std::string> {
    namespace fs = std::filesystem;
    const auto asset_directory = group.destination_kind == output_destination_kind_t::directory
        ? std::optional{group.relative_output_path / "assets"}
        : export_asset_directory(group.relative_output_path, group.destination_kind);
    if (!asset_directory) {
        return "Output asset directory is invalid.";
    }
    std::set<asset_id_t> asset_ids;
    std::set<fs::path> asset_destinations;
    const auto output_directory = group.destination_kind == output_destination_kind_t::file
                                      ? group.relative_output_path.parent_path()
                                      : group.relative_output_path;
    for (const auto& staged : request.staged_assets) {
        const bool pending = std::ranges::any_of(pending_assets, [&](const auto& copy) {
            return copy.asset == staged.asset;
        });
        const auto relative = output_directory / staged.relative_path;
        std::string error;
        if (!staged.asset || !asset_ids.insert(staged.asset).second ||
            !is_valid_relative(staged.relative_path) ||
            staged.relative_path.parent_path() != fs::path{"assets"} || !is_valid_relative(relative) ||
            relative.parent_path() != *asset_directory || destinations.contains(relative) ||
            !asset_destinations.insert(relative).second ||
            !is_safe_existing_components(project_directory, relative, !pending, error)) {
            return error.empty() ? "Staged asset path is invalid or missing." : error;
        }
        plan.assets.push_back({staged.asset, relative});
    }
    return std::nullopt;
}

// Conversion copies these beside the osu! chart, where osu! looks them up by name.
auto osu_skin_sample_asset_ids(const song_document_t& song, const output_group_t& group) -> std::set<asset_id_t> {
    std::set<asset_id_t> result;
    if (group.family != format_family_t::osu_mania) return result;
    for (const auto& found : build_osu_skin_sample_assets(song)) result.insert(found.asset);
    return result;
}

auto referenced_export_assets(const song_document_t& song, const output_group_t& group)
    -> result_t<std::set<asset_id_t>, std::string> {
    std::set<asset_id_t> referenced;
    const auto skin_samples = osu_skin_sample_asset_ids(song, group);
    for (const auto& resource : song.resources) {
        if (resource.asset && !skin_samples.contains(resource.asset)) referenced.insert(resource.asset);
    }
    for (const auto chart_id : group.charts) {
        const auto chart = song.read_chart(chart_id);
        if (chart == nullptr)
            return std::unexpected(std::string{"Output group refers to a missing chart."});
        const auto audio = metadata_audio_references_t::from(song, chart->metadata.overrides);
        if (!audio.ok()) return std::unexpected(audio.diagnostics.front().message);
        referenced.insert(audio.value.assets.begin(), audio.value.assets.end());
        for (const auto& channel : chart->sound_channels) {
            if (channel.source_clip.source) referenced.insert(channel.source_clip.source);
        }
    }
    return referenced;
}

auto append_discovered_asset(const song_document_t& song, const output_group_t& group,
    const std::filesystem::path& project_directory, asset_id_t asset, std::stop_token stop,
    std::vector<conversion_request_t::staged_asset_t>& mappings) -> result_t<void, std::string> {
    const auto audio = song.find_audio_asset(asset);
    const auto visual = std::ranges::find(song.visual_assets, asset, &visual_asset_t::id);
    if ((audio == nullptr) == (visual == song.visual_assets.end()))
        return std::unexpected(std::string{"A resource has no unique source asset."});
    const auto& source = audio != nullptr ? audio->source_path : visual->source_path;
    const auto inspected = inspect_export_asset_file({.project_directory = project_directory,
        .source_path = source, .relative_output_path = group.relative_output_path,
        .destination_kind = group.destination_kind, .asset = asset}, stop);
    if (!inspected && inspected.error().code == failure_code_t::not_found) return {};
    if (!inspected)
        return std::unexpected(std::string{"Export asset #" + std::to_string(asset.value) + " (" + source.string() + "): " + inspected.error().message});
    const auto relative = export_asset_reference_path(
        group.relative_output_path, inspected->relative_path, group.destination_kind);
    if (!relative)
        return std::unexpected(std::string{"Verified export asset has an invalid chart-relative path."});
    mappings.push_back({asset, *relative});
    return {};
}

auto has_storyboard_path_conflict(const std::set<std::filesystem::path>& destinations,
    const std::filesystem::path& relative) -> bool {
    const auto name = relative.generic_string();
    const auto next = destinations.lower_bound(relative);
    if (next != destinations.end() && (*next == relative || next->generic_string().starts_with(name + "/"))) return true;
    for (auto parent = relative.parent_path(); !parent.empty(); parent = parent.parent_path()) {
        if (destinations.contains(parent)) return true;
        if (parent == parent.root_path()) break;
    }
    return false;
}
struct storyboard_media_context_t {
    const std::filesystem::path& project_directory;
    const output_group_t& group;
    const conversion_request_t& request;
    std::set<std::filesystem::path>& destinations;
    auto append_file(const std::filesystem::path& image, const project_path_copy_request_t* prepared,
        std::vector<std::filesystem::path>& output) const -> std::optional<std::string> {
        if (!conversion_publication_plan_payload_t::can_track_path(image))
            return "Storyboard media has an unsafe package path.";
        if (!prepared || !prepared->expected_source)
            return "Storyboard media has no prepared source receipt: " + image.string();
        const auto source = inspect_project_path_source(*prepared);
        if (!source) return source.error().message;
        const auto directory = group.destination_kind == output_destination_kind_t::file
            ? group.relative_output_path.parent_path() : group.relative_output_path;
        const auto relative = directory / image;
        if (has_storyboard_path_conflict(destinations, relative))
            return "Storyboard media collides with another package file: " + image.string();
        std::string error;
        if (!is_safe_existing_components(project_directory, relative, false, error)) return error;
        std::error_code status;
        if (std::filesystem::exists(project_directory / relative, status) && !request.overwrite)
            return "Storyboard media exists and overwrite is disabled: " + image.string();
        if (status) return "Could not inspect storyboard media destination.";
        destinations.insert(relative);
        output.push_back(relative);
        return std::nullopt;
    }
    auto append(std::span<const std::filesystem::path> files,
        std::span<const project_path_copy_request_t> sources,
        std::vector<std::filesystem::path>& output) const -> std::optional<std::string> {
        const auto maximum = osbx_validation_options_t{}.maximum_images;
        if (files.size() > maximum - std::min(destinations.size(), maximum) || sources.size() > maximum)
            return "Storyboard media count exceeds its bounds.";
        std::map<std::filesystem::path, const project_path_copy_request_t*> by_path;
        for (const auto& source : sources) {
            const auto [position, inserted] = by_path.emplace(source.relative_path, &source);
            if (!inserted && position->second->expected_source != source.expected_source)
                return "Storyboard media has conflicting source receipts.";
        }
        for (const auto& image : files) {
            const auto found = by_path.find(image);
            if (auto failure = append_file(image, found == by_path.end() ? nullptr : found->second, output)) return failure;
        }
        return std::nullopt;
    }
};

auto append_storyboard_images(const std::filesystem::path& project_directory,
    const output_group_t& group, const conversion_request_t& request,
    const conversion_result_t& converted, conversion_publication_plan_payload_t& plan,
    std::set<std::filesystem::path>& destinations) -> std::optional<std::string> {
    const storyboard_media_context_t media{project_directory, group, request, destinations};
    if (auto failure = media.append(converted.storyboard_images, request.osbx_image_sources, plan.storyboard_images)) return failure;
    return media.append(converted.storyboard_samples, request.osbx_sample_sources, plan.storyboard_samples);
}

auto append_storyboard_files(const std::filesystem::path& project_directory,
    const output_group_t& group, const conversion_request_t& request,
    const conversion_result_t& converted, conversion_publication_plan_payload_t& plan,
    std::set<std::filesystem::path>& destinations) -> std::optional<std::string> {
    const auto directory = group.destination_kind == output_destination_kind_t::file
        ? group.relative_output_path.parent_path() : group.relative_output_path;
    for (std::size_t index = 0; index < converted.storyboard_files.size(); ++index) {
        const auto& file = converted.storyboard_files[index];
        const auto relative = directory / file.relative_path;
        if (!conversion_publication_plan_payload_t::can_track_path(file.relative_path))
            return "Native actor has an unsafe package path";
        const auto name = relative.generic_string();
        if (std::ranges::any_of(destinations, [&](const auto& path) {
            const auto existing = path.generic_string();
            return name == existing || name.starts_with(existing + "/") || existing.starts_with(name + "/");
        })) return "Native actor collides with another package file";
        std::string failure;
        if (!is_safe_existing_components(project_directory, relative, false, failure)) return failure;
        std::error_code error;
        if (std::filesystem::exists(project_directory / relative, error) && !request.overwrite)
            return "Native actor file exists and overwrite is disabled";
        if (error) return "Cannot inspect native actor destination";
        destinations.insert(relative);
        plan.storyboard_files.push_back({.output_index = index, .project_relative_path = relative});
    }
    return std::nullopt;
}

auto build_storyboard_playback_span(const song_document_t& song, const conversion_request_t& request)
    -> result_t<osbx_osb_span_t, std::string> {
    long double begin = 0, end = 0;
    bool selected = false;
    for (const auto* chart : song.read_charts()) {
        if (request.chart && chart->id != *request.chart) continue;
        if (!request.charts.empty() && !std::ranges::contains(request.charts, chart->id)) continue;
        const auto timing = timing_index_t::from(*chart);
        if (!timing.value) return std::unexpected("OSBX playback timing could not be built.");
        const auto first = timing.value->seconds_at(0, timing_boundary_side_t::before_delays);
        const auto last = timing.value->seconds_at(timing.value->total_rows, timing_boundary_side_t::after_stops);
        if (!first || !last) return std::unexpected("OSBX playback endpoints are unavailable.");
        begin = std::min(begin, to_long_double(*first.seconds) * 1000);
        end = std::max(end, to_long_double(*last.seconds) * 1000);
        selected = true;
    }
    for (const auto& resource : song.resources) {
        if (!resource.has_role(resource_role_t::music)) continue;
        const auto* audio = song.find_audio_asset(resource.asset);
        if (audio && audio->stream.frame_count && audio->stream.sample_rate)
            end = std::max(end, static_cast<long double>(*audio->stream.frame_count) *
                                  1000 / audio->stream.sample_rate);
    }
    if (!selected || !std::isfinite(begin) || !std::isfinite(end) ||
        std::floor(begin) < std::numeric_limits<std::int64_t>::min() ||
        std::ceil(end) > std::numeric_limits<std::int64_t>::max())
        return std::unexpected("OSBX playback span is unavailable or outside millisecond limits.");
    return osbx_osb_span_t{static_cast<std::int64_t>(std::floor(begin)),
                           static_cast<std::int64_t>(std::ceil(end))};
}

auto storyboard_source_copy(std::string_view file, const std::filesystem::path& directory)
    -> result_t<project_path_copy_request_t, std::string> {
    auto normalized = std::string{file};
    std::ranges::replace(normalized, '\\', '/');
    const auto path = text_util::path_from_utf8(normalized).lexically_normal();
    project_path_copy_request_t copy{.source_directory = directory, .relative_path = path};
    const auto source = inspect_project_path_source(copy);
    if (!source) return std::unexpected(source.error().message);
    copy.expected_source = project_asset_copy_receipt_t::from(path, *source,
        project_asset_copy_phase_t::copied).published_identity;
    if (!copy.expected_source) return std::unexpected("Storyboard source could not be identified");
    return copy;
}
auto probe_storyboard_image(const osbx_image_t& image, const project_path_copy_request_t& copy,
    conversion_request_t& request, const export_image_probe_t& probe, std::stop_token stop) -> std::optional<std::string> {
    if (!probe) return "OSBX export requires image decoding support to validate image dimensions.";
    if (stop.stop_requested()) return "OSBX export cancelled before image preparation.";
    const auto source = inspect_project_path_source(copy);
    if (!source) return source.error().message;
    const auto decoded = probe(*source);
    if (!decoded) return "OSBX image " + image.id + ": " + decoded.error();
    if (!inspect_project_path_source(copy)) return "OSBX image changed during preparation: " + image.id;
    request.osbx_image_sizes.push_back({.image_id = image.id,
        .width = decoded->first, .height = decoded->second, .file = image.file});
    return std::nullopt;
}
auto prepare_storyboard_image(const osbx_image_t& image, conversion_request_t& request,
    const std::filesystem::path& source_directory, const export_image_probe_t& probe,
    std::stop_token stop) -> std::optional<std::string> {
    if (!probe) return "OSBX export requires image decoding support to validate image dimensions.";
    if (stop.stop_requested()) return "OSBX export cancelled before image preparation.";
    auto copy = storyboard_source_copy(image.file, source_directory);
    if (!copy) return "OSBX image " + image.id + ": " + copy.error();
    if (auto failure = probe_storyboard_image(image, *copy, request, probe, stop)) return failure;
    request.osbx_image_sources.push_back(std::move(*copy));
    return std::nullopt;
}
class storyboard_media_sources_t {
    const std::filesystem::path& directory_;
    std::stop_token stop_;
    std::uintmax_t remaining_bytes_{conversion_publication_options_t{}.maximum_existing_bytes};
    std::size_t remaining_storage_;
    std::set<std::filesystem::path> counted_;
    auto reserve_storage(std::string_view file) -> bool {
        const auto overhead = sizeof(project_path_copy_request_t) + sizeof(std::filesystem::path) + sizeof(std::size_t) * 4;
        if (overhead > remaining_storage_) return false;
        const auto maximum = (remaining_storage_ - overhead) / sizeof(std::filesystem::path::value_type);
        auto units = directory_.native().size();
        if (units > maximum) return false;
        for (int copy = 0; copy < 3; ++copy)
            if (!checked_math::account_size(units, file.size(), maximum)) return false;
        remaining_storage_ -= overhead + units * sizeof(std::filesystem::path::value_type);
        return true;
    }
  public:
    storyboard_media_sources_t(const std::filesystem::path& directory, std::stop_token stop, std::size_t plan_bytes)
        : directory_(directory), stop_(stop), remaining_storage_(osbx_validation_options_t{}.maximum_expanded_storage_bytes -
            std::min(plan_bytes, osbx_validation_options_t{}.maximum_expanded_storage_bytes)) {}
    auto capture(std::string_view file, std::vector<project_path_copy_request_t>& output) -> std::optional<std::string> {
        if (stop_.stop_requested()) return "OSBX media preparation cancelled";
        if (!reserve_storage(file)) return "OSBX media source storage exceeds its bounds";
        auto copy = storyboard_source_copy(file, directory_);
        if (!copy) return copy.error();
        if (!copy->expected_source) return "OSBX media source receipt is missing";
        if (counted_.insert(copy->relative_path).second) {
            if (copy->expected_source->size > remaining_bytes_) return "OSBX media source bytes exceed the publication bound";
            remaining_bytes_ -= copy->expected_source->size;
        }
        output.push_back(std::move(*copy));
        return std::nullopt;
    }
};
using storyboard_image_dimensions_t = std::map<std::pair<std::string,std::string>, std::pair<std::uint32_t,std::uint32_t>>;
auto animation_dimensions(const storyboard_animation_assets_t& animation, const storyboard_media_plan_t& media,
    const storyboard_image_dimensions_t& sizes) -> result_t<std::pair<std::uint32_t,std::uint32_t>,std::string> {
    std::optional<std::pair<std::uint32_t,std::uint32_t>> dimensions;
    for (const auto index : animation.frames) {
        if (index >= media.images.size()) return std::unexpected("OSBX Animation frame receipt index is invalid");
        const auto& image = media.images[index];
        const auto found = sizes.find(std::pair{image.id,image.file});
        if (found == sizes.end() || found->second.first == 0 || found->second.second == 0)
            return std::unexpected("OSBX Animation dimensions are unavailable");
        if (dimensions && dimensions != found->second)
            return std::unexpected("OSBX Animation frames have differing dimensions; export cannot certify their shared metric");
        dimensions = found->second;
    }
    if (!dimensions) return std::unexpected("OSBX Animation has no concrete frames");
    return *dimensions;
}
auto validate_animation_dimensions(const storyboard_media_plan_t& media, conversion_request_t& request)
    -> std::optional<std::string> {
    storyboard_image_dimensions_t sizes;
    for (const auto& size : request.osbx_image_sizes) sizes.emplace(std::pair{size.image_id, size.file}, std::pair{size.width,size.height});
    for (const auto& animation : media.animations) {
        const auto dimensions = animation_dimensions(animation, media, sizes);
        if (!dimensions) return dimensions.error();
        const auto key = std::pair{animation.image.id,animation.image.file};
        if (const auto base = sizes.find(key); base != sizes.end() && base->second != *dimensions)
            return "OSBX Animation base/frame dimensions conflict";
        // Geometry scope proven from every frame, never a virtual file receipt.
        request.osbx_image_sizes.push_back({.image_id = animation.image.id, .width = dimensions->first,
            .height = dimensions->second, .file = animation.image.file});
    }
    return std::nullopt;
}
auto verify_storyboard_receipts(std::span<const project_path_copy_request_t> sources, std::stop_token stop)
    -> std::optional<std::string> {
    for (const auto& source : sources) {
        if (stop.stop_requested()) return "OSBX media preparation cancelled";
        if (!inspect_project_path_source(source)) return "OSBX media changed during preparation";
    }
    return std::nullopt;
}
auto prepare_storyboard_media(const storyboard_media_plan_t& media, conversion_request_t& request,
    const std::filesystem::path& source_directory, const export_image_probe_t& probe,
    std::stop_token stop) -> std::optional<std::string> {
    if (!media.images.empty() && !probe) return "OSBX export requires image decoding support to validate image dimensions.";
    storyboard_media_sources_t sources{source_directory, stop, media.logical_storage_bytes};
    for (const auto& image : media.images)
        if (auto failure = sources.capture(image.file, request.osbx_image_sources)) return failure;
    for (const auto& sample : media.samples)
        if (auto failure = sources.capture(text_util::path_utf8(sample), request.osbx_sample_sources)) return failure;
    // All physical sources and byte bounds are admitted before invoking a decoder.
    for (std::size_t i = 0; i < media.images.size(); ++i)
        if (auto failure = probe_storyboard_image(media.images[i], request.osbx_image_sources[i], request, probe, stop)) return failure;
    if (auto failure = validate_animation_dimensions(media, request)) return failure;
    if (auto failure = verify_storyboard_receipts(request.osbx_image_sources, stop)) return failure;
    return verify_storyboard_receipts(request.osbx_sample_sources, stop);
}

auto prepare_native_scene_images(const storyboard_document_t& storyboard, conversion_request_t& request,
    const std::filesystem::path& source_directory, const export_image_probe_t& probe,
    std::stop_token stop) -> std::optional<std::string> {
    const auto prepared = osbx_lua_prepared_scope_t::find(*storyboard.osbx);
    const auto& scene = prepared ? *prepared : *storyboard.osbx;
    std::set<std::string> references;
    for (const auto& node : scene.nodes) if (node.kind == osbx_node_kind_t::sprite && node.image) references.insert(*node.image);
    for (const auto& image : scene.images) {
        if (!references.contains(image.id)) continue;
        const auto failure = prepare_storyboard_image(image, request, source_directory, probe, stop);
        if (stop.stop_requested()) return "Native storyboard preparation cancelled";
        if (!failure) continue;
        for (const auto& node : scene.nodes) {
            if (node.image != image.id) continue;
            request.stepmania_storyboard_report.push_back({
                .node_id = std::to_string(storyboard.id.value) + "/" + node.id, .reason = *failure});
        }
    }
    return std::nullopt;
}


} // namespace

auto prepare_osu_storyboard_export(const song_document_t& song, conversion_request_t& request,
    const std::filesystem::path& source_directory, const export_image_probe_t& probe,
    std::stop_token stop) -> std::optional<std::string> {
    if (request.target != format_family_t::osu_mania ||
        std::ranges::none_of(song.storyboards, [](const auto& storyboard) { return storyboard.osbx.has_value(); }))
        return std::nullopt;
    request.osbx_image_sizes.clear();
    request.osbx_image_sources.clear();
    request.osbx_sample_sources.clear();
    request.osbx_playback_span.reset();
    const auto span = build_storyboard_playback_span(song, request);
    if (!span) return span.error();
    const auto media = storyboard_media_plan_t::from(song, {}, stop);
    if (!media) return media.error();
    if (auto failure = prepare_storyboard_media(*media, request, source_directory, probe, stop)) return failure;
    request.osbx_playback_span = *span;
    return std::nullopt;
}

auto prepare_stepmania_storyboard_export(const song_document_t& song, conversion_request_t& request,
    const std::filesystem::path& source_directory, const export_image_probe_t& probe,
    std::stop_token stop) -> std::optional<std::string> {
    if (request.target != format_family_t::stepmania || !request.stepmania ||
        request.stepmania->storyboard != stepmania_storyboard_mode_t::actors) return std::nullopt;
    request.osbx_image_sizes.clear();
    request.osbx_image_sources.clear();
    request.osbx_sample_sources.clear();
    request.osbx_playback_span.reset();
    request.stepmania_storyboard_report.clear();
    if (stop.stop_requested()) return "Native storyboard preparation cancelled";
    const auto span = build_storyboard_playback_span(song, request);
    if (span) request.osbx_playback_span = *span;
    for (const auto& storyboard : song.storyboards) {
        if (!storyboard.osbx) continue;
        if (auto failure = prepare_native_scene_images(storyboard, request, source_directory, probe, stop)) return failure;
    }
    return std::nullopt;
}

auto staged_asset_discovery_t::from(const song_document_t& song, const output_group_t& group,
    const std::filesystem::path& project_directory, std::stop_token stop) -> staged_asset_discovery_outcome_t {
    std::vector<conversion_request_t::staged_asset_t> mappings;
    if (group.family != format_family_t::bms && group.family != format_family_t::bmson &&
        group.family != format_family_t::osu_mania && group.family != format_family_t::stepmania)
        return mappings;
    if (!project_directory.is_absolute() ||
        !export_asset_directory(group.relative_output_path, group.destination_kind))
        return std::unexpected(staged_asset_discovery_error_t::from(std::move(mappings), "Export asset directory is invalid."));
    auto referenced = referenced_export_assets(song, group);
    if (!referenced) return std::unexpected(staged_asset_discovery_error_t::from(std::move(mappings), std::move(referenced.error())));
    for (const auto asset : *referenced) {
        if (stop.stop_requested())
            return std::unexpected(staged_asset_discovery_error_t::from(std::move(mappings), "Export asset discovery was cancelled."));
        auto appended = append_discovered_asset(song, group, project_directory, asset, stop, mappings);
        if (!appended) return std::unexpected(staged_asset_discovery_error_t::from(std::move(mappings), std::move(appended.error())));
    }
    return mappings;
}

auto plan_pending_export_assets(const song_document_t& song, const output_group_t& group,
    const std::filesystem::path& project_directory,
    std::span<const conversion_request_t::staged_asset_t> mappings)
    -> result_t<std::vector<project_asset_copy_request_t>, std::string> {
    const auto referenced = referenced_export_assets(song, group);
    if (!referenced) return std::unexpected(referenced.error());
    std::vector<project_asset_copy_request_t> pending;
    for (const auto id : *referenced) {
        if (std::ranges::any_of(mappings, [id](const auto& mapping) { return mapping.asset == id; }))
            continue;
        const auto* audio = song.find_audio_asset(id);
        const auto visual = std::ranges::find(song.visual_assets, id, &visual_asset_t::id);
        if ((audio == nullptr) == (visual == song.visual_assets.end()))
            return std::unexpected("Export asset #" + std::to_string(id.value) + " has no unique source.");
        pending.push_back({.project_directory = project_directory,
            .source_path = audio ? audio->source_path : visual->source_path, .asset = id});
    }
    const auto directory = export_asset_directory(group.relative_output_path, group.destination_kind);
    if (!directory)
        return std::unexpected("Export asset directory is invalid.");
    auto planned = plan_project_asset_copies(pending, *directory);
    if (!planned)
        return std::unexpected(planned.error().message);
    return std::move(*planned);
}

auto conversion_publication_plan_payload_t::from(const std::filesystem::path& project_directory,
                                 const output_group_t& group, const conversion_request_t& request,
                                 const conversion_result_t& converted,
                                 std::span<const project_asset_copy_request_t> pending_assets)
    -> result_t<conversion_publication_plan_payload_t, conversion_publication_plan_error_t> {
    namespace fs = std::filesystem;
    conversion_publication_plan_payload_t plan{};
    if (!project_directory.is_absolute() || !is_valid_relative(group.relative_output_path) ||
        group.family != request.target || !converted || converted.outputs.empty()) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), "Project, output group, or conversion result is invalid."));
    }
    if (group.destination_kind == output_destination_kind_t::file && converted.outputs.size() != 1 &&
        !(group.family == format_family_t::osu_mania && converted.outputs.size() == 2 &&
          std::ranges::count_if(converted.outputs, [](const auto& output) { return output.chart.has_value(); }) == 1 &&
          std::ranges::any_of(converted.outputs, [](const auto& output) {
              return !output.chart && std::filesystem::path(output.suggested_filename).extension() == ".osb";
          }))) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), "A single chart filename cannot hold multiple outputs."));
    }
    const auto asset_directory = group.destination_kind == output_destination_kind_t::directory
        ? std::optional{group.relative_output_path / "assets"}
        : export_asset_directory(group.relative_output_path, group.destination_kind);
    if (!asset_directory)
        return std::unexpected(conversion_publication_plan_error_t::from(
            std::move(plan), "Output asset directory is invalid."));
    auto planned_assets = plan_project_asset_copies(pending_assets, *asset_directory);
    if (!planned_assets)
        return std::unexpected(conversion_publication_plan_error_t::from(
            std::move(plan), planned_assets.error().message));
    std::set<fs::path> destinations;
    if (auto error = append_conversion_files(project_directory, group, request, converted, plan,
                                             destinations);
        error.has_value()) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), std::move(*error)));
    }
    if (auto error = append_generated_files(project_directory, group, request, converted, plan, destinations);
        error.has_value()) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), std::move(*error)));
    }
    if (auto error = validate_pending_assets(request, *planned_assets))
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), *error));
    if (auto error = append_staged_assets(project_directory, group, request, plan, destinations, *planned_assets);
        error.has_value()) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), std::move(*error)));
    }
    if (auto error = append_storyboard_files(project_directory, group, request, converted, plan, destinations))
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), *error));
    if (auto error = append_storyboard_images(project_directory, group, request, converted, plan, destinations))
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), *error));
    return plan;
}

auto staged_asset_discovery_t::from(staged_asset_discovery_outcome_t&& outcome) -> staged_asset_discovery_t {
    if (!outcome) {
        staged_asset_discovery_t result{.mappings = std::move(outcome.error().mappings), .errors = {}};
        result.errors.push_back(std::move(outcome.error().message));
        return result;
    }
    return {.mappings = std::move(*outcome), .errors = {}};
}

auto discover_staged_export_assets(const song_document_t& song, const output_group_t& group,
    const std::filesystem::path& project_directory, std::stop_token stop) -> staged_asset_discovery_t {
    return staged_asset_discovery_t::from(staged_asset_discovery_t::from(song, group, project_directory, stop));
}

} // namespace wormedit
