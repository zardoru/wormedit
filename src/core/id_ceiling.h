#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/bms_program.h>
#include <wormedit/core/bms_program_edit.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

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
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        bms_identity_visitor_t<Visitor> identities{visitor};
        if (!identities(song.id) || !visit_declared_id_values(song.audio_assets, identities) ||
            !visit_declared_id_values(song.visual_assets, identities) ||
            !visit_declared_id_values(song.resources, identities) ||
            !visit_declared_id_values(song.midi_assets, identities) ||
            !visit_declared_id_values(song.generator_recipes, identities) ||
            !visit_declared_id_values(song.storyboards, identities) ||
            !visit_declared_id_values(song.sound_groups, identities)) return false;
        for (const auto& chart : song.charts)
            if (!visit_chart_domain(chart, visitor)) return false;
        for (const auto& owner : song.prepared_bms_programs)
            if (owner && (!identities(owner->difficulty()) || !visit(owner->prepared(), visitor))) return false;
        return true;
    }
    template<class Visitor>
    static auto visit_chart_domain(const chart_document_t& chart, Visitor& visitor) -> bool {
        bms_identity_visitor_t<Visitor> identities{visitor};
        if (!identities(chart.id) || !visit(chart, visitor) ||
            !visit_declared_id_values(chart.measures, identities) ||
            !visit_declared_id_values(chart.semantic_layers, identities) ||
            !visit_declared_id_values(chart.sv_recipes, identities)) return false;
        for (const auto& channel : chart.sound_channels)
            if (!identities(channel.source_clip.id)) return false;
        std::vector<const bms_control_node_t*> pending;
        if (chart.bms_control_tree) pending.push_back(&*chart.bms_control_tree);
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            if (!identities(node->id)) return false;
            for (const auto& child : node->children) pending.push_back(&child);
        }
        return true;
    }
    template<class Visitor>
    static auto visit(const bms_prepared_program_t& prepared, Visitor& visitor) -> bool {
        bms_identity_visitor_t<Visitor> identities{visitor};
        if (prepared.settings.layout_segment && !identities(*prepared.settings.layout_segment)) return false;
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
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        for (const auto& chart : song.charts)
            if (!visit_declared_id_values(chart.measures, visitor)) return false;
        for (const auto& owner : song.prepared_bms_programs) {
            if (!owner) continue;
            for (const auto id : owner->prepared().measures)
                if (!visitor(id)) return false;
        }
        return true;
    }
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
        for (const auto& owner : song.prepared_bms_programs) {
            if (!owner) continue;
            for (const auto& node : owner->prepared().program.nodes()) {
                const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
                if (!chart) continue;
                for (const auto& item : chart->items) {
                    const auto* native = std::get_if<bms_native_content_t>(&item.value);
                    if (native && std::holds_alternative<sv_recipe_t>(native->payload()) &&
                        !visitor(recipe_id_t{item.id.value})) return false;
                }
            }
        }
        return true;
    }
};

template<> struct id_declarations_t<resource_id_t> {
    template<class Visitor>
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        if (!visit_declared_id_values(song.resources, visitor)) return false;
        for (const auto& owner : song.prepared_bms_programs)
            if (owner && !visit_declared_id_values(owner->prepared().resources, visitor)) return false;
        return true;
    }
};

template<> struct id_declarations_t<asset_id_t> {
    template<class Visitor>
    static auto visit(const song_document_t& song, Visitor& visitor) -> bool {
        if (!(visit_declared_id_values(song.audio_assets, visitor) &&
               visit_declared_id_values(song.visual_assets, visitor) &&
               visit_declared_id_values(song.midi_assets, visitor))) return false;
        for (const auto& owner : song.prepared_bms_programs)
            if (owner && (!visit_declared_id_values(owner->prepared().audio_assets, visitor) ||
                !visit_declared_id_values(owner->prepared().visual_assets, visitor))) return false;
        return true;
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
        if (!visit_declared_id_values(song.charts, visitor)) return false;
        for (const auto& owner : song.prepared_bms_programs)
            if (owner && !visitor(owner->difficulty())) return false;
        return true;
    }
};

// Identity reservations retain their maximum through Undo/Redo without counting
// historical declarations toward the current document's capacity.
template<class Query>
auto maximum_history_identity(const document_history_t& history, std::uint64_t maximum, Query query) -> std::uint64_t {
    const auto scan = [&](const auto& records) {
        for (const auto& record : records) {
            maximum = std::max(maximum, query(record.before));
            maximum = std::max(maximum, query(record.after));
        }
    };
    scan(history.undo); scan(history.redo);
    return maximum;
}

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
        if constexpr (std::same_as<Owner, song_document_t>) {
            for (const auto& recipe : owner.generator_recipes) {
                if constexpr (std::same_as<Id, object_id_t>)
                    for (const auto& binding : recipe.generated_notes)
                        result.reserve(Id{binding.value.id.value});
                if constexpr (std::same_as<Id, object_id_t> || std::same_as<Id, layer_id_t>)
                    for (const auto& binding : recipe.generated_layers)
                        result.reserve(Id{binding.value.id.value});
            }
        }
        if constexpr (std::same_as<Owner, song_document_t> && !std::same_as<Id, object_id_t>) {
            if (!owner.prepared_bms_programs.empty())
                result.maximum = std::max(result.maximum, id_ceiling_t<object_id_t>::from(owner).maximum);
        }
        return result;
    }

    static auto from(const document_history_t& history,
        std::size_t bound = std::numeric_limits<std::size_t>::max()) -> id_ceiling_t {
        auto result = from(history.document, bound);
        result.maximum = maximum_history_identity(history, result.maximum,
            [](const song_document_t& song) { return from(song).maximum; });
        return result;
    }

    // Historical witnesses reserve values without counting a live declaration.
    void reserve(Id id) { maximum = std::max(maximum, id.value); }

    auto operator()(Id id) -> bool {
        if (count >= limit) return false;
        ++count;
        reserve(id);
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
