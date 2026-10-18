#include "sys/chart_import_session.h"

#include <wormedit/core/chart_import.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using namespace wormedit;

namespace {

struct scratch_t {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("wormedit-chart-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    scratch_t() {
        std::filesystem::create_directories(root / "project");
        std::filesystem::create_directories(root / "source");
    }
    ~scratch_t() { std::error_code error; std::filesystem::remove_all(root, error); }
    void write(const std::filesystem::path& relative, std::string_view bytes) const {
        std::ofstream{root / relative, std::ios::binary} << bytes;
    }
};

auto parse_sm_fixture(std::string_view name) -> song_document_t {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "stepmania" / std::string(name),
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_stepmania_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.value.has_value());
    return *parsed.value;
}

auto parse_bms_text(std::string_view text) -> song_document_t {
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    REQUIRE(parsed.value.song.has_value());
    return *parsed.value.song;
}

constexpr std::string_view target_bms = "#PLAYER 1\n#TITLE Target\n#BPM 150\n"
                                        "#WAV01 kick.wav\n#WAV02 other.wav\n#00111:0102\n";

auto find_resource(const chart_import_proposal_t& proposal, std::string_view name)
    -> const chart_import_resource_t* {
    const auto found = std::ranges::find_if(proposal.resources, [&](const auto& item) {
        return item.name.find(name) != std::string::npos;
    });
    return found == proposal.resources.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("Chart import plans reuse of identical files, new files, missing files and timing differences",
          "[unit][core][chart-import]") {
    scratch_t scratch;
    scratch.write("project/kick.wav", "kick-bytes");
    scratch.write("project/other.wav", "other-bytes");
    scratch.write("source/kick.wav", "kick-bytes");    // identical content in another directory
    scratch.write("source/snare.wav", "snare-bytes");  // new
    // source/hat.wav is missing.
    const auto target = parse_bms_text(target_bms);
    const auto source = parse_sm_fixture("keysounds.sm");
    const chart_import_paths_t paths{scratch.root / "source", scratch.root / "project"};
    const auto difficulties = list_chart_import_difficulties(source);
    REQUIRE(difficulties.size() >= 1);
    CHECK(difficulties.front().family == format_family_t::stepmania);
    const std::vector<chart_id_t> selected{difficulties.front().chart};
    const auto proposal = plan_chart_import(target, source, paths, selected);
    REQUIRE(proposal);
    const auto* kick = find_resource(proposal, "kick");
    const auto* snare = find_resource(proposal, "snare");
    const auto* hat = find_resource(proposal, "hat");
    REQUIRE((kick && snare && hat));
    CHECK(kick->action == chart_import_resource_action_t::reuse);
    CHECK(kick->existing.has_value());
    CHECK(snare->action == chart_import_resource_action_t::add);
    CHECK(hat->action == chart_import_resource_action_t::missing);
    CHECK(proposal.count(chart_import_resource_action_t::reuse) == 1);
    REQUIRE_FALSE(proposal.timing_differences.empty());
    CHECK(proposal.timing_differences.front().message.find("120") != std::string::npos);
    CHECK(proposal.timing_differences.front().message.find("150") != std::string::npos);
}

TEST_CASE("Chart import adds an SM difficulty to a BMS song as one undoable command",
          "[unit][core][chart-import]") {
    scratch_t scratch;
    scratch.write("project/kick.wav", "kick-bytes");
    scratch.write("project/other.wav", "other-bytes");
    scratch.write("source/kick.wav", "kick-bytes");
    scratch.write("source/snare.wav", "snare-bytes");
    auto history = document_history_t::from(parse_bms_text(target_bms));
    const auto before = history.document;
    const auto source = parse_sm_fixture("keysounds.sm");
    const chart_import_paths_t paths{scratch.root / "source", scratch.root / "project"};
    const std::vector<chart_id_t> selected{source.charts.front().id};
    const auto result = apply_chart_import(history, source, paths, selected);
    INFO((result.command.diagnostics.empty() ? "" : result.command.diagnostics.front().message));
    REQUIRE(result.command.applied);
    REQUIRE(result.payload.charts.size() == 1);
    CHECK(history.undo.size() == 1);
    const auto& document = history.document;
    CHECK(document.charts.size() == before.charts.size() + 1);
    CHECK(validate_document(document).empty());
    // The existing chart and resources are untouched.
    CHECK(document.charts.front().id == before.charts.front().id);
    CHECK(document.charts.front().measures.front().notes.size() == before.charts.front().measures.front().notes.size());
    for (const auto& resource : before.resources) {
        const auto kept = std::ranges::find(document.resources, resource.id, &resource_entry_t::id);
        REQUIRE(kept != document.resources.end());
        CHECK(kept->asset == resource.asset);
    }
    const auto& added = document.charts.back();
    CHECK(added.id == result.payload.charts.front());
    CHECK(added.source_family == format_family_t::stepmania);
    // The imported chart's sounds point at catalog entries of the project: kick reused.
    for (const auto& measure : added.measures)
        for (const auto& note : measure.notes)
            if (note.sound)
                CHECK(std::ranges::find(document.resources, *note.sound, &resource_entry_t::id) != document.resources.end());
    // kick.wav is reused (identical content); snare.wav and the missing hat.wav get new assets.
    CHECK(document.audio_assets.size() == before.audio_assets.size() + 2);
    CHECK(document.resources.size() == before.resources.size() + 2);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.size() == before.charts.size());
    CHECK(history.document.resources.size() == before.resources.size());
}

