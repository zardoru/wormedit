#include <wormedit/core/osbx_edit.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/command_journal.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_delta.h>
#include <wormedit/formats/native_project_storage.h>
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>
#include <catch2/catch_approx.hpp>
#include <source_location>

using namespace wormedit;

namespace {
// Keep Catch's assertion branching at one test boundary, retaining each
// caller's location in failures (the existing OSBX link tests use this pattern).
template<bool Expected = true, class Value>
void check_edit(const Value& value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    CHECK(static_cast<bool>(value) == Expected);
}
template<bool Expected = true, class Value>
void require_edit(const Value& value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    REQUIRE(static_cast<bool>(value) == Expected);
}
constexpr storyboard_id_t storyboard{3};
auto edit_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1,
        .storyboards = {{.id = storyboard, .name = "Scene", .osbx = osbx_document_t{
            .images = {{.id = "image", .file = "sb/image.png"}, {.id = "spare", .file = "sb/spare.png"}},
            .nodes = {{.id = "group"},
                {.id = "sprite", .kind = osbx_node_kind_t::sprite, .parent = "group", .order = 1, .image = "image",
                    .ops = {{.id = "pivot", .kind = osbx_transform_kind_t::pivot,
                        .children = {{.id = "rotate", .kind = osbx_transform_kind_t::rotate,
                            .track = {.default_value = {0}, .keys = {
                                {.id = "turn", .time_ms = 0, .value = {0}},
                                {.id = "stop", .time_ms = 100, .value = {90}}}}}}},
                        {.id = "translate"}},
                    .tracks = {.opacity = {.default_value = {1}, .keys = {
                        {.id = "fade", .time_ms = 0, .value = {0}},
                        {.id = "bright", .time_ms = 100, .value = {1}}}}}},
                {.id = "other", .order = 2},
                {.id = "front", .layer = osbx_layer_t::foreground}}}}}};
}
auto bytes(song_document_t song) -> std::string {
    // History revisions advance on Undo/Redo; native content equality excludes that clock.
    song.revision = 1;
    const auto written = write_native_project(song);
    std::string diagnostics;
    for (const auto& diagnostic : written.diagnostics) diagnostics += diagnostic.message + "\n";
    INFO(diagnostics);
    require_edit(written.value);
    return *written.value;
}
auto round_trip(const osbx_edit_change_t& change) -> document_history_t {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard, .change = change});
    for (const auto& diagnostic : result.diagnostics) INFO(diagnostic.message);
    require_edit(result.applied);
    require_edit(history.undo.size() == 1);
    const auto after = bytes(history.document);
    check_edit(after != before);
    require_edit(undo_document_command(history));
    check_edit(bytes(history.document) == before);
    require_edit(redo_document_command(history));
    check_edit(bytes(history.document) == after);
    return history;
}
auto scene(const document_history_t& history) -> const osbx_document_t& {
    return *history.document.storyboards.front().osbx;
}
auto sprite(const document_history_t& history) -> const osbx_node_t& { return scene(history).nodes[1]; }
void reject_edit(document_history_t& history, const osbx_edit_t& edit,
                 diagnostic_code_t code = diagnostic_code_t::invalid_binding) {
    const auto before = history.document;
    const auto undo_count = history.undo.size(), redo_count = history.redo.size();
    const auto result = apply_osbx_edit(history, edit);
    check_edit<false>(result.applied);
    require_edit<false>(result.diagnostics.empty());
    check_edit(result.diagnostics.front().code == code);
    check_edit(bytes(history.document) == bytes(before));
    check_edit(history.document.revision == before.revision);
    check_edit(history.undo.size() == undo_count);
    check_edit(history.redo.size() == redo_count);
}
void reject_change(const osbx_edit_change_t& change) {
    auto history = document_history_t::from(edit_song());
    reject_edit(history, {.storyboard = storyboard, .change = change});
}
const osbx_track_target_t rotation{.node = "sprite", .kind = osbx_track_kind_t::transform, .op = "rotate"};
const osbx_track_target_t opacity{.node = "sprite", .kind = osbx_track_kind_t::opacity};
} // namespace

TEST_CASE("OSBX create storyboard allocates a history-safe identity", "[osbx-edit]") {
    auto history = round_trip(osbx_create_storyboard_t{.name = "New"});
    require_edit(history.document.storyboards.size() == 2);
    const auto id = history.document.storyboards.back().id;
    check_edit(history.document.storyboards.back().name == "New");
    require_edit(undo_document_command(history));
    require_edit(apply_osbx_edit(history, {.change = osbx_create_storyboard_t{.name = "Replacement"}}));
    check_edit(history.document.storyboards.back().id.value > id.value);
}
TEST_CASE("OSBX delete storyboard is one reversible command", "[osbx-edit]") {
    const auto history = round_trip(osbx_delete_storyboard_t{});
    check_edit(history.document.storyboards.empty());
}
TEST_CASE("OSBX rename storyboard", "[osbx-edit]") {
    check_edit(round_trip(osbx_rename_storyboard_t{.name = "Renamed"}).document.storyboards.front().name == "Renamed");
}
TEST_CASE("OSBX add groups and sprites uses deterministic parent IDs", "[osbx-edit]") {
    const auto kind = GENERATE(osbx_node_kind_t::group, osbx_node_kind_t::sprite);
    const osbx_add_node_t change{.kind = kind, .parent = "group",
        .image = kind == osbx_node_kind_t::sprite ? std::optional<std::string>{"image"} : std::nullopt};
    auto history = round_trip(change);
    const auto id = scene(history).nodes.back().id;
    check_edit(id == "group/node/1");
    check_edit(scene(history).nodes.back().kind == kind);
    check_edit(scene(history).nodes.back().parent == "group");
    require_edit(undo_document_command(history));
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard, .change = change}));
    check_edit(scene(history).nodes.back().id == "group/node/2");
    auto independent = document_history_t::from(edit_song());
    require_edit(apply_osbx_edit(independent, {.storyboard = storyboard, .change = change}));
    check_edit(scene(independent).nodes.back().id == id);
}
TEST_CASE("OSBX delete node removes descendants and preserves unrelated nodes", "[osbx-edit]") {
    const auto history = round_trip(osbx_delete_node_t{.node = "group"});
    require_edit(scene(history).nodes.size() == 2);
    check_edit(scene(history).nodes.front().id == "other");
    check_edit(scene(history).nodes.back().id == "front");
}
TEST_CASE("OSBX rename node updates child references", "[osbx-edit]") {
    const auto history = round_trip(osbx_rename_node_t{.node = "group", .new_id = "renamed"});
    check_edit(scene(history).nodes.front().id == "renamed");
    check_edit(sprite(history).parent == "renamed");
}
TEST_CASE("OSBX reparent preserves node identity and layer", "[osbx-edit]") {
    const auto history = round_trip(osbx_reparent_node_t{.node = "sprite", .parent = "other"});
    check_edit(sprite(history).parent == "other");
    check_edit(sprite(history).id == "sprite");
    check_edit(sprite(history).layer == osbx_layer_t::background);
}
TEST_CASE("OSBX reorder changes only the selected layer order", "[osbx-edit]") {
    const auto history = round_trip(osbx_reorder_node_t{.node = "other", .index = 0});
    check_edit(scene(history).nodes[2].order == 0);
    check_edit(scene(history).nodes[0].order == 1);
    check_edit(sprite(history).order == 2);
    check_edit(scene(history).nodes[3] == edit_song().storyboards[0].osbx->nodes[3]);
}
TEST_CASE("OSBX set layer moves descendants and detaches the moved root", "[osbx-edit]") {
    SECTION("whole subtree") {
        const auto history = round_trip(osbx_set_layer_t{.node = "group", .layer = osbx_layer_t::foreground});
        check_edit(scene(history).nodes[0].layer == osbx_layer_t::foreground);
        check_edit(sprite(history).layer == osbx_layer_t::foreground);
        check_edit(sprite(history).parent == "group");
        check_edit(scene(history).nodes[2].layer == osbx_layer_t::background);
    }
    SECTION("child detaches") {
        const auto history = round_trip(osbx_set_layer_t{.node = "sprite", .layer = osbx_layer_t::pass});
        check_edit<false>(sprite(history).parent);
        check_edit(sprite(history).layer == osbx_layer_t::pass);
    }
}
TEST_CASE("OSBX set sprite image", "[osbx-edit]") {
    check_edit(sprite(round_trip(osbx_set_image_t{.node = "sprite", .image = "spare"})).image == "spare");
}
TEST_CASE("OSBX set node origin", "[osbx-edit]") {
    check_edit(sprite(round_trip(osbx_set_origin_t{.node = "sprite", .origin = "TopLeft"})).origin == "TopLeft");
}
TEST_CASE("OSBX add transform ops supplies valid defaults for every kind", "[osbx-edit]") {
    const auto kind = GENERATE(osbx_transform_kind_t::translate, osbx_transform_kind_t::rotate,
        osbx_transform_kind_t::scale, osbx_transform_kind_t::skew, osbx_transform_kind_t::matrix, osbx_transform_kind_t::pivot);
    const auto history = round_trip(osbx_add_op_t{.node = "sprite", .parent_op = "pivot", .kind = kind});
    const auto& op = sprite(history).ops.front().children.back();
    check_edit(op.id == "pivot/op/1");
    check_edit(op.kind == kind);
    check_edit(validate_osbx_document(scene(history)).value);
}
TEST_CASE("OSBX remove transform op removes its nested ops and keys", "[osbx-edit]") {
    const auto history = round_trip(osbx_remove_op_t{.node = "sprite", .op = "pivot"});
    require_edit(sprite(history).ops.size() == 1);
    check_edit(sprite(history).ops.front().id == "translate");
}
TEST_CASE("OSBX reorder transform op preserves nested content", "[osbx-edit]") {
    const auto history = round_trip(osbx_reorder_op_t{.node = "sprite", .op = "translate", .index = 0});
    check_edit(sprite(history).ops.front().id == "translate");
    check_edit(sprite(history).ops.back().children.front().track.keys.size() == 2);
}
TEST_CASE("OSBX set defaults supports numeric visibility and blend tracks", "[osbx-edit]") {
    SECTION("transform") {
        const auto history = round_trip(osbx_set_default_t{.track = rotation, .value = std::vector<double>{45}});
        check_edit(sprite(history).ops[0].children[0].track.default_value == std::vector<double>{45});
    }
    SECTION("opacity") {
        check_edit(sprite(round_trip(osbx_set_default_t{.track = opacity, .value = std::vector<double>{0.5}})).tracks.opacity.default_value == std::vector<double>{0.5});
    }
    SECTION("color") {
        const auto history = round_trip(osbx_set_default_t{.track = {.node = "sprite", .kind = osbx_track_kind_t::color}, .value = std::vector<double>{0.2, 0.3, 0.4, 1}});
        check_edit(sprite(history).tracks.color.default_value[0] == 0.2);
    }
    SECTION("visibility") {
        check_edit<false>(sprite(round_trip(osbx_set_default_t{.track = {.node = "sprite", .kind = osbx_track_kind_t::visibility}, .value = false})).tracks.visibility.default_value);
    }
    SECTION("blend") {
        check_edit(sprite(round_trip(osbx_set_default_t{.track = {.node = "sprite", .kind = osbx_track_kind_t::blend}, .value = osbx_blend_t::additive})).tracks.blend.default_value == osbx_blend_t::additive);
    }
}
TEST_CASE("OSBX add key allocates a stable ID and orders by time", "[osbx-edit]") {
    const auto history = round_trip(osbx_add_key_t{.track = rotation, .time_ms = 50, .value = std::vector<double>{45}});
    const auto& keys = sprite(history).ops[0].children[0].track.keys;
    require_edit(keys.size() == 3);
    check_edit(keys[1].id == "rotate/key/1");
    check_edit(keys[1].time_ms == 50);
    check_edit(keys[1].value == std::vector<double>{45});
}
TEST_CASE("OSBX move key resorts keys while preserving values and IDs", "[osbx-edit]") {
    const auto history = round_trip(osbx_move_key_t{.track = opacity, .key = "fade", .time_ms = 200});
    check_edit(sprite(history).tracks.opacity.keys.back().id == "fade");
    check_edit(sprite(history).tracks.opacity.keys.back().time_ms == 200);
    check_edit(sprite(history).tracks.opacity.keys.back().value == std::vector<double>{0});
}
TEST_CASE("OSBX set key value", "[osbx-edit]") {
    const auto history = round_trip(osbx_set_key_value_t{.track = opacity, .key = "fade", .value = std::vector<double>{0.25}});
    check_edit(sprite(history).tracks.opacity.keys.front().value == std::vector<double>{0.25});
}
TEST_CASE("OSBX set key easing", "[osbx-edit]") {
    const auto history = round_trip(osbx_set_easing_t{.track = rotation, .key = "turn", .easing = osbx_osb_easing_t{.value = 2}});
    check_edit(std::get<osbx_osb_easing_t>(sprite(history).ops[0].children[0].track.keys[0].easing).value == 2);
}
TEST_CASE("OSBX delete key", "[osbx-edit]") {
    const auto history = round_trip(osbx_delete_key_t{.track = opacity, .key = "fade"});
    require_edit(sprite(history).tracks.opacity.keys.size() == 1);
    check_edit(sprite(history).tracks.opacity.keys.front().id == "bright");
}
TEST_CASE("OSBX multi-key delta moves simultaneously across tracks", "[osbx-edit]") {
    const auto history = round_trip(osbx_move_keys_t{.keys = {
        {.track = opacity, .key = "fade"}, {.track = opacity, .key = "bright"}, {.track = rotation, .key = "turn"}}, .delta_ms = 200});
    check_edit(sprite(history).tracks.opacity.keys.front().time_ms == 200);
    check_edit(sprite(history).tracks.opacity.keys.back().time_ms == 300);
    check_edit(sprite(history).ops[0].children[0].track.keys.front().id == "stop");
}
TEST_CASE("OSBX add image asset entry uses a project-relative path", "[osbx-edit]") {
    const auto history = round_trip(osbx_add_image_t{.file = "images/new.png"});
    check_edit(scene(history).images.back().file == "images/new.png");
    check_edit(scene(history).images.back().id == "scene/image/1");
}
TEST_CASE("OSBX remove unused image asset", "[osbx-edit]") {
    check_edit(scene(round_trip(osbx_remove_image_t{.image = "spare"})).images.size() == 1);
}

