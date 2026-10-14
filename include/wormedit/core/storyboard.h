#pragma once

#include <wormedit/core/result.h>

#include <wormedit/core/arena.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

// Frame storage for transient storyboard draw data. Allocations are stable
// until reset(); reset keeps committed chunks for reuse on the next frame. The
// implementation is the generic frame arena in <wormedit/core/arena.h>.
using storyboard_arena_error_t = arena_error_t;
using storyboard_arena_allocation_t = arena_allocation_t;
using storyboard_arena_options_t = arena_options_t;
using storyboard_frame_arena_t = frame_arena_t;

enum class storyboard_opcode_t : std::uint8_t {
    fade,
    move,
    move_x,
    move_y,
    scale,
    vector_scale,
    rotate,
    color,
};

struct storyboard_command_t {
    storyboard_opcode_t opcode{storyboard_opcode_t::fade};
    std::int32_t easing{};
    std::int64_t start_time_ms{};
    std::int64_t end_time_ms{};
    std::array<double, 4> from{};
    std::array<double, 4> to{};
    std::uint8_t value_count{};
    std::uint64_t source_order{};

    auto operator==(const storyboard_command_t&) const -> bool = default;
};

enum class storyboard_node_kind_t : std::uint8_t {
    command,
    loop,
    retained_line,
};

// Loop children keep their OSB-relative timestamps. The evaluator computes
// the active iteration arithmetically, so even very large iteration counts do
// not create a correspondingly large event list.
struct storyboard_node_t {
    storyboard_node_kind_t kind{storyboard_node_kind_t::command};
    storyboard_command_t command;
    std::int64_t loop_start_time_ms{};
    std::uint64_t loop_iteration_count{};
    std::uint64_t source_order{};
    std::string retained_line;
    std::vector<storyboard_node_t> children;

    auto operator==(const storyboard_node_t&) const -> bool = default;
};

struct storyboard_sprite_t {
    std::uint64_t stable_id{};
    std::uint64_t source_order{};
    std::string layer;
    std::string origin;
    std::string path;
    double x{};
    double y{};
    std::vector<storyboard_node_t> nodes;

    auto operator==(const storyboard_sprite_t&) const -> bool = default;
};

struct storyboard_top_level_line_t {
    std::uint64_t source_order{};
    std::string text;

    auto operator==(const storyboard_top_level_line_t&) const -> bool = default;
};

struct storyboard_document_model_t {
    std::vector<storyboard_sprite_t> sprites;
    std::vector<storyboard_top_level_line_t> retained_top_level_lines;

    auto operator==(const storyboard_document_model_t&) const -> bool = default;
};

enum class storyboard_diagnostic_code_t : std::uint8_t {
    input_too_large,
    line_limit,
    node_limit,
    nesting_limit,
    malformed_line,
    invalid_number,
    invalid_command,
    invalid_model,
    capacity_exceeded,
    time_overflow,
};

inline constexpr std::size_t storyboard_default_maximum_path_bytes = 4 * 1024;

struct storyboard_diagnostic_t {
    [[nodiscard]] constexpr auto is_error() const noexcept -> bool { return true; }
    auto operator==(const storyboard_diagnostic_t&) const -> bool = default;
    storyboard_diagnostic_code_t code{storyboard_diagnostic_code_t::malformed_line};
    std::size_t line{};
    std::string message;
};

struct storyboard_parse_options_t {
    std::size_t maximum_input_bytes{8 * 1024 * 1024};
    std::size_t maximum_lines{250'000};
    std::size_t maximum_nodes{250'000};
    std::size_t maximum_nesting{128};
    std::size_t maximum_path_bytes{storyboard_default_maximum_path_bytes};
};

using storyboard_parse_outcome_t = diagnosed_t<std::optional<storyboard_document_model_t>, std::vector<storyboard_diagnostic_t>>;
struct storyboard_parse_result_t {
    [[nodiscard]] static auto from(std::string_view text, storyboard_parse_options_t options = {}) -> storyboard_parse_outcome_t;
    [[nodiscard]] static auto from(storyboard_parse_outcome_t&& outcome) -> storyboard_parse_result_t;
    std::optional<storyboard_document_model_t> document;
    std::vector<storyboard_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return document.has_value();
    }
};

using storyboard_emit_outcome_t = diagnosed_t<std::optional<std::string>, std::vector<storyboard_diagnostic_t>>;
struct storyboard_emit_result_t {
    [[nodiscard]] static auto from(const storyboard_document_model_t& document, std::size_t maximum_output_bytes = 16 * 1024 * 1024) -> storyboard_emit_outcome_t;
    [[nodiscard]] static auto from(storyboard_emit_outcome_t&& outcome) -> storyboard_emit_result_t;
    std::optional<std::string> text;
    std::vector<storyboard_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return text.has_value();
    }
};

[[nodiscard]] auto parse_osb(std::string_view text, storyboard_parse_options_t options = {})
    -> storyboard_parse_result_t;
[[nodiscard]] auto emit_osb(const storyboard_document_model_t& document,
                            std::size_t maximum_output_bytes = 16 * 1024 * 1024)
    -> storyboard_emit_result_t;

