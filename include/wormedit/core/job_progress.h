#pragma once

#include <atomic>
#include <cstddef>

namespace wormedit {
// Worker writes, UI reads. Counters are advisory and do not admit publication.
enum class job_phase_t { preparing, reading, publishing };
struct job_progress_t {
    std::atomic<job_phase_t> phase{job_phase_t::preparing};
    std::atomic<std::size_t> completed{};
    std::atomic<std::size_t> total{};
};
} // namespace wormedit
