#pragma once

namespace wormedit::sys {

// Install process-wide fatal-signal and C++ termination diagnostics early in main.
void install_crash_backtrace_handlers() noexcept;

} // namespace wormedit::sys
