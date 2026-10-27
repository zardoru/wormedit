#include <wormedit/core/command_journal.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_delta.h>
#include <wormedit/core/bms_program_edit.h>
#include "../../src/formats/native_project_value_codec.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include "../support/temporary_directory.h"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace wormedit;

auto make_benchmark_song() -> song_document_t {
    chart_document_t chart{.id = chart_id_t{2}, .divisions_per_standard_measure = 1000};
    lane_layout_t layout{.profile_id = "journal-benchmark-8k", .display_name = "Benchmark 8K"};
    for (std::uint32_t lane = 0; lane < 8; ++lane)
        layout.lanes.push_back(lane_definition_t::from(lane + 1, lane, stage_group_t::none));
    chart.lane_layouts.push_back({.id = object_id_t{50'001},
        .position = {measure_id_t{1}, 0}, .layout = std::move(layout)});
    for (std::uint64_t measure = 0; measure < 400; ++measure) {
        measure_chunk_t chunk{.id = measure_id_t{measure + 1}};
        chunk.notes.reserve(125);
        for (std::uint64_t note = 0; note < 125; ++note)
            chunk.notes.push_back({.id = object_id_t{measure * 125 + note + 1},
                .position = {chunk.id, note * 8}, .lane = lane_id_t{note % 8 + 1}});
        chart.measures.push_back(std::move(chunk));
    }
    return {.id = song_id_t{1}, .revision = 2, .charts = {std::move(chart)}};
}

void benchmark_codec(const song_document_t& song, const std::string& bytes) {
    BENCHMARK("write_native_project / 50000 notes") {
        return write_native_project(song);
    };
    BENCHMARK("parse_native_project / 50000 notes") {
        return parse_native_project(bytes);
    };
}

auto create_empty_journals(const std::filesystem::path& directory, int count)
    -> std::vector<command_journal_t> {
    std::vector<command_journal_t> journals;
    journals.reserve(count);
    for (int run = 0; run < count; ++run) {
        const auto path = directory / ("append-" + std::to_string(run) + ".journal");
        std::filesystem::remove(path);
        journals.emplace_back(path);
    }
    return journals;
}

auto create_seeded_journals(const std::filesystem::path& directory,
                            const command_journal_entry_t& baseline, int count)
    -> std::vector<command_journal_t> {
    auto journals = create_empty_journals(directory, count);
    for (const auto& journal : journals) REQUIRE(journal.append(baseline));
    return journals;
}

void benchmark_seed_append(const std::filesystem::path& directory, const command_journal_entry_t& baseline) {
    BENCHMARK_ADVANCED("command_journal_t::append / full seed")
    (Catch::Benchmark::Chronometer meter) {
        auto journals = create_empty_journals(directory, meter.runs());
        std::vector<result_t<void, wormedit::error_t<failure_code_t>>> results(meter.runs());
        meter.measure([&](int run) { results[run] = journals[run].append(baseline); });
        for (const auto& result : results) REQUIRE(result);
    };
}

void benchmark_append(const std::filesystem::path& directory,
                      const command_journal_entry_t& baseline,
                      const command_journal_entry_t& moved) {
    // Each measurement appends exactly one moved post-state to a seeded, cached writer.
    // File setup, snapshot construction and result assertions stay outside the timer.
    BENCHMARK_ADVANCED("command_journal_t::append / one-note delta")
    (Catch::Benchmark::Chronometer meter) {
        auto journals = create_seeded_journals(directory, baseline, meter.runs());
        std::vector<result_t<void, wormedit::error_t<failure_code_t>>> results(meter.runs());
        meter.measure([&](int run) { results[run] = journals[run].append(moved); });
        for (const auto& result : results) REQUIRE(result);
    };
}

void report_record_sizes(const std::filesystem::path& directory,
                         const command_journal_entry_t& baseline, const command_journal_entry_t& moved) {
    const auto path = directory / "record-sizes.journal";
    command_journal_t journal{path};
    REQUIRE(journal.append(baseline));
    const auto seed_end = std::filesystem::file_size(path);
    REQUIRE(journal.append(moved));
    std::cout << "Seed record: " << seed_end - command_journal_header.size() << " bytes\n"
              << "One-note delta record: " << std::filesystem::file_size(path) - seed_end << " bytes\n";
}

auto create_three_record_journal(const std::filesystem::path& directory,
                                 const command_journal_entry_t& baseline,
                                 const command_journal_entry_t& moved) -> std::string {
    const auto path = directory / "read.journal";
    const command_journal_t journal{path};
    REQUIRE(journal.append(baseline));
    REQUIRE(journal.append(moved));
    auto third = moved;
    third.sequence = 3;
    third.before_revision = third.snapshot.song.revision++;
    ++third.snapshot.song.charts.front().measures.front().notes.front().position.row;
    REQUIRE(journal.append(third));
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, {}};
}

