#include "sys/application_defaults.h"

#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/measure_editor.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit::sys {
namespace {

auto make_initial_chart(lane_input_profile_t profile, format_family_t source_family)
    -> chart_document_t {
    return {
        .id = chart_id_t{2},
        .metadata = {.name = "New Difficulty", .author = ""},
        .source_family = source_family,
        .measures = {{.id = measure_id_t{3}}},
        .lane_layouts = {{.id = object_id_t{4},
                          .position = {measure_id_t{3}, 0},
                          .layout = std::move(profile.layout)}},
        .input_profile = std::move(profile.input),
        .bpms = {{.id = object_id_t{5},
                  .position = {measure_id_t{3}, 0},
                  .beats_per_minute = {120, 1}}},
    };
}

auto make_bms_initial_chart() -> chart_document_t {
    auto chart = make_initial_chart(make_bms_7k_profile(), format_family_t::bms);
    chart.bgm_source_lanes.reserve(15);
    for (std::size_t index = 0; index < 15; ++index) {
        chart.bgm_source_lanes.push_back({.id = object_id_t{6 + index},
                                          .name = "BGM " + std::to_string(index + 1),
                                          .source_order = index});
    }
    return chart;
}

} // namespace

auto create_session_project_id() -> song_id_t {
    try {
        std::random_device source;
        const auto value =
            (static_cast<std::uint64_t>(source()) << 32U) | static_cast<std::uint64_t>(source());
        if (value != 0)
            return song_id_t{value};
    } catch (...) {
    }
    const auto wall =
        static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
    const auto monotonic =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto value = wall ^ (monotonic + 0x9e3779b97f4a7c15ULL + (wall << 6U) + (wall >> 2U));
    return song_id_t{value == 0 ? 1 : value};
}

auto make_empty_session_document(song_id_t session_id) -> song_document_t {
    return {.id = session_id, .revision = 1, .metadata = {.title = "No project loaded"}};
}

auto make_initial_document(song_id_t project_id) -> song_document_t {
    return {.id = project_id,
            .revision = 1,
            .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
            .charts = {make_bms_initial_chart()}};
}

auto make_initial_document(song_id_t project_id, std::uint32_t lane_count)
    -> std::optional<song_document_t> {
    return make_initial_document(project_id, "osu-mania-" + std::to_string(lane_count) + "k");
}

auto make_initial_document(song_id_t project_id, std::string_view layout_id)
    -> std::optional<song_document_t> {
    const auto* layout = find_new_project_layout(layout_id);
    if (!project_id || !layout) return std::nullopt;
    auto chart = make_initial_chart(layout->profile(), layout->source_family());
    return song_document_t{.id = project_id,
                           .revision = 1,
                           .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
                           .charts = {std::move(chart)},
                           .output_groups = make_new_project_output_groups(*layout, chart_id_t{2})};
}

auto make_audio_project_document(song_id_t project_id, std::filesystem::path audio_source_path,
                            std::string title, std::string artist, std::uint32_t lane_count,
                            audio_stream_info_t stream)
    -> std::optional<song_document_t> {
    return make_audio_project_document(project_id, std::move(audio_source_path), std::move(title),
                                  std::move(artist),
                                  "osu-mania-" + std::to_string(lane_count) + "k", stream);
}

auto make_audio_project_document(song_id_t project_id, std::filesystem::path audio_source_path,
                            std::string title, std::string artist, std::string_view layout_id,
                            audio_stream_info_t stream)
    -> std::optional<song_document_t> {
    const auto* layout = find_new_project_layout(layout_id);
    if (!project_id || !layout || audio_source_path.filename().empty()) return std::nullopt;
    auto chart = make_initial_chart(layout->profile(), layout->source_family());
    if (!extend_chart_to_audio_end(chart, stream)) return std::nullopt;
    auto resource_name = audio_source_path.filename().generic_string();
    return song_document_t{
        .id = project_id,
        .revision = 1,
        .metadata = {.title = std::move(title), .artist = std::move(artist)},
        .audio_assets = {{.id = asset_id_t{6}, .source_path = std::move(audio_source_path),
                          .stream = stream}},
        .resources = {{.id = resource_id_t{7},
                       .name = std::move(resource_name),
                       .asset = asset_id_t{6},
                       .roles = {resource_role_t::music, resource_role_t::bgm}}},
        .charts = {std::move(chart)},
        .output_groups = make_new_project_output_groups(*layout, chart_id_t{2}),
    };
}

} // namespace wormedit::sys
