#pragma once

#include "ui/frontend.h"
#include <wormedit/core/result.h>

#include <SDL3/SDL_events.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace wormedit::sys {

enum class resource_drop_target_t { none, sounds, bga };

struct sound_drop_origin_t {
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t revision{};
    WormUiWorkspaceTab tab{WORM_UI_WORKSPACE_TAB_SETUP};
    std::uint32_t window_id{};
    bool slots_mode{};
    std::uint16_t first_slot{};
    resource_drop_target_t target{resource_drop_target_t::none};
};

struct sound_drop_batch_t {
    sound_drop_origin_t origin;
    std::vector<std::string> paths;
    std::string reason;
};
using sound_drop_outcome_t = result_t<sound_drop_batch_t, sound_drop_batch_t>;

struct sound_drop_event_result_t {
    bool consumed{};
    std::optional<sound_drop_outcome_t> completed;
};

// Retains one SDL file-drop transaction across event polls. The caller reports
// which resource list the latest drop position lies over. A new BEGIN
// returns the previous claimed batch as rejected if it had not completed.
class sound_drop_transaction_t {
  public:
    [[nodiscard]] auto handle(const SDL_Event& event, const sound_drop_origin_t& current_origin,
                              resource_drop_target_t position_target) -> sound_drop_event_result_t;
    // Existing sound-only callers use the same transaction implementation.
    [[nodiscard]] auto handle(const SDL_Event& event, const sound_drop_origin_t& origin,
                              bool over_sound_list) -> sound_drop_event_result_t {
        return handle(event, origin, over_sound_list ? resource_drop_target_t::sounds
                                                   : resource_drop_target_t::none);
    }
    [[nodiscard]] auto cancel() -> std::optional<sound_drop_outcome_t>;
    [[nodiscard]] auto is_active() const noexcept -> bool { return active_; }
    [[nodiscard]] auto is_claimed() const noexcept -> bool { return claimed_; }

  private:
    void begin(const sound_drop_origin_t& origin);
    void note_origin(const sound_drop_origin_t& origin);
    void append_path(const char* path);
    [[nodiscard]] auto finish(const sound_drop_origin_t& origin) -> sound_drop_outcome_t;
    [[nodiscard]] auto handle_begin(const SDL_Event& event,
                                   const sound_drop_origin_t& origin) -> sound_drop_event_result_t;
    [[nodiscard]] auto handle_position(const SDL_Event& event,
                                       resource_drop_target_t target) -> sound_drop_event_result_t;
    [[nodiscard]] auto handle_file(const SDL_Event& event,
                                  resource_drop_target_t target) -> sound_drop_event_result_t;
    [[nodiscard]] auto handle_complete(const SDL_Event& event,
                                       const sound_drop_origin_t& origin) -> sound_drop_event_result_t;
    void reset();

    bool active_{};
    bool claimed_{};
    bool saw_file_{};
    bool positioned_{};
    resource_drop_target_t position_target_{resource_drop_target_t::none};
    bool origin_changed_{};
    sound_drop_origin_t origin_{};
    std::vector<std::string> paths_;
    std::unordered_set<std::string> normalized_paths_;
    std::string reason_;
};

} // namespace wormedit::sys
