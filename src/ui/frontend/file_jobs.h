#pragma once

#include <cstddef>
#include <string>

namespace wormedit::ui {
struct file_job_view_t {
    bool active{};
    bool cancelling{};
    std::size_t completed{};
    std::size_t total{};
    std::string label;
};
struct file_jobs_view_t {
    file_job_view_t opening;
    file_job_view_t exporting;
    file_job_view_t preview;
};
struct file_jobs_action_t {
    bool cancel_open{};
    bool cancel_export{};
    bool cancel_preview{};
};
} // namespace wormedit::ui
