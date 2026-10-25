#pragma once

#include <wormedit/core/result.h>
#include <wormedit/core/vocabulary.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

struct timing_index_t;

inline constexpr std::size_t maximum_export_cuts = 100'000;
inline constexpr std::size_t maximum_export_cut_label_bytes = 4U * 1024U;
inline constexpr std::size_t maximum_export_cut_total_label_bytes = 1U * 1024U * 1024U;

struct export_cut_t {
    object_id_t id;
    std::optional<std::string> label;
    std::int64_t begin_milliseconds{};
    std::int64_t end_milliseconds{};
    [[nodiscard]] auto is_valid() const -> bool;
    auto operator==(const export_cut_t&) const -> bool = default;
};

// Source-song intervals are half-open: [begin, end).
struct export_cut_interval_t {
    std::int64_t begin_milliseconds{};
    std::int64_t end_milliseconds{};
    [[nodiscard]] static auto milliseconds_at(const timing_index_t& timing,
        std::uint64_t row) -> std::optional<std::int64_t>;
    [[nodiscard]] static auto from_rows(const timing_index_t& timing,
        std::uint64_t begin_row, std::uint64_t end_row) -> std::optional<export_cut_interval_t>;
    auto operator==(const export_cut_interval_t&) const -> bool = default;
};

// This owner stores the normalized union and its prefix index once. All mapping
// queries are allocation-free; negative preroll passes through unchanged.
class export_cut_timeline_t {
  public:
    [[nodiscard]] static auto from(std::span<const export_cut_t> cuts)
        -> diagnosed_t<export_cut_timeline_t>;
    [[nodiscard]] auto intervals() const -> std::span<const export_cut_interval_t> { return intervals_; }
    [[nodiscard]] auto contains(std::int64_t source_milliseconds) const -> bool;
    [[nodiscard]] auto contains_seconds(signed_rational_t source_seconds) const -> bool;
    [[nodiscard]] auto intersects(export_cut_interval_t source_span) const -> bool;
    [[nodiscard]] auto collapsed_milliseconds(std::int64_t source_milliseconds) const -> std::int64_t;
    // A seam resolves to the right edge of its removed interval.
    [[nodiscard]] auto source_milliseconds(std::int64_t collapsed_milliseconds) const
        -> std::optional<std::int64_t>;
    [[nodiscard]] auto next_kept_milliseconds(std::int64_t source_milliseconds) const -> std::int64_t;
    [[nodiscard]] auto collapsed_seconds(signed_rational_t source_seconds) const
        -> std::optional<signed_rational_t>;
    [[nodiscard]] auto source_seconds(signed_rational_t collapsed_seconds) const
        -> std::optional<signed_rational_t>;
    // Continuous UI/audio clocks retain their fractional milliseconds. Exact
    // source timestamps and sample-frame clocks use the rational overloads.
    [[nodiscard]] auto collapsed_seconds(double source_seconds) const -> double;
    [[nodiscard]] auto source_seconds(double collapsed_seconds) const -> double;
    [[nodiscard]] auto kept_intervals(std::int64_t source_begin, std::int64_t source_end) const
        -> std::vector<export_cut_interval_t>;
    [[nodiscard]] auto removed_milliseconds() const -> std::int64_t;
    auto operator==(const export_cut_timeline_t&) const -> bool = default;
  private:
    std::vector<export_cut_interval_t> intervals_;
    std::vector<std::int64_t> collapsed_seams_;
};

[[nodiscard]] auto validate_export_cuts(std::span<const export_cut_t> cuts) -> diagnostic_list_t;

} // namespace wormedit
