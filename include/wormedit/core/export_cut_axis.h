#pragma once

#include <wormedit/core/export_cut.h>
#include <wormedit/core/timing.h>

#include <span>
#include <vector>

namespace wormedit {

struct export_cut_row_interval_t {
    long double begin{};
    long double end{};
    auto operator==(const export_cut_row_interval_t&) const -> bool = default;
};

// A view-only musical axis: authored positions remain source rows, while cut
// row intervals occupy no pixels. Audio-time cuts inside pauses need no row gap.
class export_cut_axis_t {
  public:
    [[nodiscard]] static auto from(const timing_index_t& timing, const export_cut_timeline_t& cuts)
        -> diagnosed_t<export_cut_axis_t>;
    [[nodiscard]] auto timeline() const -> const export_cut_timeline_t& { return timeline_; }
    [[nodiscard]] auto intervals() const -> std::span<const export_cut_row_interval_t> { return intervals_; }
    [[nodiscard]] auto kept_intervals(long double begin, long double end) const
        -> std::vector<export_cut_row_interval_t>;
    [[nodiscard]] auto is_empty() const -> bool { return intervals_.empty() && row_visibility_.empty(); }
    [[nodiscard]] auto collapsed_row(long double source_row) const -> long double;
    [[nodiscard]] auto source_row(long double collapsed_row) const -> long double;
    // Continuous boundaries retain pause-side semantics even if their event is hidden.
    [[nodiscard]] auto source_boundary_row(long double collapsed_row) const -> long double;
    [[nodiscard]] auto source_row_before_seam(long double collapsed_row) const -> long double;
    [[nodiscard]] auto contains_row(long double source_row) const -> bool;
    [[nodiscard]] auto contains_span(long double begin_row, long double end_row) const -> bool;
    [[nodiscard]] auto next_kept_row(long double source_row) const -> long double;
    [[nodiscard]] auto previous_kept_row(long double source_row) const -> long double;
    auto operator==(const export_cut_axis_t&) const -> bool = default;
  private:
    struct row_visibility_t {
        long double row{};
        bool hidden{};
        auto operator==(const row_visibility_t&) const -> bool = default;
    };
    [[nodiscard]] auto is_geometrically_hidden(long double row) const -> bool;
    void retain_event_visibility(const timing_index_t& timing);
    void append_kept_interval(long double begin, long double end, std::vector<export_cut_row_interval_t>& kept) const;
    std::vector<row_visibility_t> row_visibility_;
    export_cut_timeline_t timeline_;
    std::vector<export_cut_row_interval_t> intervals_;
    std::vector<long double> seams_;
};

} // namespace wormedit
