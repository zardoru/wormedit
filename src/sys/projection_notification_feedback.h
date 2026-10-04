#pragma once

#include <wormedit/core/notifications.h>
#include <algorithm>
#include <optional>
#include <vector>

namespace wormedit::sys {

enum class projection_feedback_kind_t {
    layer_palette, audio_bookmark, beat_bookmark_position, beat_bookmark_geometry,
    audio_section, beat_section_position, beat_section_geometry, measure_geometry,
    field_index, empty_measures, initial_interaction, snap_configuration,
    recover_interaction, field_scene, choose_chart, missing_chart, workspace_view
};

struct projection_feedback_t {
    projection_feedback_kind_t producer;
    std::uint64_t object{};
    notification_severity_t severity;
    notification_source_t source;
    std::string message;
    auto operator==(const projection_feedback_t&) const -> bool = default;
};

inline auto projection_diagnostic_severity(diagnostic_severity_t severity) -> notification_severity_t {
    switch (severity) {
    case diagnostic_severity_t::info: return notification_severity_t::info;
    case diagnostic_severity_t::warning: return notification_severity_t::warning;
    case diagnostic_severity_t::error: return notification_severity_t::error;
    }
    return notification_severity_t::error;
}

inline auto projection_diagnostic_severity(const diagnostic_list_t& diagnostics) -> notification_severity_t {
    return diagnostics.empty() ? notification_severity_t::error
                              : projection_diagnostic_severity(diagnostics.front().severity);
}

struct projection_identity_t {
    song_id_t song;
    std::uint64_t revision{};
    chart_id_t chart;
    std::uint64_t session{};
    auto operator==(const projection_identity_t&) const -> bool = default;
};

class projection_notification_feedback_t {
    std::optional<projection_identity_t> identity_;
    std::vector<projection_feedback_t> previous_;
public:
    void publish(projection_identity_t identity, const std::vector<projection_feedback_t>& feedback,
                 notification_sink_t sink) {
        if (identity_ != identity) previous_.clear();
        identity_ = identity;
        for (const auto& item : feedback)
            if (std::ranges::find(previous_, item) == previous_.end())
                sink.post(item.severity, item.source, item.message);
        // A repaired projection resets its edge even at the same revision.
        previous_ = feedback;
    }
};

} // namespace wormedit::sys
