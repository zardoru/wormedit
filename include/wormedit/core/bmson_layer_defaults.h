#pragma once

#include <wormedit/core/document.h>

#include <string>

namespace wormedit {

struct bmson_layer_defaults_result_t {
    bool initialized{};
    std::string error;
};

// Adds chart-local layers and initial note/BGM membership for BMSON sound
// channels when the imported chart has no authored semantic layers.
[[nodiscard]] auto initialize_bmson_sound_channel_layers(chart_document_t& chart)
    -> bmson_layer_defaults_result_t;

} // namespace wormedit