TEST_CASE("OSBX hierarchy rejects cycles cross-layer parents and duplicate IDs", "[osbx-edit]") {
    const auto change = GENERATE(osbx_edit_change_t{osbx_reparent_node_t{.node = "group", .parent = "sprite"}},
        osbx_edit_change_t{osbx_reparent_node_t{.node = "sprite", .parent = "front"}},
        osbx_edit_change_t{osbx_reparent_node_t{.node = "sprite", .parent = "missing"}},
        osbx_edit_change_t{osbx_rename_node_t{.node = "sprite", .new_id = "image"}},
        osbx_edit_change_t{osbx_rename_node_t{.node = "sprite", .new_id = ""}},
        osbx_edit_change_t{osbx_reorder_node_t{.node = "sprite", .index = 100}},
        osbx_edit_change_t{osbx_add_node_t{.kind = osbx_node_kind_t::sprite}},
        osbx_edit_change_t{osbx_add_node_t{.parent = "missing"}});
    reject_change(change);
}
TEST_CASE("OSBX properties and assets reject invalid authored values", "[osbx-edit]") {
    const auto change = GENERATE(osbx_edit_change_t{osbx_set_image_t{.node = "sprite", .image = "missing"}},
        osbx_edit_change_t{osbx_set_origin_t{.node = "sprite", .origin = "invalid"}},
        osbx_edit_change_t{osbx_add_image_t{.file = "../escape.png"}},
        osbx_edit_change_t{osbx_remove_image_t{.image = "image"}},
        osbx_edit_change_t{osbx_set_default_t{.track = rotation, .value = std::vector<double>{1, 2}}},
        osbx_edit_change_t{osbx_set_default_t{.track = opacity, .value = false}},
        osbx_edit_change_t{osbx_add_op_t{.node = "sprite", .parent_op = "rotate"}},
        osbx_edit_change_t{osbx_reorder_op_t{.node = "sprite", .op = "rotate", .index = 2}});
    reject_change(change);
}
TEST_CASE("OSBX keys reject collisions missing IDs invalid easing and overflow atomically", "[osbx-edit]") {
    const auto change = GENERATE(osbx_edit_change_t{osbx_move_key_t{.track = opacity, .key = "fade", .time_ms = 100}},
        osbx_edit_change_t{osbx_add_key_t{.track = rotation, .time_ms = 0, .value = std::vector<double>{0}}},
        osbx_edit_change_t{osbx_delete_key_t{.track = opacity, .key = "missing"}},
        osbx_edit_change_t{osbx_set_key_value_t{.track = rotation, .key = "turn", .value = std::vector<double>{std::numeric_limits<double>::infinity()}}},
        osbx_edit_change_t{osbx_set_easing_t{.track = rotation, .key = "turn", .easing = osbx_osb_easing_t{.value = 1000}}},
        osbx_edit_change_t{osbx_move_keys_t{.keys = {{.track = opacity, .key = "fade"}, {.track = opacity, .key = "fade"}}, .delta_ms = 5}},
        osbx_edit_change_t{osbx_move_keys_t{.keys = {{.track = opacity, .key = "fade"}, {.track = opacity, .key = "bright"}}, .delta_ms = std::numeric_limits<std::int64_t>::max()}});
    reject_change(change);
}
TEST_CASE("OSBX edits reject missing OSB and generator-owned storyboard targets", "[osbx-edit]") {
    auto history = document_history_t::from(edit_song());
    SECTION("missing") {
        reject_edit(history, {.storyboard = storyboard_id_t{999}, .change = osbx_delete_storyboard_t{}}, diagnostic_code_t::invalid_id);
    }
    SECTION("OSB") {
        history.document.storyboards[0].osbx.reset();
        history.document.storyboards[0].model.emplace();
        reject_edit(history, {.storyboard = storyboard, .change = osbx_rename_storyboard_t{.name = "No"}}, diagnostic_code_t::unsupported_feature);
    }
    SECTION("generator") {
        history.document.generator_recipes.push_back({.id = recipe_id_t{2}});
        history.document.storyboards[0].owning_generator_recipe = recipe_id_t{2};
        reject_edit(history, {.storyboard = storyboard, .change = osbx_delete_storyboard_t{}});
        const auto result = apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_rename_storyboard_t{.name = "No"}});
        require_edit<false>(result.diagnostics.empty());
        check_edit(result.diagnostics.front().message.find("detach first") != std::string::npos);
    }
}
TEST_CASE("OSBX key move round trips as a native delta and journal delta", "[osbx-edit]") {
    auto song = edit_song();
    song.storyboards.push_back({.id = storyboard_id_t{4}, .retained_source_lines = {std::string(8192, ' ')}});
    auto history = document_history_t::from(song);
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_move_key_t{.track = opacity, .key = "fade", .time_ms = 25}}));
    const auto delta = native_project_delta_t::from(song, history.document);
    require_edit(delta);
    const auto decoded = native_project_delta_t::parse(delta->bytes());
    require_edit(decoded);
    const auto applied = decoded->apply_on(song);
    require_edit(applied.value);
    check_edit(bytes(*applied.value) == bytes(history.document));
    const auto directory = test::temporary_directory_t::from();
    require_edit(directory);
    const command_journal_t journal{directory->path() / "move-key.journal"};
    require_edit(journal.append({.sequence = 1, .before_revision = 1, .command = "Seed", .snapshot = {.song = song}}));
    require_edit(journal.append({.sequence = 2, .before_revision = 1, .command = "Move OSBX key", .snapshot = {.song = history.document}}));
    const auto recovered = journal.read();
    require_edit(recovered.ok());
    require_edit(recovered.value.snapshot);
    require_edit(recovered.value.positions.size() == 2);
    check_edit<false>(recovered.value.positions[1].is_seed);
    check_edit(bytes(recovered.value.snapshot->song) == bytes(history.document));
}

