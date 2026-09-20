#include "ui/nuklear/setup_state.h"

bool worm_ui_nuklear_setup_policy_values_valid(
    WormUiStepmaniaFileProfile file_profile, WormUiStepmaniaTimingSyntax timing_syntax,
    WormUiStepmaniaImplementation implementation) {
    return file_profile >= WORM_UI_STEPMANIA_FILE_SM &&
           file_profile <= WORM_UI_STEPMANIA_FILE_SSC &&
           timing_syntax >= WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS &&
           timing_syntax <= WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_STOP &&
           implementation >= WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY &&
           implementation <= WORM_UI_STEPMANIA_IMPLEMENTATION_ETTERNA_0_75_1;
}

bool worm_ui_nuklear_setup_view_valid(const WormUiSetupView* view) {
    return view && (!view->visible ||
                    (view->stepmania_policy_status &&
                     worm_ui_nuklear_setup_policy_values_valid(
                         view->stepmania_file_profile, view->stepmania_timing_syntax,
                         view->stepmania_implementation)));
}