void benchmark_read(const std::filesystem::path& directory,
                    const command_journal_entry_t& baseline,
                    const command_journal_entry_t& moved) {
    const auto bytes = create_three_record_journal(directory, baseline, moved);
    const auto read = command_journal_read_t::from(bytes);
    REQUIRE(read.ok());
    REQUIRE(read.value.accepted_records == 3);
    REQUIRE(read.value.snapshot);
    REQUIRE(read.value.snapshot->song.revision == moved.snapshot.song.revision + 1);
    std::cout << "Three-record journal: " << bytes.size() << " bytes\n";
    BENCHMARK("command_journal_read_t::from / 3 records") {
        return command_journal_read_t::from(bytes);
    };
}
} // namespace

// Opt in explicitly with wormedit_command_journal_benchmarks "[.benchmark]".
// Use --benchmark-samples 5 for a shorter Debug run; no timing thresholds are asserted.
TEST_CASE("Command journal persistence costs for 50000 native notes", "[.benchmark][command-journal]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto song = make_benchmark_song();
    const auto serialized = write_native_project(song);
    REQUIRE(serialized.ok());
    REQUIRE(serialized.value);
    const auto parsed = parse_native_project(*serialized.value);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value);
    REQUIRE(parsed.value->charts.front().measures.size() == 400);
    std::cout << "Native payload (50000 notes): " << serialized.value->size() << " bytes\n";
    const command_journal_entry_t baseline{.sequence = 1, .before_revision = 1,
        .command = "Initial chart", .snapshot = {.song = song}};
    auto moved = baseline;
    moved.sequence = 2;
    moved.before_revision = moved.snapshot.song.revision++;
    moved.command = "Move note";
    ++moved.snapshot.song.charts.front().measures.front().notes.front().position.row;
    benchmark_codec(song, *serialized.value);
    report_record_sizes(directory->path(), baseline, moved);
    benchmark_seed_append(directory->path(), baseline);
    benchmark_append(directory->path(), baseline, moved);
    benchmark_read(directory->path(), baseline, moved);
}

