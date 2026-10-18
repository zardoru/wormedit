#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <string>
#include <utility>

namespace wormedit {

// A successful false value means authored layers or absent channels require no work.
// Adds chart-local layers and initial note/BGM membership for BMSON sound
// channels when the imported chart has no authored semantic layers.
[[nodiscard]] auto create_bmson_sound_channel_layers(chart_document_t& chart)
    -> result_t<bool, std::string>;

} // namespace wormedit
