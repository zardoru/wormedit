#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bms_branch_projection.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace wormedit;

namespace {

constexpr std::string_view source = "#PLAYER 1\n"
                                    "#TITLE Branch command test\n"
                                    "#BPM 120\n"
                                    "#WAV01 active.wav\n"
                                    "#WAV02 inactive.wav\n"
                                    "#WAV03 replacement.wav\n"
                                    "#RANDOM 2\n"
                                    "#IF 1\n"
                                    "#00011:0100\n"
                                    "#ELSE\n"
                                    "#00011:0200\n"
                                    "#ENDIF\n"
                                    "#ENDRANDOM\n";

auto source_line_order(std::string_view text, std::string_view line) -> std::size_t {
    const auto offset = text.find(line);
    if (offset == std::string_view::npos) {
        return std::string_view::npos;
    }
    return static_cast<std::size_t>(std::ranges::count(text.substr(0, offset), '\n'));
}

auto make_history() -> document_history_t {
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.branch_choices = {{
        .control_source_order = source_line_order(source, "#RANDOM 2"),
        .selected_value = 1,
    }};
    const auto parsed =
        parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}), std::move(options));
    if (!parsed.song || parsed.has_errors()) {
        throw std::runtime_error("BMS branch command test fixture did not parse");
    }
    return make_document_history(*parsed.song);
}

auto find_node(const bms_control_node_t& root, bms_control_kind_t kind,
               std::string_view original_text) -> const bms_control_node_t* {
    if (root.kind == kind && root.original_text == original_text) {
        return &root;
    }
    for (const auto& child : root.children) {
        if (const auto* match = find_node(child, kind, original_text)) {
            return match;
        }
    }
    return nullptr;
}

auto find_node_by_id(const bms_control_node_t& root, branch_id_t id) -> const bms_control_node_t* {
    if (root.id == id) {
        return &root;
    }
    for (const auto& child : root.children) {
        if (const auto* match = find_node_by_id(child, id)) {
            return match;
        }
    }
    return nullptr;
}

auto chart_tree(const document_history_t& history) -> const bms_control_node_t& {
    return *history.document.charts.front().bms_control_tree;
}

auto replace_request(const document_history_t& history, std::string replacement)
    -> bms_branch_statement_edit_t {
    const auto* target =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200");
    if (target == nullptr) {
        throw std::runtime_error("inactive raw statement not found in test fixture");
    }
    return {
        .chart = history.document.charts.front().id,
        .statement = target->id,
        .expected_source_order = target->source_order,
        .expected_source_span = target->source_span,
        .expected_original_text = target->original_text,
        .replacement_text = std::move(replacement),
    };
}

auto insert_request(const document_history_t& history) -> bms_branch_statement_insert_t {
    const auto* leaf = find_node(chart_tree(history), bms_control_kind_t::else_branch, "#ELSE");
    const auto* anchor =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200");
    if (leaf == nullptr || anchor == nullptr) {
        throw std::runtime_error("inactive branch or anchor not found in test fixture");
    }
    return {
        .chart = history.document.charts.front().id,
        .leaf_branch = leaf->id,
        .expected_leaf_source_order = leaf->source_order,
        .expected_leaf_source_span = leaf->source_span,
        .expected_leaf_original_text = leaf->original_text,
        .after_statement = anchor->id,
        .expected_statement_source_order = anchor->source_order,
        .expected_statement_source_span = anchor->source_span,
        .expected_statement_original_text = anchor->original_text,
        .statement_text = "#00012:01",
    };
}

auto delete_request(const document_history_t& history) -> bms_branch_statement_delete_t {
    const auto* leaf = find_node(chart_tree(history), bms_control_kind_t::else_branch, "#ELSE");
    const auto* target =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200");
    if (leaf == nullptr || target == nullptr) {
        throw std::runtime_error("inactive branch or statement not found in test fixture");
    }
    return {
        .chart = history.document.charts.front().id,
        .leaf_branch = leaf->id,
        .expected_leaf_source_order = leaf->source_order,
        .expected_leaf_source_span = leaf->source_span,
        .expected_leaf_original_text = leaf->original_text,
        .statement = target->id,
        .expected_source_order = target->source_order,
        .expected_source_span = target->source_span,
        .expected_original_text = target->original_text,
    };
}

} // namespace

TEST_CASE("inactive BMS statement replacement is one revision-checked undoable command",
          "[unit][core][bms][branch][command]") {
    auto history = make_history();
    const auto statement =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200");
    REQUIRE(statement != nullptr);
    const auto statement_id = statement->id;
    const auto request = replace_request(history, "#00011:0300");

    const auto applied =
        apply_bms_branch_statement_edit_command(history, history.document.revision, request);
    REQUIRE(applied);
    CHECK(applied.format_diagnostics.empty());
    CHECK(applied.command.diagnostics.empty());
    CHECK(history.document.revision == 2);
    CHECK(history.undo.size() == 1);
    CHECK(is_document_dirty(history));
    CHECK(find_node_by_id(chart_tree(history), statement_id)->original_text == "#00011:0300");

    REQUIRE(undo_document_command(history));
    CHECK(history.document.revision == 1);
    CHECK_FALSE(is_document_dirty(history));
    CHECK(find_node_by_id(chart_tree(history), statement_id)->original_text == "#00011:0200");

    REQUIRE(redo_document_command(history));
    CHECK(history.document.revision == 2);
    CHECK(is_document_dirty(history));
    CHECK(find_node_by_id(chart_tree(history), statement_id)->original_text == "#00011:0300");
}

