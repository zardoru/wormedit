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

auto supported_lane_count(std::uint32_t count) -> bool {
    switch (count) {
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 14: return true;
    default: return false;
    }
}

auto step_type(std::uint32_t count) -> std::string_view {
    switch (count) {
    case 4: return "dance-single";
    case 5: return "pump-single";
    case 6: return "dance-solo";
    case 7: return "kb7";
    case 8: return "dance-double";
    case 10: return "pump-double";
    default: return {};
    }
}

auto make_playable_profile(std::uint32_t count) -> lane_input_profile_t {
    lane_input_profile_t result;
    const auto selected_step_type = step_type(count);
    result.layout.profile_id = selected_step_type.empty()
                                   ? "audio-" + std::to_string(count) + "k"
                                   : "stepmania:" + std::string(selected_step_type);
    result.layout.display_name = std::to_string(count) + "K";
    result.layout.lanes.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        result.layout.lanes.push_back({.id = lane_id_t{index + 1},
                                       .name = "Key " + std::to_string(index + 1),
                                       .role = lane_role_t::playable,
                                       .stage = stage_group_t::left,
                                       .display_order = index});
    }
    if (const auto input = make_number_row_input_profile(result.layout))
        result.input = *input;
    else
        result.input.profile_id = "unbound-" + std::to_string(count) + "k";
    return result;
}

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

auto default_output_groups(std::uint32_t lane_count) -> std::vector<output_group_t> {
    std::vector<output_group_t> groups{{.name = "osu!mania",
                                        .family = format_family_t::osu_mania,
                                        .charts = {chart_id_t{2}},
                                        .relative_output_path = "osu"}};
    if (!step_type(lane_count).empty()) {
        groups.push_back({.name = "StepMania SM",
                          .family = format_family_t::stepmania,
                          .charts = {chart_id_t{2}},
                          .relative_output_path = "sm"});
        groups.push_back({.name = "StepMania SSC",
                          .family = format_family_t::stepmania,
                          .charts = {chart_id_t{2}},
                          .relative_output_path = "ssc"});
    }
    return groups;
}

} // namespace

auto new_session_project_id() -> song_id_t {
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

auto empty_session_document(song_id_t session_id) -> song_document_t {
    return {.id = session_id, .revision = 1, .metadata = {.title = "No project loaded"}};
}

auto initial_document(song_id_t project_id) -> song_document_t {
    return {.id = project_id,
            .revision = 1,
            .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
            .charts = {make_bms_initial_chart()}};
}

auto initial_document(song_id_t project_id, std::uint32_t lane_count)
    -> std::optional<song_document_t> {
    if (!project_id || !supported_lane_count(lane_count)) return std::nullopt;
    auto chart = make_initial_chart(make_playable_profile(lane_count),
                                    format_family_t::native_project);
    return song_document_t{.id = project_id,
                           .revision = 1,
                           .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
                           .charts = {std::move(chart)},
                           .output_groups = default_output_groups(lane_count)};
}

auto audio_project_document(song_id_t project_id, std::filesystem::path audio_source_path,
                            std::string title, std::string artist, std::uint32_t lane_count,
                            audio_stream_info_t stream)
    -> std::optional<song_document_t> {
    if (!project_id || !supported_lane_count(lane_count) ||
        audio_source_path.filename().empty()) return std::nullopt;
    auto chart = make_initial_chart(make_playable_profile(lane_count),
                                    format_family_t::native_project);
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
        .output_groups = default_output_groups(lane_count),
    };
}

} // namespace wormedit::sys