TEST_CASE("OSBX key and op IDs remain reserved across delete and Undo branches", "[osbx-edit]") {
    auto history = document_history_t::from(edit_song());
    const osbx_edit_t add_op{.storyboard = storyboard, .change = osbx_add_op_t{.node = "sprite"}};
    require_edit(apply_osbx_edit(history, add_op));
    check_edit(sprite(history).ops.back().id == "sprite/op/1");
    require_edit(undo_document_command(history));
    require_edit(apply_osbx_edit(history, add_op));
    check_edit(sprite(history).ops.back().id == "sprite/op/2");
    const osbx_edit_t add_key{.storyboard = storyboard, .change = osbx_add_key_t{
        .track = opacity, .time_ms = 50, .value = std::vector<double>{0.5}}};
    require_edit(apply_osbx_edit(history, add_key));
    check_edit(sprite(history).tracks.opacity.keys[1].id == "sprite/key/1");
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_delete_key_t{.track = opacity, .key = "sprite/key/1"}}));
    require_edit(apply_osbx_edit(history, add_key));
    check_edit(sprite(history).tracks.opacity.keys[1].id == "sprite/key/2");
}
TEST_CASE("OSBX typed keys support visibility and blend values", "[osbx-edit]") {
    SECTION("visibility") {
        const auto history = round_trip(osbx_add_key_t{
            .track = {.node = "sprite", .kind = osbx_track_kind_t::visibility}, .time_ms = 50, .value = false});
        check_edit<false>(sprite(history).tracks.visibility.keys[0].value);
    }
    SECTION("blend") {
        const auto history = round_trip(osbx_add_key_t{
            .track = {.node = "sprite", .kind = osbx_track_kind_t::blend}, .time_ms = 50, .value = osbx_blend_t::additive});
        check_edit(sprite(history).tracks.blend.keys[0].value == osbx_blend_t::additive);
    }
}
TEST_CASE("OSBX edits reject missing local targets atomically", "[osbx-edit]") {
    const auto change = GENERATE(osbx_edit_change_t{osbx_delete_node_t{.node = "missing"}},
        osbx_edit_change_t{osbx_rename_node_t{.node = "missing", .new_id = "new"}},
        osbx_edit_change_t{osbx_set_layer_t{.node = "missing"}},
        osbx_edit_change_t{osbx_set_origin_t{.node = "missing", .origin = "Centre"}},
        osbx_edit_change_t{osbx_set_image_t{.node = "missing", .image = "image"}},
        osbx_edit_change_t{osbx_add_op_t{.node = "missing"}},
        osbx_edit_change_t{osbx_remove_op_t{.node = "sprite", .op = "missing"}},
        osbx_edit_change_t{osbx_remove_image_t{.image = "missing"}},
        osbx_edit_change_t{osbx_set_default_t{.track = {.node = "missing"}, .value = std::vector<double>{1}}});
    reject_change(change);
}

TEST_CASE("OSBX allocation skips historical counter gaps and rejects exhaustion", "[osbx-edit]") {
    auto history = document_history_t::from(edit_song());
    history.document.storyboards[0].osbx->nodes.push_back({.id = "group/node/9"});
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_add_node_t{.parent = "group"}}));
    check_edit(scene(history).nodes.back().id == "group/node/10");
    history.document.storyboards[0].osbx->nodes.back().id = "group/node/18446744073709551615";
    reject_edit(history, {.storyboard = storyboard, .change = osbx_add_node_t{.parent = "group"}});
}

TEST_CASE("OSBX unchanged edits do not publish or discard Redo", "[osbx-edit]") {
    auto history = round_trip(osbx_set_origin_t{.node = "sprite", .origin = "TopLeft"});
    require_edit(undo_document_command(history));
    const auto before = bytes(history.document);
    const auto revision = history.document.revision;
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_set_default_t{.track = opacity, .value = std::vector<double>{1}}});
    check_edit<false>(result.applied);
    check_edit(result.diagnostics.empty());
    check_edit(bytes(history.document) == before);
    check_edit(history.document.revision == revision);
    check_edit(history.undo.empty());
    check_edit(history.redo.size() == 1);
}
TEST_CASE("OSBX Lua-linked track and children edits explain detach first", "[osbx-edit]") {
    auto history = document_history_t::from(edit_song());
    const std::string source = "return function(t) return {t} end";
    const osbx_lua_link_t link{.id = "linked", .source = {.frozen_source = source,
        .source_reference = "scripts/linked.lua", .source_hash = native_project_content_checksum(source)}};
    SECTION("linked track") {
        history.document.storyboards[0].osbx->nodes[1].tracks.opacity.lua = osbx_lua_track_link_t{.link = link};
        reject_edit(history, {.storyboard = storyboard,
            .change = osbx_set_default_t{.track = opacity, .value = std::vector<double>{0.5}}});
        reject_edit(history, {.storyboard = storyboard,
            .change = osbx_move_keys_t{.keys = {{.track = opacity, .key = "fade"}}, .delta_ms = 5}});
    }
    SECTION("linked children") {
        history.document.storyboards[0].osbx->nodes[2].children_link = link;
        reject_edit(history, {.storyboard = storyboard, .change = osbx_add_node_t{.parent = "other"}});
    }
}

namespace {
auto drag_change(std::int64_t time) -> osbx_edit_change_t {
    return osbx_move_key_t{.track = opacity, .key = "fade", .time_ms = time};
}
auto begin_drag(document_history_t& history, editor_interaction_state_t& state,
                storyboard_id_t target = storyboard) -> editor_interaction_result_t {
    return dispatch_editor_action(history, state,
        begin_osbx_edit_gesture_action_t{.edit = {.storyboard = target, .change = drag_change(0)}});
}
auto update_drag(document_history_t& history, editor_interaction_state_t& state, std::int64_t time,
                 proposal_operation_t operation = proposal_operation_t::revise) -> editor_interaction_result_t {
    return dispatch_editor_action(history, state,
        update_osbx_edit_gesture_action_t{.change = drag_change(time), .operation = operation});
}
} // namespace

TEST_CASE("OSBX dispatched song-only drag stages updates and commits exactly one Undo", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    require_edit(history.document.charts.empty());
    const auto before = bytes(history.document);
    require_edit(begin_drag(history, state));
    require_edit(update_drag(history, state, 10));
    require_edit(update_drag(history, state, 20));
    require_edit(update_drag(history, state, 30));
    check_edit(history.undo.empty());
    check_edit(bytes(history.document) == before);
    const auto* pending = state.gesture<pending_osbx_edit_gesture_t>();
    require_edit(pending);
    check_edit(std::get<osbx_move_key_t>(pending->edit.change).time_ms == 30);
    const auto result = update_drag(history, state, 40, proposal_operation_t::apply);
    require_edit(result);
    check_edit(result.document_changed);
    check_edit(result.diagnostics.empty());
    check_edit<false>(result.view);
    check_edit<false>(state.is_pending());
    require_edit(history.undo.size() == 1);
    check_edit(sprite(history).tracks.opacity.keys.front().time_ms == 40);
    const auto after = bytes(history.document);
    require_edit(undo_document_command(history));
    check_edit(bytes(history.document) == before);
    require_edit(redo_document_command(history));
    check_edit(bytes(history.document) == after);
}
TEST_CASE("OSBX dispatched cancellation preserves document and history without a chart", "[osbx-edit][gesture]") {
    const auto reason = GENERATE(editor_gesture_cancel_reason_t::escape, editor_gesture_cancel_reason_t::focus_lost,
        editor_gesture_cancel_reason_t::pointer_capture_lost, editor_gesture_cancel_reason_t::modifier_released);
    auto history = round_trip(osbx_set_origin_t{.node = "sprite", .origin = "TopLeft"});
    require_edit(undo_document_command(history));
    const auto before = bytes(history.document);
    const auto revision = history.document.revision, next_revision = history.next_revision;
    const auto sequence = history.next_history_sequence;
    const auto redo_before = bytes(history.redo.front().after);
    editor_interaction_state_t state;
    require_edit(begin_drag(history, state));
    require_edit(update_drag(history, state, 25));
    const auto result = dispatch_editor_action(history, state, cancel_editor_gesture_action_t{.reason = reason});
    require_edit(result);
    check_edit(result.diagnostics.empty());
    check_edit<false>(state.is_pending());
    check_edit(bytes(history.document) == before);
    check_edit(history.document.revision == revision);
    check_edit(history.next_revision == next_revision);
    check_edit(history.next_history_sequence == sequence);
    check_edit(history.undo.empty());
    require_edit(history.redo.size() == 1);
    check_edit(bytes(history.redo.front().after) == redo_before);
}
TEST_CASE("OSBX drag pointer-capture loss cancels through shared dispatch", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    const auto before = bytes(history.document);
    require_edit(begin_drag(history, state));
    require_edit(update_drag(history, state, 25));
    const auto result = dispatch_editor_action(history, state, update_osbx_edit_gesture_action_t{
        .change = drag_change(50), .operation = proposal_operation_t::apply, .pointer_captured = false});
    require_edit(result);
    check_edit(result.diagnostics.empty());
    check_edit<false>(state.is_pending());
    check_edit(bytes(history.document) == before);
    check_edit(history.undo.empty());
}
TEST_CASE("OSBX dispatched stale revision and same-clock project replacement reject atomically", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    require_edit(begin_drag(history, state));
    SECTION("revision") {
        require_edit(apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_rename_storyboard_t{.name = "External edit"}}));
    }
    SECTION("project identity at same revision") { history.document.id = song_id_t{999}; }
    const auto before = bytes(history.document);
    const auto undo_count = history.undo.size();
    const auto result = update_drag(history, state, 25, proposal_operation_t::apply);
    check_edit<false>(result);
    require_edit<false>(result.diagnostics.empty());
    check_edit(result.diagnostics.front().code == diagnostic_code_t::invalid_hold);
    check_edit<false>(state.is_pending());
    check_edit(bytes(history.document) == before);
    check_edit(history.undo.size() == undo_count);
}
TEST_CASE("OSBX invalid preview retains its valid draft and invalid apply cleans it up", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    const auto before = bytes(history.document);
    require_edit(begin_drag(history, state));
    require_edit(update_drag(history, state, 25));
    check_edit<false>(update_drag(history, state, 100));
    require_edit(state.gesture<pending_osbx_edit_gesture_t>());
    check_edit(std::get<osbx_move_key_t>(state.gesture<pending_osbx_edit_gesture_t>()->edit.change).time_ms == 25);
    check_edit(bytes(history.document) == before);
    check_edit<false>(update_drag(history, state, 100, proposal_operation_t::apply));
    check_edit<false>(state.is_pending());
    check_edit(bytes(history.document) == before);
    check_edit(history.undo.empty());
}
TEST_CASE("OSBX drag cannot retarget captured track or key", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    require_edit(begin_drag(history, state));
    const auto result = dispatch_editor_action(history, state, update_osbx_edit_gesture_action_t{
        .change = osbx_move_key_t{.track = rotation, .key = "turn", .time_ms = 25}, .operation = proposal_operation_t::apply});
    check_edit<false>(result);
    check_edit<false>(state.is_pending());
    check_edit(history.undo.empty());
}
TEST_CASE("OSBX dispatched no-op drag creates no Undo", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    const auto before = bytes(history.document);
    require_edit(begin_drag(history, state));
    const auto result = update_drag(history, state, 0, proposal_operation_t::apply);
    require_edit(result);
    check_edit<false>(result.document_changed);
    check_edit(result.diagnostics.empty());
    check_edit<false>(state.is_pending());
    check_edit(bytes(history.document) == before);
    check_edit(history.undo.empty());
}
TEST_CASE("OSBX drag begin rejects missing OSB and generator-owned targets", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    SECTION("missing") {
        check_edit<false>(begin_drag(history, state, storyboard_id_t{999}));
    }
    SECTION("OSB") {
        history.document.storyboards[0].osbx.reset();
        history.document.storyboards[0].model.emplace();
        check_edit<false>(begin_drag(history, state));
    }
    SECTION("generator") {
        history.document.generator_recipes.push_back({.id = recipe_id_t{2}});
        history.document.storyboards[0].owning_generator_recipe = recipe_id_t{2};
        const auto result = begin_drag(history, state);
        check_edit<false>(result);
        require_edit<false>(result.diagnostics.empty());
        check_edit(result.diagnostics.front().message.find("detach first") != std::string::npos);
    }
    check_edit<false>(state.is_pending());
    check_edit(history.undo.empty());
}
TEST_CASE("OSBX and chart gesture admission remains mutually exclusive", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    SECTION("storyboard blocks chart gesture before chart lookup") {
        require_edit(begin_drag(history, state));
        const auto result = dispatch_editor_action(history, state, begin_note_move_gesture_action_t{.reference_note = object_id_t{7}});
        check_edit<false>(result);
        require_edit<false>(result.diagnostics.empty());
        check_edit(result.diagnostics.front().code == diagnostic_code_t::invalid_hold);
        check_edit(state.gesture<pending_osbx_edit_gesture_t>());
    }
    SECTION("chart gesture blocks storyboard gesture") {
        state.active_gesture = proposal_t<pending_note_move_gesture_t, document_stamp_t>{
            .stamp = document_stamp_t::from(history.document.revision), .payload = {}};
        const auto result = begin_drag(history, state);
        check_edit<false>(result);
        require_edit<false>(result.diagnostics.empty());
        check_edit(result.diagnostics.front().code == diagnostic_code_t::invalid_hold);
        check_edit(state.gesture<pending_note_move_gesture_t>());
    }
    check_edit(history.undo.empty());
}

