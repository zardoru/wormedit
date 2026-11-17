#pragma once
#include "timing_preview.h"
#include <wormedit/core/timing_edit_policy.h>
#include <wormedit/core/vocabulary.h>
#include <cstdint>
#include <optional>
#include <string>

/* A timing pointer target captures its source-document fraction from the drawn axis when available.
   Raw viewport coordinates and the visible slice also support surfaces without chart-axis geometry. */
namespace wormedit::ui {
struct timing_target_t {
    double screen_y;
    double viewport_height_pixels;
    double document_begin_fraction;
    double document_end_fraction;
    bool downscroll;
    std::optional<double> source_document_fraction;
    auto operator==(const timing_target_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* Payload of one timing-lane edit (stage, quick insert, drag, value revision). `stage_action` selects
   the step; the other members are the fields that step reads. */
namespace wormedit::ui {
struct timing_edit_request_t {
    std::string meter_numerator_text;
    std::string meter_denominator_text;
    std::optional<timing_anchoring_t> anchoring;
    wormedit::ui::timing_stage_action_t stage_action;
    wormedit::chart_id_t chart_id;
    uint64_t document_revision;
    wormedit::object_id_t object_id;
    wormedit::timing_lane_kind_t lane;
    bool has_target_y;
    wormedit::ui::timing_target_t target;
    double value;
    double secondary_value;
    bool transition_is_seconds;
    bool toggle_primary;
    wormedit::pause_unit_t pause_unit;
    std::string bpm_decimal_text;
    bool bpm_decimal_edited;
    int bpm_step;
    std::string primary_numerator_text;
    std::string primary_denominator_text;
    std::string secondary_numerator_text;
    std::string secondary_denominator_text;
};
} // namespace wormedit::ui
