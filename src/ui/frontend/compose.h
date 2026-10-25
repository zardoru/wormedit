#pragma once
#include "generator.h"
#include "bga_usage.h"
#include "bms_program.h"
#include "compose_measure_lane.h"
#include <wormedit/core/shortcut_vocabulary.h>
#include <wormedit/core/resource_vocabulary.h>
#include "metadata_audio.h"
#include "hitsounds.h"
#include "preview_audio.h"
#include <array>
#include <cstddef>
#include <algorithm>
#include <span>
#include <cmath>
#include <optional>
#include <string_view>
#include <string>

namespace wormedit { struct sv_recipe_t; }
#include <vector>
#include <tuple>

namespace wormedit::ui {
struct compose_rect_t {
    double left{};
    double top{};
    double right{};
    double bottom{};
    template<class Rect>
    [[nodiscard]] static auto from(const Rect& rect) -> compose_rect_t {
        return {.left = rect.left, .top = rect.top, .right = rect.right, .bottom = rect.bottom};
    }
    [[nodiscard]] auto is_valid() const -> bool {
        constexpr double coordinate_limit = 10000000.0;
        return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
            std::isfinite(bottom) && right >= left && bottom >= top &&
            std::abs(left) <= coordinate_limit && std::abs(top) <= coordinate_limit &&
            std::abs(right) <= coordinate_limit && std::abs(bottom) <= coordinate_limit;
    }
    auto operator==(const compose_rect_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
enum class compose_paste_ghost_kind_t { note_endpoint, hold_body, bgm_event, timing_object };
enum class compose_timing_preview_kind_t { note_endpoint, bgm_event, hold_body };

struct compose_paste_ghost_view_t {
    wormedit::ui::compose_rect_t bounds{};
    compose_paste_ghost_kind_t kind{};
    wormedit::lane_glyph_t glyph{};
    wormedit::timing_lane_kind_t timing_lane{}; // Timing ghost bounds carry only top/bottom.
    auto operator==(const compose_paste_ghost_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_band_view_t {
    wormedit::ui::compose_rect_t bounds{};
    bool split_stage{};
    auto operator==(const compose_band_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_lane_view_t {
    wormedit::ui::compose_rect_t bounds{};
    wormedit::lane_id_t lane_id{};
    uint32_t display_order{};
    wormedit::lane_role_t role{};
    wormedit::stage_group_t stage{};
    wormedit::lane_glyph_t glyph{};
    wormedit::lane_key_color_t key_color{};
    auto operator==(const compose_lane_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_grid_line_view_t {
    double y_pixels{};
    wormedit::editor_grid_line_kind_t kind{};
    wormedit::editor_grid_emphasis_t emphasis{};
    uint32_t measure_number{};
    uint64_t chart_row{};
    uint8_t snap_class{}; // Exact standard-measure subdivision; 0 = other.
    // The measure a real (not virtual) measure line starts; none for every other line.
    wormedit::measure_id_t measure_id{};
    auto operator==(const compose_grid_line_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_lane_layout_segment_view_t {
    wormedit::object_id_t segment_id{};
    uint32_t measure_number{};
    uint64_t row{};
    double y_pixels{};
    std::string_view profile_name{};
    auto operator==(const compose_lane_layout_segment_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_timing_preview_rect_view_t {
    wormedit::ui::compose_rect_t bounds{};
    compose_timing_preview_kind_t kind{};
    auto operator==(const compose_timing_preview_rect_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* A Stop, Delay or Warp as a dead span: time the audio spends where notes cannot sit. Bounds are
   viewport pixels in drawn orientation. */
namespace wormedit::ui {
struct compose_dead_span_view_t {
    wormedit::ui::compose_rect_t bounds{};
    wormedit::timing_lane_kind_t kind{wormedit::timing_lane_kind_t::stop};
    wormedit::object_id_t object_id{};
    std::string_view label{};
    auto operator==(const compose_dead_span_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* One merged Kiai area. Only top/bottom of bounds are meaningful: the area is drawn inside the
   Kiai timing lane. Head is the opening edge in chart order, tail the closing edge. */
namespace wormedit::ui {
struct compose_kiai_span_view_t {
    wormedit::ui::compose_rect_t bounds{};
    wormedit::object_id_t head_object_id{};
    wormedit::object_id_t tail_object_id{};
    uint64_t begin_row{};
    uint64_t end_row{};
    bool open_ended{};
    bool head_visible{};
    bool tail_visible{};
    bool selected{};
    auto operator==(const compose_kiai_span_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* One vertex of the SV graph. x is normalized to the visible value range in [0, 1] and drawn
   inside the SV timing lane; y is a viewport pixel row. */
namespace wormedit::ui {
struct compose_sv_graph_point_view_t {
    double x_fraction{};
    double y_pixels{};
    auto operator==(const compose_sv_graph_point_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* One stored SV recipe, shown as an area in its timing lane (SV for sv output, SPD for
   speeds). Only top/bottom of bounds are meaningful. The recipe's own relative value is drawn as a
   continuous line normalized to the visible range: graph_count vertices starting at graph_first
   in the view's recipe_graph_points. */
namespace wormedit::ui {
enum class compose_recipe_command_t : std::uint8_t {
    toggle_enabled, materialize, remove, duplicate, earlier, later
};

struct compose_recipe_entry_view_t {
    wormedit::recipe_id_t id{};
    std::string label;
    // Shares rows and an output family with the selected recipe (or is it), so their stored
    // order decides which value wins.
    bool overlaps_selection{};
    auto operator==(const compose_recipe_entry_view_t&) const -> bool = default;
};

struct compose_recipe_area_view_t {
    wormedit::ui::compose_rect_t bounds{};
    wormedit::recipe_id_t recipe_id{};
    std::string_view label{};
    wormedit::timing_lane_kind_t lane{};
    wormedit::sv_recipe_curve_t curve{};
    bool enabled{};
    bool selected{};
    bool evaluable{};
    bool begin_visible{};
    bool end_visible{};
    /* The graph shows the unapplied form candidate instead of the stored recipe. */
    bool previewing{};
    size_t graph_first{};
    size_t graph_count{};
    auto operator==(const compose_recipe_area_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* The transition extent of one StepMania SPEEDS event, drawn in the SPD lane (top/bottom of bounds
   only). transition_length is in beats, or seconds when transition_is_seconds. */
namespace wormedit::ui {
struct compose_speed_span_view_t {
    wormedit::ui::compose_rect_t bounds{};
    wormedit::object_id_t object_id{};
    double multiplier{};
    double transition_length{};
    bool transition_is_seconds{};
    bool clipped{};
    auto operator==(const compose_speed_span_view_t&) const -> bool = default;
};
} // namespace wormedit::ui





namespace wormedit::ui {
enum class timing_recipe_fixed_extent_t { point_count, step_rows };

// One exact value as typed: a decimal or a fraction (see parse_exact_rational).
struct timing_recipe_rational_text_t {
    char text[WORM_UI_TIMING_RECIPE_RATIONAL_TEXT_BYTES]{};
    auto operator==(const timing_recipe_rational_text_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct timing_recipe_lua_parameter_form_t {
    char name[129]{};
    timing_recipe_rational_text_t value{};
    auto operator==(const timing_recipe_lua_parameter_form_t&) const -> bool = default;
};
struct timing_recipe_impulse_form_t {
    timing_recipe_rational_text_t at{};
    timing_recipe_rational_text_t area{};
    auto operator==(const timing_recipe_impulse_form_t&) const -> bool = default;
};
struct timing_recipe_postprocess_form_t {
    wormedit::sv_recipe_postprocess_kind_t kind{wormedit::sv_recipe_postprocess_kind_t::multiply};
    wormedit::sv_recipe_normalization_t mode{wormedit::sv_recipe_normalization_t::preserve_displacement};
    wormedit::sv_recipe_bump_solve_t solve{wormedit::sv_recipe_bump_solve_t::y};
    timing_recipe_rational_text_t value{};
    auto operator==(const timing_recipe_postprocess_form_t&) const -> bool = default;
};
struct timing_recipe_form_t {
    char lua_source_reference[4097]{};
    char lua_entry_point[129]{"curve"};
    char lua_seed[32]{"0"};
    wormedit::sv_recipe_lua_mode_t lua_mode{wormedit::sv_recipe_lua_mode_t::pure};
    std::vector<timing_recipe_lua_parameter_form_t> lua_parameters;
    std::vector<std::uint32_t> lua_cycles;
    std::vector<timing_recipe_impulse_form_t> impulses;
    std::vector<timing_recipe_postprocess_form_t> postprocess;
    timing_recipe_rational_text_t finite_box_width_milliseconds{};
    bool lua_reload{};
    wormedit::sv_recipe_output_t output{};
    wormedit::sv_recipe_curve_t curve{};
    wormedit::sv_recipe_sampling_mode_t sampling_mode{};
    wormedit::ui::timing_recipe_fixed_extent_t fixed_extent{};
    wormedit::ui::timing_recipe_rational_text_t value_from{};
    wormedit::ui::timing_recipe_rational_text_t value_to{};
    wormedit::ui::timing_recipe_rational_text_t shape{};
    wormedit::ui::timing_recipe_rational_text_t bump_start{};
    wormedit::ui::timing_recipe_rational_text_t bump_length{};
    wormedit::ui::timing_recipe_rational_text_t bump_x{};
    wormedit::ui::timing_recipe_rational_text_t bump_y{};
    wormedit::ui::timing_recipe_rational_text_t tolerance{};
    wormedit::ui::timing_recipe_rational_text_t transition_duration{};
    bool transition_is_seconds{};
    char fixed_point_count[WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES]{};
    char fixed_step_rows[WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES]{};
    char adaptive_max_points[WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES]{};
    auto operator==(const timing_recipe_form_t&) const -> bool = default;
    [[nodiscard]] static auto from(const wormedit::sv_recipe_t&) -> timing_recipe_form_t;
    void refill(const wormedit::sv_recipe_t&);
};
} // namespace wormedit::ui

/* The selected SV recipe's editable form, as stored in the document. name and expression_source
   are valid for the frame; expression_source is the frozen module source of a Lua recipe.
   status_text carries the last preview or apply diagnostic. */
namespace wormedit::ui {
struct compose_recipe_sample_view_t {
    std::uint64_t absolute_row{};
    double value{};
    auto operator==(const compose_recipe_sample_view_t&) const -> bool = default;
};
struct compose_recipe_impulse_box_view_t {
    std::uint64_t entry_row{}, exit_row{};
    double raw_area{}, area{}, realized_area{}, area_error{}, width_milliseconds{};
    auto operator==(const compose_recipe_impulse_box_view_t&) const -> bool = default;
};
// A whole-note grid the recipe can sample on, as rows between points ("1/16" is 1/16 of a
// standard measure).
struct compose_recipe_spacing_view_t {
    std::uint64_t denominator{};
    std::uint64_t rows{};
    auto operator==(const compose_recipe_spacing_view_t&) const -> bool = default;
};
// A vertex of the selected recipe's whole-span shape: `position` and `value` both in [0, 1],
// the value normalized to the span's own range (a flat curve sits at 0.5).
struct compose_recipe_shape_point_view_t {
    double position{};
    double value{};
    auto operator==(const compose_recipe_shape_point_view_t&) const -> bool = default;
};
struct compose_recipe_form_view_t {
    std::vector<compose_recipe_sample_view_t> sampled_points;
    std::vector<compose_recipe_spacing_view_t> spacing_options;
    std::vector<compose_recipe_shape_point_view_t> shape_points;
    double shape_minimum{};
    double shape_maximum{};
    // The shape and lane show the edited candidate rather than the stored recipe.
    bool previewing{};
    std::vector<compose_recipe_impulse_box_view_t> impulse_boxes;
    std::size_t composed_point_count{};
    bool evaluation_busy{};
    std::vector<std::string_view> postprocess_labels;
    bool visible{};
    bool enabled{};
    wormedit::recipe_id_t recipe_id{};
    std::string_view name{};
    std::string_view expression_source{};
    std::string_view status_text{};
    wormedit::ui::timing_recipe_form_t form{};
    auto operator==(const compose_recipe_form_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_difference_view_t {
    wormedit::ui::compose_rect_t bounds{};
    auto operator==(const compose_difference_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_hold_view_t {
    wormedit::ui::compose_rect_t bounds{};
    wormedit::object_id_t note_id{};
    wormedit::lane_id_t lane_id{};
    uint32_t display_lane_number{}; // 1-based chart layout order; 0 when unavailable.
    wormedit::lane_key_color_t key_color{};
    uint8_t snap_class{}; // subdivision of the owning note head.
    bool selected{};
    bool read_only{};
    bool proposal_origin{};
    bool proposal_ghost{};
    bool semantic_color_visible{};
    uint8_t semantic_red{};
    uint8_t semantic_green{};
    uint8_t semantic_blue{};
    uint8_t semantic_alpha{};
    wormedit::note_move_conflict_t move_conflict{};
    bool move_conflict_occupant{};
    auto operator==(const compose_hold_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_note_view_t {
    wormedit::ui::compose_rect_t bounds{};
    wormedit::object_id_t note_id{};
    wormedit::lane_id_t lane_id{};
    uint32_t display_lane_number{}; // 1-based chart layout order; 0 when unavailable.
    wormedit::lane_key_color_t key_color{};
    bool sound_is_stem{};
    bool sound_missing{};
    wormedit::lane_glyph_t glyph{};
    std::string_view sound_label{};
    std::string_view sound_detail{};
    wormedit::resource_id_t sound_resource_id{};
    const wormedit::ui::hitsound_note_view_t* hitsound{};
    uint8_t snap_class{}; // 4, 8, 12, 16, 24, 32, 48, 64; 0 = other.
    wormedit::note_kind_t kind{};
    bool tail{};
    bool selected{};
    bool read_only{};
    bool proposal_origin{};
    bool proposal_ghost{};
    bool semantic_color_visible{};
    uint8_t semantic_red{};
    uint8_t semantic_green{};
    uint8_t semantic_blue{};
    uint8_t semantic_alpha{};
    bool paint_preview{};
    bool paint_erase_preview{};
    bool paint_invalid_preview{};
    wormedit::note_move_conflict_t move_conflict{};
    bool move_conflict_occupant{};
    auto operator==(const compose_note_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_cursor_view_t {
    wormedit::ui::compose_rect_t bounds{};
    wormedit::lane_id_t lane_id{};
    auto operator==(const compose_cursor_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_bgm_lane_view_t {
    wormedit::ui::compose_rect_t bounds;
    wormedit::object_id_t source_lane_id{};
    size_t source_order{};
    bool stem{};
    bool muted{};
    bool solo{};
    bool mix_silent{};
    double width_pixels{};
    double resize_edge_pixels{};
    std::string_view filename{};
    wormedit::object_id_t bmson_channel_id{};
    bool bmson_channel_continues{};
    auto operator==(const compose_bgm_lane_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_bgm_event_view_t {
    wormedit::ui::compose_rect_t bounds;
    wormedit::object_id_t event_id{};
    wormedit::object_id_t source_lane_id{};
    wormedit::resource_id_t sound_id{};
    bool has_sound{};
    bool bmson_slice{};
    bool bmson_restart{};
    bool selected{};
    bool has_tail{};
    bool proposal_ghost{};
    bool move_conflict{};
    bool armable_stem{}; // Valid stem resource, independent of Sounds filtering.
    std::string_view sound_label{};
    std::string_view sound_detail{};
    bool semantic_color_visible{};
    uint8_t semantic_red{};
    uint8_t semantic_green{};
    uint8_t semantic_blue{};
    uint8_t semantic_alpha{};
    float head_top_pixels{};
    float head_bottom_pixels{};
    auto operator==(const compose_bgm_event_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_bgm_draft_view_t {
    wormedit::ui::compose_rect_t bounds;
    bool restart{};
    bool playable{};
    auto operator==(const compose_bgm_draft_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_waveform_row_view_t {
    double y_pixels{};
    std::array<float, 2> absolute_peak{};
    std::array<float, 2> rms{};
    bool sample_point{};
    std::array<float, 2> samples{};
    auto operator==(const compose_waveform_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_waveform_overview_row_view_t {
    double audio_fraction{};
    float absolute_peak{};
    auto operator==(const compose_waveform_overview_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_bgm_bar_mark_view_t {
    double audio_fraction{};
    uint32_t measure{};
    auto operator==(const compose_bgm_bar_mark_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

// A beat or audio bookmark placed on the BGM bar; the label is owned by the bookmark view.
namespace wormedit::ui {
struct compose_bgm_bar_bookmark_view_t {
    double audio_fraction{};
    std::string_view label{};
    bool audio{};
    auto operator==(const compose_bgm_bar_bookmark_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_bgm_move_preview_view_t {
    wormedit::ui::compose_rect_t bounds;
    bool valid{};
    auto operator==(const compose_bgm_move_preview_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

// A visible palette slot. The application retains stable IDs, names, and
// colors; the frontend must return the ID rather than a transient index.
namespace wormedit::ui {
struct compose_semantic_layer_view_t {
    wormedit::layer_id_t layer_id{};
    std::string name;
    uint8_t red{};
    uint8_t green{};
    uint8_t blue{};
    uint8_t alpha{};
    size_t assigned_note_count{};
    bool visible{};
    bool locked{};
    bool selected{};
    size_t assigned_bgm_count{};
    bool assist_tick_enabled{};
    auto operator==(const compose_semantic_layer_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_bookmark_view_t {
    wormedit::object_id_t bookmark_id{};
    std::string label;
    uint64_t measure_number{};
    uint64_t row{};
    uint64_t chart_row{};
    bool projection_visible{};
    double projected_y_pixels{};
    bool proposed_projection_visible{};
    double proposed_y_pixels{};
    auto operator==(const compose_bookmark_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_section_view_t {
    wormedit::object_id_t section_id{};
    std::string label;
    uint64_t begin_measure_number{};
    uint64_t begin_row{};
    uint64_t end_measure_number{};
    uint64_t end_row{};
    uint64_t begin_chart_row{};
    uint64_t end_chart_row{};
    bool begin_projection_visible{};
    bool end_projection_visible{};
    bool span_projection_visible{};
    double projected_begin_y_pixels{};
    double projected_end_y_pixels{};
    double projected_span_top_y_pixels{};
    double projected_span_bottom_y_pixels{};
    bool proposed_begin_projection_visible{};
    bool proposed_end_projection_visible{};
    bool proposed_span_projection_visible{};
    double proposed_begin_y_pixels{};
    double proposed_end_y_pixels{};
    double proposed_span_top_y_pixels{};
    double proposed_span_bottom_y_pixels{};
    auto operator==(const compose_section_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_audio_bookmark_view_t {
    wormedit::object_id_t bookmark_id{};
    std::string label;
    int64_t audio_milliseconds{};
    bool projection_visible{};
    double projected_y_pixels{};
    auto operator==(const compose_audio_bookmark_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* The authored song preview (open question 26) as a band. Without an authored length the end
   is the audition default, which is never stored. Rows are chart rows for Export preview audio. */
namespace wormedit::ui {
struct compose_preview_span_view_t {
    bool available{};
    bool has_length{};
    bool chart_override{};
    int64_t start_milliseconds{};
    int64_t end_milliseconds{};
    uint64_t begin_row{};
    uint64_t end_row{};
    bool begin_projection_visible{};
    bool end_projection_visible{};
    bool span_projection_visible{};
    double projected_begin_y_pixels{};
    double projected_end_y_pixels{};
    double projected_span_top_y_pixels{};
    double projected_span_bottom_y_pixels{};
    auto operator==(const compose_preview_span_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_audio_section_view_t {
    wormedit::object_id_t section_id{};
    std::string label;
    int64_t begin_audio_milliseconds{};
    int64_t end_audio_milliseconds{};
    bool begin_projection_visible{};
    bool end_projection_visible{};
    bool span_projection_visible{};
    double projected_begin_y_pixels{};
    double projected_end_y_pixels{};
    double projected_span_top_y_pixels{};
    double projected_span_bottom_y_pixels{};
    auto operator==(const compose_audio_section_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiComposeSelectionMode {
    WORM_UI_COMPOSE_SELECTION_REPLACE = 0,
    WORM_UI_COMPOSE_SELECTION_ADD,
    WORM_UI_COMPOSE_SELECTION_TOGGLE,
} WormUiComposeSelectionMode;

namespace wormedit::ui {
struct compose_lane_key_binding_t {
    uint32_t scancode{};
    wormedit::lane_id_t lane_id{};
    auto operator==(const compose_lane_key_binding_t&) const -> bool = default;
};
} // namespace wormedit::ui

// Shortcut keys cross the frontend boundary as USB HID Keyboard/Keypad
// usages. Modifier bits match shortcut_modifier_t's actual modifiers.
namespace wormedit::ui {
struct shortcut_event_t {
    wormedit::shortcut_key_t hid_usage{};
    uint8_t modifiers{};
    bool repeat{};
    uint64_t timestamp_ns{};
    bool tap_timing_space{};
    auto operator==(const shortcut_event_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiShortcutModifier {
    WORM_UI_SHORTCUT_MODIFIER_SHIFT = 1 << 0,
    WORM_UI_SHORTCUT_MODIFIER_CONTROL = 1 << 1,
    WORM_UI_SHORTCUT_MODIFIER_ALT = 1 << 2,
    WORM_UI_SHORTCUT_MODIFIER_META = 1 << 3,
} WormUiShortcutModifier;

namespace wormedit::ui {
enum class compose_waveform_piece_kind_t { stem = 0, packed_event = 2 };
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_stem_waveform_view_t {
    /* Stable channel identity, independent of visible ordering and lane position. */
    uint64_t stem_key{};
    wormedit::object_id_t source_lane_id{};
    std::span<const compose_waveform_row_view_t> rows{};
    /* The audio is still decoding or missing: draw a placeholder band instead of a waveform. */
    bool pending{};
    compose_waveform_piece_kind_t piece_kind{};
    /* Packed pending bands use event-bounded viewport coordinates. */
    bool bounded{};
    double begin_y_pixels{};
    double end_y_pixels{};
    auto operator==(const compose_stem_waveform_view_t& other) const -> bool {
        return std::tie(stem_key, source_lane_id, pending, piece_kind, bounded,
                        begin_y_pixels, end_y_pixels) ==
                   std::tie(other.stem_key, other.source_lane_id, other.pending,
                            other.piece_kind, other.bounded, other.begin_y_pixels,
                            other.end_y_pixels) && std::ranges::equal(rows, other.rows);
    }
};
} // namespace wormedit::ui

/* Where a stem's audio is cut: the row of one slice point, drawn in the stem's BGM lane even when
   a note in the field stands for the point. Continue points are green and restarts orange; they
   light up with the point's tile (the note when object_is_note, else the BGM marker). */
namespace wormedit::ui {
struct compose_stem_cut_view_t {
    double y_pixels{};
    wormedit::object_id_t bgm_lane_id{};
    bool restart{};
    bool object_is_note{};
    wormedit::object_id_t object_id{};
    auto operator==(const compose_stem_cut_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_branch_row_view_t {
    std::string display_text;
    size_t depth{};
    bool active{};
    bool selected{};
    bool display_text_truncated{};
    bool has_source_order{};
    size_t source_order{};
    /* The control node this row shows. Raw statements are the lines the parser did not understand;
       choice blocks are #RANDOM/#SWITCH, which carry a compose_branch_choice_view_t by branch_id. */
    wormedit::branch_id_t node_id{};
    bool raw_statement{};
    bool choice_block{};
    /* Nonzero only for raw statements; identity and full text are borrowed from the retained tree. */
    wormedit::branch_id_t statement_id{};
    std::string statement_text;
    auto operator==(const compose_branch_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiComposeBranchKind {
    WORM_UI_COMPOSE_BRANCH_RANDOM = 0,
    WORM_UI_COMPOSE_BRANCH_SWITCH,
} WormUiComposeBranchKind;

namespace wormedit::ui {
struct compose_branch_choice_view_t {
    wormedit::branch_id_t branch_id{};
    WormUiComposeBranchKind kind{};
    bool has_source_order{};
    size_t source_order{};
    bool active{};
    bool editable{};
    bool has_selected_value{};
    int64_t selected_value{};
    bool has_previous_value{};
    int64_t previous_value{};
    bool has_next_value{};
    int64_t next_value{};
    bool can_clear{};
    auto operator==(const compose_branch_choice_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_timing_marker_view_t {
    wormedit::object_id_t object_id{};
    uint64_t source_order{};
    uint64_t storage_order{};
    double marker_y_pixels{};
    double marker_left_fraction{};
    double marker_right_fraction{};
    uint64_t chart_row{};
    wormedit::timing_lane_kind_t lane{};
    std::string_view short_label{};
    std::string_view detail{};
    bool fixed_initial{};
    bool selected{};
    auto operator==(const compose_timing_marker_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiComposeResourceRole {
    WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE = 1 << 0,
    WORM_UI_COMPOSE_RESOURCE_ROLE_BGM = 1 << 1,
    WORM_UI_COMPOSE_RESOURCE_ROLE_BGA = 1 << 2,
    /* Import-only: sets or replaces the song's main audio. */
    WORM_UI_COMPOSE_RESOURCE_ROLE_MUSIC = 1 << 3,
} WormUiComposeResourceRole;

namespace wormedit::ui {
struct compose_resource_view_t {
    wormedit::resource_id_t resource_id{};
    wormedit::asset_id_t asset_id{};
    uint8_t roles{};
    bool audio_asset{};
    bool selected_audio_asset{};
    bool image_asset{};
    bool selected_visual_asset{};
    bool missing{};
    std::string target_index;
    std::string name;
    std::string source_name;
    std::string source_path;
    uint64_t use_count{};
    auto operator==(const compose_resource_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* One row of the Compose keysound listbox: a placeable sample of the catalog. */
namespace wormedit::ui {
struct compose_bga_row_view_t {
    wormedit::resource_id_t resource_id{};
    std::string slot;
    std::string name;
    uint64_t use_count{};
    bool missing{};
    bool video{};
    std::string source_path;
    uint64_t image_content_id{};
    uint32_t image_width{}, image_height{};
    std::span<const uint8_t> rgba_pixels;
    auto operator==(const compose_bga_row_view_t& other) const -> bool {
        return std::tie(resource_id, slot, name, use_count, missing, video, source_path, image_content_id, image_width, image_height) ==
               std::tie(other.resource_id, other.slot, other.name, other.use_count, other.missing, other.video, other.source_path, other.image_content_id, other.image_width, other.image_height) &&
               std::ranges::equal(rgba_pixels, other.rgba_pixels);
    }
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct bga_dock_request_t {
    song_id_t selected_project;
    resource_id_t selected_resource;
    bool find_uses_open{};
    bool visible{};
    bool ids_available{};
    std::string_view search;
    size_t first_row{}, row_limit{};
    bool sort_by_id{};
    wormedit::resource_use_filter_t use_filter{};
    auto operator==(const bga_dock_request_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct sound_picker_row_view_t {
    wormedit::resource_id_t resource_id{};
    std::string label;
    bool missing{};
    auto operator==(const sound_picker_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui
namespace wormedit::ui {
inline constexpr std::size_t sound_picker_query_byte_limit = 95;
struct sound_picker_view_t {
    std::string query;
    std::vector<sound_picker_row_view_t> rows;
    size_t total_count{};
    bool ready{};
    auto operator==(const sound_picker_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct keysound_dock_request_t {
    bool sound_picker_open{};
    std::string_view sound_picker_query;
    bool sort_by_id{};
    bool slots_mode{};
    uint16_t selected_slot{};
    bool selected_slot_explicit{};
    std::string_view search;
    size_t first_row{}, row_limit{};
    std::string_view automatic_search;
    bool group_filter_active{};
    std::vector<wormedit::sound_group_id_t> group_filter_ids;
    bool include_ungrouped{};
    wormedit::keysound_layer_filter_kind_t layer_filter_kind{};
    wormedit::layer_id_t layer_filter_id{};
    std::vector<wormedit::sound_group_id_t> collapsed_group_ids;
    bool ungrouped_collapsed{};
    std::vector<wormedit::resource_id_t> selected_resource_ids;
    bool include_selected_metadata{};
    size_t automatic_first_row{}, automatic_row_limit{};
    wormedit::resource_id_t primary_resource_id{};
    size_t filter_layer_first_row{}, filter_layer_row_limit{};
    wormedit::resource_use_filter_t use_filter{};
    auto operator==(const keysound_dock_request_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct bookmark_chooser_request_t {
    size_t first_row{}, row_limit{};
    bool open{};
    char query[256]{};
    auto operator==(const bookmark_chooser_request_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct bookmark_chooser_row_view_t {
    wormedit::object_id_t bookmark_id{};
    uint8_t anchor{};
    std::string position_label;
    std::string name;
    std::string display_label;
    bool navigation_available{};
    auto operator==(const bookmark_chooser_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct bookmark_chooser_view_t {
    wormedit::song_id_t project_id{};
    wormedit::chart_id_t chart_id{};
    uint64_t document_revision{};
    std::vector<wormedit::ui::bookmark_chooser_row_view_t> rows;
    size_t total_count{}, first_row{};
    std::string query;
    auto operator==(const bookmark_chooser_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

enum {
    WORM_UI_KEYSOUND_LAYER_FILTER_ANY = 0,
    WORM_UI_KEYSOUND_LAYER_FILTER_NONE = 1,
    WORM_UI_KEYSOUND_LAYER_FILTER_SPECIFIC = 2,
};

namespace wormedit::ui {
struct compose_sound_group_view_t {
    wormedit::sound_group_id_t group_id{};
    std::string name;
    auto operator==(const compose_sound_group_view_t&) const -> bool = default;
};
} // namespace wormedit::ui


/* A missing sound's BMS audio fallback outcome (docs/references/bms-audio-fallback.md). */
namespace wormedit::ui {
struct keysound_fallback_view_t {
    wormedit::resource_id_t resource_id{};
    wormedit::audio_fallback_outcome_t outcome{};
    std::vector<std::string> candidates;
    auto operator==(const keysound_fallback_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_keysound_row_view_t {
    wormedit::resource_id_t resource_id{};
    /* BMS #WAV index such as "01", or an empty string. */
    std::string slot;
    std::string name;
    bool missing{};
    /* Bit i is set when the sound's default layers include keysound_layers[i]. */
    uint64_t layer_mask{};
    /* Bit i means adding keysound_layers[i] conflicts with existing note layers. */
    uint64_t note_layer_conflict_mask{};
    wormedit::sound_group_id_t sound_group_id{};
    wormedit::object_id_t stem_source_lane_id{};
    std::string stem_lane_name;
    uint64_t use_count{};
    std::string source_path;
    [[nodiscard]] auto status_label(bool armed, bool typed_armed) const -> std::string_view {
        if (missing) return "missing";
        if (typed_armed) return stem_source_lane_id ? "armed stem" : "armed note";
        if (armed) return "armed";
        return use_count ? "ok" : "unused";
    }
    auto operator==(const compose_keysound_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiComposeKeysoundDisplayRowKind {
    WORM_UI_COMPOSE_KEYSOUND_GROUP_HEADER = 0,
    WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW = 1,
} WormUiComposeKeysoundDisplayRowKind;

namespace wormedit::ui {
struct compose_keysound_display_row_view_t {
    WormUiComposeKeysoundDisplayRowKind kind;
    wormedit::sound_group_id_t group_id{};
    std::string group_name;
    size_t group_sound_count{};
    bool expanded{};
    wormedit::resource_id_t resource_id{};
    auto operator==(const compose_keysound_display_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_keysound_advance_row_view_t {
    wormedit::resource_id_t resource_id{};
    bool missing{};
    auto operator==(const compose_keysound_advance_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_keysound_selection_view_t {
    wormedit::resource_id_t resource_id{};
    wormedit::sound_group_id_t group_id{};
    uint64_t layer_mask{};
    uint64_t note_layer_conflict_mask{};
    std::string slot;
    bool missing{};
    auto operator==(const compose_keysound_selection_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_bga_event_view_t {
    wormedit::object_id_t event_id{};
    wormedit::resource_id_t resource_id{};
    uint64_t canvas_row{};
    size_t overlap_count{};
    wormedit::visual_event_kind_t kind{};
    std::string_view name;
    std::string_view bmp_id;
    bool missing{};
    bool selected{};
    wormedit::ui::compose_rect_t bounds;
    auto operator==(const compose_bga_event_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_sound_slot_row_view_t {
    uint16_t number{};
    std::string slot;
    wormedit::resource_id_t resource_id{};
    std::string name;
    uint64_t use_count{};
    bool missing{}, placeable{};
    std::string source_path;
    auto operator==(const compose_sound_slot_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_keysound_layer_view_t {
    wormedit::layer_id_t layer_id{};
    std::string name;
    uint8_t red{};
    uint8_t green{};
    uint8_t blue{};
    uint8_t alpha{};
    bool locked{};
    auto operator==(const compose_keysound_layer_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_output_group_view_t {
    size_t document_index{};
    std::string name;
    std::string relative_output_path;
    std::string relative_asset_directory;
    WormUiExistingFilesChoice existing_files{};
    auto operator==(const compose_output_group_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_measure_row_view_t {
    uint64_t measure_index{};
    /* The number "go to measure" takes: #000-based for BMS charts, 1-based otherwise. */
    uint64_t number{};
    uint64_t length_numerator, length_denominator;
    bool has_meter{};
    uint32_t meter_numerator, meter_denominator;
    wormedit::measure_id_t measure_id{};
    auto operator==(const compose_measure_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_quantize_preset_view_t {
    std::string_view name{};
    uint64_t divisions{};
    wormedit::quantization_space_t space{};
    wormedit::quantization_tie_t tie{};
    wormedit::quantization_hold_t holds{};
    auto operator==(const compose_quantize_preset_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_quantize_position_view_t {
    wormedit::measure_id_t measure_id{};
    uint64_t row{};
    auto operator==(const compose_quantize_position_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_quantize_report_row_view_t {
    wormedit::object_id_t object_id{};
    wormedit::lane_id_t lane_id{};
    uint64_t start_before_absolute_row{};
    uint64_t start_after_absolute_row{};
    wormedit::ui::compose_quantize_position_view_t start_before{};
    wormedit::ui::compose_quantize_position_view_t start_after{};
    bool has_end_before{};
    bool has_end_after{};
    uint64_t end_before_absolute_row{};
    uint64_t end_after_absolute_row{};
    wormedit::ui::compose_quantize_position_view_t end_before{};
    wormedit::ui::compose_quantize_position_view_t end_after{};
    bool moved{};
    bool collision{};
    wormedit::object_id_t collision_peer_id{};
    bool has_collision_peer{};
    bool selected{};
    bool head_halfway{};
    bool tail_halfway{};
    bool stationary_tail_annotation{};
    bool bgm{};
    auto operator==(const compose_quantize_report_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct compose_quantize_ghost_view_t {
    wormedit::object_id_t object_id{};
    wormedit::lane_id_t lane_id{};
    wormedit::lane_glyph_t before_head_glyph{};
    wormedit::lane_glyph_t after_head_glyph{};
    wormedit::lane_glyph_t before_tail_glyph{};
    wormedit::lane_glyph_t after_tail_glyph{};
    wormedit::ui::compose_rect_t before_head{};
    wormedit::ui::compose_rect_t after_head{};
    wormedit::ui::compose_rect_t before_tail{};
    wormedit::ui::compose_rect_t after_tail{};
    double before_head_anchor_x{}, before_head_anchor_y{};
    double after_head_anchor_x{}, after_head_anchor_y{};
    double before_tail_anchor_x{}, before_tail_anchor_y{};
    double after_tail_anchor_x{}, after_tail_anchor_y{};
    bool has_tail{};
    bool moved_head{};
    bool moved_tail{};
    bool head_halfway{};
    bool tail_halfway{};
    bool stationary_tail_annotation{};
    bool collision{};
    auto operator==(const compose_quantize_ghost_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

// Renderer-neutral frame view. Sys refills retained storage in place; the frontend
// reads presentation values and stable identities. Coordinates are viewport pixels.
namespace wormedit::ui {
struct compose_view_t {
    wormedit::ui::kind_catalog_view_t<wormedit::timing_lane_kind_t> timing_lanes;
    wormedit::ui::kind_catalog_view_t<wormedit::timing_lane_kind_t> timing_lane_captions;
    wormedit::ui::kind_catalog_view_t<wormedit::note_kind_t> note_palette;
    // A modeless program window owns this frame's content input.
    bool input_occluded{};
    wormedit::note_kind_t armed_note_kind{};
    uint32_t allowed_note_kind_mask{};
    bool note_kind_palette_visible, lane_key_legend_visible;
    wormedit::ui::hitsound_palette_view_t hitsounds{};
    // Editor preference: whether clicks, placements, and selections play their sound.
    bool audition_on_click{};
    uint32_t area_band_alpha{};
    bool visible{};
    bool scene_available{};
    bool editor_gesture_active{};
    bool editor_gesture_current{};
    // Application acknowledgement of an Alt/Option or armed Hold/Roll pointer begin.
    bool editor_pointer_hold_active{};
    // Full chart index presence, independent of viewport and visibility filters.
    bool has_bgm_events{};
    // BMS charts only: the BGM source column count and whether the toolbar may change it.
    uint32_t bgm_column_count{};
    bool bgm_column_count_editable{};
    // Lane profiles the layout at the cursor can switch to (0 = none); the current one is marked.
    std::ptrdiff_t lane_profile_current{};
    std::vector<std::string> lane_profile_names;
    // Layout profiles can change the lane count; this editor is separate from
    // the same-count profile switch above.
    std::ptrdiff_t lane_layout_profile_current{};
    uint32_t lane_layout_edit_measure_number{};
    wormedit::object_id_t lane_layout_edit_segment_id{};
    bool lane_layout_edit_is_update{};
    bool lane_layout_edit_available{};
    // Removing the cursor measure: `measure_removal_available` is false at a virtual cursor.
    // Nonempty measures confirm first, and `measure_removal_message` explains a blocked removal.
    bool measure_removal_available{};
    bool measure_removal_has_contents{};
    bool measure_removal_blocked{};
    wormedit::measure_id_t measure_removal_measure_id{};
    std::string measure_removal_message;
    size_t measure_removal_deleted_notes, measure_removal_deleted_bgm_events;
    size_t measure_removal_deleted_bga_events, measure_removal_shortened_holds;
    size_t measure_removal_moved_positions{};
    std::vector<std::string> lane_layout_profile_names;
    std::vector<std::string> lane_layout_profile_ids;
    std::vector<wormedit::ui::compose_lane_layout_segment_view_t> lane_layout_segments;
    bool downscroll{};
    bool bms_measure_labels{};
    // The active chart came from BMSON, so the Sounds dock offers its BMSON tab.
    bool bmson_tools_available{};
    uint64_t compose_measure_count{};
    uint32_t selected_bms_measure_number_plus_one{};
    bool truncated{};
    uint8_t draw_source_kind; /* 0 none, 1 sound, 2 BGA */
    wormedit::resource_id_t draw_source_id{};
    std::string draw_source_label; /* frame-owned, bounded UTF-8; includes resource identity */
    bool draw_source_missing{};
    bool draw_source_is_stem{};
    wormedit::object_id_t draw_source_stem_lane_id{};
    std::string draw_source_stem_lane_name;
    size_t semantic_layer_target_row{};
    bool note_paint_mode_active{};
    bool semantic_layer_paint_mode_active{};
    bool resource_paint_mode_active{};
    bool resource_paint_available{};
    bool resource_paint_has_sample{};
    bool semantic_layer_gesture_active{};
    size_t selected_note_count{};
    wormedit::object_id_t keyboard_context_note_id; // First selected note, including offscreen multi-selection.
    bool same_sound_selection_available{};
    bool move_to_bgm_available{};
    std::string move_to_bgm_shortcut;
    // Grids the chart can snap to (the Quantize chips outside it are quantize-only).
    std::vector<uint64_t> supported_snap_denominators;
    // Quantization request currently selected in the inspector.
    uint64_t quantize_divisions{};
    wormedit::quantization_space_t quantize_space{};
    wormedit::quantization_tie_t quantize_tie{};
    wormedit::quantization_hold_t quantize_holds{};
    bool quantize_selected_preset_valid{};
    size_t quantize_selected_preset_index{};
    std::vector<wormedit::ui::compose_quantize_preset_view_t> quantize_presets;
    /* Measures dock rows in chart order, bounded; `measure_row_ids` lists their IDs for selection. */
    std::vector<wormedit::ui::compose_measure_row_view_t> measure_rows;
    std::vector<uint64_t> measure_row_ids;
    // Dry-run counts and bounded report for the current request.
    size_t quantize_preview_moved, quantize_preview_unchanged, quantize_preview_blocked;
    size_t quantize_preview_colliding_objects{};
    size_t quantize_preview_halfway_notes, quantize_preview_holds;
    bool quantize_q_bound{};
    bool quantize_preview_enabled{};
    std::vector<wormedit::ui::compose_quantize_report_row_view_t> quantize_preview_rows;
    size_t quantize_preview_total_rows{};
    bool quantize_preview_rows_truncated{};
    uint64_t quantize_source_resolution{};
    uint64_t quantize_display_resolution{};
    uint64_t quantize_resolution_scale{};
    bool quantize_geometry_valid{};
    bool quantize_preview_drawable{};
    bool quantize_ghosts_drawable{};
    std::vector<wormedit::ui::compose_quantize_ghost_view_t> quantize_ghosts;
    std::string quantize_preview_message;
    // Report from the most recently applied request, retained across frames.
    std::vector<wormedit::ui::compose_quantize_report_row_view_t> quantize_applied_rows;
    size_t quantize_applied_total_rows{};
    bool quantize_applied_rows_truncated{};
    size_t selected_object_count{};
    wormedit::resource_id_t selected_assigned_resource_id{};
    wormedit::object_id_t selected_bgm_event_id{};
    wormedit::object_id_t selected_bmson_bgm_event_id{};
    wormedit::object_id_t selected_bmson_note_event_id{};
    wormedit::object_id_t selected_bmson_source_lane_id{};
    std::vector<uint64_t> staged_bmson_marker_playable_lanes;
    std::vector<uint8_t> staged_bmson_marker_restarts;
    bool beat_marker_navigation_available{};
    bool audio_marker_navigation_available{};
    bool audio_marker_transport_available{};
    std::vector<wormedit::ui::compose_bookmark_view_t> beat_bookmarks;
    std::vector<wormedit::ui::compose_section_view_t> beat_sections;
    bool area_range_available{};
    bool area_range_virtual{};
    bool area_delete_supported{};
    bool transport_loop_selection_available{};
    bool transport_loop_selection_enabled{};
    uint64_t area_range_begin_row{};
    uint64_t area_range_end_row{};
    wormedit::ui::preview_audio_range_view_t preview_audio_range{};
    uint64_t area_canvas_rows{};
    uint64_t area_rows_per_measure{};
    uint64_t area_snap_denominator{};
    wormedit::quantization_space_t area_snap_space{};
    // False while pointer edits land on the finest chart row instead of the active grid.
    bool edit_snap_enabled{};
    /* Y-lock: dragging notes changes lanes only, never rows. */
    bool y_lock{};
    wormedit::ui::compose_measure_lane_view_t measure_lane{};
    double area_scroll_pixels{};
    bool beat_section_selection_span_available{};
    uint64_t beat_section_selection_begin_measure_number{};
    uint64_t beat_section_selection_begin_row{};
    uint64_t beat_section_selection_end_measure_number{};
    uint64_t beat_section_selection_end_row{};
    std::vector<wormedit::ui::compose_audio_bookmark_view_t> audio_bookmarks;
    std::vector<wormedit::ui::compose_audio_section_view_t> audio_sections;
    wormedit::ui::compose_preview_span_view_t preview_span{};
    bool semantic_layer_assist_available{};
    std::vector<wormedit::ui::compose_semantic_layer_view_t> semantic_layers;
    size_t semantic_layer_total_count{};
    size_t semantic_layer_first_row{};
    std::vector<wormedit::ui::compose_semantic_layer_view_t> note_menu_layers;
    size_t note_menu_layer_first_row{};
    size_t note_menu_layer_total_count{};
    wormedit::ui::compose_semantic_layer_view_t semantic_layer_target{};
    bool semantic_layer_target_none{};
    size_t semantic_layer_unassigned_note_count{};
    size_t semantic_layer_page{};
    size_t semantic_layer_page_count{};
    bool semantic_layer_target_locked{};
    bool semantic_layer_target_visible{};
    bool semantic_layer_target_required_filter{};
    bool semantic_layer_target_excluded_filter{};
    bool semantic_layer_solo_active{};
    wormedit::song_id_t project_id{};
    wormedit::chart_id_t chart_id{};
    uint64_t document_revision{};
    // Exact semantic filter changes invalidate retained static field pixels.
    uint64_t static_scene_style_generation{};
    // Immutable expanded scalar scene; unavailable on truncated/transient windows.
    const wormedit::ui::compose_view_t* static_scene;
    uint64_t static_scene_source_generation{};
    double static_scene_world_origin_pixels{};
    double static_scene_covered_begin_pixels{};
    double static_scene_covered_end_pixels{};
    size_t static_scene_accounted_bytes{};
    double viewport_width_pixels{};
    double viewport_height_pixels{};
    double bgm_horizontal_scroll_pixels{};
    double bgm_maximum_horizontal_scroll_pixels{};
    double playable_field_left_pixels{};
    double playable_field_right_pixels{};
    double timing_document_begin_fraction{};
    double timing_document_end_fraction{};
    double pixels_per_standard_measure{};
    bool playhead_visible{};
    double playhead_y_pixels{};
    std::string_view status_text{};
    std::string selected_note_label;
    wormedit::object_id_t selected_note_label_note_id{};
    bool focus_note_label_editor{};
    bool bga_lane_visible{};
    std::vector<wormedit::ui::compose_bga_event_view_t> bga_events;
    wormedit::ui::compose_rect_t bga_lane_bounds{};
    uint8_t bga_revealed_lane_mask{};
    size_t bga_lane_count{};
    wormedit::ui::compose_rect_t bga_lane_rects[4]{};
    uint8_t bga_lane_kinds[4]{};
    bool bga_move_preview_valid{};
    wormedit::object_id_t bga_move_preview_event_id{};
    double bga_move_preview_y_pixels{};
    bga_usage_view_t bga_usage;
    std::vector<wormedit::ui::compose_bga_row_view_t> bga_rows;
    size_t bga_total_matching, bga_first_row;
    size_t bga_ids_used, bga_ids_total, bga_largest_free_run;
    bool bga_ids_available{};
    std::vector<uint64_t> bga_matching_ids;
    std::vector<uint64_t> bga_all_resource_ids;
    std::vector<uint64_t> bga_advance_ids;
    std::string bga_search;
    bool paint_ghost_visible{};
    wormedit::ui::compose_note_view_t paint_ghost{};
    bool hover_note_visible{};
    wormedit::object_id_t hover_note_id{};
    std::string hover_tooltip;
    int64_t hover_timing_source_frame{};
    uint64_t hover_timing_total_frames{};
    uint32_t hover_timing_sample_rate{};
    bool hover_timing_valid{};
    std::string hover_timing_label;
    bool viewport_autoscroll_active{};
    double viewport_autoscroll_anchor_x_pixels{};
    double viewport_autoscroll_anchor_y_pixels{};
    double viewport_autoscroll_pointer_x_pixels{};
    double viewport_autoscroll_pointer_y_pixels{};
    bool clipboard_copy_available{};
    bool clipboard_paste_available{};
    bool paste_active{};
    bool paste_valid{};
    std::vector<wormedit::ui::compose_paste_ghost_view_t> paste_ghosts;
    std::string_view paste_diagnostic{};
    std::vector<wormedit::ui::compose_band_view_t> bands;
    std::vector<wormedit::ui::compose_lane_view_t> lanes;
    std::vector<wormedit::ui::compose_grid_line_view_t> grid_lines;
    std::vector<wormedit::ui::compose_grid_line_view_t> proposed_grid_lines;
    std::vector<wormedit::ui::compose_timing_preview_rect_view_t> timing_preview_rects;
    std::vector<wormedit::ui::compose_timing_marker_view_t> timing_preview_markers;
    std::vector<wormedit::ui::compose_timing_marker_view_t> timing_markers;
    std::vector<wormedit::ui::compose_dead_span_view_t> dead_spans;
    std::vector<wormedit::ui::compose_kiai_span_view_t> kiai_spans;
    std::vector<wormedit::ui::compose_speed_span_view_t> speed_spans;
    wormedit::ui::compose_recipe_form_view_t recipe_form{};
    wormedit::ui::compose_generator_view_t generator{};
    std::vector<wormedit::ui::compose_paste_ghost_view_t> generator_ghosts;
    std::vector<wormedit::ui::compose_recipe_area_view_t> recipe_areas;
    std::vector<wormedit::ui::compose_recipe_entry_view_t> recipe_entries;
    std::vector<wormedit::ui::compose_sv_graph_point_view_t> recipe_graph_points;
    std::vector<wormedit::ui::compose_sv_graph_point_view_t> sv_graph_points;
    double sv_graph_minimum{};
    double sv_graph_maximum{};
    double sv_graph_one_fraction; /* negative when 1.0x is outside the visible range */
    /* Interpolated speed curve; same format as the SV graph, drawn in the speed lane. */
    std::vector<wormedit::ui::compose_sv_graph_point_view_t> speed_graph_points;
    double speed_graph_one_fraction{};
    uint8_t timing_lane_visibility_mask{};
    // Compose mode specialization; see ui/compose_mode.h. The mask above is
    // already the effective one for this mode.
    bool timing_focus{};
    bool timing_stage_active{};
    bool timing_stage_valid{};
    wormedit::timing_lane_kind_t timing_stage_lane{};
    double timing_stage_y_pixels{};
    std::vector<wormedit::ui::compose_resource_view_t> resources;
    wormedit::asset_id_t selected_audio_asset_id{};
    wormedit::asset_id_t selected_visual_asset_id{};
    size_t resource_total_count{};
    /* Keysound listbox: every placeable sample, plus the chart layers its masks index. */
    std::vector<wormedit::ui::compose_keysound_row_view_t> keysounds;
    std::vector<wormedit::ui::compose_keysound_row_view_t> keysound_selected_rows;
    std::vector<wormedit::ui::keysound_fallback_view_t> keysound_fallbacks;
    std::vector<wormedit::ui::compose_keysound_selection_view_t> keysound_selection_rows;
    std::vector<wormedit::ui::compose_keysound_display_row_view_t> keysound_display_rows;
    /* True when section/filter projection was used, including legitimate empty windows. */
    bool keysound_section_projection_available{};
    size_t keysound_display_total_count{};
    size_t keysound_display_first_row{};
    bool keysound_display_rows_truncated{};
    bool keysound_primary_display_index_valid{};
    size_t keysound_primary_display_index{};
    wormedit::resource_id_t keysound_projected_primary_resource_id{};
    bool keysound_projected_filters_unrestricted{};
    std::vector<uint64_t> keysound_listed_resource_ids;
    std::vector<wormedit::ui::compose_keysound_advance_row_view_t> keysound_advance_rows;
    std::vector<uint64_t> keysound_all_resource_ids;
    std::vector<wormedit::ui::compose_sound_group_view_t> compose_sound_groups;
    size_t keysound_total_count{};
    bool keysounds_truncated{};
    std::vector<wormedit::ui::compose_keysound_layer_view_t> keysound_layers;
    std::vector<wormedit::ui::compose_keysound_layer_view_t> keysound_filter_layers;
    size_t keysound_filter_layer_first{};
    size_t keysound_filter_layer_total{};
    std::string keysound_filter_selected_layer_name;
    bool keysound_filter_selected_layer_valid{};
    bool keysounds_available{};
    size_t keysound_ids_used, keysound_ids_total, keysound_largest_free_run;
    bool keysound_ids_available{};
    std::vector<wormedit::ui::compose_sound_slot_row_view_t> sound_slots;
    size_t sound_slot_first_row, sound_slot_total_matching;
    bool sound_slots_mode{};
    uint16_t sound_slot_selected_number{};
    size_t sound_slot_selected_row{};
    std::vector<wormedit::ui::compose_output_group_view_t> output_groups;
    bool export_confirmation_required{};
    wormedit::song_id_t export_confirmation_project_id{};
    uint64_t export_confirmation_revision{};
    std::string_view export_confirmation_message{};
    std::string_view export_confirmation_detail{};
    std::vector<wormedit::ui::compose_difference_view_t> differences;
    std::vector<wormedit::ui::compose_hold_view_t> hold_bodies;
    std::vector<wormedit::ui::compose_note_view_t> note_endpoints;
    std::vector<wormedit::ui::compose_bgm_lane_view_t> bgm_lanes;
    bool bgm_mix_available{};
    std::vector<wormedit::ui::compose_bgm_event_view_t> bgm_events;
    std::vector<wormedit::ui::compose_waveform_row_view_t> waveform_rows;
    // Cached canvas passes need presence without copying waveform rows.
    bool has_waveform_geometry{};
    /* BMSON stems other than the selected one, each drawn in its BGM lane. */
    std::vector<wormedit::ui::compose_stem_waveform_view_t> stem_waveforms;
    std::vector<wormedit::ui::compose_stem_cut_view_t> stem_cuts;
    std::vector<wormedit::ui::compose_waveform_row_view_t> channel_waveform_rows;
    wormedit::object_id_t channel_waveform_source_lane_id{};
    std::vector<wormedit::ui::compose_waveform_overview_row_view_t> waveform_overview_rows;
    uint64_t waveform_overview_generation{};
    double waveform_overview_visible_begin_fraction{};
    double waveform_overview_visible_end_fraction{};
    bool waveform_beat_zero_visible{};
    double waveform_beat_zero_fraction{};
    uint64_t waveform_overview_total_frames{};
    uint32_t waveform_overview_sample_rate{};
    // The BGM bar shows [begin, end] of the song at full row resolution, with measure starts.
    std::vector<wormedit::ui::compose_waveform_overview_row_view_t> bgm_bar_rows;
    double bgm_bar_begin_fraction{};
    double bgm_bar_end_fraction{};
    std::vector<wormedit::ui::compose_bgm_bar_mark_view_t> bgm_bar_marks;
    std::vector<wormedit::ui::compose_bgm_bar_bookmark_view_t> bgm_bar_bookmarks;
    // The bar row exists only for songs with audio and while the toolbar toggle keeps it on:
    // available = the song has audio, reserved = the row is laid out (and takes vertical space).
    bool bgm_bar_available{};
    bool bgm_bar_reserved{};
    bool source_playhead_available{};
    wormedit::asset_id_t source_playhead_asset_id{};
    uint64_t source_playhead_frame{};
    uint32_t source_playhead_sample_rate{};
    wormedit::chart_id_t source_playhead_chart_id{};
    uint64_t source_playhead_document_revision{};
    bool audio_offset_editable{};
    bool audio_offset_locked{};
    int64_t waveform_audio_offset_numerator{};
    uint64_t waveform_audio_offset_denominator{};
    bool waveform_offset_drag_active{};
    bool beat_fit_drag_active{};
    bool offset_fit_drag_active{};
    double offset_fit_target_y_pixels{};
    uint64_t beat_fit_chart_row{};
    double beat_fit_anchor_y_pixels{};
    double beat_fit_beat_y_pixels{};
    uint64_t beat_fit_target_sample_frame{};
    double beat_fit_target_y_pixels{};
    int64_t beat_fit_original_bpm_numerator{};
    uint64_t beat_fit_original_bpm_denominator{};
    int64_t beat_fit_proposed_bpm_numerator{};
    uint64_t beat_fit_proposed_bpm_denominator{};
    bool beat_fit_onset_snap_active{};
    bool beat_fit_snap_active{};
    int64_t beat_fit_snap_step_numerator{};
    uint64_t beat_fit_snap_step_denominator{};
    bool waveform_proposed_beat_zero_visible{};
    double waveform_proposed_beat_zero_fraction{};
    int64_t waveform_proposed_audio_offset_numerator{};
    uint64_t waveform_proposed_audio_offset_denominator{};
    bool waveform_timing_locked{};
    bool waveform_analysis_pending{};
    std::vector<wormedit::ui::compose_bgm_draft_view_t> bmson_marker_drafts;
    std::optional<wormedit::ui::compose_bgm_move_preview_view_t> bmson_marker_move_preview;
    std::optional<wormedit::ui::compose_cursor_view_t> cursor;
    bool area_selection_preview_visible{};
    wormedit::ui::compose_rect_t area_selection_preview_bounds{};
    WormUiComposeSelectionMode area_selection_preview_mode{};
    bool space_range_preview_visible{};
    wormedit::ui::compose_rect_t space_range_preview_bounds{};
    WormUiComposeSelectionMode space_range_preview_mode{};
    double space_range_anchor_y_pixels{};
    double space_range_tail_y_pixels{};
    std::vector<wormedit::ui::compose_lane_key_binding_t> lane_key_bindings;
    bool lane_keys_unbound{};
    bms_program_view_t bms_program;
    bool branch_outline_available{};
    bool branch_rows_truncated{};
    bool branch_choices_truncated{};
    bool branch_display_text_truncated{};
    bool branch_diagnostics_truncated{};
    std::vector<wormedit::ui::compose_branch_row_view_t> branch_rows;
    std::vector<wormedit::ui::compose_branch_choice_view_t> branch_choices;
    std::vector<std::string> branch_diagnostics;
    std::string branch_statement_feedback;
    [[nodiscard]] auto branch_diagnostic_count() const -> size_t {
        return branch_diagnostics.size() + !branch_statement_feedback.empty();
    }
    [[nodiscard]] auto branch_diagnostic_at(size_t index) const -> std::string_view {
        return index < branch_diagnostics.size() ? std::string_view{branch_diagnostics[index]}
                                                : std::string_view{branch_statement_feedback};
    }
    size_t selected_bgm_event_count{};
    size_t semantic_layer_unassigned_bgm_count{};
    wormedit::ui::bookmark_chooser_view_t bookmark_chooser{};
    wormedit::ui::sound_picker_view_t sound_picker{};
    size_t move_conflict_count{};
    size_t move_to_bgm_count{}; // Unique moved-note count, before geometry truncation.
    void reset();
};
} // namespace wormedit::ui