TEST_CASE("OSBX drag apply revalidates missing read-only and generated targets", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    history.document.generator_recipes.push_back({.id = recipe_id_t{2}});
    editor_interaction_state_t state;
    require_edit(begin_drag(history, state));
    SECTION("missing target") { history.document.storyboards.clear(); }
    SECTION("read-only OSB target") {
        history.document.storyboards[0].osbx.reset();
        history.document.storyboards[0].model.emplace();
    }
    SECTION("generator-owned target") { history.document.storyboards[0].owning_generator_recipe = recipe_id_t{2}; }
    const auto before = bytes(history.document);
    const auto result = update_drag(history, state, 25, proposal_operation_t::apply);
    check_edit<false>(result);
    require_edit<false>(result.diagnostics.empty());
    check_edit<false>(state.is_pending());
    check_edit(bytes(history.document) == before);
    check_edit(history.undo.empty());
}
TEST_CASE("OSBX property drag stages canvas transform defaults through dispatch", "[osbx-edit][gesture]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    require_edit(dispatch_editor_action(history, state, begin_osbx_edit_gesture_action_t{.edit = {
        .storyboard = storyboard, .change = osbx_set_default_t{.track = rotation, .value = std::vector<double>{0}}}}));
    require_edit(dispatch_editor_action(history, state, update_osbx_edit_gesture_action_t{
        .change = osbx_set_default_t{.track = rotation, .value = std::vector<double>{10}}}));
    check_edit(sprite(history).ops[0].children[0].track.default_value == std::vector<double>{0});
    require_edit(dispatch_editor_action(history, state, update_osbx_edit_gesture_action_t{
        .change = osbx_set_default_t{.track = rotation, .value = std::vector<double>{20}}, .operation = proposal_operation_t::apply}));
    check_edit(sprite(history).ops[0].children[0].track.default_value == std::vector<double>{20});
    check_edit(history.undo.size() == 1);
}

namespace {
auto node_by_id(const document_history_t& history, std::string_view id) -> const osbx_node_t& {
    const auto& nodes = scene(history).nodes;
    const auto found = std::ranges::find(nodes, id, &osbx_node_t::id);
    require_edit(found != nodes.end());
    return *found;
}
auto begin_change(document_history_t& history, editor_interaction_state_t& state, osbx_edit_change_t change)
    -> editor_interaction_result_t {
    return dispatch_editor_action(history, state, begin_osbx_edit_gesture_action_t{
        .edit = {.storyboard = storyboard, .change = std::move(change)}});
}
auto revise_change(document_history_t& history, editor_interaction_state_t& state,
                   osbx_edit_change_t change, proposal_operation_t operation = proposal_operation_t::revise)
    -> editor_interaction_result_t {
    return dispatch_editor_action(history, state, update_osbx_edit_gesture_action_t{
        .change = std::move(change), .operation = operation});
}
auto new_opacity_key(double value, std::int64_t time = 50) -> osbx_add_key_t {
    return {.track = opacity, .time_ms = time, .value = std::vector<double>{value}};
}
} // namespace

