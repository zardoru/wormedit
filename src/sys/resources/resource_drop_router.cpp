#include "sys/resources/resource_drop_router.h"
#include "ui/frontend.h"
namespace wormedit::sys {
auto is_resource_drop_event(const SDL_Event& event) -> bool {
    return event.type == SDL_EVENT_DROP_BEGIN ||
        event.type == SDL_EVENT_DROP_POSITION || event.type == SDL_EVENT_DROP_FILE ||
        event.type == SDL_EVENT_DROP_COMPLETE;
}
auto handle_resource_drop(const SDL_Event& event, const sound_drop_origin_t& origin,
                          sound_drop_transaction_t& transaction) -> sound_drop_event_result_t {
    if (!is_resource_drop_event(event)) return {};
    const auto target = worm_ui_frontend_sound_list_contains(event.drop.windowID, event.drop.x, event.drop.y)
        ? resource_drop_target_t::sounds
        : worm_ui_frontend_bga_list_contains(event.drop.windowID, event.drop.x, event.drop.y)
            ? resource_drop_target_t::bga : resource_drop_target_t::none;
    return transaction.handle(event, origin, target);
}
}
