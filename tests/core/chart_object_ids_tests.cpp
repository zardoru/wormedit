#include "../../src/core/chart_object_ids.h"
#include "../../src/core/chart_id_visitor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace wormedit;

namespace {
auto chart_with_declaration(int domain, object_id_t id) -> chart_document_t {
    chart_document_t chart;
    switch (domain) {
    case 0: chart.bgm_source_lanes.push_back({.id = id}); break;
    case 1: chart.lane_layouts.push_back({.id = id}); break;
    case 2: chart.bpms.push_back({.id = id}); break;
    case 3: chart.pauses.push_back({.id = id}); break;
    case 4: chart.warps.push_back({.id = id}); break;
    case 5: chart.scrolls.push_back({.id = id}); break;
    case 6: chart.speeds.push_back({.id = id}); break;
    case 7: chart.kiai.push_back({.id = id}); break;
    case 8: chart.timing_anchors.push_back({.id = id}); break;
    case 9: chart.bookmarks.push_back({.id = id}); break;
    case 10: chart.sections.push_back({.id = id}); break;
    case 11: chart.bga_events.push_back({.id = id}); break;
    case 12: chart.sound_channels.push_back({.id = id}); break;
    case 13:
        chart.measures.resize(1);
        chart.measures.front().notes.push_back({.id = id});
        break;
    case 14:
        chart.measures.resize(1);
        chart.measures.front().bgm_events.push_back({.id = id});
        break;
    case 16: chart.volumes.push_back({.id = id}); break;
    case 17: chart.sample_sets.push_back({.id = id}); break;
    case 15:
        chart.sound_channels.push_back({.id = object_id_t{1}, .events = {{.id = id}}});
        break;
    }
    return chart;
}
} // namespace

TEST_CASE("Chart object ceiling includes every declaration domain", "[chart-object-ids]") {
    const auto domain = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    const auto maximum = GENERATE(std::uint64_t{41}, std::numeric_limits<std::uint64_t>::max());
    const auto chart = chart_with_declaration(domain, object_id_t{maximum});
    const auto scan = id_ceiling_t<object_id_t>::from(chart);
    CHECK(scan.maximum == maximum);
    const auto expected_count = domain == 15 ? 2 : 1;
    const auto has_expected_count = scan.count == expected_count &&
        chart_object_id_count(chart, object_id_t{maximum}) == 1;
    CHECK(has_expected_count);
    const auto next = next_chart_object_id(chart);
    CHECK_FALSE(next.over_limit);
    const auto is_exhausted = maximum == std::numeric_limits<std::uint64_t>::max();
    CHECK(next.exhausted == is_exhausted);
    const auto expected_next = is_exhausted ? std::nullopt : std::optional{object_id_t{maximum + 1}};
    CHECK(next.id == expected_next);
}

TEST_CASE("Chart object allocation preserves empty and capacity boundaries", "[chart-object-ids]") {
    chart_document_t chart;
    const auto empty = id_ceiling_t<object_id_t>::from(chart);
    const auto is_empty = empty.count == 0 && empty.maximum == 0 &&
        next_chart_object_id(chart).id == object_id_t{1};
    CHECK(is_empty);
    CHECK(next_chart_object_id(chart, 0).over_limit);
    chart.bookmarks = {{.id = object_id_t{7}}, {.id = object_id_t{11}}};
    const auto is_at_capacity = next_chart_object_id(chart, 1).over_limit &&
        next_chart_object_id(chart, 2).over_limit;
    CHECK(is_at_capacity);
    CHECK(next_chart_object_id(chart, 3).id == object_id_t{12});
    const auto bounded = id_ceiling_t<object_id_t>::from(chart, 1);
    const auto is_bounded = bounded.count == 1 && bounded.maximum == 7;
    CHECK(is_bounded);
}

TEST_CASE("Chart object ceiling excludes references and other ID domains", "[chart-object-ids]") {
    chart_document_t chart;
    chart.id = chart_id_t{999};
    chart.measures.push_back({.id = measure_id_t{999}});
    chart.semantic_layers.push_back({.id = layer_id_t{999}});
    chart.sv_recipes.push_back({.id = recipe_id_t{999}});
    chart.sound_channels.push_back({.id = object_id_t{4}, .source_clip = {.id = clip_id_t{999}}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{7}, .stem_channel = object_id_t{999}});
    const auto scan = id_ceiling_t<object_id_t>::from(chart);
    CHECK(scan.count == 2);
    CHECK(scan.maximum == 7);
    CHECK(next_chart_object_id(chart).id == object_id_t{8});
}

namespace {
template<class Id, class Owner>
void check_empty_id_domain(const Owner& owner) {
    const auto scan = id_ceiling_t<Id>::from(owner);
    CHECK((scan.count == 0 && scan.maximum == 0 && scan.next() == Id{1}));
}

template<class Id, class Owner>
void check_declared_id_maximum(const Owner& owner, std::uint64_t maximum) {
    const auto scan = id_ceiling_t<Id>::from(owner);
    const auto expected = maximum == 41 ? std::optional{Id{42}} : std::nullopt;
    CHECK((scan.count == 1 && scan.maximum == maximum));
    CHECK(scan.next() == expected);
    CHECK(id_counter_t<Id>::from(owner, Id{maximum}, 1).count == 1);
}
} // namespace