TEST_CASE("OSBX duplicate subtree returns fresh owned identities and preserves external images", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    const auto result = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_duplicate_node_t{.node = "group"}});
    require_edit(result.command.applied);
    require_edit(result.payload.node);
    const auto id = *result.payload.node;
    check_edit(id != "group");
    require_edit(scene(history).nodes.size() == 6);
    const auto child = std::ranges::find_if(scene(history).nodes, [&](const auto& item) { return item.parent == id; });
    require_edit(child != scene(history).nodes.end());
    check_edit(child->image == "image");
    check_edit(child->ops[0].id != "pivot");
    check_edit(child->ops[0].children[0].track.keys[0].id != "turn");
    check_edit(child->tracks.opacity.keys[0].id != "fade");
    check_edit(validate_osbx_document(scene(history)).value);
    check_edit(history.undo.size() == 1);
    const auto after = bytes(history.document);
    require_edit(undo_document_command(history));
    check_edit(bytes(history.document) == before);
    require_edit(redo_document_command(history));
    check_edit(bytes(history.document) == after);
}
TEST_CASE("OSBX node payload reserves identities across Undo and Redo", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    const auto first = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_add_node_t{.parent = "group"}});
    require_edit(first.command.applied && first.payload.node.has_value());
    require_edit(undo_document_command(history));
    const auto second = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_duplicate_node_t{.node = "sprite"}});
    require_edit(second.command.applied && second.payload.node.has_value());
    check_edit(first.payload.node != second.payload.node);
    check_edit(node_by_id(history, *second.payload.node).parent == "group");
}
TEST_CASE("OSBX duplicate rejects missing generated and OSB targets without creation payload", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    SECTION("missing") { reject_change(osbx_duplicate_node_t{.node = "missing"}); }
    SECTION("generated") {
        history.document.generator_recipes.push_back({.id = recipe_id_t{2}});
        history.document.storyboards[0].owning_generator_recipe = recipe_id_t{2};
        const auto result = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
            .change = osbx_duplicate_node_t{.node = "group"}});
        check_edit<false>(result.command.applied);
        check_edit<false>(result.payload.node);
        check_edit(history.undo.empty());
    }
    SECTION("OSB") {
        history.document.storyboards[0].osbx.reset();
        history.document.storyboards[0].model.emplace();
        reject_edit(history, {.storyboard = storyboard, .change = osbx_duplicate_node_t{.node = "group"}},
                    diagnostic_code_t::unsupported_feature);
    }
}
TEST_CASE("OSBX drop reparents and reorders in one reversible command", "[osbx-edit][hierarchy]") {
    const auto history = round_trip(osbx_drop_node_t{.node = "other", .parent = "group", .before = "sprite"});
    check_edit(node_by_id(history, "other").parent == "group");
    check_edit(node_by_id(history, "other").order < node_by_id(history, "sprite").order);
}
TEST_CASE("OSBX group drop moves descendant stacking together", "[osbx-edit][hierarchy]") {
    const auto history = round_trip(osbx_drop_node_t{.node = "group"});
    check_edit(node_by_id(history, "other").order < node_by_id(history, "group").order);
    check_edit(node_by_id(history, "group").order < node_by_id(history, "sprite").order);
    check_edit(node_by_id(history, "sprite").parent == "group");
}
TEST_CASE("OSBX drop rejects cross-layer cycles and stale sibling identities atomically", "[osbx-edit][hierarchy]") {
    reject_change(osbx_drop_node_t{.node = "group", .layer = osbx_layer_t::foreground});
    reject_change(osbx_drop_node_t{.node = "group", .parent = "sprite"});
    reject_change(osbx_drop_node_t{.node = "group", .before = "sprite"});
    reject_change(osbx_drop_node_t{.node = "sprite", .before = "missing"});
    reject_change(osbx_drop_node_t{.node = "sprite", .parent = "front"});
}
TEST_CASE("OSBX identical drop is a no-op without normalizing signed orders", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_drop_node_t{.node = "group", .before = "other"}});
    check_edit<false>(result.applied);
    check_edit(result.diagnostics.empty());
    check_edit(history.undo.empty());
    check_edit(bytes(history.document) == before);
}
TEST_CASE("OSBX explicit signed node order remains independent of lifetime", "[osbx-edit][inspector]") {
    const auto history = round_trip(osbx_set_order_t{.node = "sprite", .order = std::numeric_limits<int>::min()});
    check_edit(sprite(history).order == std::numeric_limits<int>::min());
    check_edit<false>(sprite(history).lifetime);
}
TEST_CASE("OSBX auto-key gesture retains history-safe identity and one Undo", "[osbx-edit][gesture][inspector]") {
    auto history = document_history_t::from(edit_song());
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard, .change = new_opacity_key(0.4)}));
    const auto old_id = sprite(history).tracks.opacity.keys[1].id;
    require_edit(undo_document_command(history));
    const auto before = bytes(history.document);
    editor_interaction_state_t state;
    require_edit(begin_change(history, state, new_opacity_key(0.1)));
    const auto* pending = state.gesture<pending_osbx_edit_gesture_t>();
    require_edit(pending);
    const auto expected = pending->ids.next("sprite", "key", scene(history));
    check_edit(expected != old_id);
    require_edit(revise_change(history, state, new_opacity_key(0.2)));
    require_edit(revise_change(history, state, new_opacity_key(0.3)));
    check_edit(bytes(history.document) == before);
    check_edit(history.undo.empty());
    require_edit(revise_change(history, state, new_opacity_key(0.4), proposal_operation_t::apply));
    check_edit(history.undo.size() == 1);
    check_edit(sprite(history).tracks.opacity.keys[1].id == expected);
    check_edit(sprite(history).tracks.opacity.keys[1].value == std::vector<double>{0.4});
    const auto after = bytes(history.document);
    require_edit(undo_document_command(history));
    check_edit(bytes(history.document) == before);
    require_edit(redo_document_command(history));
    check_edit(bytes(history.document) == after);
}
TEST_CASE("OSBX auto-key gesture refuses snapped-time retarget and occupied keys", "[osbx-edit][gesture][inspector]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    check_edit<false>(begin_change(history, state, new_opacity_key(0.1, 0)));
    check_edit<false>(state.is_pending());
    require_edit(begin_change(history, state, new_opacity_key(0.1)));
    check_edit<false>(revise_change(history, state, new_opacity_key(0.2, 60)));
    require_edit(revise_change(history, state, new_opacity_key(0.3), proposal_operation_t::apply));
    check_edit(sprite(history).tracks.opacity.keys[1].time_ms == 50);
}
TEST_CASE("OSBX auto-key cancellation restores document and history clocks exactly", "[osbx-edit][gesture][inspector]") {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    const auto revision = history.document.revision, next = history.next_revision, sequence = history.next_history_sequence;
    editor_interaction_state_t state;
    require_edit(begin_change(history, state, new_opacity_key(0.1)));
    require_edit(revise_change(history, state, new_opacity_key(0.2)));
    require_edit(dispatch_editor_action(history, state,
        cancel_editor_gesture_action_t{.reason = editor_gesture_cancel_reason_t::escape}));
    check_edit(bytes(history.document) == before);
    check_edit(history.document.revision == revision && history.next_revision == next);
    check_edit(history.next_history_sequence == sequence);
    check_edit(history.undo.empty() && history.redo.empty());
}
TEST_CASE("OSBX order scrub uses shared gesture and commits once", "[osbx-edit][gesture][inspector]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    require_edit(begin_change(history, state, osbx_set_order_t{.node = "sprite", .order = -10}));
    require_edit(revise_change(history, state, osbx_set_order_t{.node = "sprite", .order = -20}));
    check_edit(sprite(history).order == 1);
    check_edit<false>(revise_change(history, state, osbx_set_order_t{.node = "group", .order = -30}));
    require_edit(revise_change(history, state, osbx_set_order_t{.node = "sprite", .order = -40}, proposal_operation_t::apply));
    check_edit(sprite(history).order == -40);
    check_edit(history.undo.size() == 1);
    check_edit<false>(sprite(history).lifetime);
}
TEST_CASE("OSBX lifetime scrub rejects invalid range but retains valid proposal", "[osbx-edit][gesture][inspector]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    require_edit(begin_change(history, state, osbx_set_lifetime_t{.node = "sprite",
        .lifetime = osbx_lifetime_t{.begin_ms = 10, .end_ms = 20}}));
    check_edit<false>(revise_change(history, state, osbx_set_lifetime_t{.node = "sprite",
        .lifetime = osbx_lifetime_t{.begin_ms = 20, .end_ms = 10}}));
    check_edit<false>(sprite(history).lifetime);
    require_edit(revise_change(history, state, osbx_set_lifetime_t{.node = "sprite",
        .lifetime = osbx_lifetime_t{.begin_ms = 15, .end_ms = 30}}, proposal_operation_t::apply));
    check_edit(sprite(history).lifetime == osbx_lifetime_t{.begin_ms = 15, .end_ms = 30});
    check_edit(history.undo.size() == 1);
}
TEST_CASE("OSBX Convert retains its exhaustive command label after adding edit variants", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    history.document.storyboards[0].osbx.reset();
    history.document.storyboards[0].model.emplace();
    const auto result = apply_osbx_edit_with_result(history, {.storyboard = storyboard,
        .change = osbx_convert_storyboard_t{}});
    require_edit(result.command.applied);
    require_edit(result.payload);
    require_edit(history.undo.size() == 1);
    check_edit(history.undo[0].label == "Convert OSB to OSBX");
}
TEST_CASE("OSBX new scrub variants reject stale commits through shared runner", "[osbx-edit][gesture][inspector]") {
    const auto change = GENERATE(osbx_edit_change_t{new_opacity_key(0.2)},
        osbx_edit_change_t{osbx_set_order_t{.node = "sprite", .order = -10}},
        osbx_edit_change_t{osbx_set_lifetime_t{.node = "sprite",
            .lifetime = osbx_lifetime_t{.begin_ms = 10, .end_ms = 20}}});
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    require_edit(begin_change(history, state, change));
    ++history.document.revision;
    const auto before = bytes(history.document);
    check_edit<false>(revise_change(history, state, change, proposal_operation_t::apply));
    check_edit(bytes(history.document) == before);
    check_edit(history.undo.empty());
    check_edit<false>(state.is_pending());
}
TEST_CASE("OSBX order no-op scrub creates no Undo", "[osbx-edit][gesture][inspector]") {
    auto history = document_history_t::from(edit_song());
    editor_interaction_state_t state;
    require_edit(begin_change(history, state, osbx_set_order_t{.node = "sprite", .order = 1}));
    require_edit(revise_change(history, state, osbx_set_order_t{.node = "sprite", .order = 1}, proposal_operation_t::apply));
    check_edit(history.undo.empty());
    check_edit<false>(state.is_pending());
}
TEST_CASE("OSBX duplicate preserves exact subtree lifetime and authored Lua identity ownership", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    auto& original = history.document.storyboards[0].osbx->nodes[1];
    original.lifetime = osbx_lifetime_t{.begin_ms = -10, .end_ms = 37};
    const std::string source = "return function(t) return {t} end";
    original.tracks.opacity.lua = osbx_lua_track_link_t{.link = {.id = "linked", .source = {
        .frozen_source = source, .source_reference = "scripts/source.lua",
        .source_hash = native_project_content_checksum(source)}}};
    const auto result = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_duplicate_node_t{.node = "sprite"}});
    require_edit(result.command.applied && result.payload.node.has_value());
    const auto& copy = node_by_id(history, *result.payload.node);
    check_edit(copy.lifetime == osbx_lifetime_t{.begin_ms = -10, .end_ms = 37});
    require_edit(copy.tracks.opacity.lua);
    check_edit(copy.tracks.opacity.lua->link.id != "linked");
    check_edit(copy.tracks.opacity.lua->link.source.frozen_source == source);
    check_edit(validate_osbx_document(scene(history)).value);
}
TEST_CASE("OSBX multi-key deletion spans tracks in one reversible command", "[osbx-edit][timeline]") {
    const auto history = round_trip(osbx_delete_keys_t{.keys = {
        {.track = opacity, .key = "fade"}, {.track = rotation, .key = "turn"}}});
    check_edit(sprite(history).tracks.opacity.keys.size() == 1);
    check_edit(sprite(history).ops[0].children[0].track.keys.size() == 1);
    check_edit(history.undo.size() == 1);
}
TEST_CASE("OSBX multi-key deletion rejects missing malformed and duplicate targets atomically", "[osbx-edit][timeline]") {
    reject_change(osbx_delete_keys_t{.keys = {{.track = opacity, .key = "fade"}, {.track = rotation, .key = "missing"}}});
    reject_change(osbx_delete_keys_t{.keys = {{.track = opacity, .key = "fade"}, {.track = opacity, .key = "fade"}}});
    reject_change(osbx_delete_keys_t{.keys = {{.track = opacity, .key = "fade"}, {.track = {}, .key = "turn"}}});
    reject_change(osbx_delete_keys_t{.keys = {{.track = opacity, .key = "fade"}, {.track = rotation, .key = ""}}});
}
TEST_CASE("OSBX empty multi-key deletion is an unchanged result", "[osbx-edit][timeline]") {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    const auto result = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_delete_keys_t{}});
    check_edit<false>(result.command.applied);
    check_edit(result.command.diagnostics.empty());
    check_edit(result.payload == osbx_edit_created_ids_t{});
    check_edit(history.undo.empty() && history.redo.empty());
    check_edit(bytes(history.document) == before);
}
TEST_CASE("OSBX multi-key deletion retains Lua and generated ownership guards", "[osbx-edit][timeline]") {
    auto history = document_history_t::from(edit_song());
    const osbx_delete_keys_t deletion{.keys = {{.track = rotation, .key = "turn"}, {.track = opacity, .key = "fade"}}};
    SECTION("Lua track") {
        const std::string source = "return function(t) return {t} end";
        history.document.storyboards[0].osbx->nodes[1].tracks.opacity.lua = osbx_lua_track_link_t{
            .link = {.id = "link", .source = {.frozen_source = source, .source_reference = "scripts/source.lua",
                .source_hash = native_project_content_checksum(source)}}};
        reject_edit(history, {.storyboard = storyboard, .change = deletion});
    }
    SECTION("generated") {
        history.document.generator_recipes.push_back({.id = recipe_id_t{2}});
        history.document.storyboards[0].owning_generator_recipe = recipe_id_t{2};
        reject_edit(history, {.storyboard = storyboard, .change = deletion});
    }
}
TEST_CASE("OSBX before-group drop resolves before its earliest descendant draw position", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    history.document.storyboards[0].osbx->nodes[0].order = 10;
    history.document.storyboards[0].osbx->nodes[1].order = -10;
    const auto before = bytes(history.document);
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_drop_node_t{.node = "other", .before = "group"}}));
    check_edit(node_by_id(history, "other").order < node_by_id(history, "sprite").order);
    check_edit(node_by_id(history, "sprite").order < node_by_id(history, "group").order);
    check_edit(history.undo.size() == 1);
    require_edit(undo_document_command(history));
    check_edit(bytes(history.document) == before);
}
TEST_CASE("OSBX parent-end drop appends after final descendant even when parent sorts later", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    history.document.storyboards[0].osbx->nodes[0].order = 10;
    history.document.storyboards[0].osbx->nodes[1].order = -10;
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_drop_node_t{.node = "other", .parent = "group"}}));
    check_edit(node_by_id(history, "other").order > node_by_id(history, "group").order);
    check_edit(node_by_id(history, "other").order > node_by_id(history, "sprite").order);
    check_edit(node_by_id(history, "other").parent == "group");
}
TEST_CASE("OSBX duplicate authored children-link owner refreshes link identity without detaching", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    const std::string source = "return function() return {} end";
    history.document.storyboards[0].osbx->nodes[2].children_link = osbx_lua_link_t{.id = "children",
        .source = {.frozen_source = source, .source_reference = "scripts/source.lua",
            .source_hash = native_project_content_checksum(source)}};
    const auto result = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_duplicate_node_t{.node = "other"}});
    require_edit(result.command.applied && result.payload.node.has_value());
    const auto& copy = node_by_id(history, *result.payload.node);
    require_edit(copy.children_link);
    check_edit(copy.children_link->id != "children");
    check_edit(copy.children_link->source.frozen_source == source);
    check_edit(node_by_id(history, "other").children_link->id == "children");
    check_edit(validate_osbx_document(scene(history)).value);
    reject_edit(history, {.storyboard = storyboard,
        .change = osbx_duplicate_node_t{.node = "other/lua/virtual-child"}});
}
TEST_CASE("OSBX duplicate and drop refuse protected descendants before changing data", "[osbx-edit][hierarchy]") {
    auto history = document_history_t::from(edit_song());
    const std::string source = "return function() return {} end";
    history.document.storyboards[0].osbx->nodes[0].children_link = osbx_lua_link_t{.id = "children",
        .source = {.frozen_source = source, .source_reference = "scripts/source.lua",
            .source_hash = native_project_content_checksum(source)}};
    const auto before = history.document;
    const auto duplicate = apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_duplicate_node_t{.node = "sprite"}});
    check_edit<false>(duplicate.applied);
    require_edit<false>(duplicate.diagnostics.empty());
    check_edit(duplicate.diagnostics[0].message.find("detach first") != std::string::npos);
    const auto dropped = apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_drop_node_t{.node = "sprite"}});
    check_edit<false>(dropped.applied);
    // This protected-child fixture is intentionally invalid for publication.
    // Check its full storyboard values, then serialize the remaining song data.
    check_edit(history.document.storyboards == before.storyboards);
    auto remaining_before = before;
    auto remaining_after = history.document;
    remaining_before.storyboards.clear();
    remaining_after.storyboards.clear();
    check_edit(bytes(remaining_after) == bytes(remaining_before));
    check_edit(history.document.revision == before.revision);
    check_edit(history.undo.empty() && history.redo.empty());
}

