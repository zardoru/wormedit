#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace wormedit::sys {
enum class frame_phase_t : std::size_t {
    events, audio, workspace, scene, nuklear, render, count
};
inline constexpr std::size_t frame_timing_capacity = 240;
inline constexpr std::size_t frame_phase_count = static_cast<std::size_t>(frame_phase_t::count);
inline constexpr std::array<const char*, frame_phase_count> frame_phase_names{
    "Event handling", "Audio service/pump", "Workspace projection", "Compose scene",
    "Nuklear build", "Render/present"};
struct frame_timing_sample_t {
    double frame_ms{};
    std::array<double, frame_phase_count> phase_ms{};
    std::array<bool, frame_phase_count> measured{};
};
struct frame_timing_summary_t {
    std::size_t count{};
    double average_ms{};
    double p99_ms{};
};
class frame_timing_t {
public:
    void begin_frame();
    void end_frame();
    auto enter(frame_phase_t phase) -> frame_phase_t;
    void restore(frame_phase_t phase);
    void record(const frame_timing_sample_t& sample);
    auto sample(std::size_t chronological_index) const -> const frame_timing_sample_t&;
    auto size() const -> std::size_t { return size_; }
    auto summary(frame_phase_t phase = frame_phase_t::count) const -> frame_timing_summary_t;
private:
    void checkpoint();
    std::array<frame_timing_sample_t, frame_timing_capacity> samples_{};
    frame_timing_sample_t current_{};
    std::size_t next_{};
    std::size_t size_{};
    std::uint64_t begin_{};
    std::uint64_t last_{};
    frame_phase_t phase_{frame_phase_t::count};
    bool active_{};
};
auto frame_timing() -> frame_timing_t&;
class frame_phase_scope_t {
public:
    explicit frame_phase_scope_t(frame_phase_t phase, frame_timing_t& timer = frame_timing());
    ~frame_phase_scope_t();
    frame_phase_scope_t(const frame_phase_scope_t&) = delete;
    auto operator=(const frame_phase_scope_t&) -> frame_phase_scope_t& = delete;
private:
    frame_timing_t& timer_;
    frame_phase_t previous_;
};
} // namespace wormedit::sys
