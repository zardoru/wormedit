#include <wormedit/core/measure_editor.h>
#include <wormedit/core/time_shift.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>

using namespace wormedit;

namespace {

// Engine output and modulo selection have a specified sequence across standard libraries.
struct sequence_t {
    std::mt19937 engine;
    auto pick(std::uint32_t count) -> std::uint32_t { return engine() % count; }
};

auto snapshot(const song_document_t& song) -> std::string {
    const auto encoded = write_native_project(song);
    REQUIRE(encoded.value.has_value());
    return *encoded.value;
}

auto generated_song(sequence_t& random) -> song_document_t {
    song_document_t song{.id = song_id_t{1}, .charts = {{.id = chart_id_t{2}}}};
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 960;
    const auto count = 1 + random.pick(8);
    for (std::uint32_t i = 0; i < count; ++i) {
        chart.measures.push_back({.id = measure_id_t{10 + i},
            .definition = {{1 + random.pick(3), 2}, std::nullopt}});
    }
    const lane_layout_t layout{.profile_id = "property-2k", .lanes = {
        {.id = lane_id_t{3}, .name = "A"}, {.id = lane_id_t{4}, .name = "B"}}};
    chart.lane_layouts.push_back({.id = object_id_t{30}, .position = {measure_id_t{10}, 0}, .layout = layout});
    auto later_layout = layout;
    std::swap(later_layout.lanes[0], later_layout.lanes[1]);
    if (count > 1)
        chart.lane_layouts.push_back({.id = object_id_t{31}, .position = {measure_id_t{11}, 0}, .layout = later_layout});
    for (std::uint32_t i = 0; i < count; ++i) {
        auto& measure = chart.measures[i];
        const auto head = 120U + 30U * random.pick(4);
        measure.notes.push_back({.id = object_id_t{100 + i}, .position = {measure.id, head},
            .lane = lane_id_t{3}});
        if (i + 1 < count) {
            measure.notes.push_back({.id = object_id_t{200 + i}, .position = {measure.id, 240},
                .lane = lane_id_t{4}, .kind = note_kind_t::hold,
                .end = local_position_t{chart.measures[i + 1].id, 60}});
        }
        chart.bpms.push_back({.id = object_id_t{300 + i}, .position = {measure.id, 0},
            .beats_per_minute = {100 + random.pick(100), 1}});
        chart.bookmarks.push_back({.id = object_id_t{400 + i}, .label = "property",
            .anchor = marker_anchor_t::beat, .position = local_position_t{measure.id, 300}});
    }
    REQUIRE(validate_document(song).empty());
    return song;
}

// Account for every position generated above, including both endpoints of each hold.
// Layout segments are checked by validation: a layout at a moved bar follows that bar.
struct object_rows_t {
    std::map<std::pair<std::uint64_t, bool>, std::uint64_t> values;
    static auto from(const chart_document_t& chart) -> object_rows_t {
        const auto index = measure_index_t::from(chart);
        REQUIRE(index.has_value());
        object_rows_t result;
        const auto add = [&](object_id_t id, local_position_t position, bool end = false) {
            const auto row = index.value().row_of(position, measure_chart_end_policy_t::allow);
            REQUIRE(row.has_value());
            result.values.emplace(std::pair{id.value, end}, *row);
        };
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                add(note.id, note.position);
                if (note.end) add(note.id, *note.end, true);
            }
        }
        for (const auto& bpm : chart.bpms) add(bpm.id, bpm.position);
        for (const auto& bookmark : chart.bookmarks) add(bookmark.id, *bookmark.position);
        return result;
    }
    auto operator==(const object_rows_t&) const -> bool = default;
};

struct edit_t {
    unsigned kind{}; // add, remove, move, staged keep-song-time, staged propagate
    measure_id_t bar;
    std::uint64_t row{};
    std::int64_t delta{};
    std::optional<object_id_t> note;

    static auto from(sequence_t& random, const chart_document_t& chart) -> edit_t {
        const auto index = measure_index_t::from(chart);
        REQUIRE(index.has_value());
        const auto count = static_cast<std::uint32_t>(chart.measures.size());
        edit_t edit{.kind = random.pick(5), .bar = chart.measures[random.pick(count)].id,
            .row = 120U * random.pick(static_cast<std::uint32_t>(index.value().total_rows / 120 + 3)),
            .delta = (static_cast<std::int64_t>(random.pick(9)) - 4) * 120};
        if (random.pick(8) == 0) edit.bar = measure_id_t{999999};
        const auto& notes = chart.measures[random.pick(count)].notes;
        if (edit.kind >= 3 && !notes.empty() && random.pick(2) == 0)
            edit.note = notes[random.pick(static_cast<std::uint32_t>(notes.size()))].id;
        return edit;
    }
    auto action() const -> shift_selection_action_t {
        shift_selection_action_t result{.chart = chart_id_t{2}, .rows = delta,
            .bar_lines = {bar}, .bar_line_time = kind == 4 ? bar_line_time_t::propagate : bar_line_time_t::keep_song_time};
        if (note) result.notes.push_back(*note);
        return result;
    }
    auto describe() const -> std::string {
        constexpr std::array names{"add", "remove", "move", "keep-song-time", "propagate"};
        std::ostringstream out;
        out << names[kind] << " bar=" << bar.value << " row=" << row << " delta=" << delta;
        if (note) out << " note=" << note->value;
        return out.str();
    }
    auto stage(song_document_t& song) const -> diagnostic_list_t {
        auto& chart = song.charts.front();
        switch (kind) {
        case 0: return add_bar_lines(chart, std::array{row});
        case 1: return remove_bar_lines(chart, std::array{bar});
        case 2: return move_bar_lines(chart, std::array{bar}, delta);
        default: return stage_shift_selection(song, action()).command.diagnostics;
        }
    }
};

