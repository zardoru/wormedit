#include <wormedit/core/command_journal.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_delta.h>
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>
#include <limits>

using namespace wormedit;
namespace {
auto osbx_song() -> song_document_t {
    osbx_node_t node{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image",
        .ops = {{.id = "pivot", .kind = osbx_transform_kind_t::pivot,
            .children = {{.id = "rotation", .kind = osbx_transform_kind_t::rotate,
                .track = {.default_value = {0}, .keys = {
                    {.id = "turn", .time_ms = -20, .value = {90}, .easing = osbx_osb_easing_t{.value = 2}},
                    {.id = "stop", .time_ms = 20, .value = {180}, .easing = osbx_step_easing_t{}}}}}}}},
        .provenance = {.file = "scene.osbx.lua", .line = 42, .generator_key = "hero"}};
    node.tracks.opacity.keys = {{.id = "fade", .time_ms = 100, .value = {0.5},
        .easing = osbx_cubic_bezier_t{.x1 = 0.1, .y1 = 0.2, .x2 = 0.8, .y2 = 1.0}}};
    node.tracks.visibility.keys = {{.id = "hide", .time_ms = 200, .value = false}};
    node.tracks.blend.keys = {{.id = "add", .time_ms = 50, .value = osbx_blend_t::additive}};
    return {.id = song_id_t{1}, .revision = 1,
        .generator_recipes = {{.id = recipe_id_t{2}}},
        .storyboards = {{.id = storyboard_id_t{3}, .name = "OSBX",
            .osbx = osbx_document_t{.images = {{.id = "image", .file = "sb/image.png"}}, .nodes = {node}},
            .owning_generator_recipe = recipe_id_t{2}}}};
}
auto encoded_osbx_song() -> nlohmann::json {
    const auto written = write_native_project(osbx_song());
    INFO((written.diagnostics.empty() ? "no diagnostic" : written.diagnostics.front().message));
    REQUIRE(written.value);
    return nlohmann::json::parse(*written.value);
}
void require_same_bytes(const song_document_t& actual, const song_document_t& expected) {
    const auto a = write_native_project(actual), b = write_native_project(expected);
    REQUIRE(a.value);
    REQUIRE(b.value);
    CHECK(*a.value == *b.value);
}
void check_invalid_written(const song_document_t& song) {
    const auto written = write_native_project(song);
    CHECK_FALSE(written.value);
    REQUIRE_FALSE(written.diagnostics.empty());
    CHECK(written.diagnostics.front().code == native_project_diagnostic_code_t::invalid_document);
}
void check_invalid_parsed(const nlohmann::json& root,
    native_project_diagnostic_code_t expected = native_project_diagnostic_code_t::invalid_document) {
    const auto parsed = parse_native_project(root.dump());
    CHECK_FALSE(parsed.value);
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().code == expected);
}
enum class journal_record_policy_t { require_delta, allow_seed };
void check_journal_state(const command_journal_read_t& read, const song_document_t& after,
    journal_record_policy_t policy) {
    REQUIRE(read.snapshot);
    CHECK(read.accepted_records == 2);
    REQUIRE(read.positions.size() == 2);
    if (policy == journal_record_policy_t::require_delta) CHECK_FALSE(read.positions[1].is_seed);
    require_same_bytes(read.snapshot->song, after);
}
void require_journal_append(const command_journal_t& journal, const command_journal_entry_t& entry) {
    const auto appended = journal.append(entry);
    INFO((appended ? std::string{"ok"} : appended.error().message));
    REQUIRE(appended);
}
void check_journal_recovery(const song_document_t& before, const song_document_t& after,
    journal_record_policy_t policy = journal_record_policy_t::require_delta) {
    const auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "osbx.journal";
    const command_journal_t journal{path};
    require_journal_append(journal, {.sequence = 1, .before_revision = 1, .command = "Seed", .snapshot = {.song = before}});
    require_journal_append(journal, {.sequence = 2, .before_revision = before.revision, .command = "OSBX key", .snapshot = {.song = after}});
    const auto recovered = command_journal_t{path}.read();
    REQUIRE(recovered.ok());
    check_journal_state(recovered.value, after, policy);
}

}