TEST_CASE("Typed ID domains allocate one from empty owners", "[chart-object-ids][id-ceiling]") {
    const chart_document_t chart;
    const song_document_t song;
    check_empty_id_domain<object_id_t>(chart);
    check_empty_id_domain<measure_id_t>(chart);
    check_empty_id_domain<layer_id_t>(chart);
    check_empty_id_domain<recipe_id_t>(song);
    check_empty_id_domain<asset_id_t>(song);
    check_empty_id_domain<resource_id_t>(song);
    check_empty_id_domain<sound_group_id_t>(song);
    check_empty_id_domain<chart_id_t>(song);
}

TEST_CASE("Measure and layer ID domains exclude object IDs and references",
          "[chart-object-ids][id-ceiling]") {
    const auto maximum = GENERATE(std::uint64_t{41}, std::numeric_limits<std::uint64_t>::max());
    chart_document_t chart{.id = chart_id_t{999},
        .measures = {{.id = measure_id_t{maximum},
            .notes = {{.id = object_id_t{999}, .position = {measure_id_t{999}, 0},
                       .semantic_layers = {layer_id_t{999}}}}}},
        .semantic_layers = {{.id = layer_id_t{maximum}}}};
    check_declared_id_maximum<measure_id_t>(chart, maximum);
    check_declared_id_maximum<layer_id_t>(chart, maximum);
}

TEST_CASE("Recipe ID declarations include song recipes and every chart",
          "[chart-object-ids][id-ceiling]") {
    const auto domain = GENERATE(0, 1, 2);
    const auto maximum = GENERATE(std::uint64_t{41}, std::numeric_limits<std::uint64_t>::max());
    song_document_t song{.charts = {{.id = chart_id_t{999}}, {.id = chart_id_t{1000}}}};
    if (domain == 0) song.generator_recipes.push_back({.id = recipe_id_t{maximum}});
    else song.charts[static_cast<std::size_t>(domain - 1)].sv_recipes.push_back({.id = recipe_id_t{maximum}});
    check_declared_id_maximum<recipe_id_t>(song, maximum);
}

TEST_CASE("Asset ID declarations include audio visual and MIDI catalogs",
          "[chart-object-ids][id-ceiling]") {
    const auto domain = GENERATE(0, 1, 2);
    const auto maximum = GENERATE(std::uint64_t{41}, std::numeric_limits<std::uint64_t>::max());
    song_document_t song{.resources = {{.id = resource_id_t{999}, .asset = asset_id_t{999}}}};
    switch (domain) {
    case 0: song.audio_assets.push_back({.id = asset_id_t{maximum}}); break;
    case 1: song.visual_assets.push_back({.id = asset_id_t{maximum}}); break;
    case 2: song.midi_assets.push_back({.id = asset_id_t{maximum}}); break;
    }
    check_declared_id_maximum<asset_id_t>(song, maximum);
}

TEST_CASE("Catalog and chart ID declarations remain separate typed domains",
          "[chart-object-ids][id-ceiling]") {
    const auto maximum = GENERATE(std::uint64_t{41}, std::numeric_limits<std::uint64_t>::max());
    song_document_t song{.audio_assets = {{.id = asset_id_t{999}}},
        .resources = {{.id = resource_id_t{maximum}, .asset = asset_id_t{999}}},
        .sound_groups = {{.id = sound_group_id_t{maximum}}},
        .charts = {{.id = chart_id_t{maximum}, .measures = {{.id = measure_id_t{999}}}}}};
    check_declared_id_maximum<resource_id_t>(song, maximum);
    check_declared_id_maximum<sound_group_id_t>(song, maximum);
    check_declared_id_maximum<chart_id_t>(song, maximum);
}

TEST_CASE("Typed ID reservations are atomic at count and integer boundaries",
          "[chart-object-ids][id-ceiling]") {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const song_document_t song{.sound_groups = {{.id = sound_group_id_t{maximum - 2}}}};
    const auto scan = id_ceiling_t<sound_group_id_t>::from(song);
    const auto before = scan;
    CHECK(scan.next(2) == sound_group_id_t{maximum - 1});
    CHECK_FALSE(scan.next(3));
    CHECK_FALSE(scan.next(0));
    const auto bounded = id_ceiling_t<sound_group_id_t>::from(song, 2);
    CHECK((bounded.next(1) == sound_group_id_t{maximum - 1} && !bounded.next(2)));
    CHECK(scan == before);
}

TEST_CASE("Sample controls remap declarations and measure references separately", "[chart-object-ids]") {
    chart_document_t chart;
    chart.volumes.push_back({.id = object_id_t{41}, .position = {measure_id_t{999}, 3}});
    chart.sample_sets.push_back({.id = object_id_t{42}, .position = {measure_id_t{999}, 7}});
    CHECK(id_ceiling_t<object_id_t>::from(chart).maximum == 42);
    auto remap = [](auto& id) { id.value += 1000; };
    chart_id_detail::visit_chart_ids(chart, remap);
    CHECK(chart.volumes.front().id == object_id_t{1041});
    CHECK(chart.sample_sets.front().id == object_id_t{1042});
    CHECK(chart.volumes.front().position == local_position_t{measure_id_t{1999}, 3});
    CHECK(chart.sample_sets.front().position == local_position_t{measure_id_t{1999}, 7});
    CHECK(id_ceiling_t<object_id_t>::from(chart).maximum == 1042);
}
