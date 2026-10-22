#include "sys/app/crash_backtrace.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

enum class crash_case_t { abort_signal, segfault_signal, cpp_terminate };

struct child_result_t {
    int status{};
    std::string error_output;
    bool started{};
};

[[noreturn]] void crash_child(crash_case_t crash_case) {
    const rlimit no_core{0, 0};
    (void)setrlimit(RLIMIT_CORE, &no_core);
    alarm(5);
    wormedit::sys::install_crash_backtrace_handlers();
    switch (crash_case) {
    case crash_case_t::abort_signal: std::abort();
    case crash_case_t::segfault_signal: raise(SIGSEGV); break;
    case crash_case_t::cpp_terminate: throw std::runtime_error("backtrace probe");
    }
    _exit(100);
}

auto run_child(crash_case_t crash_case) -> child_result_t {
    int capture[2]{};
    if (pipe(capture) != 0) return {};
    const auto child = fork();
    if (child < 0) {
        close(capture[0]);
        close(capture[1]);
        return {};
    }
    if (child == 0) {
        close(capture[0]);
        if (dup2(capture[1], STDERR_FILENO) < 0) _exit(101);
        close(capture[1]);
        crash_child(crash_case);
    }
    close(capture[1]);
    child_result_t result{.started = true};
    char buffer[4096]{};
    while (const auto size = read(capture[0], buffer, sizeof(buffer))) {
        if (size < 0) break;
        result.error_output.append(buffer, static_cast<std::size_t>(size));
    }
    close(capture[0]);
    if (waitpid(child, &result.status, 0) != child) result.started = false;
    return result;
}

auto verify_crash(crash_case_t crash_case, int expected_signal,
                  const char* expected_text) -> bool {
    const auto result = run_child(crash_case);
    const auto okay = result.started && WIFSIGNALED(result.status) &&
                      WTERMSIG(result.status) == expected_signal &&
                      result.error_output.find(expected_text) != std::string::npos &&
                      result.error_output.find("backtrace:") != std::string::npos &&
                      result.error_output.find("wormedit_crash_backtrace_tests") !=
                          std::string::npos;
    if (!okay)
        std::fprintf(stderr, "Crash backtrace case failed: %s\n%s", expected_text,
                     result.error_output.c_str());
    return okay;
}

} // namespace

int main() {
    const auto abort_ok = verify_crash(crash_case_t::abort_signal, SIGABRT, "fatal signal SIGABRT");
    const auto segfault_ok = verify_crash(crash_case_t::segfault_signal, SIGSEGV,
                                         "fatal signal SIGSEGV");
    const auto terminate_ok = verify_crash(crash_case_t::cpp_terminate, SIGABRT,
                                           "unhandled C++ exception: backtrace probe");
    return abort_ok && segfault_ok && terminate_ok ? 0 : 1;
}
