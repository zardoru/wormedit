#pragma once
#include <wormedit/core/osu_hitsample.h>

namespace wormedit::ui {
struct hitsound_palette_view_t {
    bool visible{};
    hitsound_values_t values;
    [[nodiscard]] static auto from(bool visible, std::span<const osu_hitsample_t> samples)
        -> hitsound_palette_view_t {
        return {.visible = visible, .values = hitsound_values_t::from(samples)};
    }
};
}
