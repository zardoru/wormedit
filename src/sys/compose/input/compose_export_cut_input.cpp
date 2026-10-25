#include "sys/compose/input/compose_input_internal.h"
#include "core/checked_math.h"
#include <wormedit/core/export_cut.h>

namespace wormedit::sys::compose_input_detail {
namespace {
auto moved_cut(const export_cut_t& cut, std::uint8_t edge, std::int64_t pointer, std::int64_t anchor)
    -> result_t<export_cut_edit_value_t, std::string> {
    auto begin = std::optional{cut.begin_milliseconds};
    auto end = std::optional{cut.end_milliseconds};
    switch (edge) {
    case WORM_UI_SPAN_HANDLE_BEGIN: begin = pointer; break;
    case WORM_UI_SPAN_HANDLE_END: end = pointer; break;
    case WORM_UI_SPAN_HANDLE_BODY:
        begin = checked_math::checked_add(*begin, pointer - anchor);
        end = checked_math::checked_add(*end, pointer - anchor);
        break;
    default: return std::unexpected("The export cut handle is stale.");
    }
    if (!begin || !end || *begin < 0 || *end <= *begin)
        return std::unexpected("Keep the export cut start nonnegative and before its end.");
    return export_cut_edit_value_t{.label = cut.label, .begin_milliseconds = *begin, .end_milliseconds = *end};
}
} // namespace

auto edit_compose_export_cut_span(const wormedit::ui::compose_input_action_t& input,
    compose_input_context_t& context, editor_interaction_result_t& result,
    std::string_view& accepted) -> std::optional<std::string> {
    if (!wormedit::ui::compose_export_cuts_visible(compose_mode_of(context.environment.commands)))
        return "Edit export cut edges in Timing focus.";
    if (!context.is_timing_input_current(input)) return stale_action_message("Export cut edit");
    auto& history = context.environment.document.history;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(history, context.state);
    const auto* cut = history.document.find_export_cut(input.object_id);
    const auto* indices = chart ? history.indices(chart->id) : nullptr;
    if (!cut || !view || !indices || !indices->measures || !indices->timing.value)
        return "The export cut or its chart timing is no longer available.";
    const auto pointer_row = timing_pointer_row(context, *chart, *view, *indices->measures, input.y_pixels);
    const auto anchor_row = timing_pointer_row(context, *chart, *view, *indices->measures, input.span_anchor_y_pixels);
    const auto& timing = *indices->timing.value;
    const auto pointer = pointer_row ? export_cut_interval_t::milliseconds_at(timing, *pointer_row) : std::nullopt;
    const auto anchor = anchor_row ? export_cut_interval_t::milliseconds_at(timing, *anchor_row) : std::nullopt;
    if (!pointer || !anchor) return "The export cut destination is outside the source-song timeline.";
    auto value = moved_cut(*cut, input.span_edge, *pointer, *anchor);
    if (!value) return std::move(value.error());
    const auto applied = apply_export_cut_edit(history, update_edit_t<export_cut_edit_value_t>{
        .id = cut->id, .value = std::move(*value)});
    result.accepted = applied.applied || applied.diagnostics.empty();
    result.document_changed = applied.applied;
    result.diagnostics = applied.diagnostics;
    accepted = applied.applied ? "Export cut edited." : "Export cut unchanged.";
    return std::nullopt;
}
} // namespace wormedit::sys::compose_input_detail
