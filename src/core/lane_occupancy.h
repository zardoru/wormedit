#pragma once

#include <wormedit/core/document.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace wormedit {

struct lane_occupancy_key_t {
    enum class domain_t { play, bgm };
    domain_t domain{domain_t::play};
    std::uint64_t value{};
    [[nodiscard]] static auto from(lane_id_t lane) -> lane_occupancy_key_t {
        return {.domain = domain_t::play, .value = lane.value};
    }
    [[nodiscard]] static auto from(object_id_t lane) -> lane_occupancy_key_t {
        return {.domain = domain_t::bgm, .value = lane.value};
    }
    auto operator==(const lane_occupancy_key_t&) const -> bool = default;
};

struct lane_occupant_t {
    lane_occupancy_key_t lane{};
    std::uint64_t begin{};
    std::uint64_t end{};
    object_id_t note{};
    bool proposed{};
    note_kind_t kind{note_kind_t::tap};
    bool has_end{};
    std::size_t ordinal{std::numeric_limits<std::size_t>::max()};

    [[nodiscard]] static auto from(const note_t& note, std::optional<std::uint64_t> begin,
                                   std::optional<std::uint64_t> end)
        -> std::optional<lane_occupant_t> {
        if (!begin || !end) return std::nullopt;
        return lane_occupant_t{.lane = lane_occupancy_key_t::from(note.lane), .begin = *begin, .end = *end,
            .note = note.id, .kind = note.kind, .has_end = note.end.has_value()};
    }
    [[nodiscard]] static auto from(const note_t& note, const measure_index_t& index)
        -> std::optional<lane_occupant_t> {
        const auto begin = index.row_of(note.position);
        return from(note, begin, note.end ? index.row_of(*note.end) : begin);
    }
    [[nodiscard]] auto head_key() const { return std::tuple{lane.domain, lane.value, begin}; }
    [[nodiscard]] auto row_span() const { return std::pair{begin, end}; }
    [[nodiscard]] static auto is_expired(std::uint64_t end, std::uint64_t begin) -> bool {
        return end < begin;
    }
    [[nodiscard]] auto contains(std::uint64_t row) const -> bool {
        return begin <= row && row <= end;
    }
    [[nodiscard]] auto overlaps(const lane_occupant_t& other) const -> bool {
        return lane == other.lane && begin <= other.end && other.begin <= end;
    }
    [[nodiscard]] auto covered_rows(std::span<const std::uint64_t> rows) const
        -> std::span<const std::uint64_t> {
        const auto first = std::ranges::lower_bound(rows, begin);
        const auto after = std::ranges::upper_bound(first, rows.end(), end);
        return {first, after};
    }
    auto operator==(const lane_occupant_t&) const -> bool = default;
};

enum class lane_occupancy_order_t { head, paint, resize, move, paste };
enum class lane_occupancy_pairs_t { all, proposed };

