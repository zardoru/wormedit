#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/bms_program.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace wormedit {

template<class Values, class Visitor>
auto visit_declared_id_values(const Values& values, Visitor& visitor) -> bool {
    for (const auto& value : values)
        if (!visitor(value.id)) return false;
    return true;
}

// Each specialization lists declarations only; references never reserve IDs.
template<class Id> struct id_declarations_t;

// Prepared BMS projections reserve one disjoint identity space across nodes,
// content and shared pools. Adapt those typed declarations to the content allocator.
template<class Visitor> struct bms_identity_visitor_t {
    Visitor& visitor;
    template<class Id> auto operator()(Id id) -> bool {
        return visitor(object_id_t{id.value});
    }
};

template<> struct id_declarations_t<object_id_t> {
    template<class Visitor>
    static auto visit(const bms_prepared_program_t& prepared, Visitor& visitor) -> bool {
        bms_identity_visitor_t<Visitor> identities{visitor};
        if (!visit_declared_id_values(prepared.program.nodes(), identities) ||
            !visit_declared_id_values(prepared.bgm_columns, identities) ||
            !visit_declared_id_values(prepared.resources, identities) ||
            !visit_declared_id_values(prepared.audio_assets, identities) ||
            !visit_declared_id_values(prepared.visual_assets, identities) ||
            !visit_declared_id_values(prepared.semantic_layers, identities)) return false;
        for (const auto id : prepared.measures)
            if (!identities(id)) return false;
        for (const auto& node : prepared.program.nodes()) {
            const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
            if (!chart) continue;
            if (!visit_declared_id_values(chart->items, identities)) return false;
            for (const auto& item : chart->items) {
                const auto* native = std::get_if<bms_native_content_t>(&item.value);
                if (!native) continue;
                for (const auto id : native->declared_ids()) if (!identities(id)) return false;
            }
        }
        return true;
    }
    template<class Visitor>
    static auto visit(const chart_document_t& chart, Visitor& visitor) -> bool {
        if (!visit_declared_id_values(chart.bgm_source_lanes, visitor) ||
            !visit_declared_id_values(chart.lane_layouts, visitor) ||
            !visit_declared_id_values(chart.bpms, visitor) ||
            !visit_declared_id_values(chart.pauses, visitor) ||
            !visit_declared_id_values(chart.warps, visitor) ||
            !visit_declared_id_values(chart.scrolls, visitor) ||
            !visit_declared_id_values(chart.speeds, visitor) ||
            !visit_declared_id_values(chart.kiai, visitor) ||
            !visit_declared_id_values(chart.timing_anchors, visitor) ||
            !visit_declared_id_values(chart.bookmarks, visitor) ||
            !visit_declared_id_values(chart.sections, visitor) ||
            !visit_declared_id_values(chart.bga_events, visitor) ||
            !visit_declared_id_values(chart.sound_channels, visitor)) return false;
        for (const auto& measure : chart.measures)
            if (!visit_declared_id_values(measure.notes, visitor) ||
                !visit_declared_id_values(measure.bgm_events, visitor)) return false;
        for (const auto& channel : chart.sound_channels)
            if (!visit_declared_id_values(channel.events, visitor)) return false;
        return true;
    }
};

template<> struct id_declarations_t<measure_id_t> {
    template<class Visitor>
    static auto visit(const chart_document_t& chart, Visitor& visitor) -> bool {
        return visit_declared_id_values(chart.measures, visitor);
    }
};

template<> struct id_declarations_t<layer_id_t> {
    template<class Visitor>
    static auto visit(const chart_document_t& chart, Visitor& visitor) -> bool {
        return visit_declared_id_values(chart.semantic_layers, visitor);
    }
};

template<> struct id_declarations_t<recipe_id_t> {
    template<class Visitor>
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        if (!visit_declared_id_values(song.generator_recipes, visitor)) return false;
        for (const auto& chart : song.charts)
            if (!visit_declared_id_values(chart.sv_recipes, visitor)) return false;
        return true;
    }
};

template<> struct id_declarations_t<resource_id_t> {
    template<class Visitor>
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        return visit_declared_id_values(song.resources, visitor);
    }
};

template<> struct id_declarations_t<asset_id_t> {
    template<class Visitor>
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        return visit_declared_id_values(song.audio_assets, visitor) &&
               visit_declared_id_values(song.visual_assets, visitor) &&
               visit_declared_id_values(song.midi_assets, visitor);
    }
};

template<> struct id_declarations_t<sound_group_id_t> {
    template<class Visitor>
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        return visit_declared_id_values(song.sound_groups, visitor);
    }
};

template<> struct id_declarations_t<chart_id_t> {
    template<class Visitor>
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        return visit_declared_id_values(song.charts, visitor);
    }
};

template<class Id> struct id_ceiling_t {
    std::size_t limit{std::numeric_limits<std::size_t>::max()};
    std::size_t count{};
    std::uint64_t maximum{};

    template<class Owner>
    static auto from(const Owner& owner,
                     std::size_t bound = std::numeric_limits<std::size_t>::max())
        -> id_ceiling_t {
        id_ceiling_t result{.limit = bound};
        (void)id_declarations_t<Id>::visit(owner, result);
        return result;
    }

    auto operator()(Id id) -> bool {
        if (count >= limit) return false;
        ++count;
        maximum = std::max(maximum, id.value);
        return true;
    }

    [[nodiscard]] auto is_at_capacity() const -> bool { return count >= limit; }

    // A pure query: the complete consecutive reservation must fit before the
    // caller changes its document. Zero IDs is handled by the caller's policy.
    [[nodiscard]] auto next(std::size_t reservation_count = 1) const -> std::optional<Id> {
        if (reservation_count == 0 || is_at_capacity() || reservation_count > limit - count ||
            maximum == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
        const auto first = maximum + 1;
        if (reservation_count - 1 > std::numeric_limits<std::uint64_t>::max() - first)
            return std::nullopt;
        return Id{first};
    }

    auto operator==(const id_ceiling_t&) const -> bool = default;
};

template<class Id> struct id_counter_t {
    Id sought;
    std::size_t limit;
    std::size_t count{};

    template<class Owner>
    static auto from(const Owner& owner, Id id,
                     std::size_t bound = std::numeric_limits<std::size_t>::max())
        -> id_counter_t {
        id_counter_t result{.sought = id, .limit = bound};
        (void)id_declarations_t<Id>::visit(owner, result);
        return result;
    }

    auto operator()(Id id) -> bool {
        if (id == sought) ++count;
        return count < limit;
    }

    auto operator==(const id_counter_t&) const -> bool = default;
};

} // namespace wormedit