namespace {
auto retained_preview(const editor_interaction_state_t& state) -> std::shared_ptr<const osbx_edit_preview_t> {
    const auto* pending = state.gesture<pending_osbx_edit_gesture_t>();
    require_edit(pending);
    require_edit(pending->preview_owner);
    return pending->preview_owner;
}
void check_retained_key(const osbx_edit_preview_t& preview, std::int64_t time) {
    const auto& keys = preview.document.storyboards[0].osbx->nodes[1].tracks.opacity.keys;
    check_edit(keys.front().time_ms == time);
    check_edit(preview.capture.revision < preview.document.revision);
}
class preview_link_backend_t final : public osbx_lua_backend_t {
  public:
    mutable int calls{};
    auto prepare_track(const osbx_lua_track_link_t& link, std::size_t) const
        -> diagnosed_t<std::vector<osbx_key_t<>>> override {
        ++calls;
        return {.value = {{.id = "start", .time_ms = link.begin_ms, .value = {0.25}, .easing = osbx_linear_easing_t{}},
            {.id = "end", .time_ms = link.end_ms, .value = {0.75}}}};
    }
    auto prepare_children(const osbx_lua_link_t&) const -> diagnosed_t<osbx_document_t> override {
        ++calls;
        return {.value = {.nodes = {{.id = "generated", .ops = {{.id = "move",
            .track = {.default_value = {50, 20}}}}}}}};
    }
};
auto preview_link(std::string id) -> osbx_lua_link_t {
    const std::string source = "return function() return {0.5} end";
    return {.id = std::move(id), .source = {.frozen_source = source, .source_reference = "scripts/preview.lua",
        .source_hash = native_project_content_checksum(source)}};
}
auto linked_preview_song() -> song_document_t {
    auto song = edit_song();
    auto& nodes = song.storyboards[0].osbx->nodes;
    nodes[1].tracks.opacity.lua = osbx_lua_track_link_t{.link = preview_link("fade-link"), .begin_ms = 25, .end_ms = 75};
    nodes[2].children_link = preview_link("children-link");
    return song;
}
void check_rebased_outputs(const osbx_edit_preview_t& preview, const osbx_lua_prepared_scene_t& original) {
    require_edit(preview.prepared.size() == 1);
    const auto& prepared = preview.prepared[0];
    check_edit(prepared.links == original.links);
    check_edit(prepared.diagnostics == original.diagnostics);
    require_edit(prepared.scene);
    check_edit(prepared.scene->nodes.back() == original.scene->nodes.back());
    const auto& keys = prepared.scene->nodes[1].tracks.opacity.keys;
    check_edit(keys.front().id == "fade" && keys.back().id == "bright");
}
void check_prepared_preview_frame(const osbx_edit_preview_t& preview) {
    const osbx_lua_prepared_scope_t scope{preview.prepared, preview.prepared_generation};
    const auto index = osbx_evaluation_index_t::from(*preview.document.storyboards[0].osbx);
    require_edit(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, storyboard_time_t{.milliseconds = 50}, arena);
    const auto* sprite = frame.node_state(*index.value, "sprite");
    const auto* child = frame.node_state(*index.value, preview.prepared[0].scene->nodes.back().id);
    require_edit(sprite && child);
    check_edit(sprite->matrix[4] == 40 && sprite->matrix[5] == 12);
    check_edit(sprite->opacity == 0.5);
    check_edit(child->matrix[4] == 50 && child->matrix[5] == 20);
}
} // namespace

TEST_CASE("OSBX validated preview retains immutable ownership after revise failure and cancellation",
          "[osbx-edit][gesture][storyboard-gizmo]") {
    auto history = document_history_t::from(edit_song());
    const auto before = write_native_project(history.document); require_edit(before.value);
    const auto revision = history.document.revision;
    editor_interaction_state_t state;
    require_edit(begin_drag(history, state));
    require_edit(update_drag(history, state, 25));
    const auto first = retained_preview(state);
    require_edit(update_drag(history, state, 30));
    const auto second = retained_preview(state);
    check_edit(first != second);
    require_edit(update_drag(history, state, 30));
    check_edit(retained_preview(state) == second);
    check_edit<false>(update_drag(history, state, 100));
    check_edit(retained_preview(state) == second);
    require_edit(dispatch_editor_action(history, state, cancel_editor_gesture_action_t{}));
    check_retained_key(*first, 25); check_retained_key(*second, 30);
    const auto after = write_native_project(history.document); require_edit(after.value);
    check_edit(*after.value == *before.value && history.document.revision == revision);
    check_edit(history.undo.empty() && history.redo.empty());
}

TEST_CASE("OSBX property preview reuses prepared linked outputs and last-good metadata without a backend call",
          "[osbx-edit][gesture][storyboard-gizmo][lua-link]") {
    auto history = document_history_t::from(linked_preview_song());
    preview_link_backend_t backend;
    auto prepared = osbx_lua_prepared_scene_t::from(scene(history), document_stamp_t::from(history.document), backend);
    prepared.storyboard = storyboard;
    require_edit(prepared.complete && prepared.scene);
    prepared.links[0].is_last_good = true;
    prepared.diagnostics.push_back({.code = osbx_diagnostic_code_t::invalid_lua_link, .message = "retained source error"});
    const osbx_lua_prepared_scope_t scope{std::span{&prepared, 1}, 17};
    const auto calls = backend.calls;
    editor_interaction_state_t state;
    require_edit(begin_change(history, state, osbx_set_default_t{
        .track = {.node = "sprite", .kind = osbx_track_kind_t::transform, .op = "translate"},
        .value = std::vector<double>{40, 12}}));
    const auto preview = retained_preview(state);
    check_rebased_outputs(*preview, prepared);
    check_prepared_preview_frame(*preview);
    check_edit(backend.calls == calls);
    check_edit(preview->document.storyboards[0].osbx->nodes.size() == history.document.storyboards[0].osbx->nodes.size());
    auto changed = scene(history); changed.nodes[1].tracks.opacity.lua->end_ms = 80;
    check_edit<false>(osbx_lua_prepared_scope_t::rebase(scene(history), changed,
        document_stamp_t::from(history.document), storyboard));
}

TEST_CASE("OSBX property preview refuses linked sources without their installed preparation",
          "[osbx-edit][gesture][storyboard-gizmo][lua-link][negative]") {
    auto history = document_history_t::from(linked_preview_song());
    editor_interaction_state_t state;
    check_edit<false>(begin_change(history, state, osbx_set_default_t{
        .track = {.node = "sprite", .kind = osbx_track_kind_t::transform, .op = "translate"},
        .value = std::vector<double>{40, 12}}));
    check_edit<false>(state.is_pending());
    check_edit(history.undo.empty());
}

