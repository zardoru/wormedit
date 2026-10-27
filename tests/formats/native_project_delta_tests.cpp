#include <wormedit/formats/native_project_delta.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/native_project_storage.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/command.h>
#include <wormedit/core/timing_links.h>
#include "../support/timing_links_fixture.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>

using namespace wormedit;
namespace {
auto delta_song() -> song_document_t {
    chart_document_t chart{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3},
        .notes = {{.id = object_id_t{11}, .position = {measure_id_t{3}, 12}, .lane = lane_id_t{1}},
                  {.id = object_id_t{12}, .position = {measure_id_t{3}, 24}, .lane = lane_id_t{1}}}}}};
    chart.lane_layouts.push_back({.id = object_id_t{10}, .position = {measure_id_t{3}, 0},
        .layout = {.lanes = {lane_definition_t::from(1, 0, stage_group_t::none)}}});
    return {.id = song_id_t{1}, .revision = 1, .charts = {std::move(chart)}};
}
void require_canonical_equal(const song_document_t& actual, const song_document_t& expected) {
    const auto a = write_native_project(actual), b = write_native_project(expected);
    REQUIRE(a.value);
    REQUIRE(b.value);
    CHECK(*a.value == *b.value);
}
void require_encoded_round_trip(const song_document_t& before, const song_document_t& after,
    const native_project_delta_t& delta) {
    const auto seed = write_native_project(before);
    REQUIRE(seed.value);
    auto replay = native_project_replay_t::from(*seed.value);
    REQUIRE(replay);
    REQUIRE(replay->apply(delta));
    const auto final_state = replay->finish();
    REQUIRE(final_state.ok());
    REQUIRE(final_state.value);
    require_canonical_equal(*final_state.value, after);
}
void require_delta_round_trip(const song_document_t& before, const song_document_t& after) {
    const auto delta = native_project_delta_t::from(before, after);
    INFO((delta ? std::string{delta->bytes()} : delta.error().message));
    REQUIRE(delta);
    const auto decoded = native_project_delta_t::parse(delta->bytes());
    REQUIRE(decoded);
    const auto applied = decoded->apply_on(before);
    INFO((applied.diagnostics.empty() ? "no diagnostic" : applied.diagnostics.front().message));
    REQUIRE(applied.ok());
    REQUIRE(applied.value);
    require_canonical_equal(*applied.value, after);
    require_encoded_round_trip(before, after, *decoded);
}
void require_delta_both_directions(const song_document_t& before, const song_document_t& after) {
    require_delta_round_trip(before, after);
    require_delta_round_trip(after, before);
}
auto conditional_song() -> song_document_t {
    const std::string source = "#TITLE Branches\r\n#BPM 120\r\n#WAV01 sound.wav\r\n"
        "#RANDOM 2\r\n#IF 1\r\n#00111:01\r\n#ELSEIF 2\r\n#00111:000100\r\n#ENDIF\r\n#ENDRANDOM\r\n";
    auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
        {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(parsed.value.song);
    return std::move(*parsed.value.song);
}
void replace_program(song_document_t& song, bms_prepared_program_t prepared, bms_program_evaluation_t evaluation) {
    auto owner = bms_program_document_t::from(song.prepared_bms_programs.front()->difficulty(), std::move(prepared), std::move(evaluation));
    REQUIRE(owner.ok());
    REQUIRE(owner.value);
    song.prepared_bms_programs.front() = *owner.value;
}
void edit_inactive_note(song_document_t& after) {
    const auto& owner = *after.prepared_bms_programs.front();
    auto prepared = owner.prepared();
    std::vector<bms_program_node_t> nodes(prepared.program.nodes().begin(), prepared.program.nodes().end());
    auto found = std::ranges::find_if(nodes, [](const auto& node) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        return chart && std::ranges::any_of(chart->items, [](const auto& item) {
            const auto* note = std::get_if<bms_note_content_t>(&item.value);
            return note && note->position.fraction == rational_t{1, 3};
        });
    });
    REQUIRE(found != nodes.end());
    auto& items = std::get<bms_chart_content_t>(found->value).items;
    auto note = std::ranges::find_if(items, [](const auto& item) { return std::holds_alternative<bms_note_content_t>(item.value); });
    REQUIRE(note != items.end());
    std::get<bms_note_content_t>(note->value).attributes.label = "Inactive accepted value";
    note->provenance.source_spelling = "retained exact bytes";
    auto program = bms_program_t::from(std::move(nodes));
    REQUIRE(program);
    prepared.program = std::move(*program);
    prepared.source.unchanged_since_import = false;
    replace_program(after, std::move(prepared), owner.evaluation());
}

}

