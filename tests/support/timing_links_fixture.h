#pragma once
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/timing_links.h>
#include <catch2/catch_test_macros.hpp>
#include <utility>
namespace wormedit::test {
struct timing_links_fixture_t {
    document_history_t history;
    chart_id_t source_id;
    chart_id_t target_id;
    static auto from(lane_input_profile_t profile = make_bms_7k_profile()) -> timing_links_fixture_t {
        auto history = document_history_t::from(song_document_t{.id = song_id_t{7001}, .revision = 1,
            .metadata = {.title = "Linked timing", .artist = "Fixture"}});
        const auto source = apply_new_chart_command(history, std::move(profile));
        REQUIRE(source.payload.chart);
        const auto target = apply_duplicate_chart_command(history, *source.payload.chart);
        REQUIRE(target.payload.chart);
        const auto linked = apply_timing_link_edit(history, chart_stamp_t::from(history.document, *target.payload.chart),
            {.category = timing_category_t::bpm, .source = *source.payload.chart, .discard_confirmed = true});
        REQUIRE(linked.applied);
        return {.history = document_history_t::from(std::move(history.document)),
            .source_id = *source.payload.chart, .target_id = *target.payload.chart};
    }
    [[nodiscard]] auto source() const -> const chart_document_t& { return *history.document.find_chart(source_id); }
    [[nodiscard]] auto target() const -> const chart_document_t& { return *history.document.find_chart(target_id); }
    auto set_bpm(chart_id_t chart, signed_rational_t value) -> document_command_result_t {
        const auto& event = history.document.find_chart(chart)->bpms.front();
        return apply_bpm_edit(history, update_edit_t<bpm_edit_value_t>{.chart = chart, .id = event.id,
            .value = bpm_edit_value_t::from(event.position, timing_bpm_value_t{.beats_per_minute = value})});
    }
    auto relink() -> document_command_result_t {
        return apply_timing_link_edit(history, chart_stamp_t::from(history.document, target_id),
            {.category = timing_category_t::bpm, .operation = timing_link_operation_t::relink, .discard_confirmed = true});
    }
};
}
