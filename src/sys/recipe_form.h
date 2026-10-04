#pragma once

#include <wormedit/core/notifications.h>

#include "ui/frontend.h"

#include <wormedit/core/command.h>
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

// The unapplied form candidate of a stored recipe, with the message a malformed or invalid
// candidate produced (empty when it is valid). It belongs to one chart at one document revision.
struct recipe_form_preview_t {
    chart_id_t chart;
    std::uint64_t revision{};
    sv_recipe_t candidate;
    std::string error;
    notification_severity_t severity{notification_severity_t::hint};
};

// Builds the preview of `form` applied to the stored recipe `recipe` of `chart`. Nothing is
// written. A malformed form keeps the stored recipe as candidate with an error; a candidate the
// core validator rejects keeps its first message.
[[nodiscard]] auto preview_recipe_form(const chart_document_t& chart, std::uint64_t revision,
                                       const sv_recipe_t& recipe, const WormUiTimingRecipeForm& form)
    -> recipe_form_preview_t;

// Stores `form` as the recipe's new curve, values and sampling in one history command. Returns the
// status line: success, or the core's message when the recipe, the revision or the values are
// rejected. The document is unchanged on failure.
[[nodiscard]] auto apply_recipe_form(document_history_t& history, chart_id_t chart,
                                     recipe_id_t recipe, const WormUiTimingRecipeForm& form,
                                     bool& applied, notification_severity_t* severity = nullptr) -> std::string;

} // namespace wormedit::sys