TEST_CASE("Native deltas preserve note identities values and explicit collection order", "[native][delta]") {
    const auto before = delta_song();
    auto after = before;
    auto& notes = after.charts.front().measures.front().notes;
    SECTION("move resize annotate and assign exact native values") {
        notes.front().position.row = 20;
        notes.front().kind = note_kind_t::hold;
        notes.front().end = local_position_t{measure_id_t{3}, 30};
        notes.front().label = "quoted \" label\n";
        notes.front().provenance = {.family = format_family_t::osu_mania, .source_spelling = "retained", .source_order = 99};
    }
    SECTION("insert remove and reorder") {
        notes.erase(notes.begin());
        notes.insert(notes.begin(), {.id = object_id_t{13}, .position = {measure_id_t{3}, 1}, .lane = lane_id_t{1}});
        std::ranges::reverse(notes);
    }
    SECTION("move across measures") {
        auto note = notes.front();
        notes.erase(notes.begin());
        note.position.measure = measure_id_t{4};
        after.charts.front().measures.push_back({.id = measure_id_t{4}});
        after.charts.front().measures.back().notes.push_back(note);
    }
    SECTION("measure definition and measure order") {
        after.charts.front().measures.front().definition.length = {3, 2};
        after.charts.front().measures.front().definition.meter = meter_t{6, 4};
        after.charts.front().measures.push_back({.id = measure_id_t{4}});
    }
    ++after.revision;
    require_delta_both_directions(before, after);
    const auto delta = native_project_delta_t::from(before, after);
    REQUIRE(delta);
    const auto patch = nlohmann::json::parse(delta->bytes()).at("document");
    CHECK(patch.at("fields").at("charts").at("key") == "id");
    CHECK(patch.at("fields").at("charts").at("update").front().at("id") == 2);
}

TEST_CASE("Native deltas cover timing layouts layers bookmarks and retained channels", "[native][delta]") {
    const auto before = delta_song();
    auto after = before;
    auto& chart = after.charts.front();
    const local_position_t at{measure_id_t{3}, 0};
    SECTION("timing") {
        chart.bpms.push_back({.id = object_id_t{20}, .position = at, .beats_per_minute = {123, 1}});
        chart.pauses.push_back({.id = object_id_t{21}, .position = at, .duration = {1, 7}});
        chart.scrolls.push_back({.id = object_id_t{22}, .position = at, .multiplier = {3, 2}});
        chart.speeds.push_back({.id = object_id_t{23}, .position = at, .multiplier = {2, 1}});
        chart.kiai.push_back({.id = object_id_t{24}, .position = at, .enabled = true});
        chart.timing_anchors.push_back({.id = object_id_t{25}, .position = at});
        chart.warps.push_back({.id = object_id_t{26}, .span = {at, {measure_id_t{3}, 2}}});
    }
    SECTION("layout and layers") {
        chart.lane_layouts.front().layout.display_name = "Changed";
        chart.semantic_layers.push_back({.id = layer_id_t{50}, .name = "Keys"});
        chart.measures.front().notes.front().semantic_layers = {layer_id_t{50}};
        chart.input_profile.profile_id = "custom";
    }
    SECTION("bookmarks sections and BGM") {
        chart.bookmarks.push_back({.id = object_id_t{20}, .label = "Bookmark", .position = at});
        chart.sections.push_back({.id = object_id_t{21}, .label = "Section", .beat_span = position_span_t{at, {measure_id_t{3}, 48}}});
        chart.bgm_source_lanes.push_back({.id = object_id_t{22}, .name = "BGM"});
        chart.measures.front().bgm_events.push_back({.id = object_id_t{23}, .position = at, .source_lane = object_id_t{22}});
        after.bookmark_orders.push_back({.chart = chart.id, .bookmarks = {object_id_t{20}}});
    }
    SECTION("SV recipes") {
        chart.sv_recipes.push_back({.id = recipe_id_t{30}, .name = "Ramp", .range = {at, {measure_id_t{3}, 48}}});
    }
    SECTION("retained sound channel") {
        after.audio_assets.push_back({.id = asset_id_t{60}, .source_path = "stem.ogg"});
        chart.sound_channels.push_back({.id = object_id_t{20}, .name = "Stem",
            .source_clip = {.id = clip_id_t{70}, .source = asset_id_t{60}},
            .events = {{.id = object_id_t{21}, .position = at, .lane = lane_id_t{1}}}});
    }
    ++after.revision;
    require_delta_both_directions(before, after);
}