TEST_CASE("stale BMS branch actions reject without changing history or losing typed diagnostics",
          "[unit][core][bms][branch][command][negative]") {
    auto history = make_history();
    const auto original_revision = history.document.revision;
    auto request = replace_request(history, "#00011:0300");

    const auto stale_revision =
        apply_bms_branch_statement_edit_command(history, original_revision + 1, request);
    CHECK_FALSE(stale_revision);
    CHECK(stale_revision.format_diagnostics.empty());
    REQUIRE(stale_revision.command.diagnostics.size() == 1);
    CHECK(stale_revision.command.diagnostics.front().severity == diagnostic_severity_t::error);
    CHECK(history.document.revision == original_revision);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
    CHECK(find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200") !=
          nullptr);

    ++request.expected_source_span.byte_begin;
    const auto stale_source =
        apply_bms_branch_statement_edit_command(history, original_revision, request);
    CHECK_FALSE(stale_source);
    CHECK(stale_source.command.diagnostics.empty());
    REQUIRE(stale_source.format_diagnostics.size() == 1);
    CHECK(stale_source.format_diagnostics.front().code ==
          bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch);
    CHECK(history.document.revision == original_revision);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
}

TEST_CASE("inactive BMS statement insertion enters history and replays retained source state",
          "[unit][core][bms][branch][command]") {
    auto history = make_history();
    const auto request = insert_request(history);
    const auto inserted =
        apply_bms_branch_statement_insert_command(history, history.document.revision, request);
    REQUIRE(inserted);
    CHECK(inserted.format_diagnostics.empty());
    CHECK(inserted.command.diagnostics.empty());
    CHECK(find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00012:01") !=
          nullptr);
    REQUIRE(undo_document_command(history));
    CHECK(find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00012:01") ==
          nullptr);
    REQUIRE(redo_document_command(history));
    CHECK(find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00012:01") !=
          nullptr);
}

TEST_CASE("inactive BMS statement deletion enters history and replays retained source state",
          "[unit][core][bms][branch][command]") {
    auto history = make_history();
    const auto request = delete_request(history);
    const auto deleted =
        apply_bms_branch_statement_delete_command(history, history.document.revision, request);
    REQUIRE(deleted);
    CHECK(deleted.format_diagnostics.empty());
    CHECK(deleted.command.diagnostics.empty());
    CHECK(find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200") ==
          nullptr);
    REQUIRE(undo_document_command(history));
    CHECK(find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200") !=
          nullptr);
    REQUIRE(redo_document_command(history));
    CHECK(find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200") ==
          nullptr);
}

TEST_CASE("BMS branch projection previews exact entering and leaving retained statements",
          "[unit][core][bms][branch][projection]") {
    const auto history = make_history();
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.bms_control_tree.has_value());
    REQUIRE(chart.active_branch_selection.size() == 1);
    const auto* active =
        find_node(*chart.bms_control_tree, bms_control_kind_t::raw_statement, "#00011:0100");
    const auto* inactive =
        find_node(*chart.bms_control_tree, bms_control_kind_t::raw_statement, "#00011:0200");
    REQUIRE(active != nullptr);
    REQUIRE(inactive != nullptr);

    auto proposed = chart.active_branch_selection;
    proposed.front().selected_value = 2;
    const auto preview = preview_bms_branch_projection(*chart.bms_control_tree,
                                                       chart.active_branch_selection, proposed);
    REQUIRE(preview);
    CHECK(preview.current.active_statement_ids == std::vector{active->id});
    CHECK(preview.current.active_statement_source_orders == std::vector{active->source_order});
    CHECK(preview.proposed.active_statement_ids == std::vector{inactive->id});
    CHECK(preview.proposed.active_statement_source_orders == std::vector{inactive->source_order});
    CHECK(preview.entering_statement_ids == std::vector{inactive->id});
    CHECK(preview.leaving_statement_ids == std::vector{active->id});
    CHECK(chart.active_branch_selection.front().selected_value == 1);
}

TEST_CASE("BMS branch projection rejects invalid stale and unbounded proposals without deltas",
          "[unit][core][bms][branch][projection][negative]") {
    const auto history = make_history();
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.bms_control_tree.has_value());
    REQUIRE(chart.active_branch_selection.size() == 1);

    auto out_of_range = chart.active_branch_selection;
    out_of_range.front().selected_value = 3;
    const auto invalid = project_bms_branch_tree(*chart.bms_control_tree, out_of_range);
    CHECK_FALSE(invalid);
    CHECK(invalid.status == bms_branch_projection_status_t::invalid_selection);
    CHECK(invalid.active_statement_ids.empty());

    auto duplicate = chart.active_branch_selection;
    duplicate.push_back(duplicate.front());
    const auto duplicated = project_bms_branch_tree(*chart.bms_control_tree, duplicate);
    CHECK_FALSE(duplicated);
    CHECK(duplicated.status == bms_branch_projection_status_t::duplicate_selection);

    auto unknown = chart.active_branch_selection;
    unknown.front().branch = branch_id_t{999'999};
    const auto stale = project_bms_branch_tree(*chart.bms_control_tree, unknown);
    CHECK_FALSE(stale);
    CHECK(stale.status == bms_branch_projection_status_t::invalid_selection);

    const auto capped = project_bms_branch_tree(
        *chart.bms_control_tree, chart.active_branch_selection,
        {.maximum_nodes = 1, .maximum_depth = 256, .maximum_active_statements = 1});
    CHECK_FALSE(capped);
    CHECK(capped.status == bms_branch_projection_status_t::capacity_exceeded);

    const auto failed_preview = preview_bms_branch_projection(
        *chart.bms_control_tree, chart.active_branch_selection, out_of_range);
    CHECK_FALSE(failed_preview);
    CHECK(failed_preview.entering_statement_ids.empty());
    CHECK(failed_preview.leaving_statement_ids.empty());
}
