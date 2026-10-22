#pragma once
#include "sys/resources/sound_drop_transaction.h"
namespace wormedit::sys {
// Production SDL resource-drop admission; hit-tests the current frontend and
// lets the shared transaction retain POSITION, destination and origin.
[[nodiscard]] auto is_resource_drop_event(const SDL_Event&) -> bool;
[[nodiscard]] auto handle_resource_drop(const SDL_Event& event,
    const sound_drop_origin_t& origin, sound_drop_transaction_t& transaction)
    -> sound_drop_event_result_t;
}