TEST_CASE("Native deltas preserve all project catalogs and fields without stable identities", "[native][delta]") {
    const auto before = delta_song();
    auto after = before;
    SECTION("assets resources groups and visual events") {
        after.audio_assets.push_back({.id = asset_id_t{60}, .source_path = "sound.wav"});
        after.visual_assets.push_back({.id = asset_id_t{61}, .source_path = "image.png"});
        after.sound_groups.push_back({.id = sound_group_id_t{62}, .name = "Drums"});
        after.resources.push_back({.id = resource_id_t{63}, .name = "Sound", .asset = asset_id_t{60},
            .roles = {resource_role_t::sample}, .tags = {"kick"}, .sound_group = sound_group_id_t{62}});
        after.resources.push_back({.id = resource_id_t{64}, .name = "Image", .asset = asset_id_t{61}});
        after.charts.front().measures.front().notes.front().sound = resource_id_t{63};
        after.charts.front().bga_events.push_back({.id = object_id_t{20}, .position = {measure_id_t{3}, 0}, .resource = resource_id_t{64}});
    }
    SECTION("MIDI and storyboards") {
        after.midi_assets.push_back({.id = asset_id_t{60}, .source_path = "music.mid", .ticks_per_quarter = 480});
        after.storyboards.push_back({.id = storyboard_id_t{61}, .name = "Storyboard", .retained_source_lines = {"retained"}});
    }
    SECTION("metadata settings output groups presets cuts and chart order") {
        after.metadata.title = "Project";
        after.metadata.ordered_search_tags = {"one", "two"};
        after.settings.default_snap = 16;
        after.settings.audio_asset_directory = "audio";
        after.output_groups.push_back({.name = "Native", .charts = {chart_id_t{2}}});
        after.quantization_presets.push_back({.name = "Triplets", .divisions = 3});
        after.export_cuts.push_back({.id = object_id_t{60}, .label = "Preview", .end_milliseconds = 1000});
        after.chart_order = {chart_id_t{2}};
    }
    SECTION("chart addition and deletion") {
        auto chart = after.charts.front();
        chart.id = chart_id_t{4};
        chart.metadata.name = "Duplicate";
        after.charts.push_back(std::move(chart));
        std::ranges::reverse(after.charts);
    }
    SECTION("generator source and shared recipe definitions") {
        const std::string source = "return {notes={}}";
        after.generator_recipes.push_back({.id = recipe_id_t{60}, .api_version = "1", .source_path = "generate.lua",
            .source_hash = native_project_content_checksum(source), .source = source, .chart = chart_id_t{2}});
        after.sv_recipe_definitions.push_back(sv_recipe_definition_t::from(recipe_id_t{61}, {.name = "Shared ramp"}));
    }
    ++after.revision;
    require_delta_both_directions(before, after);
}

TEST_CASE("Native deltas recover inactive scoped BMS content exact fractions and source provenance", "[native][delta][bms]") {
    const auto before = conditional_song();
    auto after = before;
    edit_inactive_note(after);
    ++after.revision;
    require_delta_both_directions(before, after);
    SECTION("shared catalog edits with the identical immutable owner") {
        auto catalog = before;
        REQUIRE_FALSE(catalog.resources.empty());
        catalog.resources.front().name = "Renamed catalog entry";
        ++catalog.revision;
        CHECK(catalog.prepared_bms_programs.front() == before.prepared_bms_programs.front());
        require_delta_both_directions(before, catalog);
    }
}

TEST_CASE("Native deltas preserve linked propagation Undo Redo and divergent edits", "[native][delta][timing-links]") {
    auto fixture = test::timing_links_fixture_t::from();
    const auto before = fixture.history.document;
    REQUIRE(fixture.set_bpm(fixture.source_id, {150, 1}).applied);
    const auto changed = fixture.history.document;
    require_delta_both_directions(before, changed);
    REQUIRE(undo_document_command(fixture.history));
    require_delta_round_trip(changed, fixture.history.document);
    REQUIRE(redo_document_command(fixture.history));
    require_delta_round_trip(before, fixture.history.document);
    REQUIRE(undo_document_command(fixture.history));
    REQUIRE(fixture.set_bpm(fixture.source_id, {160, 1}).applied);
    require_delta_round_trip(before, fixture.history.document);
}