TEST_CASE("Native OSBX and owner reference are byte stable", "[native-project][osbx]") {
    auto song = osbx_song();
    SECTION("OSBX scene and owner") {}
    SECTION("owner without OSBX") { song.storyboards[0].osbx.reset(); }
    SECTION("OSBX without owner") { song.storyboards[0].owning_generator_recipe.reset(); }
    const auto written = write_native_project(song);
    REQUIRE(written.value);
    const auto parsed = parse_native_project(*written.value);
    INFO((parsed.diagnostics.empty() ? "no diagnostic" : parsed.diagnostics.front().message));
    REQUIRE(parsed.value);
    CHECK(parsed.value->storyboards == song.storyboards);
    require_same_bytes(*parsed.value, song);
}

TEST_CASE("Native clip definitions uses and selectors round trip without compiled data", "[native-project][osbx][clips]") {
    auto song = osbx_song();
    auto& scene = *song.storyboards[0].osbx;
    scene.clips = {{.id = "motion", .ops = {{.id = "clip-op", .track = {
        .default_value = {0, 0}, .parameter = "offset"}}},
        .parameters = {{.name = "offset", .default_value = {1, 2}}}}};
    scene.nodes[0].uses = {{.id = "use", .clip = "motion", .start_ms = -3, .time_scale = 0.5,
        .parameters = {{.name = "offset", .value = {3, 4}}}}};
    const auto written = write_native_project(song); REQUIRE(written.value);
    const auto parsed = parse_native_project(*written.value); REQUIRE(parsed.value);
    require_same_bytes(*parsed.value, song);
    CHECK(parsed.value->revision == song.revision);
    CHECK(written.value->find("compiled") == std::string::npos);
}

TEST_CASE("Native schema35 refuses future clip data but migrates ordinary scenes", "[native-project][osbx][clips]") {
    auto root = encoded_osbx_song();
    root["document"]["schema_version"] = 35;
    SECTION("future definitions") { root["document"]["storyboards"][0]["osbx"]["clips"] = nlohmann::json::array(); }
    SECTION("future uses") { root["document"]["storyboards"][0]["osbx"]["nodes"][0]["uses"] = nlohmann::json::array(); }
    SECTION("ordinary") {
        const auto parsed = parse_native_project(root.dump()); REQUIRE(parsed.value);
        CHECK(parsed.value->schema_version == current_document_schema_version);
        CHECK(parsed.value->storyboards[0].osbx->clips.empty());
        return;
    }
    CHECK_FALSE(parse_native_project(root.dump()).value);
}

TEST_CASE("Native schema28 storyboards migrate with empty OSBX ownership", "[native-project][osbx]") {
    auto root = encoded_osbx_song();
    root["document"]["schema_version"] = 28U;
    auto& storyboard = root["document"]["storyboards"][0];
    storyboard.erase("osbx");
    storyboard.erase("owning_generator_recipe");
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->schema_version == current_document_schema_version);
    CHECK_FALSE(parsed.value->storyboards.front().osbx);
    CHECK_FALSE(parsed.value->storyboards.front().owning_generator_recipe);
    auto expected = osbx_song();
    expected.storyboards.front().osbx.reset();
    expected.storyboards.front().owning_generator_recipe.reset();
    require_same_bytes(*parsed.value, expected);
}

TEST_CASE("Native OSBX rejects unknown fields at every encoded level", "[native-project][osbx]") {
    const auto path = GENERATE("", "/canvas", "/images/0", "/nodes/0", "/nodes/0/provenance",
        "/nodes/0/ops/0", "/nodes/0/ops/0/children/0/track", "/nodes/0/tracks",
        "/nodes/0/tracks/opacity/keys/0", "/nodes/0/tracks/opacity/keys/0/easing",
        "/nodes/0/tracks/opacity/keys/0/easing/data");
    auto root = encoded_osbx_song();
    auto& osbx = root["document"]["storyboards"][0]["osbx"];
    osbx[nlohmann::json::json_pointer(path)]["unknown"] = true;
    const auto parsed = parse_native_project(root.dump());
    CHECK_FALSE(parsed.value);
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().code == native_project_diagnostic_code_t::malformed_json);
}