TEST_CASE("OSBX preview document alias keeps its immutable snapshot alive after proposal retirement",
          "[osbx-edit][gesture][storyboard-gizmo][lifetime]") {
    auto history = document_history_t::from(edit_song());
    std::shared_ptr<const song_document_t> document;
    std::weak_ptr<const osbx_edit_preview_t> owner;
    {
        editor_interaction_state_t state;
        require_edit(begin_drag(history, state)); require_edit(update_drag(history, state, 25));
        const auto* pending = state.gesture<pending_osbx_edit_gesture_t>(); require_edit(pending);
        document = pending->preview_document(); owner = pending->preview_owner;
        require_edit(dispatch_editor_action(history, state, cancel_editor_gesture_action_t{}));
    }
    check_edit<false>(owner.expired());
    check_edit(document->storyboards[0].osbx->nodes[1].tracks.opacity.keys.front().time_ms == 25);
    document.reset();
    check_edit(owner.expired());
}

namespace {
void check_placed_sprite(const song_document_t& song, const osbx_edit_created_ids_t& created) {
    REQUIRE(created.node);
    const auto& scene = *song.storyboards.front().osbx;
    const auto node = std::ranges::find(scene.nodes, *created.node, &osbx_node_t::id);
    REQUIRE(node != scene.nodes.end());
    CHECK(node->image == "image");
    REQUIRE(node->ops.size() == 1);
    CHECK(node->ops.front().track.default_value == std::vector<double>{12.5, -8});
    CHECK_FALSE(created.image);
}
auto prepared_image_import(document_history_t& history) -> osbx_place_image_t {
    const auto asset = next_resource_asset_id(history.document);
    return {.source = osbx_import_image_t{.file = {.source_path = "/tmp/source.png",
        .copied_project_relative_path = "assets/" + std::to_string(asset->value) + "-source.png", .expected_new_asset_id = asset,
        .role = resource_role_t::bga, .name = "Imported image", .image_geometry = std::pair{1U, 1U}}},
        .sprite = osbx_sprite_placement_t{.x = 20, .y = 30}};
}
void check_atomic_import(const document_history_t& history, const osbx_edit_created_ids_t& created) {
    REQUIRE(created.image);
    REQUIRE(created.node);
    CHECK(history.document.visual_assets.size() == 1);
    CHECK(history.document.resources.size() == 1);
    CHECK(history.undo.size() == 1);
    CHECK(history.document.storyboards.front().osbx->images.back().id == *created.image);
}
}
TEST_CASE("OSBX asset placement creates one sprite and translation from an existing image in one Undo",
          "[osbx-edit][storyboard-assets]") {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    const auto placed = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_place_image_t{.source = osbx_existing_image_t{"image"},
            .sprite = osbx_sprite_placement_t{.x = 12.5, .y = -8}}});
    REQUIRE(placed.command.applied);
    check_placed_sprite(history.document, placed.payload);
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(bytes(history.document) == before);
}
TEST_CASE("OSBX native image declaration and sprite publish atomically with explicit created IDs",
          "[osbx-edit][storyboard-assets]") {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    const auto placed = apply_osbx_edit_with_created_ids(history,
        {.storyboard = storyboard, .change = prepared_image_import(history)});
    REQUIRE(placed.command.applied);
    check_atomic_import(history, placed.payload);
    REQUIRE(undo_document_command(history));
    CHECK(bytes(history.document) == before);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.resources.size() == 1);
}
TEST_CASE("OSBX invalid sprite placement rolls back imported resource image and identity payload",
          "[osbx-edit][storyboard-assets][negative]") {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    auto operation = prepared_image_import(history);
    operation.sprite->x = std::numeric_limits<double>::infinity();
    const auto placed = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard, .change = operation});
    CHECK_FALSE(placed.command.applied);
    CHECK_FALSE(placed.payload.image);
    CHECK_FALSE(placed.payload.node);
    CHECK(bytes(history.document) == before);
    CHECK(history.undo.empty());
}

TEST_CASE("OSBX song image relink declaration and sprite share one atomic publication",
          "[osbx-edit][storyboard-assets][resource]") {
    auto song = edit_song();
    song.visual_assets = {{.id = asset_id_t{7}, .kind = asset_kind_t::image, .source_path = "/tmp/image.png"}};
    song.resources = {{.id = resource_id_t{8}, .asset = asset_id_t{7}, .roles = {resource_role_t::bga}}};
    auto history = document_history_t::from(std::move(song));
    const auto before = bytes(history.document);
    const auto result = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_place_image_t{.source = osbx_resource_image_t{.resource = resource_id_t{8}, .asset = asset_id_t{7},
            .source_path = "/tmp/image.png", .file = "assets/7-image.png"}, .sprite = osbx_sprite_placement_t{}}});
    REQUIRE(result.command.applied);
    REQUIRE(result.payload.image);
    CHECK(history.document.visual_assets.front().source_path == "assets/7-image.png");
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(bytes(history.document) == before);
}
TEST_CASE("OSBX song image source replacement refuses relink and publishes no created identities",
          "[osbx-edit][storyboard-assets][resource][negative]") {
    auto song = edit_song();
    song.visual_assets = {{.id = asset_id_t{7}, .kind = asset_kind_t::image, .source_path = "/tmp/replaced.png"}};
    song.resources = {{.id = resource_id_t{8}, .asset = asset_id_t{7}, .roles = {resource_role_t::bga}}};
    auto history = document_history_t::from(std::move(song));
    const auto before = bytes(history.document);
    const auto result = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_place_image_t{.source = osbx_resource_image_t{.resource = resource_id_t{8}, .asset = asset_id_t{7},
            .source_path = "/tmp/image.png", .file = "assets/7-image.png"}, .sprite = osbx_sprite_placement_t{}}});
    CHECK_FALSE(result.command.applied);
    CHECK_FALSE(result.payload.image);
    CHECK_FALSE(result.payload.node);
    CHECK(history.undo.empty());
    CHECK(bytes(history.document) == before);
}

namespace {
auto editable_lua_track() -> osbx_lua_track_link_t {
    const std::string source = "return function(t,ctx) return 0.5 end";
    return {.link = {.source = {.frozen_source = source, .source_reference = "curve.lua",
        .source_hash = native_project_content_checksum(source)}}, .begin_ms = 0, .end_ms = 100};
}
}
TEST_CASE("OSBX Lua link set and unlink allocate once and retain authored hand keys", "[osbx-edit][lua-link]") {
    auto history = document_history_t::from(edit_song());
    const auto keys = sprite(history).tracks.opacity.keys;
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_set_lua_track_t{
        .track = {.node = "sprite"}, .link = editable_lua_track()}}).applied);
    const auto id = sprite(history).tracks.opacity.lua->link.id;
    REQUIRE_FALSE(id.empty());
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_set_lua_track_t{
        .track = {.node = "sprite"}, .expected_link = id}}).applied);
    CHECK_FALSE(sprite(history).tracks.opacity.lua);
    CHECK(sprite(history).tracks.opacity.keys == keys);
    CHECK(history.undo.size() == 2);
}
TEST_CASE("OSBX Lua link identity allocation reserves redo history", "[osbx-edit][lua-link]") {
    auto history = document_history_t::from(edit_song());
    const osbx_edit_t link_edit{.storyboard = storyboard, .change = osbx_set_lua_track_t{
        .track = {.node = "sprite"}, .link = editable_lua_track()}};
    REQUIRE(apply_osbx_edit(history, link_edit).applied);
    const auto first = sprite(history).tracks.opacity.lua->link.id;
    REQUIRE(undo_document_command(history));
    REQUIRE(apply_osbx_edit(history, link_edit).applied);
    CHECK(sprite(history).tracks.opacity.lua->link.id != first);
    CHECK(history.redo.empty());
}
TEST_CASE("OSBX Lua link stale ID and authored child replacement reject atomically", "[osbx-edit][lua-link]") {
    auto history = document_history_t::from(edit_song());
    const auto before = bytes(history.document);
    const auto stale = apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_set_lua_track_t{
        .track = {.node = "sprite"}, .expected_link = "missing", .link = editable_lua_track()}});
    CHECK_FALSE(stale.applied);
    const auto children = apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_set_lua_children_t{
        .node = "group", .link = editable_lua_track().link}});
    CHECK_FALSE(children.applied);
    CHECK(bytes(history.document) == before);
    CHECK(history.undo.empty());
}
TEST_CASE("OSBX linked mixed key batches roll back all earlier mutations on a generated ID", "[osbx-edit][lua-link]") {
    auto song = edit_song();
    song.storyboards[0].osbx->nodes[1].tracks.opacity.keys[0].time_ms = -20;
    auto history = document_history_t::from(song);
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_set_lua_track_t{
        .track = {.node = "sprite"}, .link = editable_lua_track()}}).applied);
    const auto before = bytes(history.document);
    const auto refused = apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_move_keys_t{
        .keys = {{.track = {.node = "sprite"}, .key = "fade"},
            {.track = {.node = "sprite"}, .key = "sprite/lua/sample/0"}}, .delta_ms = 5}});
    CHECK_FALSE(refused.applied);
    CHECK(bytes(history.document) == before);
    CHECK(history.undo.size() == 1);
}
TEST_CASE("OSBX Lua frozen-source hash mismatches refuse without bytes or history changes", "[osbx-edit][lua-link]") {
    auto history = document_history_t::from(edit_song());
    auto link = editable_lua_track(); link.link.source.source_hash.assign(64, '0');
    const auto before = bytes(history.document);
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_set_lua_track_t{
        .track = {.node = "sprite"}, .link = std::move(link)}});
    CHECK_FALSE(result.applied);
    CHECK_FALSE(result.diagnostics.empty());
    CHECK(bytes(history.document) == before);
    CHECK(history.undo.empty());
}


