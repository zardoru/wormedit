#pragma once

#include <wormedit/core/export_cut.h>

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace wormedit {

struct export_cut_frame_interval_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    std::uint64_t collapsed_begin_frame{};
    auto operator==(const export_cut_frame_interval_t&) const -> bool = default;
};

// A sample clock rounds each absolute source boundary once, then normalizes the
// resulting frame union. Every sample consumer shares these exact splice seams.
class export_cut_frame_timeline_t {
  public:
    [[nodiscard]] static auto from(const export_cut_timeline_t& timeline, std::uint32_t sample_rate,
        std::uint64_t source_end = std::numeric_limits<std::uint64_t>::max())
        -> diagnosed_t<export_cut_frame_timeline_t>;
    [[nodiscard]] auto intervals() const -> std::span<const export_cut_frame_interval_t> { return intervals_; }
    [[nodiscard]] auto is_empty() const -> bool { return intervals_.empty(); }
    [[nodiscard]] auto contains(std::uint64_t source_frame) const -> bool;
    [[nodiscard]] auto collapsed_frame(std::uint64_t source_frame) const -> std::uint64_t;
    [[nodiscard]] auto source_frame(std::uint64_t collapsed_frame) const -> std::uint64_t;
    [[nodiscard]] auto source_frame_at_fraction(double fraction, std::uint64_t source_end = std::numeric_limits<std::uint64_t>::max()) const -> std::optional<std::uint64_t>;
    [[nodiscard]] auto collapsed_fraction(std::uint64_t source_frame, std::uint64_t source_end = std::numeric_limits<std::uint64_t>::max()) const -> std::optional<double>;
    [[nodiscard]] auto removed_frames() const -> std::uint64_t;
    [[nodiscard]] auto kept_frame_count(std::uint64_t begin, std::uint64_t end) const -> std::uint64_t;
    [[nodiscard]] auto next_kept_interval(std::uint64_t begin, std::uint64_t end) const
        -> std::optional<export_cut_frame_interval_t>;
    [[nodiscard]] auto kept_intervals(std::uint64_t begin, std::uint64_t end) const
        -> std::vector<export_cut_frame_interval_t>;
    auto operator==(const export_cut_frame_timeline_t&) const -> bool = default;
  private:
    std::vector<export_cut_frame_interval_t> intervals_;
    std::uint64_t source_end_{std::numeric_limits<std::uint64_t>::max()};
};

} // namespace wormedit
