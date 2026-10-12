#pragma once
#include "sys/sound_drop_transaction.h"
namespace wormedit::sys {
// Production SDL resource-drop admission; hit-tests the current frontend and
// lets the shared transaction retain POSITION, destination and origin.
[[nodiscard]] auto handle_resource_drop(const SDL_Event& event,
    const sound_drop_origin_t& origin, sound_drop_transaction_t& transaction)
    -> sound_drop_event_result_t;
}
