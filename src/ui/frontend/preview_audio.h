#pragma once
#include <cmath>
#include <cstdint>
#include <string>

namespace wormedit::ui {
struct preview_audio_range_view_t {
    bool available{};
    double begin_seconds{}, end_seconds{};
    [[nodiscard]] static auto from(double begin, double end) -> preview_audio_range_view_t {
        return {.available = std::isfinite(begin) && std::isfinite(end) && begin >= 0 && end > begin,
                .begin_seconds = begin, .end_seconds = end};
    }
};
struct preview_audio_export_action_t {
    std::uint64_t project{}, chart{}, revision{}, begin_row{}, end_row{};
    std::string filename{"preview.ogg"};
    double duration_seconds{};
    std::uint32_t fade_out_ms{};
    float quality{0.4F};
    bool replace_existing{};
};
}
