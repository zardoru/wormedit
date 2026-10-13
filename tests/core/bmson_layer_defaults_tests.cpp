#include <wormedit/core/bmson_layer_defaults.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace wormedit;

namespace {

auto imported_chart(std::size_t channel_count) -> chart_document_t {
    chart_document_t chart{.id = chart_id_t{1}};
    chart.measures.push_back({.id = measure_id_t{1}});
    auto& measure = chart.measures.front();
    for (std::size_t index = 0; index < channel_count; ++index) {
        const auto channel_id = object_id_t{100 + index};
        const auto note_event_id = object_id_t{1000 + index * 2};
        const auto bgm_event_id = object_id_t{1001 + index * 2};
        const auto name = index < 2 ? std::string{"audio/shared.wav"}
                          : index == 2 ? std::string{"audio\\kick.ogg"}
                                       : "stem" + std::to_string(index) + ".flac";
        sound_channel_t channel{.id = channel_id, .name = name};
        channel.events = {
            {.id = note_event_id, .position = {measure_id_t{1}, index * 2},
             .lane = lane_id_t{1}, .playable = true},
            {.id = bgm_event_id, .position = {measure_id_t{1}, index * 2 + 1},
             .lane = lane_id_t{}, .playable = false},
        };
        chart.sound_channels.push_back(std::move(channel));
        measure.notes.push_back({.id = object_id_t{10000 + index},
                                 .position = {measure_id_t{1}, index * 2},
                                 .lane = lane_id_t{1},
                                 .sound_channel_event = note_event_id});
        measure.bgm_events.push_back({.id = object_id_t{20000 + index},
                                      .position = {measure_id_t{1}, index * 2 + 1},
                                      .source_lane = object_id_t{30000 + index},
                                      .sound_channel_event = bgm_event_id});
    }
    return chart;
}

constexpr std::array expected_colors{
    color_rgba_t{228, 88, 88, 255},   color_rgba_t{228, 141, 88, 255},
    color_rgba_t{228, 193, 88, 255},  color_rgba_t{210, 228, 88, 255},
    color_rgba_t{158, 228, 88, 255},  color_rgba_t{105, 228, 88, 255},
    color_rgba_t{88, 228, 123, 255},  color_rgba_t{88, 228, 176, 255},
    color_rgba_t{88, 228, 228, 255},  color_rgba_t{88, 176, 228, 255},
    color_rgba_t{88, 123, 228, 255},  color_rgba_t{105, 88, 228, 255},
    color_rgba_t{158, 88, 228, 255},  color_rgba_t{210, 88, 228, 255},
    color_rgba_t{228, 88, 193, 255},  color_rgba_t{228, 88, 141, 255},
};

auto initialized_chart_matches(chart_document_t chart, std::size_t channel_count) -> bool {
    const auto result = create_bmson_sound_channel_layers(chart);
    if (!result || !*result ||
        chart.semantic_layers.size() != channel_count ||
        chart.measures.front().notes.size() != channel_count ||
        chart.measures.front().bgm_events.size() != channel_count)
        return false;
    for (std::size_t index = 0; index < channel_count; ++index) {
        const auto& layer = chart.semantic_layers[index];
        const auto expected_name = index < 2 ? "shared" : index == 2 ? "kick"
                                                        : "stem" + std::to_string(index);
        const auto id = layer_id_t{index + 1};
        if (layer.id != id || layer.name != expected_name ||
            layer.color != expected_colors[index % expected_colors.size()] ||
            chart.measures.front().notes[index].semantic_layers != std::vector{id} ||
            chart.measures.front().bgm_events[index].semantic_layers != std::vector{id})
            return false;
    }
    return true;
}

auto initialization_failure_is_atomic() -> bool {
    auto chart = imported_chart(2);
    const auto invalid_event = object_id_t{99999};
    chart.measures.front().notes.front().sound_channel_event = invalid_event;
    const auto result = create_bmson_sound_channel_layers(chart);
    return !result && !result.error().empty() && chart.semantic_layers.empty() &&
           chart.measures.front().notes.front().sound_channel_event == invalid_event &&
           chart.measures.front().notes.front().semantic_layers.empty() &&
           chart.measures.front().bgm_events.front().semantic_layers.empty();
}

auto authored_layer_is_left_untouched() -> bool {
    auto chart = imported_chart(2);
    chart.semantic_layers.push_back({.id = layer_id_t{77}, .name = "Authored",
                                     .color = {12, 34, 56, 255}});
    chart.measures.front().notes.front().semantic_layers = {layer_id_t{77}};
    const auto result = create_bmson_sound_channel_layers(chart);
    return result && !*result && chart.semantic_layers.size() == 1 &&
           chart.semantic_layers.front().name == "Authored" &&
           chart.measures.front().notes.front().semantic_layers == std::vector{layer_id_t{77}} &&
           chart.measures.front().bgm_events.front().semantic_layers.empty();
}

} // namespace

TEST_CASE("BMSON channel defaults create distinct named layers with a wrapping color cycle",
          "[unit][core][bmson-layer-defaults]") {
    CHECK(initialized_chart_matches(imported_chart(12), 12));
    CHECK(initialized_chart_matches(imported_chart(20), 20));
}

TEST_CASE("BMSON layer defaults leave authored layers alone and reject bad links atomically",
          "[unit][core][bmson-layer-defaults]") {
    CHECK(authored_layer_is_left_untouched());
    CHECK(initialization_failure_is_atomic());
}
