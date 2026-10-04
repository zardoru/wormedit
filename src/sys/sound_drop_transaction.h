#pragma once

#include "ui/frontend.h"

#include <SDL3/SDL_events.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace wormedit::sys {

struct sound_drop_origin_t {
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t revision{};
    WormUiWorkspaceTab tab{WORM_UI_WORKSPACE_TAB_SETUP};
    std::uint32_t window_id{};
    bool slots_mode{};
    std::uint16_t first_slot{};
};

struct sound_drop_batch_t {
    sound_drop_origin_t origin;
    std::vector<std::string> paths;
    std::string reason;

    [[nodiscard]] auto accepted() const noexcept -> bool { return reason.empty(); }
};

struct sound_drop_event_result_t {
    bool consumed{};
    std::optional<sound_drop_batch_t> completed;
};

// Retains one SDL file-drop transaction across event polls. The caller reports
// whether the latest drop position lies over the Sounds list. A new BEGIN
// returns the previous claimed batch as rejected if it had not completed.
class sound_drop_transaction_t {
  public:
    [[nodiscard]] auto handle(const SDL_Event& event, const sound_drop_origin_t& current_origin,
                              bool position_over_sound_list) -> sound_drop_event_result_t;
    [[nodiscard]] auto cancel() -> std::optional<sound_drop_batch_t>;
    [[nodiscard]] auto active() const noexcept -> bool { return active_; }
    [[nodiscard]] auto claimed() const noexcept -> bool { return claimed_; }

  private:
    void begin(const sound_drop_origin_t& origin);
    void note_origin(const sound_drop_origin_t& origin);
    void append_path(const char* path);
    [[nodiscard]] auto finish(const sound_drop_origin_t& origin) -> sound_drop_batch_t;
    [[nodiscard]] auto handle_begin(const SDL_Event& event,
                                   const sound_drop_origin_t& origin) -> sound_drop_event_result_t;
    [[nodiscard]] auto handle_position(const SDL_Event& event,
                                       bool over_sound_list) -> sound_drop_event_result_t;
    [[nodiscard]] auto handle_file(const SDL_Event& event,
                                  bool over_sound_list) -> sound_drop_event_result_t;
    [[nodiscard]] auto handle_complete(const SDL_Event& event,
                                       const sound_drop_origin_t& origin) -> sound_drop_event_result_t;
    void reset();

    bool active_{};
    bool claimed_{};
    bool saw_file_{};
    bool positioned_{};
    bool position_over_sound_list_{};
    bool origin_changed_{};
    sound_drop_origin_t origin_{};
    std::vector<std::string> paths_;
    std::unordered_set<std::string> normalized_paths_;
    std::string reason_;
};

} // namespace wormedit::sys
