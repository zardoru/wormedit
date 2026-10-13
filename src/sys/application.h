#pragma once

#include "sys/application_startup.h"
#include <memory>

namespace wormedit::sys {
class application_t {
public:
    explicit application_t(application_startup_arguments_t arguments);
    ~application_t();
    application_t(const application_t&) = delete;
    auto operator=(const application_t&) -> application_t& = delete;
    application_t(application_t&&) = delete;
    auto operator=(application_t&&) -> application_t& = delete;

    auto start() -> bool;
    void run_frame();
    void shutdown() noexcept;
    bool running{true};

private:
    struct implementation_t;
    std::unique_ptr<implementation_t> implementation_;
};
} // namespace wormedit::sys