TEST_CASE("Native delta decoding bounds bytes nodes depth and duplicate keys", "[native][delta]") {
    const auto before = delta_song();
    auto after = before;
    ++after.revision;
    const auto delta = native_project_delta_t::from(before, after);
    REQUIRE(delta);
    CHECK_FALSE(native_project_delta_t::parse(delta->bytes(), {.maximum_source_bytes = 1}));
    CHECK_FALSE(native_project_delta_t::parse(delta->bytes(), {.maximum_json_nodes = 1}));
    CHECK_FALSE(native_project_delta_t::parse(delta->bytes(), {.maximum_nesting_depth = 1}));
    CHECK_FALSE(native_project_delta_t::parse("{\"document\":{},\"document\":{},\"schema_version\":26}"));
}

TEST_CASE("Native delta decoding rejects ambiguous operations and respects codec budgets", "[native][delta]") {
    const auto before = delta_song();
    auto after = before;
    ++after.revision;
    after.charts.front().measures.front().notes.front().position.row = 20;
    const auto delta = native_project_delta_t::from(before, after);
    REQUIRE(delta);
    auto root = nlohmann::json::parse(delta->bytes());
    auto& charts = root["document"]["fields"]["charts"];
    SECTION("missing update target") { charts["update"][0]["id"] = std::uint64_t{999}; }
    SECTION("duplicate operation") { charts["update"].push_back(charts["update"].front()); }
    SECTION("incomplete order") { charts["order"] = nlohmann::json::array(); }
    SECTION("duplicate order identity") { charts["order"] = {2U, 2U}; }
    SECTION("fractional order identity") { charts["order"] = {2.5}; }
    SECTION("unknown operation") { charts["unknown"] = true; }
    SECTION("identity mutation") { charts["update"][0]["patch"]["fields"]["id"] = {{"set", 999U}}; }
    const auto malformed = native_project_delta_t::parse(root.dump());
    REQUIRE(malformed);
    CHECK_FALSE(malformed->apply_on(before).value);
    require_canonical_equal(before, delta_song());
}

TEST_CASE("Native deltas key generated object witnesses by their embedded stable identity", "[native][delta][generator]") {
    auto before = delta_song();
    const std::string source = "return {notes={}}";
    generator_recipe_t recipe{.id = recipe_id_t{60}, .api_version = "1", .source_path = "generate.lua",
        .source_hash = native_project_content_checksum(source), .source = source, .chart = chart_id_t{2}};
    const auto& notes = before.charts.front().measures.front().notes;
    recipe.generated_notes = {{"first", notes.front()}, {"second", notes.back()}};
    before.generator_recipes.push_back(std::move(recipe));
    auto after = before;
    ++after.revision;
    after.generator_recipes.front().generated_notes.front().value.label = "Accepted witness";
    std::ranges::reverse(after.generator_recipes.front().generated_notes);
    require_delta_both_directions(before, after);
    const auto delta = native_project_delta_t::from(before, after);
    REQUIRE(delta);
    const auto root = nlohmann::json::parse(delta->bytes());
    const auto& patch = root.at("document").at("fields").at("generator_recipes").at("update").front().at("patch");
    CHECK(patch.at("fields").at("generated_notes").at("key") == "value/id");
}

TEST_CASE("Native deltas preserve storyboard sprite stable identities and order", "[native][delta][storyboard]") {
    auto before = delta_song();
    storyboard_document_model_t model;
    model.sprites = {{.stable_id = 1, .layer = "Background", .origin = "Centre", .path = "a.png"},
        {.stable_id = 2, .layer = "Background", .origin = "Centre", .path = "b.png"}};
    before.storyboards.push_back({.id = storyboard_id_t{60}, .name = "Scene", .model = std::move(model)});
    auto after = before;
    ++after.revision;
    auto& sprites = after.storyboards.front().model->sprites;
    sprites.front().x = 320;
    std::ranges::reverse(sprites);
    require_delta_both_directions(before, after);
    const auto delta = native_project_delta_t::from(before, after);
    REQUIRE(delta);
    const auto root = nlohmann::json::parse(delta->bytes());
    const auto& patch = root.at("document").at("fields").at("storyboards").at("update").front().at("patch");
    CHECK(patch.at("fields").at("model").at("fields").at("sprites").at("key") == "stable_id");
}

TEST_CASE("Native deltas add and remove complete scoped BMS difficulties", "[native][delta][bms]") {
    const auto before = conditional_song();
    auto after = before;
    after.prepared_bms_programs.clear();
    after.chart_order.clear();
    ++after.revision;
    require_delta_both_directions(before, after);
}