namespace {
auto make_scoped_benchmark_song() -> song_document_t {
    auto song = make_benchmark_song();
    // Native migration contributes one typed measure definition per measure.
    // Keep the scoped workload at exactly 50,000 total authored content items.
    for (auto& measure : song.charts.front().measures) measure.notes.pop_back();
    const auto owner = bms_program_document_t::from_native_chart(song.charts.front(), song, object_id_t{100'000});
    REQUIRE(owner.ok());
    REQUIRE(owner.value);
    std::size_t items{};
    for (const auto& node : (*owner.value)->prepared().program.nodes())
        if (const auto* content = std::get_if<bms_chart_content_t>(&node.value)) items += content->items.size();
    REQUIRE(items == 50'000);
    song.prepared_bms_programs.push_back(*owner.value);
    song.charts.clear();
    return song;
}
auto move_native_benchmark_note(bms_content_item_t& item) -> bool {
    auto* note = std::get_if<bms_native_content_t>(&item.value);
    if (!note || !std::holds_alternative<note_t>(note->payload())) return false;
    auto positions = std::vector<bms_program_position_t>(note->positions().begin(), note->positions().end());
    positions.front().fraction = {1, 1000};
    const auto value = bms_native_content_t::from(note->payload(), std::move(positions), note->resource_reference());
    REQUIRE(value);
    item.value = *value;
    return true;
}
auto move_scoped_benchmark_note(std::vector<bms_program_node_t>& nodes) -> bool {
    for (auto& node : nodes) {
        auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        for (auto& item : chart->items)
            if (move_native_benchmark_note(item)) return true;
    }
    return false;
}
void move_benchmark_note(song_document_t& song, bool scoped) {
    if (!scoped) {
        ++song.charts.front().measures.front().notes.front().position.row;
        return;
    }
    const auto& owner = *song.prepared_bms_programs.front();
    auto prepared = owner.prepared();
    std::vector<bms_program_node_t> nodes(prepared.program.nodes().begin(), prepared.program.nodes().end());
    REQUIRE(move_scoped_benchmark_note(nodes));
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE(program);
    prepared.program = *program;
    const auto replacement = bms_program_document_t::from(owner.difficulty(), std::move(prepared), owner.evaluation());
    REQUIRE(replacement.ok());
    REQUIRE(replacement.value);
    song.prepared_bms_programs.front() = *replacement.value;
}
template<class Work> auto report_cost(std::string_view name, Work work) -> double {
    const auto start = std::chrono::steady_clock::now();
    work();
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << name << ": " << ms << " ms" << std::endl;
    return ms;
}
void report_scoped_encoding(const song_document_t& before, const song_document_t& after) {
    using namespace native_project_internal;
    const auto& owner = *after.prepared_bms_programs.front();
    auto prepared = owner.prepared();
    report_cost("with_catalog (one owner)", [&] { prepared = owner.prepared().with_catalog(after); });
    report_cost("owner canonicalization/projection (one owner)", [&] {
        REQUIRE(bms_program_document_t::from(owner.difficulty(), prepared, owner.evaluation()).ok());
    });
    report_cost("canonicalization + encoding (both owners)", [&] {
        (void)encode_native_program(*before.prepared_bms_programs.front(), before);
        (void)encode_native_program(owner, after);
    });
    report_cost("small owner fields (both owners, unchanged catalog inputs)", [&] {
        (void)encode_native_fields(*before.prepared_bms_programs.front());
        (void)encode_native_fields(owner);
    });
}
void report_delta_breakdown(const song_document_t& before, const song_document_t& after,
                            const std::filesystem::path& directory) {
    using namespace native_project_internal;
    report_cost("validation", [&] { REQUIRE(validate_native_document(after, {}).empty()); });
    if (!after.prepared_bms_programs.empty()) report_scoped_encoding(before, after);
    const auto delta = native_project_delta_t::from(before, after);
    REQUIRE(delta);
    report_cost("delta from (validation + encoding + diff + dump)", [&] {
        REQUIRE(native_project_delta_t::from(before, after));
    });
    report_cost("checksum (delta payload)", [&] { (void)native_project_content_checksum(delta->bytes()); });
    report_cost("file append (delta payload)", [&] {
        REQUIRE(append_native_bytes_file(directory / "breakdown.bytes", delta->bytes(), 0, 1U << 20));
    });
}
struct benchmark_segment_t { std::string bytes; std::uintmax_t seed_bytes{}; };
void extend_replay_segment(std::string& bytes, const nlohmann::json& forward_record,
    const nlohmann::json& forward, const nlohmann::json& reverse) {
    auto checksum = forward_record.at("sha256").get<std::string>();
    // Reuse two actual accepted-value patches; setup does not benchmark 128 writes.
    // Only the record chain and accepted revision change as Move/Undo alternate.
    for (std::uint64_t i = 3; i <= 129; ++i) {
        auto record = forward_record;
        auto& body = record["record"];
        auto delta = i % 2 ? reverse : forward;
        delta["document"]["fields"]["revision"]["set"] = i + 1;
        body["payload"] = delta.dump();
        body["sequence"] = i;
        body["before_revision"] = i;
        body["base_sequence"] = i - 1;
        body["base_checksum"] = checksum;
        checksum = native_project_content_checksum(body.dump());
        record["sha256"] = checksum;
        bytes += record.dump() + '\n';
    }
}
auto create_replay_segment(const std::filesystem::path& directory,
    const command_journal_entry_t& baseline, const command_journal_entry_t& moved) -> benchmark_segment_t {
    const auto path = directory / "segment.journal";
    command_journal_t journal{path};
    REQUIRE(journal.append(baseline));
    const auto seed_bytes = std::filesystem::file_size(path);
    REQUIRE(journal.append(moved));
    std::ifstream input(path, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    const auto record_start = bytes.find('\n', command_journal_header.size()) + 1;
    const auto forward_record = nlohmann::json::parse(bytes.substr(record_start));
    const auto forward = nlohmann::json::parse(forward_record.at("record").at("payload").get<std::string>());
    const auto reverse_delta = native_project_delta_t::from(moved.snapshot.song, baseline.snapshot.song);
    REQUIRE(reverse_delta);
    const auto reverse = nlohmann::json::parse(reverse_delta->bytes());
    extend_replay_segment(bytes, forward_record, forward, reverse);
    return {.bytes = std::move(bytes), .seed_bytes = seed_bytes};
}

void require_replayed_segment(const std::string& bytes, song_document_t expected_song) {
    expected_song.revision = 130;
    const auto verify = command_journal_read_t::from(bytes);
    REQUIRE(verify.ok());
    REQUIRE(verify.value.accepted_records == 129);
    REQUIRE(verify.value.snapshot);
    const auto actual = write_native_project(verify.value.snapshot->song);
    const auto expected = write_native_project(expected_song);
    REQUIRE(actual.value);
    REQUIRE(expected.value);
    REQUIRE(*actual.value == *expected.value);
}
void benchmark_replay_segment(const std::filesystem::path& directory,
    const command_journal_entry_t& baseline, const command_journal_entry_t& moved) {
    const auto segment_bytes = create_replay_segment(directory, baseline, moved);
    const auto& bytes = segment_bytes.bytes;
    const auto seed_bytes = segment_bytes.seed_bytes;
    require_replayed_segment(bytes, baseline.snapshot.song);
    std::cout << "128 deltas: " << bytes.size() - seed_bytes << " bytes" << std::endl;
    const auto seed = report_cost("read seed alone", [&] {
        REQUIRE(command_journal_read_t::from(std::string_view{bytes}.substr(0, seed_bytes)).ok());
    });
    const auto segment = report_cost("read seed + 128 deltas", [&] { REQUIRE(command_journal_read_t::from(bytes).ok()); });
    std::cout << "Replay ratio: " << segment / seed << std::endl;
    CHECK(segment <= seed * 1.5);
}
} // namespace

// Run with '[.replay-benchmark]' --benchmark-samples 1 --benchmark-no-analysis --benchmark-warmup-time 0.
TEST_CASE("Command journal replay costs for 50000 flat and scoped items", "[.replay-benchmark]") {
    const auto scoped = GENERATE(false, true);
    std::cout << (scoped ? "SCOPED" : "FLAT") << " 50000 items" << std::endl;
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto song = scoped ? make_scoped_benchmark_song() : make_benchmark_song();
    const command_journal_entry_t baseline{.sequence = 1, .before_revision = 1,
        .command = "Initial chart", .snapshot = {.song = song}};
    auto moved = baseline;
    moved.sequence = 2;
    moved.before_revision = moved.snapshot.song.revision++;
    moved.command = "Move note";
    move_benchmark_note(moved.snapshot.song, scoped);
    report_delta_breakdown(song, moved.snapshot.song, directory->path());
    report_record_sizes(directory->path(), baseline, moved);
    benchmark_seed_append(directory->path(), baseline);
    benchmark_append(directory->path(), baseline, moved);
    benchmark_replay_segment(directory->path(), baseline, moved);
}

TEST_CASE("Scoped journal delta cost breakdown for 50000 items", "[.delta-breakdown]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto before = make_scoped_benchmark_song();
    auto after = before;
    ++after.revision;
    move_benchmark_note(after, true);
    report_delta_breakdown(before, after, directory->path());
    const command_journal_entry_t baseline{.sequence = 1, .before_revision = 1,
        .command = "Initial chart", .snapshot = {.song = before}};
    auto moved = baseline;
    moved.sequence = 2;
    moved.before_revision = before.revision;
    moved.snapshot.song = after;
    report_record_sizes(directory->path(), baseline, moved);
    const auto seed_ms = report_cost("seed append", [&] {
        command_journal_t journal{directory->path() / "seed.journal"};
        REQUIRE(journal.append(baseline));
    });
    command_journal_t journal{directory->path() / "delta.journal"};
    REQUIRE(journal.append(baseline));
    const auto delta_ms = report_cost("delta append", [&] { REQUIRE(journal.append(moved)); });
    CHECK(delta_ms <= seed_ms * 0.25);
}
