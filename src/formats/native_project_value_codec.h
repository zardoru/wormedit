#pragma once

// Internal bridge to the native codec's existing value serializers. Journal deltas
// never define a parallel persisted representation of domain values.
#include <wormedit/core/document.h>
#include <wormedit/core/bms_program.h>
#include <wormedit/formats/native_project.h>
#include <nlohmann/json_fwd.hpp>
#include <array>

namespace wormedit::native_project_internal {
[[nodiscard]] auto parse_native_root(std::string_view bytes, native_project_limits_t limits)
    -> result_t<nlohmann::json, error_t<failure_code_t>>;
// The root has already passed parse_native_root; retain its exact parsed numeric types.
[[nodiscard]] auto decode_native_root(const nlohmann::json&, native_project_limits_t limits)
    -> native_project_parse_outcome_t;
[[nodiscard]] auto encode_native_root(nlohmann::json document) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const audio_asset_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const visual_asset_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const resource_entry_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const midi_asset_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const generator_recipe_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const sv_recipe_definition_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const storyboard_document_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const chart_document_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const export_cut_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const sound_group_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const measure_chunk_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const lane_layout_segment_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const semantic_layer_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const bgm_source_lane_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const bpm_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const pause_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const warp_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const volume_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const sample_set_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const scroll_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const speed_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const sv_recipe_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const kiai_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const timing_anchor_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const bookmark_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const section_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const bga_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const sound_channel_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const note_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const bgm_event_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const song_document_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_fields(const song_document_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_fields(const chart_document_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_fields(const measure_chunk_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_program(const bms_program_document_t&, const song_document_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_program_fields(const bms_program_document_t&, const song_document_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_fields(const bms_program_document_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_fields(const bms_program_node_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const bms_program_node_t&) -> nlohmann::json;
[[nodiscard]] auto encode_native_value(const bms_content_item_t&) -> nlohmann::json;
// Codec-owned location of a Chart node's content collection, shared by the differ.
inline constexpr std::array<std::string_view, 3> native_bms_content_fields{"value", "data", "items"};

[[nodiscard]] auto validate_native_document(const song_document_t&, native_project_limits_t)
    -> std::vector<native_project_diagnostic_t>;

// The codec and typed differ share the collection field names and optional policy.
template<class Visitor> void visit_native_collections(const song_document_t&, Visitor&& visit) {
    visit("audio_assets", &song_document_t::audio_assets, false);
    visit("visual_assets", &song_document_t::visual_assets, false);
    visit("resources", &song_document_t::resources, false);
    visit("midi_assets", &song_document_t::midi_assets, false);
    visit("generator_recipes", &song_document_t::generator_recipes, false);
    visit("sv_recipe_definitions", &song_document_t::sv_recipe_definitions, false);
    visit("storyboards", &song_document_t::storyboards, false);
    visit("charts", &song_document_t::charts, false);
    visit("export_cuts", &song_document_t::export_cuts, false);
    visit("sound_groups", &song_document_t::sound_groups, true);
}

template<class Visitor> void visit_native_collections(const chart_document_t&, Visitor&& visit) {
    visit("measures", &chart_document_t::measures, false);
    visit("lane_layouts", &chart_document_t::lane_layouts, false);
    visit("semantic_layers", &chart_document_t::semantic_layers, false);
    visit("bgm_source_lanes", &chart_document_t::bgm_source_lanes, false);
    visit("bpms", &chart_document_t::bpms, false);
    visit("pauses", &chart_document_t::pauses, false);
    visit("warps", &chart_document_t::warps, false);
    visit("scrolls", &chart_document_t::scrolls, false);
    visit("volumes", &chart_document_t::volumes, false);
    visit("sample_sets", &chart_document_t::sample_sets, false);
    visit("speeds", &chart_document_t::speeds, false);
    visit("sv_recipes", &chart_document_t::sv_recipes, false);
    visit("kiai", &chart_document_t::kiai, false);
    visit("timing_anchors", &chart_document_t::timing_anchors, false);
    visit("bookmarks", &chart_document_t::bookmarks, false);
    visit("sections", &chart_document_t::sections, false);
    visit("bga_events", &chart_document_t::bga_events, false);
    visit("sound_channels", &chart_document_t::sound_channels, false);
}

template<class Visitor> void visit_native_collections(const measure_chunk_t&, Visitor&& visit) {
    visit("notes", &measure_chunk_t::notes, false);
    visit("bgm_events", &measure_chunk_t::bgm_events, false);
}

} // namespace wormedit::native_project_internal
