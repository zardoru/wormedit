#include "../support/bms_program_fixture.h"
#include "../support/temporary_directory.h"
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command_journal.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace wormedit;
using namespace wormedit::test::bms_program;
namespace {
constexpr chart_id_t difficulty{9000};
constexpr auto source = "#TITLE Root\n#ARTIST Author\n#BPM 120\n#WAV01 a.wav\n#BMP01 a.png\n"
    "#00011:01\n#00001:01\n#RANDOM 2\n#IF 1\n#WAV02 b.wav\n#00112:02\n"
    "; second Chart\n#00212:01\n#ELSE\n#00113:01\n#ENDIF\n#ENDRANDOM\n";
auto snapshot(bms_prepared_program_t value) -> song_document_t {
    bms_program_evaluation_t evaluation;
    for (const auto id : selectors(value.program)) evaluation.selections.push_back({id, 1});
    auto owner = bms_program_document_t::from(difficulty, std::move(value), std::move(evaluation));
    for (const auto& diagnostic : owner.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(owner.value);
    REQUIRE(owner.ok());
    return {.id = song_id_t{9001}, .prepared_bms_programs = {std::move(*owner.value)}};
}
auto serialized(const song_document_t& song) -> std::string {
    auto written = write_native_project(song);
    for (const auto& diagnostic : written.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(written.ok());
    REQUIRE(written.value);
    return std::move(*written.value);
}
auto reloaded(const song_document_t& song) -> song_document_t {
    const auto bytes = serialized(song);
    auto read = parse_native_project(bytes);
    for (const auto& diagnostic : read.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(read.ok());
    REQUIRE(read.value);
    CHECK(serialized(*read.value) == bytes);
    return std::move(*read.value);
}
auto legacy(std::string_view text = source) -> document_history_t {
    auto parsed = parse_bms_bytes(bytes(text), {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(parsed.value.song);
    return document_history_t::from(std::move(*parsed.value.song));
}
auto migration_proposal(document_history_t& history, chart_id_t id) -> song_document_t {
    const auto before = serialized(history.document);
    auto result = prepare_native_bms_migration(history, id);
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.value);
    CHECK(serialized(history.document) == before);
    return std::move(*result.value);
}
void commit_migration(document_history_t& history, song_document_t proposal) {
    document_command_transaction_t transaction(history);
    auto staged = transaction.stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        document = std::move(proposal);
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Migrate scoped BMS"}};
    });
    REQUIRE(transaction.finish_execution(std::move(staged)).applied);
}
void check_migration_history(document_history_t& history, chart_id_t id, const song_document_t& after) {
    REQUIRE(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.find_chart(id));
    REQUIRE(redo_document_command(history));
    CHECK(serialized(history.document) == serialized(after));
}
auto journal_snapshot(const command_journal_t& journal) -> song_document_t {
    const auto read = journal.read();
    REQUIRE(read.ok());
    REQUIRE(read.value.snapshot);
    return read.value.snapshot->song;
}
void check_native_annotations(const note_t& note, const bms_program_document_t& owner) {
    const auto* view = owner.read_chart();
    REQUIRE(view);
    const auto& restored = view->measures.front().notes.front();
    CHECK(restored.label == note.label);
    CHECK(restored.osu_hitsample == note.osu_hitsample);
    REQUIRE(restored.semantic_layers.size() == 1);
    CHECK(restored.semantic_layers.front() == owner.prepared().semantic_layers.front().id);
    CHECK(owner.prepared().sound_layer_defaults.front().layers == restored.semantic_layers);
}
void check_shared_asset(const song_document_t& result, const song_document_t& original, chart_id_t id) {
    const auto& asset = result.find_bms_program(id)->prepared().audio_assets.front();
    CHECK(asset.id == original.audio_assets.front().id);
    CHECK(asset.waveform_cache_path == original.audio_assets.front().waveform_cache_path);
    const auto encoded = nlohmann::json::parse(serialized(result));
    CHECK(encoded["document"]["prepared_bms_programs"][0]["audio_assets"][0].is_number_unsigned());
}
void check_migration_refusal(document_history_t& history, chart_id_t id) {
    const auto before = write_native_project(history.document);
    const auto backing = history.document.metadata.format_groups.front().encoding.original_bytes;
    const auto migrated = prepare_native_bms_migration(history, id);
    CHECK_FALSE(migrated.value);
    REQUIRE_FALSE(migrated.diagnostics.empty());
    CHECK(migrated.diagnostics.front().code == native_project_diagnostic_code_t::invalid_document);
    CHECK(write_native_project(history.document).value == before.value);
    CHECK(history.document.metadata.format_groups.front().encoding.original_bytes == backing);
    CHECK(history.undo.empty());
}
auto last_scoped_note(nlohmann::json& root) -> nlohmann::json& {
    nlohmann::json* result{};
    for (auto& node : root["document"]["prepared_bms_programs"][0]["nodes"]) {
        if (node["value"]["type"] != "chart") continue;
        for (auto& item : node["value"]["data"]["items"])
            if (item["value"]["type"] == "note") result = &item;
    }
    REQUIRE(result);
    return *result;
}
auto native_event(const chart_document_t& chart, const bms_prepared_program_t& input, int kind)
    -> bms_native_event_t {
    const auto position = local_position_t{.measure = chart.measures.front().id, .row = 1};
    const auto lane = input.lane_profile->layout.lanes.front().id;
    const auto bmp = input.resources.back().id;
    const std::vector<bms_native_event_t> values{
        note_t{.position = position, .lane = lane, .label = "native", .osu_hitsample = osu_hitsample_t{.clap = true}},
        bgm_event_t{.position = position, .source_lane = input.bgm_columns.front().id},
        bga_event_t{.position = position, .resource = bmp},
        bpm_event_t{.position = position, .beats_per_minute = {150, 1}},
        pause_event_t{.position = position, .duration = {1, 4}},
        warp_event_t{.span = {position, {.measure = position.measure, .row = 2}}},
        scroll_event_t{.position = position, .multiplier = {-1, 2}},
        speed_event_t{.position = position, .multiplier = {2, 1}},
        kiai_event_t{.position = position, .enabled = true},
        timing_anchor_t{.position = position, .source_sample_frame = 100},
        bookmark_t{.label = "native", .position = position},
        section_t{.label = "native", .beat_span = position_span_t{position, {.measure = position.measure, .row = 2}}},
        sv_recipe_t{.name = "native", .range = {position, {.measure = position.measure, .row = 2}}},
        sound_channel_t{.name = "native", .source_clip = {.id = clip_id_t{7001}, .source = input.audio_assets.front().id},
            .events = {{.id = object_id_t{7002}, .position = position, .playable = false}}}};
    return values.at(static_cast<std::size_t>(kind));
}
auto with_native_event(int kind) -> song_document_t {
    auto input = prepared("#BPM 120\n#WAV01 a.wav\n#BMP01 a.png\n#00011:01\n#00001:01\n");
    const auto view = projected(input);
    auto native = bms_native_content_t::from(native_event(view.song().charts.front(), input, kind), view.song().charts.front());
    REQUIRE(native);
    auto nodes = std::vector<bms_program_node_t>(input.program.nodes().begin(), input.program.nodes().end());
    const auto chart = input.program.target_chart(input.program.root());
    auto found = std::ranges::find(nodes, *chart, &bms_program_node_t::id);
    std::get<bms_chart_content_t>(found->value).items.push_back({.id = object_id_t{7000}, .value = std::move(*native)});
    auto program = bms_program_t::from(std::move(nodes));
    REQUIRE(program);
    input.program = std::move(*program);
    return snapshot(std::move(input));
}
}

TEST_CASE("Scoped native snapshots preserve inactive content Text order and stable identities", "[bms-program-migration]") {
    const auto song = snapshot(prepared(scoped_source));
    const auto read = reloaded(song);
    REQUIRE(read.prepared_bms_programs.size() == 1);
    CHECK(read.charts.empty());
    CHECK(read.prepared_bms_programs.front()->prepared().program == song.prepared_bms_programs.front()->prepared().program);
    CHECK(read.prepared_bms_programs.front()->prepared().measures == song.prepared_bms_programs.front()->prepared().measures);
}
TEST_CASE("Scoped native snapshots round trip every native event family", "[bms-program-migration]") {
    const auto kind = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    const auto song = with_native_event(kind);
    const auto read = reloaded(song);
    CHECK(read.prepared_bms_programs.front()->prepared().program == song.prepared_bms_programs.front()->prepared().program);
}
TEST_CASE("An empty scoped Root saves without an imported source or Chart mirror", "[bms-program-migration]") {
    auto program = bms_program_t::from_root(branch_id_t{100}, branch_id_t{101});
    REQUIRE(program);
    const auto song = snapshot({.program = std::move(*program), .measures = {measure_id_t{102}}});
    CHECK(reloaded(song).read_chart(difficulty));
}
TEST_CASE("Opaque scoped Text retains non UTF8 bytes and its encoding", "[bms-program-migration]") {
    auto program = bms_program_t::from({
        {.id = branch_id_t{100}, .value = bms_root_scope_t{}, .children = {branch_id_t{101}, branch_id_t{102}}},
        {.id = branch_id_t{101}, .value = bms_chart_content_t{}},
        {.id = branch_id_t{102}, .value = bms_text_t{.bytes = std::string{"\xff\0\x81", 3}, .encoding = text_encoding_t::cp932}}});
    REQUIRE(program);
    const auto song = snapshot({.program = std::move(*program), .measures = {measure_id_t{103}}});
    CHECK(reloaded(song).prepared_bms_programs.front()->prepared().program == song.prepared_bms_programs.front()->prepared().program);
}
TEST_CASE("Scoped preview choices and layer defaults survive native reload", "[bms-program-migration]") {
    auto input = prepared(source);
    const auto selector = selectors(input.program).front();
    input.semantic_layers.push_back({.id = layer_id_t{7100}, .name = "Samples"});
    input.sound_layer_defaults.push_back({.resource = input.resources.front().id, .layers = {layer_id_t{7100}}});
    auto owner = bms_program_document_t::from(difficulty, std::move(input), {.selections = {{selector, 1}}});
    REQUIRE(owner.value);
    const song_document_t song{.id = song_id_t{9001}, .prepared_bms_programs = {*owner.value}};
    const auto read = reloaded(song);
    CHECK(read.prepared_bms_programs.front()->evaluation() == (*owner.value)->evaluation());
    CHECK(read.prepared_bms_programs.front()->prepared().sound_layer_defaults == (*owner.value)->prepared().sound_layer_defaults);
}
TEST_CASE("Native snapshots retain separate fixed selectors and anchored switch SKIP", "[bms-program-migration]") {
    const auto song = snapshot(prepared("#BPM 120\n#WAV01 a.wav\n#SETRANDOM 1\n#RANDOM 2\n#IF 1\n#00011:01\n#ENDIF\n#ENDRANDOM\n"
        "#SETSWITCH 1\n#SWITCH 2\n#CASE 1\n; anchor\n#00112:01\n#SKIP\n#DEF\n#00213:01\n#ENDSW\n"));
    CHECK(reloaded(song).prepared_bms_programs.front()->prepared().program == song.prepared_bms_programs.front()->prepared().program);
}
TEST_CASE("Malformed scoped native graphs never publish a partial document", "[bms-program-migration]") {
    auto root = nlohmann::json::parse(serialized(snapshot(prepared(source))));
    auto& owner = root["document"]["prepared_bms_programs"][0];
    const auto fault = GENERATE(0, 1, 2, 3, 4);
    if (fault == 0) owner["nodes"][1]["id"] = owner["nodes"][0]["id"];
    if (fault == 1) owner["nodes"][0]["children"].push_back(owner["nodes"][0]["id"]);
    if (fault == 2) owner["nodes"][1]["value"]["type"] = "future_unknown";
    if (fault == 3) owner["nodes"][0]["extra"] = true;
    if (fault == 4) owner["audio_assets"][0] = 999U;
    const auto read = parse_native_project(root.dump());
    CHECK_FALSE(read.value);
    REQUIRE_FALSE(read.diagnostics.empty());
    CHECK(read.diagnostics.front().code == native_project_diagnostic_code_t::malformed_json);
}
TEST_CASE("Native payloads cannot introduce a second position authority", "[bms-program-migration]") {
    auto root = nlohmann::json::parse(serialized(with_native_event(3)));
    auto& item = root["document"]["prepared_bms_programs"][0]["nodes"][1]["value"]["data"]["items"].back();
    item["value"]["data"]["payload"]["data"]["position"]["row"] = 10U;
    const auto read = parse_native_project(root.dump());
    CHECK_FALSE(read.value);
    REQUIRE_FALSE(read.diagnostics.empty());
    CHECK(read.diagnostics.front().code == native_project_diagnostic_code_t::malformed_json);
}
TEST_CASE("Scoped native budgets reject without partial output", "[bms-program-migration]") {
    const auto song = snapshot(prepared(source));
    const auto written = write_native_project(song, {.maximum_source_bytes = 64});
    CHECK_FALSE(written.value);
    REQUIRE_FALSE(written.diagnostics.empty());
    CHECK(written.diagnostics.front().code == native_project_diagnostic_code_t::resource_limit);
    const auto read = parse_native_project(serialized(song), {.maximum_json_nodes = 20});
    CHECK_FALSE(read.value);
    REQUIRE_FALSE(read.diagnostics.empty());
    CHECK(read.diagnostics.front().code == native_project_diagnostic_code_t::resource_limit);
}
TEST_CASE("Schema 13 upgrades preserve legacy authority until the explicit cutover", "[bms-program-migration]") {
    const auto history = legacy();
    auto root = nlohmann::json::parse(serialized(history.document));
    root["document"]["schema_version"] = 13U;
    const auto read = parse_native_project(root.dump());
    REQUIRE(read.value);
    CHECK(read.value->schema_version == current_document_schema_version);
    CHECK(read.value->prepared_bms_programs.empty());
    CHECK(read.value->charts.size() == 1);
}
TEST_CASE("Explicit legacy migration retains inactive scopes and is one Undo step", "[bms-program-migration]") {
    auto history = legacy();
    const auto id = history.document.charts.front().id;
    auto proposal = migration_proposal(history, id);
    CHECK(proposal.find_chart(id) == nullptr);
    REQUIRE(proposal.find_bms_program(id));
    CHECK(proposal.find_bms_program(id)->prepared().program.nodes().size() > 6);
    commit_migration(history, std::move(proposal));
    check_migration_history(history, id, reloaded(history.document));
}
TEST_CASE("Legacy native edits and malformed retained data refuse migration without loss", "[bms-program-migration]") {
    const auto fault = GENERATE(0, 1, 2);
    auto history = legacy(fault == 2 ? "#BPM 120\n#WAV01 a.wav\n#RANDOM 2\n#IF 1\n#00011:01\n" : source);
    auto& chart = history.document.charts.front();
    if (fault == 0) chart.measures.front().notes.front().position.row = 1;
    if (fault == 1) history.document.resources.front().name = "native resource edit";
    check_migration_refusal(history, chart.id);
}
TEST_CASE("Scoped recovery and journal post states preserve the semantic owner", "[bms-program-migration]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    auto song = snapshot(prepared(source));
    song.revision = 1;
    command_journal_t journal(directory->path() / "scoped.journal");
    auto after = song;
    after.revision = 2;
    REQUIRE(journal.append({.sequence = 1, .before_revision = 1, .command = "Scoped edit", .snapshot = {.song = after}}));
    CHECK(serialized(journal_snapshot(journal)) == serialized(after));
    const auto read = journal.read();
    const auto replay = read.value.replay_on(song);
    REQUIRE(replay.ok());
    CHECK(serialized(replay.value) == serialized(after));
}
TEST_CASE("Legacy migration preserves other difficulties output groups and shared asset identity", "[bms-program-migration]") {
    auto history = legacy();
    const auto id = history.document.charts.front().id;
    history.document.charts.push_back({.id = chart_id_t{8000}, .metadata = {.name = "Other"},
        .measures = {{.id = measure_id_t{8001}}}});
    history.document.output_groups.push_back({.name = "Both", .charts = {id, chart_id_t{8000}}});
    history.document.audio_assets.front().waveform_cache_path = "cache/retained.wave";
    history.document.metadata.ordered_search_tags = {"preserved"};
    const auto other = serialized(song_document_t{.id = history.document.id, .charts = {history.document.charts.back()}});
    const auto result = migration_proposal(history, id);
    REQUIRE(result.charts.size() == 1);
    CHECK(serialized(song_document_t{.id = history.document.id, .charts = result.charts}) == other);
    CHECK(result.metadata.ordered_search_tags == history.document.metadata.ordered_search_tags);
    check_shared_asset(result, history.document, id);
    CHECK(reloaded(result).output_groups.front().charts == history.document.output_groups.front().charts);
}
TEST_CASE("A scoped snapshot cannot overwrite a conflicting shared asset value", "[bms-program-migration]") {
    auto song = snapshot(prepared(source));
    song.audio_assets = song.prepared_bms_programs.front()->prepared().audio_assets;
    song.audio_assets.front().source_path = "different.wav";
    const auto written = write_native_project(song);
    CHECK_FALSE(written.value);
    REQUIRE_FALSE(written.diagnostics.empty());
    CHECK(written.diagnostics.front().code == native_project_diagnostic_code_t::serialization_failure);
}
TEST_CASE("Inactive scoped enum values are validated by the native codec", "[bms-program-migration]") {
    auto root = nlohmann::json::parse(serialized(snapshot(prepared(source))));
    last_scoped_note(root)["value"]["data"]["kind"] = 999U;
    const auto read = parse_native_project(root.dump());
    CHECK_FALSE(read.value);
    REQUIRE_FALSE(read.diagnostics.empty());
    CHECK(read.diagnostics.front().code == native_project_diagnostic_code_t::invalid_document);
}
TEST_CASE("Legacy note annotations and sound layers migrate to their typed scoped owners", "[bms-program-migration]") {
    auto history = legacy();
    auto& chart = history.document.charts.front();
    chart.semantic_layers.push_back({.id = layer_id_t{5000}, .name = "Annotated"});
    auto& note = chart.measures.front().notes.front();
    note.label = "native annotation";
    note.osu_hitsample = osu_hitsample_t{.clap = true};
    note.semantic_layers = {layer_id_t{5000}};
    chart.sound_layer_defaults.push_back({.resource = *note.sound, .layers = {layer_id_t{5000}}});
    const auto migrated = migration_proposal(history, chart.id);
    const auto* owner = migrated.find_bms_program(chart.id);
    REQUIRE(owner);
    check_native_annotations(note, *owner);
    CHECK_FALSE(reloaded(migrated).prepared_bms_programs.empty());
}
TEST_CASE("Migration identity reservations include native Redo snapshots", "[bms-program-migration]") {
    auto history = legacy();
    const auto id = history.document.charts.front().id;
    document_command_transaction_t transaction(history);
    auto staged = transaction.stage([](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        document.charts.push_back({.id = chart_id_t{600000}, .measures = {{.id = measure_id_t{600001}}}});
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Native chart"}};
    });
    REQUIRE(transaction.finish_execution(std::move(staged)).applied);
    REQUIRE(undo_document_command(history));
    const auto migrated = prepare_native_bms_migration(history, id);
    for (const auto& diagnostic : migrated.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(migrated.value);
    for (const auto& node : migrated.value->find_bms_program(id)->prepared().program.nodes())
        CHECK(node.id.value > 600001);
}
TEST_CASE("Native loading validates exact geometry in inactive scoped alternatives", "[bms-program-migration]") {
    auto root = nlohmann::json::parse(serialized(snapshot(prepared(source))));
    last_scoped_note(root)["value"]["data"]["position"]["measure"] = 999999U;
    const auto read = parse_native_project(root.dump());
    CHECK_FALSE(read.value);
    REQUIRE_FALSE(read.diagnostics.empty());
    CHECK(read.diagnostics.front().code == native_project_diagnostic_code_t::invalid_document);
}
