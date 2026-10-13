#pragma once

#include "sys/external_player.h"
#include "sys/preferences_runtime.h"
#include "ui/frontend.h"

namespace wormedit::sys {
struct external_player_runtime_t {
    external_player_t player;
    external_player_preferences_t preferences = default_external_player_preferences();
    program_preferences_runtime_t* program_preferences{};
    bool visible{};
    std::uint64_t revision{};
    std::vector<WormUiExternalPlayerDefinitionView> views;
};
} // namespace wormedit::sys
