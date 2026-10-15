#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <string>
#include <utility>

namespace wormedit {

// A successful false value means authored layers or absent channels require no work.
using bmson_layer_defaults_outcome_t = result_t<bool, std::string>;

// Adds chart-local layers and initial note/BGM membership for BMSON sound
// channels when the imported chart has no authored semantic layers.
[[nodiscard]] auto create_bmson_sound_channel_layers(chart_document_t& chart)
    -> bmson_layer_defaults_outcome_t;

} // namespace wormedit
