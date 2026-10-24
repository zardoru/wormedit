#pragma once

#include "sys/compose/views/recipe_form.h"
#include <wormedit/core/async_job.h>

namespace wormedit::sys {

struct recipe_evaluation_request_t {
    chart_document_t chart;
    recipe_form_outcome_t proposal;
    std::filesystem::path asset_base;
    bool reload{};
};

// The UI thread owns admission and publication; each worker owns its entire snapshot.
class recipe_evaluation_runtime_t {
  public:
    explicit recipe_evaluation_runtime_t(task_scheduler_t& scheduler);
    ~recipe_evaluation_runtime_t();
    auto request(const song_document_t& document, const chart_document_t& chart,
        const sv_recipe_t& recipe, const ui::timing_recipe_form_t& form,
        const std::filesystem::path& asset_base) -> std::string;
    void poll(const song_document_t& document, chart_id_t chart,
        recipe_id_t selected, std::optional<recipe_form_outcome_t>& preview);
    // Tests and shutdown may wait; the application uses poll exclusively.
    void wait(const song_document_t& document, chart_id_t chart,
        recipe_id_t selected, std::optional<recipe_form_outcome_t>& preview);
    void cancel();
    [[nodiscard]] auto is_busy() const -> bool;
    [[nodiscard]] auto requested_stamp() const -> const chart_stamp_t& { return stamp_; }
    [[nodiscard]] auto requested_recipe() const -> recipe_id_t { return recipe_; }
  private:
    using job_t = async_job_t<recipe_evaluation_request_t, recipe_form_outcome_t, chart_stamp_t>;
    static auto evaluate(const recipe_evaluation_request_t&, std::stop_token) -> job_t::work_outcome_t;
    void publish(std::optional<job_t::completion_t> completion, const song_document_t& document,
        chart_id_t chart, recipe_id_t selected, std::optional<recipe_form_outcome_t>& preview);
    auto submit_pending() -> std::string;
    job_t job_;
    std::optional<recipe_evaluation_request_t> pending_;
    std::optional<recipe_form_outcome_t> requested_;
    chart_stamp_t stamp_{};
    recipe_id_t recipe_{};
};
} // namespace wormedit::sys
