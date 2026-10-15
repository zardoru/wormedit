#pragma once

#include <wormedit/core/notifications.h>

#include "ui/frontend.h"

#include <wormedit/core/proposal.h>
#include <wormedit/core/document.h>

#include <cstdint>
#include <optional>
#include <string>

namespace wormedit::sys {

// The text form of an SV recipe's curve, values and sampling, as the frontends show it. Every
// number is an exact fraction typed as text; the recipe's name, range and enabled state are not
// part of the form.
[[nodiscard]] auto recipe_form_from_recipe(const sv_recipe_t& recipe) -> WormUiTimingRecipeForm;

// The recipe `current` with the form's output, curve, values, sampling and (for SPEEDS) transition
// applied. Nothing is validated beyond syntax: out-of-range values are the core validator's to
// reject, so an invalid recipe stays editable. Returns nothing when the form is malformed (an
// unknown enum value, a non-numeric or zero-denominator fraction). A Lua expression recipe keeps
// its source and parameters; only its sampling and transition follow the form.
[[nodiscard]] auto recipe_from_form(const WormUiTimingRecipeForm& form, const sv_recipe_t& current)
    -> std::optional<sv_recipe_t>;

// A rejected form keeps its display candidate outside the applicable proposal.
struct recipe_form_report_t {
    sv_recipe_t candidate;
    auto operator==(const recipe_form_report_t&) const -> bool = default;
};
struct recipe_form_preview_t;
using recipe_form_diagnostics_t = proposal_diagnostics_t<recipe_form_report_t>;
using recipe_form_outcome_t = proposal_outcome_t<recipe_form_preview_t,
    chart_stamp_t, recipe_form_diagnostics_t>;
using recipe_form_proposal_t = proposal_t<recipe_form_preview_t>;
using recipe_form_preview_view_t = proposal_preview_view_t<recipe_form_preview_t,
    chart_stamp_t, recipe_form_diagnostics_t>;

struct recipe_form_preview_t {
    recipe_form_report_t report;
    auto operator==(const recipe_form_preview_t&) const -> bool = default;

    // Syntax failures retain the stored recipe; semantic failures retain the edited candidate.
    [[nodiscard]] static auto propose(const song_document_t& document, const chart_document_t& chart,
        const sv_recipe_t& recipe, const WormUiTimingRecipeForm& form) -> recipe_form_outcome_t;
    // Always starts from the current stored recipe; syntax errors reuse the diagnostic buffer.
    [[nodiscard]] static auto revise(recipe_form_outcome_t previous,
        const song_document_t& document, const chart_document_t& chart,
        const sv_recipe_t& recipe, const WormUiTimingRecipeForm& form) -> recipe_form_outcome_t;
};

static_assert(staged_proposal<recipe_form_preview_t,
    std::tuple<const song_document_t&, const chart_document_t&, const sv_recipe_t&,
               const WormUiTimingRecipeForm&>,
    std::tuple<recipe_form_outcome_t, const song_document_t&, const chart_document_t&,
               const sv_recipe_t&, const WormUiTimingRecipeForm&>,
    chart_stamp_t, recipe_form_diagnostics_t>);

[[nodiscard]] auto apply_recipe_form(document_history_t& history,
                                     const recipe_form_outcome_t& proposal)
    -> document_command_result_t;

// Stores `form` as the recipe's new curve, values and sampling in one history command. Returns the
// status line: success, or the core's message when the recipe, the revision or the values are
// rejected. The document is unchanged on failure.
[[nodiscard]] auto apply_recipe_form(document_history_t& history, chart_id_t chart,
                                     recipe_id_t recipe, const WormUiTimingRecipeForm& form,
                                     bool& applied, notification_severity_t* severity = nullptr) -> std::string;

} // namespace wormedit::sys
