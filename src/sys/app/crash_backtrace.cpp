#include "sys/app/crash_backtrace.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#endif

namespace wormedit::sys {
namespace {

#if (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)

alignas(std::max_align_t) std::array<std::byte, 64 * 1024> alternate_signal_stack{};
volatile sig_atomic_t handling_fatal_signal = 0;

template <std::size_t Size>
void write_stderr(const char (&message)[Size]) noexcept {
    // Best effort inside a signal handler. GCC ignores a (void) cast on glibc's
    // warn_unused_result write() under _FORTIFY_SOURCE, so keep the result instead.
    [[maybe_unused]] const auto written = write(STDERR_FILENO, message, Size - 1);
}

void write_signal_name(int signal_number) noexcept {
    switch (signal_number) {
    case SIGABRT: write_stderr("SIGABRT"); break;
    case SIGSEGV: write_stderr("SIGSEGV"); break;
    case SIGILL: write_stderr("SIGILL"); break;
    case SIGFPE: write_stderr("SIGFPE"); break;
#if defined(SIGBUS)
    case SIGBUS: write_stderr("SIGBUS"); break;
#endif
    default: write_stderr("unknown signal"); break;
    }
}

void print_backtrace() noexcept {
    constexpr int maximum_frames = 64;
    void* frames[maximum_frames]{};
    const auto count = backtrace(frames, maximum_frames);
    if (count > 0) {
        backtrace_symbols_fd(frames, count, STDERR_FILENO);
    } else {
        write_stderr("backtrace unavailable\n");
    }
}

void fatal_signal_handler(int signal_number, siginfo_t*, void*) noexcept {
    if (handling_fatal_signal != 0) _exit(128 + signal_number);
    handling_fatal_signal = 1;
    write_stderr("wormedit: fatal signal ");
    write_signal_name(signal_number);
    write_stderr("; backtrace:\n");
    print_backtrace();

    // SA_RESETHAND restored the default action. Unblock this signal before
    // raising it again so the operating system can still produce a core dump.
    sigset_t unblocked{};
    sigemptyset(&unblocked);
    sigaddset(&unblocked, signal_number);
    sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
    raise(signal_number);
    _exit(128 + signal_number);
}

void install_fatal_signal(int signal_number, bool use_alternate_stack) noexcept {
    struct sigaction action {};
    action.sa_sigaction = fatal_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND |
                      (use_alternate_stack ? SA_ONSTACK : 0);
    if (sigaction(signal_number, &action, nullptr) != 0)
        std::fprintf(stderr, "wormedit: could not install backtrace for signal %d\n",
                     signal_number);
}

void install_fatal_signals() noexcept {
    // Eagerly load the unwinder before entering an asynchronous signal handler.
    void* frame{};
    (void)backtrace(&frame, 1);
    stack_t stack{};
    stack.ss_sp = alternate_signal_stack.data();
    stack.ss_size = alternate_signal_stack.size();
    const bool use_alternate_stack = sigaltstack(&stack, nullptr) == 0;
    install_fatal_signal(SIGABRT, use_alternate_stack);
    install_fatal_signal(SIGSEGV, use_alternate_stack);
    install_fatal_signal(SIGILL, use_alternate_stack);
    install_fatal_signal(SIGFPE, use_alternate_stack);
#if defined(SIGBUS)
    install_fatal_signal(SIGBUS, use_alternate_stack);
#endif
}

#endif

void terminate_with_context() noexcept {
    if (const auto exception = std::current_exception()) {
        try {
            std::rethrow_exception(exception);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "wormedit: unhandled C++ exception: %s\n", error.what());
        } catch (...) {
            std::fputs("wormedit: unhandled C++ exception\n", stderr);
        }
    } else {
        std::fputs("wormedit: C++ termination without an active exception\n", stderr);
    }
    std::fflush(stderr);
    std::abort();
}

} // namespace

void install_crash_backtrace_handlers() noexcept {
#if (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
    install_fatal_signals();
#endif
    std::set_terminate(terminate_with_context);
}

} // namespace wormedit::sys