TEST_CASE("Native invalid OSBX publishes no document or serialized value", "[native-project][osbx]") {
    auto song = osbx_song();
    SECTION("missing image") { song.storyboards[0].osbx->nodes[0].image = "missing"; }
    SECTION("invalid canvas") { song.storyboards[0].osbx->canvas.width = -1; }
    SECTION("invalid key arity") { song.storyboards[0].osbx->nodes[0].tracks.opacity.keys[0].value = {1, 2}; }
    SECTION("dangling recipe") { song.storyboards[0].owning_generator_recipe = recipe_id_t{99}; }
    check_invalid_written(song);
    auto root = encoded_osbx_song();
    if (song.storyboards[0].owning_generator_recipe == recipe_id_t{99})
        root["document"]["storyboards"][0]["owning_generator_recipe"] = 99U;
    else {
        auto& osbx = root["document"]["storyboards"][0]["osbx"];
        osbx["canvas"]["width"] = song.storyboards[0].osbx->canvas.width;
        osbx["nodes"][0]["image"] = *song.storyboards[0].osbx->nodes[0].image;
        osbx["nodes"][0]["tracks"]["opacity"]["keys"][0]["value"] = song.storyboards[0].osbx->nodes[0].tracks.opacity.keys[0].value;
    }
    check_invalid_parsed(root);
}

TEST_CASE("Native OSBX preserves codec budgets", "[native-project][osbx]") {
    const auto root = encoded_osbx_song();
    const auto limits = GENERATE(native_project_limits_t{.maximum_source_bytes = 100},
        native_project_limits_t{.maximum_nesting_depth = 8});
    CHECK_FALSE(parse_native_project(root.dump(), limits).value);
}

TEST_CASE("Native OSBX rejects overflowing integers", "[native-project][osbx]") {
    auto root = encoded_osbx_song();
    const auto field = GENERATE("/nodes/0/order", "/nodes/0/tracks/opacity/keys/0/time_ms",
        "/nodes/0/ops/0/children/0/track/keys/0/easing/data/value");
    root["document"]["storyboards"][0]["osbx"][nlohmann::json::json_pointer(field)] = UINT64_MAX;
    CHECK_FALSE(parse_native_project(root.dump()).value);
}

TEST_CASE("Native OSBX enum numbers reject fractional and narrowing conversions", "[native-project][osbx]") {
    const auto path = GENERATE("/nodes/0/kind", "/nodes/0/layer", "/nodes/0/ops/0/children/0/kind",
        "/nodes/0/tracks/blend/default_value", "/nodes/0/tracks/blend/keys/0/value");
    const auto fractional = GENERATE(false, true);
    CAPTURE(path, fractional);
    auto root = encoded_osbx_song();
    auto& value = root["document"]["storyboards"][0]["osbx"][nlohmann::json::json_pointer(path)];
    const auto original = value.get<std::uint64_t>();
    if (fractional) value = static_cast<double>(original) + 0.5;
    else value = original + (std::uint64_t{1} << 32);
    CHECK_FALSE(parse_native_project(root.dump()).value);
}

TEST_CASE("Native OSBX key edits round trip through deltas and durable journal recovery", "[native-project][osbx]") {
    auto before = osbx_song();
    before.revision = 2;
    // Unchanged retained OSB data makes this a useful journal delta even for
    // this tiny OSBX scene (the journal otherwise prefers a smaller seed).
    before.storyboards.push_back({.id = storyboard_id_t{4},
        .retained_source_lines = {std::string(8192, ' ')}});
    auto after = before;
    after.revision++;
    after.storyboards[0].osbx->nodes[0].tracks.opacity.keys[0].value = {0.25};
    const auto delta = native_project_delta_t::from(before, after);
    INFO((delta ? std::string{"ok"} : delta.error().message));
    REQUIRE(delta);
    const auto decoded = native_project_delta_t::parse(delta->bytes());
    REQUIRE(decoded);
    const auto applied = decoded->apply_on(before);
    REQUIRE(applied.value);
    require_same_bytes(*applied.value, after);
    check_journal_recovery(before, after);
}