TEST_CASE("Native deltas upsert and remove scoped BMS nodes and their ordered children", "[native][delta][bms]") {
    const auto before = conditional_song();
    auto after = before;
    const auto& owner = *before.prepared_bms_programs.front();
    auto prepared = owner.prepared();
    const auto root = prepared.program.root();
    const auto changed = prepared.program.with_node(root, prepared.program.find(root)->children.back(),
        {.id = branch_id_t{1'000'000}, .value = bms_bare_scope_t{}}, branch_id_t{1'000'001});
    REQUIRE(changed);
    prepared.program = *changed;
    prepared.source.unchanged_since_import = false;
    replace_program(after, std::move(prepared), owner.evaluation());
    ++after.revision;
    require_delta_both_directions(before, after);
}

TEST_CASE("Native deltas preserve signed zero in persisted storyboard numbers", "[native][delta][storyboard]") {
    auto before = delta_song();
    storyboard_document_model_t model;
    model.sprites.push_back({.stable_id = 1, .layer = "Background", .origin = "Centre", .path = "a.png"});
    before.storyboards.push_back({.id = storyboard_id_t{60}, .name = "Scene", .model = std::move(model)});
    auto after = before;
    ++after.revision;
    after.storyboards.front().model->sprites.front().x = -0.0;
    require_delta_both_directions(before, after);
}

TEST_CASE("Native scoped deltas preserve content insertion removal and explicit order", "[native][delta][bms]") {
    const auto before = conditional_song();
    auto after = before;
    const auto& owner = *before.prepared_bms_programs.front();
    auto prepared = owner.prepared();
    std::vector<bms_program_node_t> nodes(prepared.program.nodes().begin(), prepared.program.nodes().end());
    bool changed{};
    for (auto& node : nodes) {
        auto* body = std::get_if<bms_chart_content_t>(&node.value);
        if (!body || body->items.empty() || changed) continue;
        SECTION("insert opaque content") {
            body->items.push_back({.id = object_id_t{1'000'000}, .value = bms_text_t{.bytes = "new opaque line\r\n"}});
        }
        SECTION("remove first contribution") { body->items.erase(body->items.begin()); }
        SECTION("reverse ordered contributions") { std::ranges::reverse(body->items); }
        changed = true;
    }
    REQUIRE(changed);
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE(program);
    prepared.program = *program;
    prepared.source.unchanged_since_import = false;
    replace_program(after, std::move(prepared), owner.evaluation());
    ++after.revision;
    require_delta_both_directions(before, after);
}

namespace {
void require_replay_rollback(native_project_replay_t& replay, const nlohmann::json& patch,
    const native_project_delta_t& good, const song_document_t& before, const song_document_t& after) {
    const auto bad = native_project_delta_t::parse(patch.dump());
    REQUIRE(bad);
    CHECK_FALSE(replay.apply(*bad));
    const auto unchanged = replay.finish();
    REQUIRE(unchanged.value);
    require_canonical_equal(*unchanged.value, before);
    REQUIRE(replay.apply(good));
    const auto changed = replay.finish();
    REQUIRE(changed.value);
    require_canonical_equal(*changed.value, after);
}
}

TEST_CASE("Encoded native replay rolls back partial patches and codec budget failures", "[native][delta]") {
    const auto before = delta_song();
    const auto seed = write_native_project(before);
    REQUIRE(seed.value);
    auto replay = native_project_replay_t::from(*seed.value, {.maximum_source_bytes = seed.value->size() + 20});
    REQUIRE(replay);
    auto after = before;
    ++after.revision;
    after.metadata.title = "accepted";
    const auto good = native_project_delta_t::from(before, after);
    REQUIRE(good);
    auto patch = nlohmann::json::parse(good->bytes());
    SECTION("unknown operation after a changed field") {
        patch["document"]["fields"]["zz_invalid"] = {{"unknown", true}};
    }
    SECTION("collection identity mutation after a changed field") {
        patch["document"]["fields"]["charts"] = {{"key", "id"}, {"update", {{{"id", 2U},
            {"patch", {{"fields", {{"id", {{"set", 99U}}}}}}}}}}};
    }
    SECTION("replacement exceeds the accumulated byte budget") {
        patch["document"]["fields"]["metadata"]["fields"]["title"]["set"] = std::string(1000, 'z');
    }
    require_replay_rollback(*replay, patch, *good, before, after);
}