struct storyboard_event_t {
    std::int64_t time_ms{};
    std::uint64_t source_order{};
    std::uint64_t stable_object_id{};
    std::uint64_t stable_node_id{};
    enum class edge_t : std::uint8_t { begin, end } edge{edge_t::begin};
    std::uint64_t schedule_order{};

    auto operator==(const storyboard_event_t&) const -> bool = default;
};

struct storyboard_schedule_options_t {
    std::size_t maximum_events{500'000};
    std::size_t maximum_nodes{250'000};
    std::size_t maximum_nesting{128};
};

struct storyboard_schedule_t {
    std::vector<storyboard_event_t> events;
};

using storyboard_schedule_outcome_t = diagnosed_t<std::optional<storyboard_schedule_t>, std::vector<storyboard_diagnostic_t>>;
struct storyboard_schedule_result_t {
    [[nodiscard]] static auto from(const storyboard_document_model_t& document, storyboard_schedule_options_t options = {}) -> storyboard_schedule_outcome_t;
    [[nodiscard]] static auto from(storyboard_schedule_outcome_t&& outcome) -> storyboard_schedule_result_t;
    std::optional<storyboard_schedule_t> schedule;
    std::vector<storyboard_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return schedule.has_value();
    }
};

// Compile only authored root-level boundaries. A loop is represented by its
// envelope; its repeated child boundaries remain structural and are evaluated
// from the loop tree. This keeps compilation bounded by authored node count.
[[nodiscard]] auto compile_storyboard_schedule(const storyboard_document_model_t& document,
                                               storyboard_schedule_options_t options = {})
    -> storyboard_schedule_result_t;

enum class storyboard_cursor_status_t : std::uint8_t {
    ready,
    not_started,
    invalid_schedule,
    non_monotonic_window,
};

using storyboard_cursor_outcome_t = result_t<std::span<const storyboard_event_t>, storyboard_cursor_status_t>;
struct storyboard_cursor_result_t {
    [[nodiscard]] static auto from(storyboard_cursor_outcome_t&& outcome) noexcept -> storyboard_cursor_result_t;
    storyboard_cursor_status_t status{storyboard_cursor_status_t::not_started};
    std::span<const storyboard_event_t> events;
};

// The schedule must outlive this cursor. seek() is logarithmic; advance() only
// reads forward and updates one index, with no erase, relink, or allocation.
class storyboard_event_cursor_t {
  public:
    [[nodiscard]] auto start(const storyboard_schedule_t& schedule, std::int64_t time_ms)
        -> storyboard_cursor_status_t;
    [[nodiscard]] auto seek(std::int64_t time_ms) -> storyboard_cursor_status_t;
    [[nodiscard]] auto advance_events(std::int64_t exclusive_end_time_ms) noexcept -> storyboard_cursor_outcome_t;
    [[nodiscard]] auto advance(std::int64_t exclusive_end_time_ms) noexcept
        -> storyboard_cursor_result_t;
    void stop() noexcept;

    [[nodiscard]] auto head_index() const noexcept -> std::size_t {
        return head_;
    }

  private:
    const storyboard_schedule_t* schedule_{};
    std::size_t head_{};
    std::int64_t last_end_time_ms_{};
    bool active_{};
};

struct storyboard_value_t {
    std::array<double, 4> values{};
    std::uint8_t value_count{};
    bool present{};

    auto operator==(const storyboard_value_t&) const -> bool = default;
};

struct storyboard_evaluation_effect_t {
    storyboard_value_t value;
    storyboard_diagnostic_t diagnostic;
    std::int64_t start_time_ms{};
    std::uint64_t source_order{};
    std::uint64_t visit_order{};
    auto operator==(const storyboard_evaluation_effect_t&) const -> bool = default;
};
using storyboard_evaluation_outcome_t = result_t<storyboard_evaluation_effect_t, storyboard_evaluation_effect_t>;
struct storyboard_evaluation_result_t {
    [[nodiscard]] static auto from(const storyboard_sprite_t& sprite, storyboard_opcode_t opcode,
        std::int64_t time_ms, std::size_t maximum_nesting = 128) -> storyboard_evaluation_outcome_t;
    [[nodiscard]] static auto from(storyboard_evaluation_outcome_t&& outcome) -> storyboard_evaluation_result_t;
    storyboard_value_t value;
    storyboard_diagnostic_t diagnostic;
    std::int64_t start_time_ms{};
    std::uint64_t source_order{};
    std::uint64_t visit_order{};
    bool failed{};
};

// Evaluates one property at an exact OSB millisecond. The last started command
// in source order wins, and loop iteration selection uses division/modulo.
[[nodiscard]] auto evaluate_storyboard_property(const storyboard_sprite_t& sprite,
                                                storyboard_opcode_t opcode, std::int64_t time_ms,
                                                std::size_t maximum_nesting = 128)
    -> storyboard_evaluation_result_t;

// Semantic comparison ignores source-order labels and blank-only formatting
// lines, but compares retained directive text and authored loop structure.
[[nodiscard]] auto storyboard_semantically_equal(const storyboard_document_model_t& left,
                                                 const storyboard_document_model_t& right) -> bool;

} // namespace wormedit