TEST_CASE("Native OSBX recovery snapshots retain the document and recipe reference", "[native-project][osbx]") {
    const auto song = osbx_song();
    const auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto path = directory->path() / "osbx.recovery";
    const auto published = create_native_project_recovery_file(path, song, {});
    REQUIRE(published.ok());
    REQUIRE(published.value);
    std::ifstream input(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    const auto recovered = parse_native_project_recovery_snapshot(bytes);
    REQUIRE(recovered.ok());
    REQUIRE(recovered.value);
    require_same_bytes(recovered.value->song, song);
}

namespace {
void erase_osbx_link_defaults(nlohmann::json& value) {
    if (value.is_object()) {
        value.erase("lua"); value.erase("children_link");
        for (auto& child : value) erase_osbx_link_defaults(child);
    } else if (value.is_array()) {
        for (auto& child : value) erase_osbx_link_defaults(child);
    }
}
} // namespace
TEST_CASE("schema29 OSBX records without additive Lua link fields remain readable", "[unit][format][native-project][osbx]") {
    auto encoded = encoded_osbx_song();
    erase_osbx_link_defaults(encoded["document"]["storyboards"][0]["osbx"]);
    const auto loaded = parse_native_project(encoded.dump());
    INFO((loaded.diagnostics.empty() ? "no diagnostic" : loaded.diagnostics.front().message));
    REQUIRE(loaded.value);
    require_same_bytes(*loaded.value,osbx_song());
}

TEST_CASE("Native OSBX lifetimes roundtrip signed bounds deltas and journal recovery", "[native-project][osbx][lifetime]") {
    auto song = osbx_song();
    song.storyboards[0].osbx->nodes[0].lifetime = osbx_lifetime_t{
        .begin_ms = std::numeric_limits<std::int64_t>::min(), .end_ms = std::numeric_limits<std::int64_t>::max()};
    const auto written = write_native_project(song);
    REQUIRE(written.value);
    const auto parsed = parse_native_project(*written.value);
    REQUIRE(parsed.value);
    CHECK(parsed.value->storyboards == song.storyboards);
    auto changed = song;
    changed.revision++;
    changed.storyboards[0].osbx->nodes[0].lifetime = osbx_lifetime_t{.begin_ms = -2, .end_ms = -2};
    const auto delta = native_project_delta_t::from(song, changed);
    INFO((delta ? "delta encoded" : delta.error().message));
    REQUIRE(delta);
    const auto applied = delta->apply_on(song);
    REQUIRE(applied.value);
    require_same_bytes(*applied.value, changed);
    // String-ID node arrays currently use a whole-array delta. The journal may
    // legitimately reseed this small scene under its existing size policy;
    // direct delta replay above proves the new field itself survives a delta.
    check_journal_recovery(song, changed, journal_record_policy_t::allow_seed);
}

TEST_CASE("Native schema29 OSBX lifetimes migrate as unbounded", "[native-project][osbx][lifetime]") {
    auto root = encoded_osbx_song();
    root["document"]["schema_version"] = 29U;
    root["document"]["storyboards"][0]["osbx"]["nodes"][0].erase("lifetime");
    const auto parsed = parse_native_project(root.dump());
    REQUIRE(parsed.value);
    CHECK(parsed.value->schema_version == current_document_schema_version);
    CHECK_FALSE(parsed.value->storyboards[0].osbx->nodes[0].lifetime);
}

TEST_CASE("Native OSBX rejects malformed unknown and out-of-range lifetime fields", "[native-project][osbx][lifetime]") {
    const auto invalid = GENERATE(0, 1, 2, 3, 4, 5);
    auto root = encoded_osbx_song();
    auto& range = root["document"]["storyboards"][0]["osbx"]["nodes"][0]["lifetime"];
    range = {{"begin_ms", -2}, {"end_ms", 2}};
    switch (invalid) {
    case 0: range["extra"] = true; break;
    case 1: range["begin_ms"] = 3; break;
    case 2: range["end_ms"] = 2.5; break;
    case 3: range["end_ms"] = std::numeric_limits<std::uint64_t>::max(); break;
    case 4: range.erase("end_ms"); break;
    case 5: range = "not a range"; break;
    }
    const auto parsed = parse_native_project(root.dump());
    CHECK_FALSE(parsed.value);
    CHECK_FALSE(parsed.diagnostics.empty());
}

TEST_CASE("Native schema36 accepts explicit empty clip defaults without admitting unknown fields", "[native-project][osbx][clips]") {
    auto root = encoded_osbx_song();
    auto& scene = root["document"]["storyboards"][0]["osbx"];
    scene["clips"] = nlohmann::json::array();
    scene["nodes"][0]["uses"] = nlohmann::json::array();
    scene["nodes"][0]["tracks"]["opacity"]["parameter"] = nullptr;
    CHECK(parse_native_project(root.dump()).value);
    scene["nodes"][0]["unknown"] = nullptr;
    CHECK_FALSE(parse_native_project(root.dump()).value);
}

TEST_CASE("Native schema38 preserves temporal programs and refuses old37 smuggling", "[native-project][osbx][program]") {
    auto song = osbx_song();
    song.storyboards[0].osbx->nodes[0].tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat", .start_ms = -10, .count = 3,
        .keys = {{.id = "repeat-first", .value = {0}}, {.id = "repeat-last", .time_ms = 10, .value = {1}}}}}};
    const auto encoded = write_native_project(song);
    REQUIRE(encoded.value);
    const auto parsed = parse_native_project(*encoded.value);
    REQUIRE(parsed.value);
    CHECK(parsed.value->storyboards == song.storyboards);
    auto root = nlohmann::json::parse(*encoded.value);
    root["document"]["schema_version"] = 37;
    CHECK_FALSE(parse_native_project(root.dump()).value);
}

