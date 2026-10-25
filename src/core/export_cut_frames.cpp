#include <wormedit/core/export_cut_frames.h>
#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>

namespace wormedit {
namespace {

auto preceding_cut(std::span<const export_cut_frame_interval_t> cuts, std::uint64_t frame) -> std::size_t {
    return static_cast<std::size_t>(std::upper_bound(cuts.begin(), cuts.end(), frame,
        [](auto value, const auto& cut) { return value < cut.begin_frame; }) - cuts.begin());
}

auto boundary_frame(std::int64_t milliseconds, std::uint32_t rate, std::uint64_t end) -> std::uint64_t {
    return std::min(end, sample_frame_at({milliseconds, 1'000}, rate).frame.value_or(end));
}

} // namespace

auto export_cut_frame_timeline_t::from(const export_cut_timeline_t& timeline, std::uint32_t rate,
                                      std::uint64_t source_end) -> diagnosed_t<export_cut_frame_timeline_t> {
    diagnosed_t<export_cut_frame_timeline_t> result;
    result.value.source_end_ = source_end;
    if (!rate) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
            "Export cut sample clock needs a positive sample rate."));
        return result;
    }
    for (const auto& cut : timeline.intervals()) {
        const auto begin = boundary_frame(cut.begin_milliseconds, rate, source_end);
        const auto end = boundary_frame(cut.end_milliseconds, rate, source_end);
        if (end <= begin) continue;
        auto& intervals = result.value.intervals_;
        if (!intervals.empty() && begin <= intervals.back().end_frame)
            intervals.back().end_frame = std::max(intervals.back().end_frame, end);
        else intervals.push_back({.begin_frame = begin, .end_frame = end});
    }
    std::uint64_t removed{};
    for (auto& cut : result.value.intervals_) {
        cut.collapsed_begin_frame = cut.begin_frame - removed;
        removed += cut.end_frame - cut.begin_frame;
    }
    return result;
}

auto export_cut_frame_timeline_t::contains(std::uint64_t frame) const -> bool {
    const auto at = preceding_cut(intervals_, frame);
    return at && frame < intervals_[at - 1].end_frame;
}

auto export_cut_frame_timeline_t::collapsed_frame(std::uint64_t frame) const -> std::uint64_t {
    frame = std::min(frame, source_end_);
    const auto at = preceding_cut(intervals_, frame);
    if (!at) return frame;
    const auto& cut = intervals_[at - 1];
    return frame < cut.end_frame ? cut.collapsed_begin_frame : frame - cut.end_frame + cut.collapsed_begin_frame;
}

auto export_cut_frame_timeline_t::source_frame(std::uint64_t frame) const -> std::uint64_t {
    const auto at = std::upper_bound(intervals_.begin(), intervals_.end(), frame,
        [](auto value, const auto& cut) { return value < cut.collapsed_begin_frame; });
    const auto shift = at == intervals_.begin() ? 0 : std::prev(at)->end_frame - std::prev(at)->collapsed_begin_frame;
    return std::min(frame, source_end_ - shift) + shift;
}

auto export_cut_frame_timeline_t::source_frame_at_fraction(double fraction, std::uint64_t source_end) const -> std::optional<std::uint64_t> {
    const auto count = collapsed_frame(std::min(source_end, source_end_));
    if (!count || !std::isfinite(fraction) || fraction < 0 || fraction > 1) return std::nullopt;
    return source_frame(static_cast<std::uint64_t>(std::round(static_cast<long double>(fraction) * (count - 1))));
}

auto export_cut_frame_timeline_t::collapsed_fraction(std::uint64_t frame, std::uint64_t source_end) const -> std::optional<double> {
    const auto count = collapsed_frame(std::min(source_end, source_end_));
    if (!count) return std::nullopt;
    return static_cast<double>(collapsed_frame(frame)) / count;
}

auto export_cut_frame_timeline_t::removed_frames() const -> std::uint64_t {
    return intervals_.empty() ? 0 : intervals_.back().end_frame - intervals_.back().collapsed_begin_frame;
}

auto export_cut_frame_timeline_t::kept_frame_count(std::uint64_t begin, std::uint64_t end) const -> std::uint64_t {
    return end > begin ? collapsed_frame(end) - collapsed_frame(begin) : 0;
}

auto export_cut_frame_timeline_t::next_kept_interval(std::uint64_t begin, std::uint64_t end) const
    -> std::optional<export_cut_frame_interval_t> {
    begin = std::min(begin, source_end_);
    end = std::min(end, source_end_);
    auto at = preceding_cut(intervals_, begin);
    if (at && begin < intervals_[at - 1].end_frame) begin = intervals_[at - 1].end_frame;
    if (begin >= end) return std::nullopt;
    at = preceding_cut(intervals_, begin);
    if (at < intervals_.size()) end = std::min(end, intervals_[at].begin_frame);
    if (begin >= end) return std::nullopt;
    return export_cut_frame_interval_t{begin, end, collapsed_frame(begin)};
}

auto export_cut_frame_timeline_t::kept_intervals(std::uint64_t begin, std::uint64_t end) const
    -> std::vector<export_cut_frame_interval_t> {
    std::vector<export_cut_frame_interval_t> result;
    begin = std::min(begin, source_end_);
    end = std::min(end, source_end_);
    auto at = preceding_cut(intervals_, begin);
    if (at && intervals_[at - 1].end_frame > begin) --at;
    for (; at < intervals_.size() && intervals_[at].begin_frame < end; ++at) {
        if (begin < intervals_[at].begin_frame)
            result.push_back({.begin_frame = begin, .end_frame = intervals_[at].begin_frame,
                .collapsed_begin_frame = collapsed_frame(begin)});
        begin = std::max(begin, intervals_[at].end_frame);
    }
    if (begin < end) result.push_back({.begin_frame = begin, .end_frame = end,
        .collapsed_begin_frame = collapsed_frame(begin)});
    return result;
}

} // namespace wormedit