void check_keep_rows(const song_document_t& before, const song_document_t& after, const edit_t& edit) {
    if (edit.kind == 4) return; // Propagation has validity/determinism coverage, not a row oracle.
    auto expected = object_rows_t::from(before.charts.front());
    if (edit.kind == 3 && edit.note) {
        for (auto& [key, row] : expected.values) {
            if (key.first == edit.note->value)
                row = static_cast<std::uint64_t>(static_cast<std::int64_t>(row) + edit.delta);
        }
    }
    REQUIRE(object_rows_t::from(after.charts.front()) == expected);
}

void check_published_rejection(const song_document_t& before, const edit_t& edit) {
    // The stage helpers may mutate their disposable working copy before rejecting.
    // The public transaction boundary must leave both document and history unchanged.
    if (edit.kind == 0) return;
    auto history = document_history_t::from(before);
    const auto saved = snapshot(history.document);
    const auto result = edit.kind == 1
        ? apply_measure_edit(history, remove_bar_line_action_t{.chart = chart_id_t{2}, .measure = edit.bar})
        : apply_shift_selection(history, edit.action()).command;
    REQUIRE_FALSE(result.applied);
    REQUIRE_FALSE(result.diagnostics.empty());
    REQUIRE(snapshot(history.document) == saved);
    REQUIRE(history.undo.empty());
    REQUIRE(history.redo.empty());
}

struct coverage_t {
    std::array<unsigned, 5> accepted{};
    std::array<unsigned, 5> rejected{};
    std::array<unsigned, 5> changed{};
    void check() const {
        for (unsigned kind = 0; kind < 5; ++kind) {
            CAPTURE(kind);
            REQUIRE(accepted[kind] > 0);
            REQUIRE(rejected[kind] > 0);
            REQUIRE(changed[kind] > 0);
        }
    }
};

void check_rejected(const song_document_t& song, const song_document_t& candidate, const edit_t& edit) {
    // add_bar_lines itself promises atomic rejection; other geometry helpers stage edits.
    if (edit.kind == 0) REQUIRE(snapshot(candidate) == snapshot(song));
    check_published_rejection(song, edit);
    REQUIRE(validate_document(song).empty());
}

void check_step(song_document_t& song, const edit_t& edit, coverage_t& coverage) {
    auto candidate = song;
    auto repeated = song;
    const auto diagnostics = edit.stage(candidate);
    const auto again = edit.stage(repeated);
    REQUIRE(diagnostics == again);
    if (!diagnostics.empty()) {
        ++coverage.rejected[edit.kind];
        check_rejected(song, candidate, edit);
        return;
    }
    ++coverage.accepted[edit.kind];
    REQUIRE(validate_document(candidate).empty());
    const auto encoded = snapshot(candidate);
    REQUIRE(encoded == snapshot(repeated));
    if (encoded != snapshot(song)) ++coverage.changed[edit.kind];
    check_keep_rows(song, candidate, edit);
    song = std::move(candidate);
}

void run_sequence(std::uint32_t seed, unsigned steps, coverage_t& coverage) {
    sequence_t random{.engine = std::mt19937{seed}};
    auto song = generated_song(random);
    std::ostringstream trace;
    for (unsigned step = 0; step < steps; ++step) {
        const auto edit = edit_t::from(random, song.charts.front());
        trace << step << ": " << edit.describe() << '\n';
        INFO("seed=" << seed << "\n" << trace.str());
        check_step(song, edit, coverage);
    }
}

} // namespace

TEST_CASE("Seeded measure geometry edits preserve rows and reject atomically", "[core][measure-property]") {
    coverage_t coverage;
    for (const auto seed : {1U, 7U, 42U, 667U, 20261028U}) run_sequence(seed, 8, coverage);
}

TEST_CASE("Extended seeded measure geometry edit sequences", "[core][measure-property][slow]") {
    coverage_t coverage;
    for (std::uint32_t seed = 0; seed < 64; ++seed) run_sequence(seed, 100, coverage);
    coverage.check();
}