TEST_CASE("Chart import reports missing files and still adds the chart with flagged entries",
          "[unit][core][chart-import]") {
    scratch_t scratch;
    auto history = document_history_t::from(parse_bms_text(target_bms));
    const auto source = parse_sm_fixture("keysounds.sm");
    const chart_import_paths_t paths{scratch.root / "source", scratch.root / "project"};
    const std::vector<chart_id_t> selected{source.charts.front().id};
    const auto proposal = plan_chart_import(history.document, source, paths, selected);
    CHECK(proposal.count(chart_import_resource_action_t::missing) == proposal.resources.size());
    const auto result = apply_chart_import(history, source, paths, selected);
    REQUIRE(result.command.applied);
    const auto missing = std::ranges::count_if(history.document.resources, [](const auto& resource) {
        return resource.missing;
    });
    CHECK(missing >= 1);
}

TEST_CASE("Chart import rejects empty and unknown selections without changes",
          "[unit][core][chart-import]") {
    scratch_t scratch;
    auto history = document_history_t::from(parse_bms_text(target_bms));
    const auto source = parse_sm_fixture("keysounds.sm");
    const chart_import_paths_t paths{scratch.root / "source", scratch.root / "project"};
    const auto revision = history.document.revision;
    const std::vector<chart_id_t> selected{source.charts.front().id};
    CHECK_FALSE(apply_chart_import(history, source, paths, {}).command.applied);
    const std::vector<chart_id_t> unknown{chart_id_t{999999}};
    const auto result = apply_chart_import(history, source, paths, unknown);
    CHECK_FALSE(result.command.applied);
    CHECK_FALSE(result.command.diagnostics.empty());
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
    CHECK_FALSE(plan_chart_import(history.document, source, paths, unknown));
}

TEST_CASE("Importing two difficulties gives each fresh unique identities",
          "[unit][core][chart-import]") {
    scratch_t scratch;
    scratch.write("source/kick.wav", "kick-bytes");
    auto history = document_history_t::from(parse_bms_text(target_bms));
    auto source = parse_sm_fixture("keysounds.sm");
    REQUIRE(source.charts.size() >= 1);
    auto second = source.charts.front();
    second.metadata.name = "Second";
    second.id = chart_id_t{source.charts.front().id.value + 1000};
    source.charts.push_back(second);
    const chart_import_paths_t paths{scratch.root / "source", scratch.root / "project"};
    const std::vector<chart_id_t> selected{source.charts.front().id, second.id};
    const auto result = apply_chart_import(history, source, paths, selected);
    REQUIRE(result.command.applied);
    REQUIRE(result.payload.charts.size() == 2);
    CHECK(result.payload.charts[0] != result.payload.charts[1]);
    CHECK(history.undo.size() == 1);
    CHECK(validate_document(history.document).empty());
    CHECK(history.document.charts.back().metadata.name == "Second");
}

