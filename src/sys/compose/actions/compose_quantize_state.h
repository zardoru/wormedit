#pragma once
#include "sys/compose/actions/compose_quantize_preview.h"
#include <wormedit/core/command.h>
namespace wormedit::sys {
struct compose_quantize_editor_t {
    wormedit::chart_stamp_t stamp;
    wormedit::quantize_request_t request;
    std::optional<std::size_t> preset_index;
    bool preview_enabled{};
};
struct compose_quantize_applied_report_t {
    wormedit::song_id_t song;
    wormedit::chart_id_t chart;
    std::vector<quantize_report_row_t> rows;
    std::size_t total_rows{};
    bool truncated{};
};
struct compose_quantize_state_t {
    std::optional<compose_quantize_editor_t> quantize_editor;
    std::optional<compose_quantize_applied_report_t> quantize_applied_report;
};
}
