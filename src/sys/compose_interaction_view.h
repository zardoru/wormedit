#pragma once

#include <wormedit/core/interaction.h>
#include <wormedit/core/timing.h>

#include <optional>
#include <utility>
#include <variant>

namespace wormedit::sys {

inline auto compose_view_for_state(const wormedit::document_history_t& history,
                                  const wormedit::editor_interaction_state_t& state)
    -> std::optional<wormedit::editor_interaction_view_model_t> {
    const auto* indices = history.indices(state.chart);
    if (!indices || !indices->measures) return std::nullopt;
    auto inspected = wormedit::inspect_editor_interaction(history, state,
        {.chart = state.chart, .document_revision = history.document.revision,
         .index = &*indices->measures});
    if (auto* view = std::get_if<wormedit::editor_interaction_view_model_t>(&inspected))
        return std::move(*view);
    return std::nullopt;
}

} // namespace wormedit::sys
