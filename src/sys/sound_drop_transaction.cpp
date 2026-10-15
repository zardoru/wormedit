#include "sys/sound_drop_transaction.h"

#include <filesystem>

namespace wormedit::sys {
namespace {

auto same_origin(const sound_drop_origin_t& left, const sound_drop_origin_t& right) -> bool {
    return left.project_id == right.project_id && left.chart_id == right.chart_id &&
           left.revision == right.revision && left.tab == right.tab &&
           left.window_id == right.window_id && left.slots_mode == right.slots_mode;
}

auto event_window_matches(std::uint32_t event_window,
                          const sound_drop_origin_t& origin) -> bool {
    return event_window == 0 || event_window == origin.window_id;
}

auto accepts_resource_target(WormUiWorkspaceTab tab, resource_drop_target_t target) -> bool {
    if (target == resource_drop_target_t::bga) return tab == WORM_UI_WORKSPACE_TAB_COMPOSE;
    return target == resource_drop_target_t::sounds &&
        (tab == WORM_UI_WORKSPACE_TAB_COMPOSE || tab == WORM_UI_WORKSPACE_TAB_AUDIO_SLICES);
}

} // namespace

void sound_drop_transaction_t::begin(const sound_drop_origin_t& origin) {
    reset();
    active_ = true;
    origin_ = origin;
}

void sound_drop_transaction_t::note_origin(const sound_drop_origin_t& origin) {
    if (!same_origin(origin_, origin)) origin_changed_ = true;
}

void sound_drop_transaction_t::append_path(const char* path) {
    if (!path) {
        paths_.emplace_back();
        if (reason_.empty()) reason_ = "A dropped resource path is missing.";
        return;
    }
    paths_.emplace_back(path);
    const auto& retained = paths_.back();
    if (reason_.empty()) {
        if (retained.empty()) reason_ = "A dropped resource path is empty.";
        else if (retained.size() >= WORM_UI_PROJECT_PATH_CAPACITY)
            reason_ = "A dropped resource path exceeds the supported path length.";
        else if (!std::filesystem::path{retained}.is_absolute())
            reason_ = "Dropped resource paths must be absolute.";
    }
    if (!reason_.empty()) return;
    const auto normalized = std::filesystem::path{retained}.lexically_normal().generic_string();
    if (!normalized_paths_.insert(normalized).second)
        reason_ = "A resource file appears more than once in the drop.";
}

auto sound_drop_transaction_t::finish(const sound_drop_origin_t& origin) -> sound_drop_outcome_t {
    note_origin(origin);
    sound_drop_batch_t result{.origin = origin_, .paths = std::move(paths_),
                              .reason = std::move(reason_)};
    if (origin_changed_ && result.reason.empty())
        result.reason = "The project, chart, revision, tab, window, or slot mode changed during the drop.";
    if (result.paths.empty() && result.reason.empty())
        result.reason = "The resource drop did not contain any files.";
    reset();
    if (!result.reason.empty()) return std::unexpected(std::move(result));
    return result;
}

auto sound_drop_transaction_t::handle_begin(const SDL_Event& event,
                                           const sound_drop_origin_t& origin)
    -> sound_drop_event_result_t {
    std::optional<sound_drop_outcome_t> interrupted;
    if (active_ && claimed_) {
        interrupted = std::unexpected(sound_drop_batch_t{
            .origin = origin_, .paths = std::move(paths_),
            .reason = "A new file drop began before the previous drop completed."});
    }
    begin(origin);
    if (!event_window_matches(event.drop.windowID, origin_)) origin_changed_ = true;
    return {.consumed = interrupted.has_value(), .completed = std::move(interrupted)};
}

auto sound_drop_transaction_t::handle_position(const SDL_Event& event, resource_drop_target_t target)
    -> sound_drop_event_result_t {
    positioned_ = true;
    position_target_ = event_window_matches(event.drop.windowID, origin_)
        ? target : resource_drop_target_t::none;
    if (!event_window_matches(event.drop.windowID, origin_)) origin_changed_ = true;
    return {.consumed = claimed_};
}

auto sound_drop_transaction_t::handle_file(const SDL_Event& event, resource_drop_target_t target)
    -> sound_drop_event_result_t {
    const auto window_matches = event_window_matches(event.drop.windowID, origin_);
    if (!window_matches) origin_changed_ = true;
    if (!saw_file_) {
        saw_file_ = true;
        const auto destination = positioned_ ? position_target_ : target;
        claimed_ = window_matches && accepts_resource_target(origin_.tab, destination);
        if (claimed_) origin_.target = destination;
    }
    if (!claimed_) return {};
    append_path(event.drop.data);
    return {.consumed = true};
}

auto sound_drop_transaction_t::handle_complete(const SDL_Event& event,
                                               const sound_drop_origin_t& origin)
    -> sound_drop_event_result_t {
    if (!claimed_) {
        reset();
        return {};
    }
    if (!event_window_matches(event.drop.windowID, origin_)) origin_changed_ = true;
    auto completed = finish(origin);
    return {.consumed = true, .completed = std::move(completed)};
}

void sound_drop_transaction_t::reset() {
    active_ = false;
    claimed_ = false;
    saw_file_ = false;
    positioned_ = false;
    position_target_ = resource_drop_target_t::none;
    origin_changed_ = false;
    origin_ = {};
    paths_.clear();
    normalized_paths_.clear();
    reason_.clear();
}

auto sound_drop_transaction_t::handle(const SDL_Event& event,
                                      const sound_drop_origin_t& current_origin,
                                      resource_drop_target_t position_target)
    -> sound_drop_event_result_t {
    if (event.type == SDL_EVENT_DROP_BEGIN) return handle_begin(event, current_origin);
    if (event.type == SDL_EVENT_DROP_FILE && !active_)
        begin(current_origin);
    if (!active_) return {};
    note_origin(current_origin);

    if (event.type == SDL_EVENT_DROP_POSITION)
        return handle_position(event, position_target);
    if (event.type == SDL_EVENT_DROP_FILE)
        return handle_file(event, position_target);
    if (event.type == SDL_EVENT_DROP_COMPLETE)
        return handle_complete(event, current_origin);
    return {};
}

auto sound_drop_transaction_t::cancel() -> std::optional<sound_drop_outcome_t> {
    if (!active_ || !claimed_) {
        reset();
        return std::nullopt;
    }
    sound_drop_batch_t result{.origin = origin_, .paths = std::move(paths_),
                              .reason = "The resource file drop was cancelled."};
    reset();
    return std::unexpected(std::move(result));
}

} // namespace wormedit::sys
