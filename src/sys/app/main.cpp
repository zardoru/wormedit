#include "sys/app/application.h"
#include "sys/app/crash_backtrace.h"

// On Windows, SDL supplies the entry point and passes the command line as UTF-8,
// so startup paths outside the ANSI code page (for example 曲) survive.
#include <SDL3/SDL_main.h>

#include <memory>
#include <utility>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main(int argc, char* argv[]) {
    wormedit::sys::install_crash_backtrace_handlers();
    wormedit::sys::application_startup_arguments_t arguments;
    if (!wormedit::sys::parse_application_startup_arguments(argc, argv, arguments)) return 2;
    auto app = std::make_unique<wormedit::sys::application_t>(std::move(arguments));
    if (!app->start()) {
#ifdef __EMSCRIPTEN__
        MAIN_THREAD_EM_ASM({ Module.editorStatus('Editor startup failed. See browser console for SDL details.', 'failed'); });
#endif
        return 1;
    }
#ifdef __EMSCRIPTEN__
    MAIN_THREAD_EM_ASM({
        Module.editorStarted = true;
        Module.editorStatus('Editor ready. Save downloads a ZIP; the workspace is temporary.', 'ready');
    });
    // The browser owns event delivery and presentation; every frame must yield to it.
    emscripten_set_main_loop_arg([](void* context) {
        auto* browser_app = static_cast<wormedit::sys::application_t*>(context);
        if (browser_app->running) {
            browser_app->run_frame();
            static unsigned frames{};
            if (++frames == 1 || frames % 60 == 0)
                MAIN_THREAD_EM_ASM({ Module.editorFrames = $0; }, frames);
        } else {
            browser_app->shutdown();
            delete browser_app;
            emscripten_cancel_main_loop();
            MAIN_THREAD_EM_ASM({ Module.editorStatus('Editor closed. Download workspace or reload to restart.', 'closed'); });
        }
    }, app.release(), 0, true);
#else
    while (app->running) app->run_frame();
    app->shutdown();
#endif
    return 0;
}
