#pragma once
#include <wormedit/core/async_job.h>
#include "ui/frontend/storyboard.h"
#include <wormedit/core/osbx_lua_link.h>

namespace wormedit::sys {
struct osbx_lua_request_identity_t {
    document_stamp_t stamp;
    std::uint64_t session{};
    auto operator==(const osbx_lua_request_identity_t&) const -> bool = default;
};
struct osbx_lua_source_request_t {
    ui::storyboard_action_t action;
    lua_source_descriptor_t source;
    std::filesystem::path base;
};
struct osbx_lua_source_completion_t {
    ui::storyboard_action_t action;
    result_t<lua_source_descriptor_t, std::string> source;
};
struct osbx_lua_work_result_t {
    std::vector<osbx_lua_prepared_scene_t> scenes;
    std::optional<osbx_lua_source_completion_t> source;
};
struct osbx_lua_evaluation_request_t {
    song_document_t document;
    std::vector<osbx_lua_prepared_scene_t> previous;
    std::optional<osbx_lua_source_request_t> source;
};

class osbx_lua_link_runtime_t {
  public:
    explicit osbx_lua_link_runtime_t(task_scheduler_t&);
    ~osbx_lua_link_runtime_t();
    void update(const song_document_t&, std::uint64_t session);
    void wait(const song_document_t&, std::uint64_t session);
    void cancel();
    [[nodiscard]] auto request_source(const ui::storyboard_action_t&, const lua_source_descriptor_t&,
        const std::filesystem::path&) -> result_t<bool, std::string>;
    [[nodiscard]] auto take_source() -> std::optional<osbx_lua_source_completion_t>;
    [[nodiscard]] auto scenes(const song_document_t& document) const -> std::span<const osbx_lua_prepared_scene_t> {
        if (!scenes_->empty() && !scenes_->front().stamp.is_current(document)) return {};
        return *scenes_;
    }
    [[nodiscard]] auto snapshot() const -> std::shared_ptr<const std::vector<osbx_lua_prepared_scene_t>> { return scenes_; }
    [[nodiscard]] auto generation() const -> std::uint64_t { return generation_; }
    [[nodiscard]] auto is_busy() const -> bool { return job_.is_busy(); }
  private:
    using job_t = async_job_t<osbx_lua_evaluation_request_t, osbx_lua_work_result_t, osbx_lua_request_identity_t>;
    static auto evaluate(const osbx_lua_evaluation_request_t&, std::stop_token) -> job_t::work_outcome_t;
    void publish(std::optional<job_t::completion_t>, const osbx_lua_request_identity_t&);
    job_t job_;
    std::optional<osbx_lua_request_identity_t> requested_;
    std::shared_ptr<const std::vector<osbx_lua_prepared_scene_t>> scenes_{std::make_shared<const std::vector<osbx_lua_prepared_scene_t>>()};
    std::uint64_t generation_{};
    std::optional<ui::storyboard_action_t> source_pending_;
    std::optional<osbx_lua_source_completion_t> source_result_;
};

// Explicit Reload only; call on a worker, then apply the returned descriptor in
// the caller's existing transaction. Normal evaluation never reads the module.
[[nodiscard]] auto reload_osbx_lua_source(const lua_source_descriptor_t&, const std::filesystem::path& base,
    std::stop_token = {}) -> result_t<lua_source_descriptor_t, std::string>;
} // namespace wormedit::sys