namespace {
auto native_program_json() -> nlohmann::json {
    auto song = osbx_song();
    song.storyboards[0].osbx->nodes[0].tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat",
        .keys = {{.id = "relative", .value = {0.5}}}}}};
    const auto encoded = write_native_project(song); REQUIRE(encoded.value);
    return nlohmann::json::parse(*encoded.value);
}
auto native_opacity(nlohmann::json& root) -> nlohmann::json& {
    return root["document"]["storyboards"][0]["osbx"]["nodes"][0]["tracks"]["opacity"];
}
}
TEST_CASE("Native old37 refuses even empty null temporal future fields", "[native-project][osbx][program]") {
    auto root = native_program_json();
    root["document"]["schema_version"] = 37;
    SECTION("empty") { native_opacity(root)["program"] = nlohmann::json::array(); }
    SECTION("null") { native_opacity(root)["program"] = nullptr; }
    CHECK_FALSE(parse_native_project(root.dump()).value);
}
TEST_CASE("Native program shape integers and descendant keys remain strict", "[native-project][osbx][program]") {
    auto root = native_program_json();
    auto& block = native_opacity(root)["program"][0];
    SECTION("fractional count") { block["count"] = 1.5; }
    SECTION("overflow count") { block["count"] = UINT64_MAX; }
    SECTION("zero count") { block["count"] = 0; }
    SECTION("fractional start") { block["start_ms"] = -0.5; }
    SECTION("negative relative") { block["keys"][0]["time_ms"] = -1; }
    SECTION("unknown shape") { block["unknown"] = nullptr; }
    SECTION("unknown kind") { block["kind"] = "recursive"; }
    CHECK_FALSE(parse_native_project(root.dump()).value);
}
TEST_CASE("Native schema37 ordinary records migrate without invented programs", "[native-project][osbx][program]") {
    auto root = native_program_json();
    native_opacity(root).erase("program"); root["document"]["schema_version"] = 37;
    const auto parsed = parse_native_project(root.dump()); REQUIRE(parsed.value);
    CHECK(parsed.value->schema_version == current_document_schema_version);
    CHECK(parsed.value->storyboards[0].osbx->nodes[0].tracks.opacity.program.empty());
}
TEST_CASE("Native temporal loop start and trigger window preserve full signed boundaries", "[native-project][osbx][program]") {
    auto song = osbx_song();
    auto& track = song.storyboards[0].osbx->nodes[0].tracks.opacity;
    track.program = {{.data = osbx_loop_t<>{.id = "low", .start_ms = INT64_MIN, .count = UINT32_MAX,
        .keys = {{.id = "constant", .value = {0.5}}}}},
        {.data = osbx_trigger_t<>{.id = "activation", .trigger = {.name = *storyboard_trigger_name_t::from("Passing"),
            .start_time_ms = INT64_MIN, .end_time_ms = INT64_MAX, .group_number = 0},
            .keys = {{.id = "activated", .value = {0.75}}}}}};
    const auto encoded = write_native_project(song); REQUIRE(encoded.value);
    const auto parsed = parse_native_project(*encoded.value); REQUIRE(parsed.value);
    CHECK(parsed.value->storyboards == song.storyboards);
    const auto root = nlohmann::json::parse(*encoded.value);
    CHECK(root.dump().find("scenario") == std::string::npos);
}

