#include "sys/storyboard/osbx_lua_link_runtime.h"
#include "core/text_util.h"
#include <wormedit/formats/native_project_storage.h>
#if defined(WORMEDIT_HAS_LUA)
#include "scripting/osbx_lua_link.h"
#endif
#include <algorithm>
#include <fstream>

namespace wormedit::sys {
#if !defined(WORMEDIT_HAS_LUA)
namespace {
class unavailable_lua_backend_t final : public osbx_lua_backend_t {
  public:
    auto prepare_track(const osbx_lua_track_link_t&, std::size_t) const
        -> diagnosed_t<std::vector<osbx_key_t<>>> override {
        return {.diagnostics = {error(diagnostic_code_t::unsupported_feature, reason)}};
    }
    auto prepare_children(const osbx_lua_link_t&) const -> diagnosed_t<osbx_document_t> override {
        return {.diagnostics = {error(diagnostic_code_t::unsupported_feature, reason)}};
    }
  private:
    static constexpr const char* reason = "Lua support is disabled; storyboard links cannot be prepared";
};
} // namespace
#endif
osbx_lua_link_runtime_t::osbx_lua_link_runtime_t(task_scheduler_t& scheduler)
    : job_{scheduler, 2, "Invalid storyboard evaluation", "Storyboard evaluation queue is busy",
        "Storyboard evaluation returned no result", async_job_destruction_policy_t::release} {}
osbx_lua_link_runtime_t::~osbx_lua_link_runtime_t() { cancel(); }
void osbx_lua_link_runtime_t::update(const song_document_t& document, std::uint64_t session) {
    const osbx_lua_request_identity_t identity{.stamp = document_stamp_t::from(document), .session = session};
    if (requested_ && requested_->session != session) cancel();
    if (source_pending_ && (source_pending_->project != identity.stamp.project ||
        source_pending_->revision != identity.stamp.revision)) {
        job_.cancel(); source_pending_.reset(); source_result_.reset(); requested_.reset();
    }
    publish(job_.poll(), identity);
    if (source_pending_ || source_result_ || requested_ == identity) return;
    if (std::ranges::none_of(document.storyboards, [](const auto& s) { return s.osbx && has_osbx_lua_links(*s.osbx); })) {
        job_.cancel(); requested_ = identity;
        if (!scenes_->empty()) {
            scenes_ = std::make_shared<const std::vector<osbx_lua_prepared_scene_t>>();
            ++generation_;
        }
        return;
    }
    const auto started = job_.request([&] { return osbx_lua_evaluation_request_t{.document = document, .previous = *scenes_}; }, identity, evaluate);
    if (started) requested_ = identity;
}
void osbx_lua_link_runtime_t::wait(const song_document_t& document, std::uint64_t session) {
    publish(job_.wait(), {.stamp = document_stamp_t::from(document), .session = session});
}
void osbx_lua_link_runtime_t::cancel() {
    job_.cancel(); requested_.reset(); source_pending_.reset(); source_result_.reset(); scenes_ = std::make_shared<const std::vector<osbx_lua_prepared_scene_t>>(); ++generation_;
}
void osbx_lua_link_runtime_t::publish(std::optional<job_t::completion_t> completion,
    const osbx_lua_request_identity_t& identity) {
    if (!completion) return;
    const auto& captured = *completion ? completion->value().identity : completion->error().identity;
    if (captured != identity) { source_pending_.reset(); requested_.reset(); return; }
    if (source_pending_) {
        if (*completion && completion->value().payload->source) source_result_ = *completion->value().payload->source;
        else source_result_ = osbx_lua_source_completion_t{.action = *source_pending_,
            .source = std::unexpected(*completion ? "Lua source worker returned no descriptor" : completion->error().cause.message)};
        source_pending_.reset();
        return;
    }
    if (*completion) {
        const auto owner = completion->value().payload;
        scenes_ = std::shared_ptr<const std::vector<osbx_lua_prepared_scene_t>>{owner, &owner->scenes};
        ++generation_;
    } else requested_.reset();
}
auto osbx_lua_link_runtime_t::request_source(const ui::storyboard_action_t& action,
    const lua_source_descriptor_t& source, const std::filesystem::path& base) -> result_t<bool, std::string> {
    if (job_.is_busy() || source_pending_ || source_result_) return std::unexpected("Storyboard worker is busy; try again when it finishes.");
    const osbx_lua_request_identity_t identity{.stamp = {.project = action.project, .revision = action.revision},
        .session = action.session_generation};
    const auto started = job_.request([&] {
        return osbx_lua_evaluation_request_t{.source = osbx_lua_source_request_t{.action = action, .source = source, .base = base}};
    }, identity, evaluate);
    if (!started) return std::unexpected(started.error().message);
    source_pending_ = action; requested_ = identity;
    return true;
}
auto osbx_lua_link_runtime_t::take_source() -> std::optional<osbx_lua_source_completion_t> {
    auto result = std::exchange(source_result_, std::nullopt);
    if (result) requested_.reset();
    return result;
}

auto osbx_lua_link_runtime_t::evaluate(const osbx_lua_evaluation_request_t& request, std::stop_token stop)
    -> job_t::work_outcome_t {
    if (request.source) {
        auto descriptor = request.source->source;
        if (descriptor.source_reference && descriptor.source_reference->is_absolute())
            descriptor.source_reference = descriptor.source_reference->lexically_relative(request.source->base);
        return osbx_lua_work_result_t{.source = osbx_lua_source_completion_t{
            .action = request.source->action, .source = reload_osbx_lua_source(descriptor, request.source->base, stop)}};
    }
#if defined(WORMEDIT_HAS_LUA)
    const scripting::osbx_lua_link_backend_t backend{{.cancellation = stop}};
    return osbx_lua_work_result_t{.scenes = prepare_osbx_lua_scenes(request.document, backend, request.previous)};
#else
    if (stop.stop_requested()) return osbx_lua_work_result_t{};
    const unavailable_lua_backend_t backend;
    return osbx_lua_work_result_t{.scenes = prepare_osbx_lua_scenes(request.document, backend, request.previous)};
#endif
}
auto reload_osbx_lua_source(const lua_source_descriptor_t& previous, const std::filesystem::path& base,
    std::stop_token stop) -> result_t<lua_source_descriptor_t, std::string> {
    if (!previous.source_reference || !osbx_document_t::is_valid_project_path(text_util::path_utf8(*previous.source_reference)))
        return std::unexpected("Choose a project-relative Lua module path");
    if (stop.stop_requested()) return std::unexpected("Lua source reload cancelled");
    std::ifstream input{base / *previous.source_reference, std::ios::binary};
    if (!input) return std::unexpected("Cannot open Lua module: " + text_util::path_utf8(*previous.source_reference));
    auto result = previous;
    result.frozen_source.resize(lua_source_descriptor_t::maximum_frozen_source_bytes + 1);
    input.read(result.frozen_source.data(), static_cast<std::streamsize>(result.frozen_source.size()));
    result.frozen_source.resize(static_cast<std::size_t>(input.gcount()));
    if (input.bad() || result.frozen_source.empty() || result.frozen_source.size() > lua_source_descriptor_t::maximum_frozen_source_bytes)
        return std::unexpected("Lua module must contain 1..16384 readable source bytes");
    result.source_hash = native_project_content_checksum(result.frozen_source);
    return result;
}
} // namespace wormedit::sys