namespace {
auto generated_edit_song() -> song_document_t {
    auto song = edit_song();
    song.charts = {{.id = chart_id_t{100}, .measures = {{.id = measure_id_t{101}}}}};
    auto& sprite = song.storyboards.front().osbx->nodes[1];
    sprite.lifetime = osbx_lifetime_t{.begin_ms = 0, .end_ms = 300};
    sprite.tracks.opacity.lua = editable_lua_track();
    sprite.tracks.opacity.lua->link.id = "generated-opacity";
    song.storyboards.front().owning_generator_recipe = recipe_id_t{40};
    song.storyboards.push_back({.id = storyboard_id_t{4}, .name = "Other output", .osbx = osbx_document_t{},
        .owning_generator_recipe = recipe_id_t{40}});
    const std::string source = "return {}";
    song.generator_recipes = {{.id = recipe_id_t{40}, .api_version = "1", .source_path = "scripts/story.lua",
        .source_hash = native_project_content_checksum(source), .source = source, .chart = chart_id_t{100},
        .generated_storyboards = {{.key = "scene", .storyboard = storyboard, .value = *song.storyboards.front().osbx},
            {.key = "other", .storyboard = storyboard_id_t{4}}}}};
    return song;
}
void check_only_storyboard_detached(const song_document_t& actual, song_document_t expected) {
    expected.storyboards.front().owning_generator_recipe.reset();
    expected.generator_recipes.front().generated_storyboards.erase(
        expected.generator_recipes.front().generated_storyboards.begin());
    expected.revision = actual.revision;
    CHECK(bytes(actual) == bytes(expected));
    CHECK(actual.storyboards.front().id == storyboard);
    CHECK(actual.storyboards.back().owning_generator_recipe == recipe_id_t{40});
}
void check_detach_ownership_unchanged(const document_history_t& history, const song_document_t& before) {
    CHECK(history.document.storyboards == before.storyboards);
    CHECK(history.document.generator_recipes == before.generator_recipes);
    CHECK(history.document.revision == before.revision);
    CHECK(history.undo.empty());
}
void check_detach_rejected(const document_history_t& history, const song_document_t& before,
    const document_command_result_t& result) {
    CHECK_FALSE(result.applied);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_binding);
    check_detach_ownership_unchanged(history, before);
}
} // namespace

TEST_CASE("OSBX per-storyboard detach preserves the scene and all other recipe outputs", "[osbx-edit][storyboard-detach]") {
    auto history = document_history_t::from(generated_edit_song());
    const auto before = history.document;
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_detach_storyboard_t{.expected_owner = recipe_id_t{40}}});
    REQUIRE(result.applied);
    CHECK(history.undo.size() == 1);
    check_only_storyboard_detached(history.document, before);
}

TEST_CASE("OSBX per-storyboard detach undo and redo restore both ownership records exactly", "[osbx-edit][storyboard-detach]") {
    auto history = document_history_t::from(generated_edit_song());
    const auto before = bytes(history.document);
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_detach_storyboard_t{.expected_owner = recipe_id_t{40}}}).applied);
    const auto after = bytes(history.document);
    REQUIRE(undo_document_command(history));
    CHECK(bytes(history.document) == before);
    REQUIRE(redo_document_command(history));
    CHECK(bytes(history.document) == after);
}

TEST_CASE("OSBX per-storyboard detach rejects stale missing and ambiguous ownership atomically", "[osbx-edit][storyboard-detach]") {
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    auto history = document_history_t::from(generated_edit_song());
    auto owner = recipe_id_t{40};
    auto& recipe = history.document.generator_recipes.front();
    if (mode == 0) owner = {};
    if (mode == 1) owner = recipe_id_t{41};
    if (mode == 2) recipe.generated_storyboards.erase(recipe.generated_storyboards.begin());
    if (mode == 3) recipe.generated_storyboards.push_back(recipe.generated_storyboards.front());
    if (mode == 4) history.document.generator_recipes.clear();
    if (mode == 5) history.document.generator_recipes.push_back(recipe);
    if (mode == 6) recipe.generated_storyboards.front().value.canvas.width = 800;
    if (mode == 7) history.document.storyboards.front().osbx.reset();
    if (mode == 8) history.document.storyboards.front().owning_generator_recipe.reset();
    if (mode == 9) {
        auto other = recipe;
        other.id = recipe_id_t{41};
        history.document.generator_recipes.push_back(std::move(other));
    }
    if (mode == 10) recipe.generated_storyboards.front().key.clear();
    if (mode == 11) recipe.generated_storyboards.back().key = recipe.generated_storyboards.front().key;
    const auto before = history.document;
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard,
        .change = osbx_detach_storyboard_t{.expected_owner = owner}});
    check_detach_rejected(history, before, result);
}

TEST_CASE("OSBX clip use cloning refreshes use IDs and retains shared definitions through Undo Redo", "[osbx-edit][clips]") {
    auto song = edit_song();
    song.storyboards[0].osbx->clips = {{.id = "motion", .ops = {{.id = "clip-op"}}}};
    song.storyboards[0].osbx->nodes[1].uses = {{.id = "original-use", .clip = "motion"}};
    auto history = document_history_t::from(song);
    const auto before = bytes(history.document);
    const auto result = apply_osbx_edit_with_created_ids(history, {.storyboard = storyboard,
        .change = osbx_duplicate_node_t{.node = "sprite"}});
    require_edit(result.command.applied && result.payload.node.has_value());
    const auto& clone = node_by_id(history, *result.payload.node);
    require_edit(clone.uses.size() == 1);
    check_edit(clone.uses[0].id != "original-use");
    check_edit(clone.uses[0].clip == "motion");
    check_edit(scene(history).clips == song.storyboards[0].osbx->clips);
    const auto after = bytes(history.document);
    require_edit(undo_document_command(history)); check_edit(bytes(history.document) == before);
    require_edit(redo_document_command(history)); check_edit(bytes(history.document) == after);
}

TEST_CASE("OSBX edit ID allocation reserves definition op key and use identities", "[osbx-edit][clips]") {
    auto song = edit_song();
    auto& value = *song.storyboards[0].osbx;
    value.clips = {{.id = "sprite/use/1", .ops = {{.id = "sprite/use/2", .track = {
        .default_value = {0,0}, .keys = {{.id = "sprite/use/3", .value = {0,0}}}}}}}};
    value.nodes[1].uses = {{.id = "sprite/use/4", .clip = "sprite/use/1"}};
    auto history = document_history_t::from(song);
    const auto ids = osbx_edit_ids_t::from(history);
    check_edit(ids.reserved.contains("sprite/use/1"));
    check_edit(ids.reserved.contains("sprite/use/2"));
    check_edit(ids.reserved.contains("sprite/use/3"));
    check_edit(ids.reserved.contains("sprite/use/4"));
    check_edit(ids.next("sprite", "use", value) != "sprite/use/4");
}

TEST_CASE("OSBX subtree clone refreshes every temporal program and nested key identity", "[osbx-edit][program]") {
    auto song = edit_song();
    song.storyboards[0].osbx->nodes[1].tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat", .count = 2,
        .keys = {{.id = "inside", .value = {0.5}}}}}};
    auto history = document_history_t::from(song);
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_duplicate_node_t{.node = "sprite"}});
    require_edit(result);
    require_edit(validate_osbx_document(*history.document.storyboards[0].osbx).value);
    check_edit(history.document.storyboards[0].osbx->nodes.size() == song.storyboards[0].osbx->nodes.size() + 1);
}

TEST_CASE("OSBX Bezier bake replaces its range as one undoable native-key edit", "[osbx-edit][storyboard-spline]") {
    const auto curve = storyboard_spline_t{.begin_ms = 0, .end_ms = 1000,
        .points = {{{0,0}, {0,100}, {100,100}, {100,0}}}};
    auto history = round_trip(osbx_bake_spline_t{
        .track = {.node = "sprite", .kind = osbx_track_kind_t::transform, .op = "translate"}, .spline = curve});
    const auto& keys = sprite(history).ops[1].track.keys;
    require_edit(keys.size() > 2);
    check_edit(keys.front().time_ms == 0 && keys.back().time_ms == 1000);
    const auto parsed = parse_native_project(bytes(history.document)); require_edit(parsed.value);
    check_edit(parsed.value->storyboards == history.document.storyboards);
    const auto allocated = keys.front().id;
    require_edit(undo_document_command(history));
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard, .change = osbx_bake_spline_t{
        .track = {.node = "sprite", .kind = osbx_track_kind_t::transform, .op = "translate"}, .spline = curve}}).applied);
    check_edit(sprite(history).ops[1].track.keys.front().id != allocated);
}

TEST_CASE("OSBX Bezier preserves outside keys and refuses linked program and unsupported tracks", "[osbx-edit][storyboard-spline]") {
    auto history = document_history_t::from(edit_song());
    auto& track = history.document.storyboards[0].osbx->nodes[1].ops[1].track;
    track.keys = {{.id = "outside-left", .time_ms = -20, .value = {1,2}},
        {.id = "replaced", .time_ms = 500, .value = {99,99}}, {.id = "outside-right", .time_ms = 2000, .value = {3,4}}};
    const osbx_bake_spline_t bake{.track = {.node = "sprite", .kind = osbx_track_kind_t::transform, .op = "translate"},
        .spline = {.points = {{{0,0}, {0,100}, {100,100}, {100,0}}}}};
    require_edit(apply_osbx_edit(history, {.storyboard = storyboard, .change = bake}).applied);
    const auto& keys = sprite(history).ops[1].track.keys;
    check_edit(keys.front().id == "outside-left" && keys.back().id == "outside-right");
    check_edit(std::ranges::find(keys, "replaced", &osbx_key_t<>::id) == keys.end());
    auto rejected = document_history_t::from(edit_song());
    rejected.document.storyboards[0].osbx->nodes[1].ops[1].track.lua = osbx_lua_track_link_t{.link = preview_link("curve-link")};
    reject_edit(rejected, {.storyboard = storyboard, .change = bake});
    rejected = document_history_t::from(edit_song());
    rejected.document.storyboards[0].osbx->nodes[1].ops[1].track.program.push_back({.data = osbx_loop_t<>{
        .id = "curve-program", .keys = {{.id = "program-start", .time_ms = 0, .value = {0,0}},
            {.id = "program-end", .time_ms = 100, .value = {1,1}}}}});
    reject_edit(rejected, {.storyboard = storyboard, .change = bake});
    auto mismatch = bake; mismatch.spline.dimensions = 1;
    reject_change(mismatch);
    mismatch = bake; mismatch.track.op = "pivot";
    reject_change(mismatch);
}
