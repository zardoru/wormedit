#include "sys/application_defaults.h"

#include <wormedit/core/lane_profiles.h>

#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace wormedit::sys {

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
    auto profile = make_bms_7k_profile();
    std::vector<bgm_source_lane_t> bgm_lanes;
    bgm_lanes.reserve(15);
    for (std::size_t index = 0; index < 15; ++index) {
        bgm_lanes.push_back({.id = object_id_t{6 + index},
                             .name = "BGM " + std::to_string(index + 1),
                             .source_order = index});
    }
    chart_document_t chart{
        .id = chart_id_t{2},
        .metadata = {.name = "New Difficulty", .author = ""},
        .source_family = format_family_t::bms,
        .measures = {{.id = measure_id_t{3}}},
        .lane_layouts = {{.id = object_id_t{4},
                          .position = {measure_id_t{3}, 0},
                          .layout = std::move(profile.layout)}},
        .input_profile = std::move(profile.input),
        .bgm_source_lanes = std::move(bgm_lanes),
        .bpms = {{.id = object_id_t{5},
                  .position = {measure_id_t{3}, 0},
                  .beats_per_minute = {120, 1}}},
    };
    return {
        .id = project_id,
        .revision = 1,
        .metadata = {.title = "Untitled Project", .artist = "Unknown Artist"},
        .charts = {std::move(chart)},
    };
}

} // namespace wormedit::sys
