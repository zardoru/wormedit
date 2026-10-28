// Manual Release benchmark. Pass a disposable copy of an imported BMS project.
// This executable reads assets but never saves the project or changes source files.
#include "sys/project/project_open.h"
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/snap.h>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace wormedit;

namespace {
template<class Function>
auto measure(const char* name, Function function) {
    const auto start = std::chrono::steady_clock::now();
    auto result = function();
    std::cout << name << " ms=" << std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count() << '\n';
    return result;
}

auto check_history(document_history_t& history,
    std::shared_ptr<const bms_program_document_t> before,
    std::shared_ptr<const bms_program_document_t> after) -> bool {
    // Retain both owners by value while Undo/Redo replaces document storage.
    return undo_document_command(history) && history.document.prepared_bms_programs.front() == before &&
        redo_document_command(history) && history.document.prepared_bms_programs.front() == after;
}

auto move_note(document_history_t& history, chart_id_t chart, const note_t& note) -> bool {
    const std::vector<std::uint64_t> snaps{4, 8, 16, 32, 64};
    editor_interaction_state_t state{.chart = chart, .active_lane = note.lane,
        .cursor = note.position, .supported_snap_denominators = snaps,
        .active_snap_denominator = 16, .viewport_height_pixels = 900};
    state.selected_notes = {note.id};
    auto destination = note.position;
    const auto* view = history.read_chart(chart);
    const auto step = snap_step(*view, {.denominator = 16}, view->divisions_per_standard_measure);
    if (!step) return false;
    destination.row += *step;
    const auto before = history.document.prepared_bms_programs.front();
    const auto steps = history.undo.size();
    if (!dispatch_editor_action(history, state, begin_note_move_gesture_action_t{note.id}).accepted) return false;
    if (!dispatch_editor_action(history, state, update_note_move_gesture_action_t{
        .reference_position = destination, .reference_lane = note.lane}).accepted) return false;
    const auto result = measure("apply-drag", [&] {
        return dispatch_editor_action(history, state, update_note_move_gesture_action_t{
            .reference_position = destination, .reference_lane = note.lane,
            .operation = proposal_operation_t::apply});
    });
    return result.accepted && history.undo.size() == steps + 1 &&
        history.read_chart(chart)->find_note(note.id)->position == destination &&
        check_history(history, before, history.document.prepared_bms_programs.front());
}

auto create_layers(document_history_t& history, chart_id_t chart) -> bool {
    for (int index = 0; index < 7; ++index) {
        const auto before = history.document.prepared_bms_programs.front();
        const auto steps = history.undo.size();
        const auto count = history.read_chart(chart)->semantic_layers.size();
        const auto result = measure("create-layer", [&] {
            return apply_semantic_layer_edit(history, insert_edit_t<semantic_layer_edit_value_t>{
                .chart = chart, .value = {.name = "benchmark-" + std::to_string(index),
                    .color = {.red = 1, .green = 0.25, .blue = 0.125}}});
        });
        if (!result.applied || history.undo.size() != steps + 1 ||
            history.read_chart(chart)->semantic_layers.size() != count + 1 ||
            !check_history(history, before, history.document.prepared_bms_programs.front())) return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: wormedit_bms_edit_benchmark /disposable-copy/chart.bms\n";
        return 2;
    }
    std::cout << std::fixed << std::setprecision(3);
    auto opened = sys::open_project_file(argv[1]);
    if (!opened.document) return 2;
    auto history = document_history_t::from(std::move(*opened.document));
    if (history.document.prepared_bms_programs.size() != 1) return 2;
    const auto chart = history.document.prepared_bms_programs.front()->difficulty();
    const auto* view = history.read_chart(chart);
    if (!view) return 2;
    std::optional<note_t> note;
    for (const auto& measure : view->measures) {
        if (measure.notes.empty()) continue;
        note = measure.notes.front();
        break;
    }
    if (!note) return 2;
    std::cout << "title=" << history.document.metadata.title << " measures=" << view->measures.size() << '\n';
    if (!move_note(history, chart, *note) || !create_layers(history, chart)) {
        std::cerr << "Edit or exact Undo/Redo verification failed\n";
        return 1;
    }
    if (diagnostic_queries::has_errors(validate_document(history.document))) return 1;
    std::cout << "single-step-undo-redo-and-validation=passed\n";
}