TEST_CASE("Native OSBX media schema39 roundtrips canonical metadata and preserves absent placeholders", "[native-project][osbx][media]") {
    auto song = osbx_song(); auto& node = song.storyboards[0].osbx->nodes[0];
    node.kind = osbx_node_kind_t::animation; node.animation = storyboard_animation_t{.frame_count = 3, .frame_delay_ms = 12.5};
    const auto written = write_native_project(song); REQUIRE(written.value);
    const auto parsed = parse_native_project(*written.value); REQUIRE(parsed.value);
    require_same_bytes(*parsed.value, song);
    auto old = nlohmann::json::parse(*written.value); old["document"]["schema_version"] = 38;
    CHECK_FALSE(parse_native_project(old.dump()).value);
    old["document"]["storyboards"][0]["osbx"]["nodes"][0].erase("animation");
    CHECK(parse_native_project(old.dump()).value.has_value());
}

TEST_CASE("Native old OSBX schemas reject even empty future media metadata", "[native-project][osbx][media]") {
    auto root = encoded_osbx_song();
    const auto version = GENERATE(37, 38);
    const auto* field = GENERATE("animation", "sample");
    root["document"]["schema_version"] = version;
    root["document"]["storyboards"][0]["osbx"]["nodes"][0][field] = nullptr;
    CHECK_FALSE(parse_native_project(root.dump()).value);
    root["document"]["storyboards"][0]["osbx"]["nodes"][0][field] = nlohmann::json::object();
    CHECK_FALSE(parse_native_project(root.dump()).value);
}
TEST_CASE("Native complete OSBX Samples validate strict numeric and filename metadata", "[native-project][osbx][media]") {
    auto song = osbx_song(); auto& node = song.storyboards[0].osbx->nodes[0];
    node.kind = osbx_node_kind_t::sample; node.image.reset(); node.ops.clear(); node.tracks = {};
    node.sample = osbx_sample_t{.time_ms = INT64_MIN, .path = "tone.wav", .volume = 100};
    const auto written = write_native_project(song); REQUIRE(written.value);
    const auto parsed = parse_native_project(*written.value); REQUIRE(parsed.value);
    require_same_bytes(*parsed.value, song);
    auto root = nlohmann::json::parse(*written.value);
    auto& metadata = root["document"]["storyboards"][0]["osbx"]["nodes"][0]["sample"];
    SECTION("fractional time") { metadata["time_ms"] = 1.5; }
    SECTION("volume range") { metadata["volume"] = 0; }
    SECTION("unknown field") { metadata["layer"] = 0; }
    SECTION("unsafe filename") { metadata["path"] = "tone,other.wav"; }
    check_invalid_parsed(root, native_project_diagnostic_code_t::malformed_json);
}
