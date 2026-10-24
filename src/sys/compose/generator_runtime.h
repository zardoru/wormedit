#pragma once
#include "ui/frontend/generator.h"
#include <wormedit/core/generator.h>
#include <wormedit/core/async_job.h>
#include <wormedit/core/interaction_types.h>
#include <wormedit/core/editor_scene.h>
#include <tuple>
namespace wormedit::sys {
struct generator_job_request_t {
    song_document_t document;
    std::optional<bms_program_target_key_t> target;
    chart_id_t chart;
    ui::generator_form_t form;
    std::vector<editor_selection_object_t> selection;
    std::filesystem::path base;
    std::optional<generator_recipe_t> confirmed;
    std::uint64_t identity_floor{};
};
// The index borrows the chart; this owner is allocated before index construction
// and published immutably so moving an async result cannot invalidate its data.
struct generator_preview_geometry_t {
    [[nodiscard]] static auto from(const chart_document_t&, const generator_recipe_t&, std::uint64_t revision)
        -> diagnosed_t<std::shared_ptr<const generator_preview_geometry_t>>;
    chart_document_t chart;
    std::optional<editor_field_scene_index_t> index;
};
struct generator_job_result_t {
    ui::generator_form_t form;
    generator_recipe_t recipe;
    generator_outcome_t proposal;
    std::shared_ptr<const generator_preview_geometry_t> geometry;
    bool requires_confirmation{};
    std::string status;
};
class generator_runtime_t {
  public:
    explicit generator_runtime_t(task_scheduler_t&);
    ~generator_runtime_t();
    auto request(const document_history_t&, chart_id_t, const ui::generator_form_t&,
        std::vector<editor_selection_object_t>, const std::filesystem::path&, bool confirm = false) -> std::string;
    void poll(const document_history_t&, chart_id_t);
    void wait(const document_history_t&, chart_id_t);
    void cancel();
    [[nodiscard]] auto is_busy() const -> bool;
    [[nodiscard]] auto result() const -> const generator_job_result_t* { return result_.get(); }
    [[nodiscard]] auto preview() const -> const generator_preview_t*;
    [[nodiscard]] auto stamp() const -> const chart_stamp_t& { return stamp_; }
    [[nodiscard]] auto preview_scene(const editor_interaction_view_model_t&,
        const editor_field_scene_request_t&) const -> const editor_field_scene_view_t*;
    auto apply(document_history_t&, const ui::generator_form_t&) -> document_command_result_t;
  private:
    using job_t = async_job_t<generator_job_request_t, generator_job_result_t, chart_stamp_t>;
    static auto evaluate(const generator_job_request_t&, std::stop_token) -> job_t::work_outcome_t;
    auto submit() -> std::string;
    void publish(std::optional<job_t::completion_t>, const document_history_t&, chart_id_t);
    job_t job_;
    std::optional<generator_job_request_t> pending_;
    std::shared_ptr<const generator_job_result_t> result_, last_good_;
    chart_stamp_t stamp_;
    struct preview_projection_t {
        std::shared_ptr<const generator_preview_geometry_t> source;
        editor_field_scene_window_t window;
        editor_field_scene_view_outcome_t scene;
        std::optional<std::tuple<double, double>> viewport;
    };
    mutable preview_projection_t projection_;
};
}
