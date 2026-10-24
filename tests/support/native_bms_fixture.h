#pragma once
#include "core/id_ceiling.h"
#include <catch2/catch_test_macros.hpp>

namespace wormedit::test {
inline void require_native_fixture(bool value) { REQUIRE(value); }
inline auto fixture_bindings(const song_document_t& song, chart_id_t chart) -> bms_chart_content_t {
    const auto* owner = song.find_bms_program(chart);
    const auto* catalog = owner ? owner->read_catalog() : &song;
    REQUIRE(catalog);
    bms_chart_content_t content;
    for (const auto& resource : catalog->resources) {
        for (const auto& identity : resource.target_identities) {
            if (identity.family != format_family_t::bms) continue;
            std::optional<bms_resource_namespace_t> name_space;
            if (identity.namespace_name == "WAV") name_space = bms_resource_namespace_t::wav;
            if (identity.namespace_name == "BMP") name_space = bms_resource_namespace_t::bmp;
            if (name_space) content.items.push_back({.value = bms_resource_declaration_t{
                .name_space = *name_space, .identifier = identity.identifier, .resource = resource.id}});
        }
    }
    return content;
}
inline void retain_fixture_bindings(bms_prepared_program_t& prepared, bms_chart_content_t content) {
    if (content.items.empty()) return;
    const auto first = id_ceiling_t<object_id_t>::from(prepared).next(content.items.size() + 1);
    REQUIRE(first);
    auto next = first->value + 1;
    for (auto& item : content.items) item.id = object_id_t{next++};
    const auto root = prepared.program.root();
    auto updated = prepared.program.with_node(root, prepared.program.find(root)->children.back(),
        {.id = branch_id_t{first->value}, .value = std::move(content)});
    REQUIRE(updated);
    prepared.program = std::move(*updated);
}

// Explicitly construct a new native-authored Root for an unconditional test setup.
// This is not a migration or a source-preserving edit of an imported program.
inline auto native_bms_fixture(song_document_t song, chart_document_t chart) -> song_document_t {
    auto ceiling = id_ceiling_t<object_id_t>::from(song);
    require_native_fixture(id_declarations_t<object_id_t>::visit_chart_domain(chart, ceiling));
    const auto first = ceiling.next();
    require_native_fixture(first.has_value());
    auto owner = bms_program_document_t::from_native_chart(chart, song, *first);
    require_native_fixture(owner.value.has_value());
    require_native_fixture(owner.ok());
    auto prepared = (*owner.value)->prepared();
    retain_fixture_bindings(prepared, fixture_bindings(song, chart.id));
    auto bound = bms_program_document_t::from(chart.id, prepared.with_catalog(song));
    require_native_fixture(bound.value.has_value());
    song.charts.clear();
    song.prepared_bms_programs = {*bound.value};
    song.chart_order = {chart.id};
    return song;
}
}