// Absolute, closed note ranges. Callers retain validation, diagnostics and edit policy.
class lane_occupancy_t {
  public:
    [[nodiscard]] static auto from(std::vector<lane_occupant_t> entries,
                                   lane_occupancy_order_t order = lane_occupancy_order_t::paint)
        -> lane_occupancy_t {
        for (std::size_t index = 0; index < entries.size(); ++index)
            if (entries[index].ordinal == std::numeric_limits<std::size_t>::max())
                entries[index].ordinal = index;
        std::ranges::sort(entries, [order](const auto& left, const auto& right) {
            return less(left, right, order);
        });
        lane_occupancy_t result;
        result.entries_ = std::move(entries);
        return result;
    }
    [[nodiscard]] static auto from(const chart_document_t& chart, const measure_index_t& index,
        const std::unordered_set<std::uint64_t>* excluded = nullptr) -> lane_occupancy_t {
        std::size_t count{};
        for (const auto& measure : chart.measures) count += measure.notes.size();
        std::vector<lane_occupant_t> entries;
        entries.reserve(count);
        for (const auto& measure : chart.measures)
            for (const auto& note : measure.notes)
                if (!excluded || !excluded->contains(note.id.value))
                    if (auto entry = lane_occupant_t::from(note, index)) entries.push_back(*entry);
        return from(std::move(entries));
    }
    [[nodiscard]] auto entries() const -> const std::vector<lane_occupant_t>& { return entries_; }
    [[nodiscard]] auto into_entries() && -> std::vector<lane_occupant_t> { return std::move(entries_); }
    [[nodiscard]] auto first_at_head(lane_occupancy_key_t lane, std::uint64_t row) const -> const lane_occupant_t* {
        build_queries();
        const auto first = std::ranges::lower_bound(head_order_, std::tuple{lane.domain, lane.value, row}, {},
            [&](std::size_t index) { return entries_[index].head_key(); });
        return first != head_order_.end() && entries_[*first].head_key() == std::tuple{lane.domain, lane.value, row}
            ? &entries_[*first] : nullptr;
    }
    [[nodiscard]] auto first_at_head(lane_id_t lane, std::uint64_t row) const -> const lane_occupant_t* {
        return first_at_head(lane_occupancy_key_t::from(lane), row);
    }
    [[nodiscard]] auto find(object_id_t id) const -> const lane_occupant_t* {
        build_queries();
        const auto found = std::ranges::lower_bound(object_order_, id.value, {},
            [&](std::size_t index) { return entries_[index].note.value; });
        return found != object_order_.end() && entries_[*found].note == id ? &entries_[*found] : nullptr;
    }
    [[nodiscard]] auto collides(lane_id_t lane, std::uint64_t begin, std::uint64_t end) const -> bool {
        return collides(lane_occupancy_key_t::from(lane), begin, end);
    }
    [[nodiscard]] auto collides_bgm(object_id_t lane, std::uint64_t row) const -> bool {
        return collides(lane_occupancy_key_t::from(lane), row, row);
    }
    [[nodiscard]] auto first_collision(lane_occupancy_pairs_t pairs) const -> const lane_occupant_t* {
        std::optional<lane_occupancy_key_t> lane;
        std::optional<std::uint64_t> existing_end, proposed_end;
        for (const auto& entry : entries_) {
            const auto key = entry.lane;
            if (lane != key) { lane = key; existing_end.reset(); proposed_end.reset(); }
            if ((proposed_end && !lane_occupant_t::is_expired(*proposed_end, entry.begin)) ||
                (existing_end && (pairs == lane_occupancy_pairs_t::all || entry.proposed) &&
                 !lane_occupant_t::is_expired(*existing_end, entry.begin))) return &entry;
            auto& last = entry.proposed ? proposed_end : existing_end;
            last = std::max(last.value_or(0), entry.end);
        }
        return nullptr;
    }

  private:
    static auto less(const lane_occupant_t& left, const lane_occupant_t& right,
                     lane_occupancy_order_t order) -> bool {
        if (left.head_key() != right.head_key()) return left.head_key() < right.head_key();
        if (order == lane_occupancy_order_t::head) return false;
        if (order == lane_occupancy_order_t::paste) return left.proposed < right.proposed;
        if (left.end != right.end) return left.end < right.end;
        if (order != lane_occupancy_order_t::paint && left.proposed != right.proposed)
            return left.proposed < right.proposed;
        return order == lane_occupancy_order_t::resize ? false : left.note.value < right.note.value;
    }
    // Owners are call-local and single-threaded; only query consumers build these derived indexes.
    void build_queries() const {
        if (has_queries_) return;
        has_queries_ = true;
        head_order_.reserve(entries_.size()); object_order_.reserve(entries_.size());
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            head_order_.push_back(index); object_order_.push_back(index);
        }
        std::ranges::sort(head_order_, [&](auto left, auto right) {
            return std::tuple{entries_[left].head_key(), entries_[left].ordinal} <
                   std::tuple{entries_[right].head_key(), entries_[right].ordinal};
        });
        std::ranges::sort(object_order_, [&](auto left, auto right) {
            return std::pair{entries_[left].note.value, entries_[left].ordinal} <
                   std::pair{entries_[right].note.value, entries_[right].ordinal};
        });
        prefix_end_.reserve(entries_.size());
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            const auto& entry = entries_[head_order_[index]];
            const bool same_lane = index != 0 && entry.lane == entries_[head_order_[index - 1]].lane;
            prefix_end_.push_back(same_lane ? std::max(prefix_end_.back(), entry.end) : entry.end);
        }
    }
    auto collides(lane_occupancy_key_t lane, std::uint64_t begin, std::uint64_t end) const -> bool {
        build_queries();
        const auto after = std::ranges::upper_bound(head_order_, std::tuple{lane.domain, lane.value, end}, {},
            [&](std::size_t index) { return entries_[index].head_key(); });
        if (after == head_order_.begin()) return false;
        const auto position = static_cast<std::size_t>(after - head_order_.begin() - 1);
        const auto& entry = entries_[head_order_[position]];
        return entry.lane == lane && prefix_end_[position] >= begin;
    }
    std::vector<lane_occupant_t> entries_;
    mutable bool has_queries_{};
    mutable std::vector<std::size_t> head_order_, object_order_;
    mutable std::vector<std::uint64_t> prefix_end_;
};

} // namespace wormedit
