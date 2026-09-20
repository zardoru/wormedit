#pragma once

#include "ui/frontend.h"

[[nodiscard]] bool worm_ui_nuklear_setup_view_valid(const WormUiSetupView* view);

[[nodiscard]] bool
worm_ui_nuklear_setup_policy_values_valid(WormUiStepmaniaFileProfile file_profile,
                                          WormUiStepmaniaTimingSyntax timing_syntax,
                                          WormUiStepmaniaImplementation implementation);
