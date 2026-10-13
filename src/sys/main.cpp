#include "sys/application.h"
#include "sys/crash_backtrace.h"

#include <memory>
#include <utility>

int main(int argc, char** argv) {
    wormedit::sys::install_crash_backtrace_handlers();
    wormedit::sys::application_startup_arguments_t arguments;
    if (!wormedit::sys::parse_application_startup_arguments(argc, argv, arguments)) return 2;
    auto app = std::make_unique<wormedit::sys::application_t>(std::move(arguments));
    if (!app->start()) return 1;
    while (app->running) app->run_frame();
    app->shutdown();
    return 0;
}
