#pragma once
#include "compose.h"
#include "generator.h"
#include "project.h"
#include "request_stamp.h"
#include <wormedit/core/osu_hitsample.h>
#include <wormedit/core/vocabulary.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

/* Payloads of Compose requests. `compose_request_t::kind` selects the request; each struct holds the fields
   one family of requests reads. Written by the Compose panels, read by sys/compose. */
namespace wormedit::ui {
struct sound_group_assignment_t {
    sound_group_id_t group_id{};
    std::string name;
    std::vector<resource_id_t> sound_ids;
    auto operator==(const sound_group_assignment_t&) const -> bool = default;
};
/* Area range and area command inputs (selection span in chart rows). */
struct compose_area_request_t {
    uint64_t begin_row;
    uint64_t end_row;
    bool is_virtual;
    bool begin_edge;
    bool clear_requested;
    std::uint64_t bpm_multiplier_numerator{};
    std::uint64_t bpm_multiplier_denominator{};
    bool bpm_warp_resize_span{true};
};
/* Quantize preset and snap inputs. */
struct compose_quantize_request_t {
    size_t preset_index;
    bool preset_selected;
    uint64_t snap_denominator;
    uint64_t divisions;
    quantization_space_t space;
    quantization_tie_t tie;
    quantization_hold_t holds;
    std::string preset_name;
};
/* Measure targets: numbers, length and meter text. */
struct compose_measure_request_t {
    uint64_t number;
    // Chart positions, including virtual tail measures; these are not stable measure IDs.
    std::vector<uint64_t> ids;
    uint32_t second_meter_numerator{};
    uint32_t second_meter_denominator{};
    std::string length_numerator_text;
    std::string length_denominator_text;
    // SET_MEASURE_METER: the time signature to set, as typed.
    std::string meter_numerator_text;
    std::string meter_denominator_text;
};
/* Chart-level settings changed from Compose: lane profile, BGM columns, audio offset. */
struct compose_chart_request_t {
    uint32_t bgm_column_count{};
    uint32_t lane_profile_index;
    std::string lane_layout_profile_id;
    int64_t audio_offset_numerator;
    uint64_t audio_offset_denominator;
};
/* Drag of the timing region's edge handle: the width asked for (0 = none) or a reset to automatic. */
struct compose_timing_region_request_t {
    double width_pixels;
    bool width_reset;
};
/* Semantic layer being created, renamed or recolored. */
struct compose_layer_request_t {
    wormedit::layer_id_t id;
    size_t first_row;
    size_t row_count{};
    std::string name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
};
/* Bookmarks, sections, export cuts, note labels and marker text. */
struct compose_marker_request_t {
    bool note_label_focus_consumed;
    std::string note_label;
    wormedit::object_id_t bookmark_id;
    // Rename converts the bookmark to this anchor when set; empty keeps its anchor.
    std::optional<wormedit::marker_anchor_t> bookmark_anchor;
    wormedit::bookmark_reorder_t bookmark_reorder;
    wormedit::object_id_t section_id;
    wormedit::object_id_t export_cut_id;
    std::string label;
    std::string audio_begin;
    std::string audio_end;
    wormedit::object_id_t note_context_id;
};
/* BMSON BGM events and stem channels edited from the BMSON tab. */
struct compose_bmson_request_t {
    wormedit::object_id_t bgm_event_id;
    wormedit::object_id_t channel_id;
};
/* Resource, sound slot, import and renumber inputs. */
struct compose_resource_request_t {
    wormedit::object_id_t sound_hover_object_id;
    wormedit::resource_id_t id;
    uint32_t fallback_candidate;
    wormedit::asset_id_t asset_id;
    std::vector<wormedit::resource_id_t> sound_resource_ids;
    std::vector<wormedit::ui::sound_group_assignment_t> sound_group_assignments;
    bool sound_group_keep_ids_together;
    bool sound_overwrite_note_layers;
    /* Renumber WAV or BMP identifiers for the selected resources, or all in the namespace. */
    bool renumber_all;
    bool renumber_bga;
    std::vector<std::string> import_paths;
    std::optional<wormedit::hitsound_change_t> hitsound;
    uint16_t sound_slot;
    bool sound_slot_confirmed;
    size_t output_group_index;
    WormUiExistingFilesChoice existing_files;
    std::optional<resource_role_t> role;
    std::string import_path;

    void set_sound_resources(std::span<const std::uint64_t> ids) {
        sound_resource_ids.clear();
        for (const auto id : ids) sound_resource_ids.push_back(resource_id_t{id});
    }
};
/* Timing recipe and generator forms. */
struct compose_recipe_request_t {
    timing_lane_kind_t timing_lane;
    wormedit::ui::timing_recipe_form_t form;
    wormedit::ui::generator_form_t generator_form;
    wormedit::recipe_id_t recipe_id;
};
} // namespace wormedit::ui
