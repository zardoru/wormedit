#include <wormedit/core/bms_program_edit.h>
#include "chart_object_ids.h"
#include <wormedit/core/bgm_columns.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace wormedit {
namespace {

auto events_in_column(const chart_document_t& chart, object_id_t column) -> std::size_t {
    std::size_t total{};
    for (const auto& measure : chart.measures) {
        total += static_cast<std::size_t>(std::ranges::count(
            measure.bgm_events, column, &bgm_event_t::source_lane));
    }
    return total;
}

} // namespace

auto grow_bgm_columns(chart_document_t& chart, std::size_t count)
    -> result_t<void, std::string> {
    if (count > maximum_bgm_column_count)
        return std::unexpected("BGM column count cannot exceed 128.");
    if (count <= chart.bgm_source_lanes.size()) return {};
    auto next_id = id_ceiling_t<object_id_t>::from(chart).maximum;
    std::size_t order{};
    for (const auto& lane : chart.bgm_source_lanes) order = std::max(order, lane.source_order + 1);
    const auto missing = count - chart.bgm_source_lanes.size();
    if (next_id > std::numeric_limits<std::uint64_t>::max() - missing) {
        return std::unexpected("BGM source lane ID space is exhausted");
    }
    while (chart.bgm_source_lanes.size() < count) {
        chart.bgm_source_lanes.push_back(
            {.id = object_id_t{++next_id},
             .name = "BGM " + std::to_string(chart.bgm_source_lanes.size() + 1),
             .source_order = order++});
    }
    return {};
}

namespace {
auto shrink_columns(chart_document_t& chart, std::size_t count, document_command_result_t& result)
    -> bool {
    while (chart.bgm_source_lanes.size() > count) {
        const auto& last = chart.bgm_source_lanes.back();
        if (const auto events = events_in_column(chart, last.id); events != 0) {
            reject(result, diagnostic_code_t::invalid_id,
                   "Cannot remove " + last.name + ": it holds " + std::to_string(events) +
                       (events == 1 ? " BGM event" : " BGM events"));
            return false;
        }
        chart.bgm_source_lanes.pop_back();
    }
    return true;
}

auto references_column(const bms_content_item_t& item, object_id_t column) -> bool {
    if (const auto* bgm = std::get_if<bms_bgm_content_t>(&item.value)) return bgm->column == column;
    const auto* native = std::get_if<bms_native_content_t>(&item.value);
    const auto* bgm = native ? std::get_if<bgm_event_t>(&native->payload()) : nullptr;
    return bgm && bgm->source_lane == column;
}
auto retained_column_events(const bms_program_t& program, object_id_t column) -> std::size_t {
    std::size_t count{};
    for (const auto& node : program.nodes()) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        count += static_cast<std::size_t>(std::ranges::count_if(chart->items,
            [&](const auto& item) { return references_column(item, column); }));
    }
    return count;
}
auto stage_scoped_column_settings(document_command_transaction_t& transaction,
    chart_id_t difficulty, std::size_t count) -> attempt_t<document_edit_effect_t> {
    return transaction.stage([&](song_document_t& song) -> attempt_t<document_edit_effect_t> {
        const auto* source = song.find_bms_program(difficulty);
        auto prepared = source->prepared();
        if (prepared.bgm_columns.size() == count && prepared.settings.show_empty_bgm_columns)
            return {.value = document_edit_effect_t{}};
        for (std::size_t index = count; index < prepared.bgm_columns.size(); ++index) {
            const auto& column = prepared.bgm_columns[index];
            if (retained_column_events(prepared.program, column.id))
                return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                    "Cannot remove " + column.name + ": it holds retained BGM events.")}};
        }
        prepared.bgm_columns.resize(count);
        prepared.settings.show_empty_bgm_columns = true;
        auto replacement = bms_program_document_t::from(difficulty, std::move(prepared), source->evaluation());
        if (!replacement.value || !replacement.ok()) return {.diagnostics = {error(
            diagnostic_code_t::invalid_id, "Scoped BGM column settings are invalid.")}};
        const auto found = std::ranges::find_if(song.prepared_bms_programs,
            [&](const auto& owner) { return owner.get() == source; });
        *found = std::move(*replacement.value);
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Set BGM column count"}};
    });
}
auto stage_scoped_column_count(document_command_transaction_t& transaction,
    chart_id_t difficulty, std::size_t count) -> attempt_t<document_edit_effect_t> {
    const auto current = transaction.current_document().find_bms_program(difficulty)->prepared().bgm_columns.size();
    for (auto index = current; index < count; ++index) {
        const auto added = transaction.stage_bms_bgm_column(difficulty);
        if (!added.value || !added.ok()) return {.diagnostics = added.diagnostics};
    }
    auto settings = stage_scoped_column_settings(transaction, difficulty, count);
    if (settings.value && count > current) {
        settings.value->changed = true;
        settings.value->command_label = "Set BGM column count";
    }
    return settings;
}

} // namespace

auto apply_bgm_column_count(document_history_t& history,
                            chart_id_t chart, std::size_t count) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (count < minimum_bgm_column_count || count > maximum_bgm_column_count) {
        reject(result, diagnostic_code_t::invalid_id, "BGM column count must be 1 to 128");
        return result;
    }
    auto transaction = document_command_transaction_t(history);
    if (transaction.has_bms_program(chart)) return transaction.finish_execution(
        stage_scoped_column_count(transaction, chart, count), command_noop_policy_t::accept_without_diagnostics);
    auto& proposed = transaction.document();
    const auto target = proposed.find_chart(chart);
    if (!chart || target == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "BGM column chart is missing");
        return result;
    }
    if (target->source_family != format_family_t::bms &&
        target->source_family != format_family_t::osu_mania) {
        reject(result, diagnostic_code_t::invalid_id,
               "BGM column counts are available only for BMS and osu!mania charts");
        return result;
    }
    const auto current = target->bgm_source_lanes.size();
    if (count == current) return result;
    if (count > current) {
        if (auto grown = grow_bgm_columns(*target, count); !grown) {
            reject(result, diagnostic_code_t::invalid_id, std::move(grown.error()));
            return result;
        }
    } else if (!shrink_columns(*target, count, result)) return result;
    return transaction.commit("Set BGM column count");
}

} // namespace wormedit
