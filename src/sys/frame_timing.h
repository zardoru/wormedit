#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace wormedit::sys {
enum class frame_phase_t : std::size_t {
    events,
    audio,
    workspace,
    scene,
    nuklear,
    render,
    audio_service,
    audio_pump,
    workspace_flatten,
    workspace_frame_view,
    workspace_metadata,
    workspace_audio_waveforms,
    workspace_timing,
    workspace_compose_waveform,
    workspace_waveform_overlays,
    workspace_source_playhead,
    workspace_compose_controls,
    workspace_preparation,
    workspace_actions,
    workspace_follow,
    workspace_bmson,
    render_convert,
    render_submit,
    render_present,
    count
};
inline constexpr std::size_t frame_timing_capacity = 240;
inline constexpr std::size_t frame_phase_count = static_cast<std::size_t>(frame_phase_t::count);
inline constexpr std::array<const char*, frame_phase_count> frame_phase_names{
    "Event handling",
    "Audio service/pump",
    "Workspace projection",
    "Compose scene",
    "Nuklear build",
    "Render/present",
    "  Service",
    "  Pump",
    "  Flatten",
    "  Frame view",
    "  Metadata",
    "  Audio waveform views",
    "  Timing view",
    "  Compose waveform",
    "  Waveform overlays",
    "  Source playhead",
    "  Compose controls",
    "  Preparation",
    "  Actions",
    "  Follow playhead",
    "  BMSON projection",
    "  Convert",
    "  Submit",
    "  Present (VSync wait)"};
auto frame_phase_parent(frame_phase_t phase) -> frame_phase_t;
inline constexpr std::array<frame_phase_t, frame_phase_count> frame_phase_display_order{
    frame_phase_t::events,
    frame_phase_t::audio,
    frame_phase_t::audio_service,
    frame_phase_t::audio_pump,
    frame_phase_t::workspace,
    frame_phase_t::workspace_preparation,
    frame_phase_t::workspace_actions,
    frame_phase_t::workspace_follow,
    frame_phase_t::workspace_flatten,
    frame_phase_t::workspace_frame_view,
    frame_phase_t::workspace_metadata,
    frame_phase_t::workspace_audio_waveforms,
    frame_phase_t::workspace_timing,
    frame_phase_t::workspace_compose_controls,
    frame_phase_t::workspace_compose_waveform,
    frame_phase_t::workspace_waveform_overlays,
    frame_phase_t::workspace_source_playhead,
    frame_phase_t::workspace_bmson,
    frame_phase_t::scene,
    frame_phase_t::nuklear,
    frame_phase_t::render,
    frame_phase_t::render_convert,
    frame_phase_t::render_submit,
    frame_phase_t::render_present};
inline constexpr std::size_t frame_gpu_work_count = 5;
inline constexpr std::array<const char*, frame_gpu_work_count> frame_gpu_work_names{
    "Vertices", "Indices", "Draw calls", "Clip-rect changes", "Texture switches"};
struct frame_gpu_work_t {
    std::array<std::uint64_t, frame_gpu_work_count> values{};
    bool measured{};
};
// Application work excludes diagnostic windows such as the frame-timing panel,
// whose own submission is recorded separately as diagnostic work.
enum class frame_gpu_work_source_t { application, diagnostic };
struct frame_heap_allocation_t {
    std::uint64_t count{};
    std::uint64_t bytes{};
};
// Successful C++ new allocations only; no allocation, locking, or timer
// singleton initialization occurs in the hook. Counters are thread-local.
void record_frame_heap_allocation(std::size_t bytes) noexcept;
auto frame_heap_allocation_snapshot() noexcept -> frame_heap_allocation_t;
struct frame_timing_sample_t {
    frame_gpu_work_t gpu_work{};
    frame_gpu_work_t diagnostic_gpu_work{};
    frame_heap_allocation_t frame_heap{};
    std::array<frame_heap_allocation_t, frame_phase_count> phase_heap{};
    bool heap_measured{};
    double frame_ms{};
    std::array<double, frame_phase_count> phase_ms{};
    std::array<bool, frame_phase_count> measured{};
};
struct frame_timing_summary_t {
    std::size_t count{};
    double average_ms{};
    double p99_ms{};
};
struct frame_counter_summary_t {
    std::size_t count{};
    double average{};
    double p99{};
};
class frame_timing_t {
  public:
    void begin_frame();
    void end_frame();
    auto enter(frame_phase_t phase) -> frame_phase_t;
    void restore(frame_phase_t phase);
    void record(const frame_timing_sample_t& sample);
    void record_gpu_work(const frame_gpu_work_t& work,
                         frame_gpu_work_source_t source = frame_gpu_work_source_t::application);
    auto gpu_work_summary(std::size_t metric,
                          frame_gpu_work_source_t source = frame_gpu_work_source_t::application) const
        -> frame_counter_summary_t;
    auto heap_summary(frame_phase_t phase = frame_phase_t::count, bool bytes = false) const
        -> frame_counter_summary_t;
    auto sample(std::size_t chronological_index) const -> const frame_timing_sample_t&;
    static auto gpu_work(const frame_timing_sample_t& sample, frame_gpu_work_source_t source)
        -> const frame_gpu_work_t&;
    auto size() const -> std::size_t {
        return size_;
    }
    auto summary(frame_phase_t phase = frame_phase_t::count) const -> frame_timing_summary_t;

  private:
    void checkpoint();
    std::array<frame_timing_sample_t, frame_timing_capacity> samples_{};
    frame_timing_sample_t current_{};
    std::size_t next_{};
    std::size_t size_{};
    frame_heap_allocation_t heap_begin_{};
    frame_heap_allocation_t heap_last_{};
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
