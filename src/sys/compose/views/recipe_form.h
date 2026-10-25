#pragma once

#include <wormedit/core/notifications.h>

#include "ui/frontend.h"

#include <wormedit/core/proposal.h>
#include <wormedit/core/document.h>
#include <wormedit/core/sv_recipe.h>

#include <cstdint>
#include <optional>
#include <string>

namespace wormedit::sys {

// The recipe `current` with the form's output, curve, values, sampling, (for SPEEDS) transition,
// postprocess stages and impulses applied. Values are decimal or fraction text. Nothing is validated beyond syntax: out-of-range values are the core validator's to
// reject, so an invalid recipe stays editable. Returns nothing when the form is malformed (an
// unknown enum value, a non-numeric or zero-denominator fraction). Lua settings follow the form;
// frozen source and declared input hashes remain until an asynchronous Reload replaces source.
[[nodiscard]] auto recipe_from_form(const wormedit::ui::timing_recipe_form_t& form, const sv_recipe_t& current)
    -> std::optional<sv_recipe_t>;

// A rejected form keeps its display candidate outside the applicable proposal.
struct recipe_form_report_t {
    sv_recipe_t candidate;
    std::optional<ui::timing_recipe_form_t> requested_form;
    bool has_valid_form{true};
    std::vector<sv_recipe_prepared_t> prepared;
    std::vector<sv_recipe_point_t> composed_points;
    auto operator==(const recipe_form_report_t&) const -> bool = default;
};
struct recipe_form_preview_t;
class recipe_form_outcome_t;
using recipe_form_proposal_t = proposal_t<recipe_form_preview_t>;
struct recipe_form_preview_t {
    recipe_form_report_t report;
    auto operator==(const recipe_form_preview_t&) const -> bool = default;

    // Syntax failures retain the stored recipe; semantic failures retain the edited candidate.
    [[nodiscard]] static auto propose(const song_document_t& document, const chart_document_t& chart,
        const sv_recipe_t& recipe, const wormedit::ui::timing_recipe_form_t& form) -> recipe_form_outcome_t;
    // Always starts from the current stored recipe; syntax errors reuse the diagnostic buffer.
    [[nodiscard]] static auto revise(recipe_form_outcome_t previous,
        const song_document_t& document, const chart_document_t& chart,
        const sv_recipe_t& recipe, const wormedit::ui::timing_recipe_form_t& form) -> recipe_form_outcome_t;
};

// This editor retains rejected display state beside its plain proposal outcome.
// Diagnostics remain an ordinary list; retained reports stay in their payload.
class recipe_form_outcome_t : public proposal_outcome_t<recipe_form_preview_t> {
    using outcome_type = proposal_outcome_t<recipe_form_preview_t>;

  public:
    recipe_form_outcome_t() = default;
    explicit recipe_form_outcome_t(recipe_form_proposal_t editor)
        : outcome_type{.value = std::move(editor), .diagnostics = {}} {}

    [[nodiscard]] auto editor() noexcept -> recipe_form_proposal_t* {
        return value ? &*value : rejected_ ? &*rejected_ : nullptr;
    }
    [[nodiscard]] auto editor() const noexcept -> const recipe_form_proposal_t* {
        return value ? &*value : rejected_ ? &*rejected_ : nullptr;
    }
    [[nodiscard]] auto report() noexcept -> recipe_form_report_t* {
        auto* captured = editor();
        return captured ? &captured->payload.report : nullptr;
    }
    [[nodiscard]] auto report() const noexcept -> const recipe_form_report_t* {
        const auto* captured = editor();
        return captured ? &captured->payload.report : nullptr;
    }
    [[nodiscard]] auto captured_stamp() const noexcept -> const chart_stamp_t& {
        const auto* captured = editor();
        static constexpr chart_stamp_t empty{};
        return captured ? captured->stamp : empty;
    }
    [[nodiscard]] auto publication_generation() const noexcept -> std::uint64_t { return publication_generation_; }
    void complete(bool is_accepted) {
        advance_publication_generation();
        if (is_accepted) {
            if (!value && rejected_) value = std::move(rejected_);
            rejected_.reset();
        } else if (value) {
            rejected_ = std::move(value);
            value.reset();
        }
    }


  private:
    friend class recipe_evaluation_runtime_t;
    [[nodiscard]] static auto next_publication_generation() -> std::uint64_t;
    void advance_publication_generation() { publication_generation_ = next_publication_generation(); }
    std::uint64_t publication_generation_{next_publication_generation()};
    std::optional<recipe_form_proposal_t> rejected_;
};

static_assert(std::is_nothrow_move_constructible_v<recipe_form_outcome_t>);
static_assert(std::is_nothrow_move_assignable_v<recipe_form_outcome_t>);
static_assert(!std::is_polymorphic_v<recipe_form_outcome_t>);

static_assert(staged_proposal<recipe_form_preview_t,
    std::tuple<const song_document_t&, const chart_document_t&, const sv_recipe_t&,
               const wormedit::ui::timing_recipe_form_t&>,
    std::tuple<recipe_form_outcome_t, const song_document_t&, const chart_document_t&,
               const sv_recipe_t&, const wormedit::ui::timing_recipe_form_t&>,
    chart_stamp_t, diagnostic_list_t>);

[[nodiscard]] auto apply_recipe_form(document_history_t& history,
                                     const recipe_form_outcome_t& proposal)
    -> document_command_result_t;

// Stores `form` as the recipe's new curve, values and sampling in one history command. Returns the
// status line: success, or the core's message when the recipe, the revision or the values are
// rejected. The document is unchanged on failure.
[[nodiscard]] auto apply_recipe_form(document_history_t& history, chart_id_t chart,
                                     recipe_id_t recipe, const wormedit::ui::timing_recipe_form_t& form,
                                     bool& applied, notification_severity_t* severity = nullptr) -> std::string;

} // namespace wormedit::sys