TEST_CASE("Chart import session reads a file through the opener, previews, and commits one command",
          "[unit][core][chart-import][session]") {
    scratch_t scratch;
    scratch.write("project/kick.wav", "kick-bytes");
    scratch.write("source/kick.wav", "kick-bytes");
    scratch.write("source/snare.wav", "snare-bytes");
    std::filesystem::copy_file(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "stepmania" / "keysounds.sm",
                               scratch.root / "source" / "keysounds.sm");
    auto history = document_history_t::from(parse_bms_text(target_bms));
    const auto before = history.document;
    sys::chart_import_session_t session;
    sys::chart_import_begin(session);
    CHECK(session.open);
    sys::chart_import_load(session, scratch.root / "source" / "keysounds.sm", history.document,
                           scratch.root / "project");
    INFO(session.error);
    REQUIRE(session.source.has_value());
    REQUIRE(session.error.empty());
    // One difficulty in the file: it is ticked for the user.
    REQUIRE(session.selected.size() == 1);
    auto view = sys::chart_import_view(session, history.document.revision);
    CHECK(view.visible);
    CHECK(view.can_import);
    CHECK(view.row_count == 1);
    CHECK(std::string(view.summary).find("reused") != std::string::npos);
    CHECK(view.note_count >= 1);
    sys::chart_import_toggle(session, session.source->charts.front().id, history.document, scratch.root / "project");
    CHECK_FALSE(sys::chart_import_view(session, history.document.revision).can_import);
    sys::chart_import_toggle(session, session.source->charts.front().id, history.document, scratch.root / "project");
    const auto result = sys::chart_import_confirm(session, history, history.document.revision, scratch.root / "project");
    REQUIRE(result.command.applied);
    CHECK_FALSE(session.open);
    CHECK(history.document.charts.size() == before.charts.size() + 1);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Chart import session reports unreadable files and keeps the project untouched",
          "[unit][core][chart-import][session]") {
    scratch_t scratch;
    scratch.write("source/broken.sm", "not a chart");
    auto history = document_history_t::from(parse_bms_text(target_bms));
    const auto revision = history.document.revision;
    sys::chart_import_session_t session;
    sys::chart_import_begin(session);
    sys::chart_import_load(session, scratch.root / "source" / "missing.sm", history.document, scratch.root / "project");
    CHECK_FALSE(session.error.empty());
    CHECK_FALSE(sys::chart_import_view(session, revision).can_import);
    sys::chart_import_load(session, scratch.root / "source" / "readme.txt", history.document, scratch.root / "project");
    CHECK_FALSE(session.error.empty());
    const auto result = sys::chart_import_confirm(session, history, revision, scratch.root / "project");
    CHECK_FALSE(result.command.applied);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
}

namespace {
void check_chart_import_rejection(const chart_import_result_t& result,
                                   const std::string& expected) {
    CHECK_FALSE(result.command.applied);
    REQUIRE(result.command.diagnostics.size() == 1);
    CHECK(result.command.diagnostics.front().message == expected);
}
} // namespace

TEST_CASE("Chart import session shares stale feedback after missing-source checks",
          "[unit][core][chart-import][chart-import-stamp]") {
    scratch_t scratch;
    auto history = document_history_t::from(parse_bms_text(target_bms));
    sys::chart_import_session_t session;
    sys::chart_import_begin(session);
    const bool missing = GENERATE(false, true);
    if (!missing) session.source = parse_bms_text(target_bms);
    const auto revision = history.document.revision;
    const auto result = sys::chart_import_confirm(session, history, revision + 1,
                                                  scratch.root / "project");
    const auto expected = missing ? std::string{"Read a chart file before importing."}
                                  : stale_action_message("Chart import");
    check_chart_import_rejection(result, expected);
    if (!missing) CHECK(session.error == expected);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
}

TEST_CASE("Chart import rejects exhausted chart IDs without publishing staged resources",
          "[unit][core][chart-import][id-allocation]") {
    scratch_t scratch;
    auto history = document_history_t::from(parse_bms_text(target_bms));
    history.document.charts.front().id = chart_id_t{std::numeric_limits<std::uint64_t>::max()};
    const auto before = write_native_project(history.document);
    REQUIRE(before.value.has_value());
    const auto revision = history.document.revision;
    const auto next_revision = history.next_revision;
    const auto saved_revision = history.saved_revision;
    const auto source = parse_sm_fixture("keysounds.sm");
    const chart_import_paths_t paths{scratch.root / "source", scratch.root / "project"};
    const std::vector<chart_id_t> selected{source.charts.front().id};

    const auto result = apply_chart_import(history, source, paths, selected);

    check_chart_import_rejection(result, "a chart could not be given fresh identities");
    CHECK(result.command.diagnostics.front().code == diagnostic_code_t::invalid_id);
    const auto unchanged = write_native_project(history.document).value == before.value &&
        history.document.revision == revision && result.command.revision == revision &&
        history.next_revision == next_revision && history.saved_revision == saved_revision &&
        history.undo.empty() && history.redo.empty() && result.payload.charts.empty();
    CHECK(unchanged);
}
