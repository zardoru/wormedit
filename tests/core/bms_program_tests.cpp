#include <wormedit/core/bms_program.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <limits>

using namespace wormedit;
namespace {
auto root_program() -> bms_program_t {
    auto program = bms_program_t::from_root(branch_id_t{1}, branch_id_t{2});
    REQUIRE(program);
    return std::move(*program);
}
auto copy_nodes(const bms_program_t& program) -> std::vector<bms_program_node_t> {
    return {program.nodes().begin(), program.nodes().end()};
}
auto nested_program() -> bms_program_t {
    auto random = root_program().with_random(branch_id_t{1}, branch_id_t{3}, branch_id_t{4}, 2);
    REQUIRE(random);
    auto conditional = random->with_if(branch_id_t{3}, branch_id_t{5}, branch_id_t{6}, 1);
    REQUIRE(conditional);
    return std::move(*conditional);
}
}
TEST_CASE("BMS program creates an empty Root with its own first Chart", "[bms-program]") {
    const auto program = root_program();
    CHECK(program.root() == branch_id_t{1});
    REQUIRE(program.find(branch_id_t{1}));
    CHECK(program.find(branch_id_t{1})->is_scope());
    CHECK(program.target_chart(branch_id_t{1}) == branch_id_t{2});
}
TEST_CASE("BMS leaf and absent target queries are explicit", "[bms-program]") {
    const auto program = root_program();
    CHECK(program.target_chart(branch_id_t{2}) == branch_id_t{2});
    CHECK_FALSE(program.find(branch_id_t{99}));
    CHECK_FALSE(program.target_chart(branch_id_t{99}));
    CHECK(std::get<bms_chart_content_t>(program.find(branch_id_t{2})->value).items.empty());
}
TEST_CASE("BMS parent queries distinguish Root children and missing identities", "[bms-program]") {
    const auto program = root_program();
    CHECK(program.parent(branch_id_t{2}) == branch_id_t{1});
    CHECK_FALSE(program.parent(branch_id_t{1}));
    CHECK_FALSE(program.parent(branch_id_t{99}));
}
TEST_CASE("BMS scope factories supply separate Chart children and stable parents", "[bms-program]") {
    const auto program = nested_program();
    CHECK(program.nodes().size() == 6);
    CHECK(program.target_chart(branch_id_t{3}) == branch_id_t{4});
    CHECK(program.target_chart(branch_id_t{5}) == branch_id_t{6});
    CHECK(program.parent(branch_id_t{3}) == branch_id_t{1});
    CHECK(program.parent(branch_id_t{5}) == branch_id_t{3});
}
TEST_CASE("BMS scope parameters remain distinct from their Chart children", "[bms-program]") {
    const auto program = nested_program();
    CHECK(std::get<bms_random_scope_t>(program.find(branch_id_t{3})->value).range == 2);
    CHECK(std::get<bms_if_scope_t>(program.find(branch_id_t{5})->value).value == 1);
}
namespace {
auto ordered_program() -> bms_program_t {
    auto nodes = copy_nodes(nested_program());
    nodes[0].children = {branch_id_t{7}, branch_id_t{3}, branch_id_t{8}, branch_id_t{2}};
    nodes.push_back({.id = branch_id_t{7}, .value = bms_text_t{"; before Chart\r\n", text_encoding_t::cp932}});
    nodes.push_back({.id = branch_id_t{8}, .value = bms_chart_content_t{}});
    // Registry order does not identify a node or determine its sibling order.
    std::ranges::reverse(nodes);
    auto program = bms_program_t::from(std::move(nodes));
    REQUIRE(program);
    return std::move(*program);
}
}
TEST_CASE("BMS target resolution uses direct Chart order and never Text or a descendant", "[bms-program]") {
    const auto value = ordered_program();
    const auto* program = &value;
    CHECK(program->root() == branch_id_t{1});
    CHECK(program->target_chart(branch_id_t{1}) == branch_id_t{8});
    CHECK(program->target_chart(branch_id_t{2}) == branch_id_t{2});
    CHECK_FALSE(program->target_chart(branch_id_t{7}));
}
TEST_CASE("BMS ordered Text retains bytes and encoding", "[bms-program]") {
    const auto value = ordered_program();
    const auto* program = &value;
    const auto& text = std::get<bms_text_t>(program->find(branch_id_t{7})->value);
    CHECK(text.bytes == "; before Chart\r\n");
    CHECK(text.encoding == text_encoding_t::cp932);
    CHECK(program->find(branch_id_t{1})->children.front() == branch_id_t{7});
}
TEST_CASE("BMS program retains typed inactive declarations and endpoint contributions", "[bms-program]") {
    auto nodes = copy_nodes(nested_program());
    const bms_program_position_t position{measure_id_t{100}, rational_t{1, 7}};
    std::vector<bms_content_value_t> values{
        bms_note_content_t{.position = position, .lane = lane_id_t{10}, .wav = "01"},
        bms_hold_endpoint_t{position, lane_id_t{10}, "02"},
        bms_bgm_content_t{.position = position, .column = object_id_t{11}, .structural_slot = 3},
        bms_bga_content_t{position, visual_event_kind_t::layer2, "03"},
        bms_timing_definition_t{bms_timing_definition_kind_t::stop, "04", {192, 1}},
        bms_timing_reference_t{position, bms_timing_definition_kind_t::stop, "04"},
        bms_direct_bpm_t{position, {120, 1}},
        bms_measure_length_t{.measure = measure_id_t{100}, .length = {3, 4}},
        bms_resource_declaration_t{bms_resource_namespace_t::wav, "01", resource_id_t{12}},
        bms_metadata_declaration_t{bms_metadata_field_t::title, std::string{"Inactive title"}}};
    auto& content = std::get<bms_chart_content_t>(nodes[5].value);
    for (std::size_t i = 0; i < values.size(); ++i)
        content.items.push_back({.id = object_id_t{200 + i}, .value = values[i]});
    auto program = bms_program_t::from(nodes);
    REQUIRE(program);
    const auto& stored = std::get<bms_chart_content_t>(program->find(branch_id_t{6})->value).items;
    CHECK(stored == content.items);
    CHECK(std::get<bms_hold_endpoint_t>(stored[1].value).position.fraction == rational_t{1, 7});
    CHECK(std::get<bms_chart_content_t>(program->find(branch_id_t{2})->value).items.empty());
    CHECK(std::get<bms_chart_content_t>(program->find(branch_id_t{4})->value).items.empty());
}
TEST_CASE("BMS validated values copy and rebuild without identity changes", "[bms-program]") {
    const auto program = nested_program();
    const auto rebuilt = bms_program_t::from(copy_nodes(program));
    REQUIRE(rebuilt);
    CHECK(*rebuilt == program);
}
TEST_CASE("BMS graph construction rejects missing duplicate or zero identities", "[bms-program]") {
    auto nodes = copy_nodes(root_program());
    SECTION("empty program") { nodes.clear(); }
    SECTION("no Root") { nodes[0].value = bms_random_scope_t{2}; }
    SECTION("two Roots") { nodes[1].value = bms_root_scope_t{}; }
    SECTION("zero node ID") { nodes[1].id = {}; }
    SECTION("duplicate node ID") { nodes[1].id = nodes[0].id; }
    SECTION("missing child") { nodes[0].children = {branch_id_t{99}}; }
    SECTION("Root referenced as child") { nodes[0].children = {branch_id_t{1}}; }
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE_FALSE(program);
    CHECK(program.error().code == failure_code_t::invalid_data);
}
TEST_CASE("BMS graph construction rejects ambiguous orphan cyclic and leaf ownership", "[bms-program]") {
    auto nodes = copy_nodes(nested_program());
    SECTION("two parents") { nodes[2].children.push_back(branch_id_t{2}); }
    SECTION("duplicate child") { nodes[2].children.push_back(branch_id_t{4}); }
    SECTION("orphan subtree") { nodes[0].children = {branch_id_t{2}}; }
    SECTION("Chart has children") { nodes[1].children = {branch_id_t{3}}; }
    SECTION("Text has children") { nodes[2].value = bms_text_t{}; }
    SECTION("disconnected cycle") {
        nodes[0].children = {branch_id_t{2}};
        nodes[4].value = bms_random_scope_t{2};
        nodes[4].children.push_back(branch_id_t{3});
    }
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE_FALSE(program);
    CHECK(program.error().code == failure_code_t::invalid_data);
}
TEST_CASE("BMS scopes require a direct Chart and valid agreed grammar", "[bms-program]") {
    auto nodes = copy_nodes(nested_program());
    SECTION("Root only has nested Charts") { nodes[0].children = {branch_id_t{3}}; nodes.erase(nodes.begin() + 1); }
    SECTION("RANDOM only has nested Charts") { nodes[2].children = {branch_id_t{5}}; nodes.erase(nodes.begin() + 3); }
    SECTION("IF has no Chart") { nodes[5].value = bms_text_t{}; }
    SECTION("RANDOM has zero range") { nodes[2].value = bms_random_scope_t{0}; }
    SECTION("RANDOM has negative range") { nodes[2].value = bms_random_scope_t{-1}; }
    SECTION("IF has no selector") { nodes[2].value = bms_if_scope_t{1}; }
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE_FALSE(program);
    CHECK(program.error().code == failure_code_t::invalid_data);
}
TEST_CASE("BMS prepared values do not decide out of range arm policy", "[bms-program]") {
    const auto random = root_program().with_random(branch_id_t{1}, branch_id_t{3}, branch_id_t{4}, 2);
    REQUIRE(random);
    const auto program = random->with_if(branch_id_t{3}, branch_id_t{5}, branch_id_t{6}, 9);
    REQUIRE(program);
    CHECK(std::get<bms_if_scope_t>(program->find(branch_id_t{5})->value).value == 9);
    CHECK(std::get<bms_random_scope_t>(program->find(branch_id_t{3})->value).range == 2);
}
TEST_CASE("BMS scope factory failures leave the original value untouched", "[bms-program]") {
    const auto program = root_program();
    const auto before = program;
    result_t<bms_program_t, wormedit::error_t<failure_code_t>> value{program};
    SECTION("missing parent") { value = program.with_random(branch_id_t{99}, branch_id_t{3}, branch_id_t{4}, 2); }
    SECTION("Chart parent") { value = program.with_random(branch_id_t{2}, branch_id_t{3}, branch_id_t{4}, 2); }
    SECTION("duplicate ID") { value = program.with_random(branch_id_t{1}, branch_id_t{2}, branch_id_t{4}, 2); }
    SECTION("duplicate new IDs") { value = program.with_random(branch_id_t{1}, branch_id_t{3}, branch_id_t{3}, 2); }
    SECTION("zero range") { value = program.with_random(branch_id_t{1}, branch_id_t{3}, branch_id_t{4}, 0); }
    SECTION("IF outside RANDOM") { value = program.with_if(branch_id_t{1}, branch_id_t{3}, branch_id_t{4}, 1); }
    REQUIRE_FALSE(value);
    CHECK(program == before);
}
TEST_CASE("BMS graph bounds accept exact capacity and reject node or depth overflow", "[bms-program]") {
    const auto root = root_program();
    CHECK(bms_program_t::from(copy_nodes(root), {.maximum_nodes = 2, .maximum_depth = 2}));
    bms_program_limits_t limits;
    SECTION("node limit") { limits.maximum_nodes = 1; }
    SECTION("depth limit") { limits.maximum_depth = 1; }
    const auto value = bms_program_t::from(copy_nodes(root), limits);
    REQUIRE_FALSE(value);
    CHECK(value.error().code == failure_code_t::capacity_exceeded);
}
TEST_CASE("BMS scope factory budgets include its automatic Chart child", "[bms-program]") {
    const auto value = root_program().with_random(branch_id_t{1}, branch_id_t{3}, branch_id_t{4}, 2, {.maximum_nodes = 3});
    REQUIRE_FALSE(value);
    CHECK(value.error().code == failure_code_t::capacity_exceeded);
}
TEST_CASE("BMS Text byte limits include all ordered blocks", "[bms-program]") {
    auto nodes = copy_nodes(root_program());
    nodes[0].children.insert(nodes[0].children.end(), {branch_id_t{3}, branch_id_t{4}});
    nodes.push_back({.id = branch_id_t{3}, .value = bms_text_t{"ab", text_encoding_t::utf8}});
    nodes.push_back({.id = branch_id_t{4}, .value = bms_text_t{"cd", text_encoding_t::utf8}});
    CHECK(bms_program_t::from(nodes, {.maximum_text_bytes = 4}));
    const auto value = bms_program_t::from(nodes, {.maximum_text_bytes = 3});
    REQUIRE_FALSE(value);
    CHECK(value.error().code == failure_code_t::capacity_exceeded);
}
TEST_CASE("BMS inline and standalone Text share the byte limit", "[bms-program][flat-bms]") {
    auto nodes = copy_nodes(root_program());
    nodes[0].children.push_back(branch_id_t{3});
    nodes.push_back({.id = branch_id_t{3}, .value = bms_text_t{"ab", text_encoding_t::utf8}});
    std::get<bms_chart_content_t>(nodes[1].value).items.push_back({.id = object_id_t{4},
        .value = bms_text_t{"cd", text_encoding_t::utf8}});
    CHECK(bms_program_t::from(nodes, {.maximum_text_bytes = 4}));
    const auto value = bms_program_t::from(nodes, {.maximum_text_bytes = 3});
    REQUIRE_FALSE(value);
    CHECK(value.error().code == failure_code_t::capacity_exceeded);
}
TEST_CASE("BMS content item limits include inactive Charts", "[bms-program]") {
    auto nodes = copy_nodes(nested_program());
    std::get<bms_chart_content_t>(nodes[1].value).items.push_back({.id = object_id_t{10}, .value = bms_metadata_declaration_t{}});
    std::get<bms_chart_content_t>(nodes[5].value).items.push_back({.id = object_id_t{11}, .value = bms_metadata_declaration_t{}});
    CHECK(bms_program_t::from(nodes, {.maximum_content_items = 2}));
    const auto value = bms_program_t::from(nodes, {.maximum_content_items = 1});
    REQUIRE_FALSE(value);
    CHECK(value.error().code == failure_code_t::capacity_exceeded);
}
TEST_CASE("BMS content identities are unique across active and inactive owners", "[bms-program]") {
    auto nodes = copy_nodes(nested_program());
    std::get<bms_chart_content_t>(nodes[1].value).items.push_back({.id = object_id_t{10}, .value = bms_metadata_declaration_t{}});
    auto& other = std::get<bms_chart_content_t>(nodes[5].value).items;
    SECTION("duplicate") { other.push_back({.id = object_id_t{10}, .value = bms_resource_declaration_t{}}); }
    SECTION("zero") { other.push_back({.id = object_id_t{}, .value = bms_metadata_declaration_t{}}); }
    const auto value = bms_program_t::from(std::move(nodes));
    REQUIRE_FALSE(value);
    CHECK(value.error().code == failure_code_t::invalid_data);
}
TEST_CASE("BMS stable identities do not depend on contiguous numbering", "[bms-program]") {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto program = bms_program_t::from_root(branch_id_t{maximum}, branch_id_t{7});
    REQUIRE(program);
    CHECK(program->target_chart(branch_id_t{maximum}) == branch_id_t{7});
    CHECK(program->parent(branch_id_t{7}) == branch_id_t{maximum});
    CHECK_FALSE(bms_program_t::from_root(branch_id_t{}, branch_id_t{7}));
}

namespace {
auto switch_nodes() -> std::vector<bms_program_node_t> {
    return {
        {.id = branch_id_t{1}, .value = bms_root_scope_t{}, .children = {branch_id_t{2}, branch_id_t{9}, branch_id_t{10}, branch_id_t{3}}},
        {.id = branch_id_t{2}, .value = bms_chart_content_t{}},
        {.id = branch_id_t{3}, .value = bms_switch_scope_t{2}, .children = {branch_id_t{4}, branch_id_t{5}, branch_id_t{7}}},
        {.id = branch_id_t{4}, .value = bms_chart_content_t{}},
        {.id = branch_id_t{5}, .value = bms_case_scope_t{.value = 1}, .children = {branch_id_t{6}}},
        {.id = branch_id_t{6}, .value = bms_chart_content_t{}},
        {.id = branch_id_t{7}, .value = bms_case_scope_t{}, .children = {branch_id_t{8}}},
        {.id = branch_id_t{8}, .value = bms_chart_content_t{}},
        {.id = branch_id_t{9}, .value = bms_set_random_t{2}},
        {.id = branch_id_t{10}, .value = bms_set_switch_t{1}}};
}
}
TEST_CASE("BMS fixed selectors are settings leaves without invented Chart bodies", "[bms-program]") {
    const auto program = bms_program_t::from(switch_nodes());
    REQUIRE(program);
    CHECK_FALSE(program->find(branch_id_t{9})->is_scope());
    CHECK_FALSE(program->target_chart(branch_id_t{10}));
    CHECK(program->find(branch_id_t{9})->children.empty());
    CHECK(std::get<bms_set_switch_t>(program->find(branch_id_t{10})->value).value == 1);
}
TEST_CASE("BMS switch cases and defaults target their own first Chart", "[bms-program]") {
    const auto program = bms_program_t::from(switch_nodes());
    REQUIRE(program);
    CHECK(program->target_chart(branch_id_t{3}) == branch_id_t{4});
    CHECK(program->target_chart(branch_id_t{5}) == branch_id_t{6});
    CHECK(program->target_chart(branch_id_t{7}) == branch_id_t{8});
}
TEST_CASE("BMS switch grammar rejects misplaced cases and terminal default violations", "[bms-program]") {
    auto nodes = switch_nodes();
    SECTION("wrong case parent") { nodes[2].value = bms_random_scope_t{2}; }
    SECTION("duplicate DEF") { std::get<bms_case_scope_t>(nodes[4].value).value.reset(); }
    SECTION("CASE after DEF") { nodes[2].children = {branch_id_t{4}, branch_id_t{7}, branch_id_t{5}}; }
    SECTION("settings leaf has children") { nodes[8].children = {branch_id_t{6}}; }
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE_FALSE(program);
    CHECK(program.error().code == failure_code_t::invalid_data);
}
TEST_CASE("BMS SKIP is an anchored behavior owned by the nearest case", "[bms-program]") {
    auto nodes = switch_nodes();
    auto& arm = std::get<bms_case_scope_t>(nodes[4].value);
    arm.skips.push_back({.scope = branch_id_t{5}, .after_child = branch_id_t{6}});
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE(program);
    CHECK(std::get<bms_case_scope_t>(program->find(branch_id_t{5})->value).skips.front().after_child == branch_id_t{6});
    CHECK(program->find(branch_id_t{5})->children.size() == 1);
}
TEST_CASE("BMS SKIP cannot point outside its case or to another scopes child", "[bms-program]") {
    auto nodes = switch_nodes();
    bms_skip_point_t skip{.scope = branch_id_t{5}, .after_child = branch_id_t{6}};
    SECTION("another case") { skip.scope = branch_id_t{7}; skip.after_child = branch_id_t{8}; }
    SECTION("outside a case") { skip.scope = branch_id_t{1}; skip.after_child = branch_id_t{2}; }
    SECTION("foreign anchor") { skip.after_child = branch_id_t{8}; }
    SECTION("missing scope") { skip.scope = branch_id_t{99}; }
    std::get<bms_case_scope_t>(nodes[4].value).skips.push_back(skip);
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE_FALSE(program);
    CHECK(program.error().code == failure_code_t::invalid_data);
}
TEST_CASE("BMS conditional arm chains retain sibling identities without a grouping scope", "[bms-program]") {
    auto nodes = copy_nodes(nested_program());
    nodes[2].children.insert(nodes[2].children.end(), {branch_id_t{7}, branch_id_t{9}});
    nodes.push_back({.id = branch_id_t{7}, .value = bms_else_if_scope_t{branch_id_t{5}, 2}, .children = {branch_id_t{8}}});
    nodes.push_back({.id = branch_id_t{8}, .value = bms_chart_content_t{}});
    nodes.push_back({.id = branch_id_t{9}, .value = bms_else_scope_t{branch_id_t{7}}, .children = {branch_id_t{10}}});
    nodes.push_back({.id = branch_id_t{10}, .value = bms_chart_content_t{}});
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE(program);
    CHECK(program->parent(branch_id_t{7}) == branch_id_t{3});
    CHECK(program->target_chart(branch_id_t{9}) == branch_id_t{10});
    CHECK(std::get<bms_else_if_scope_t>(program->find(branch_id_t{7})->value).previous_arm == branch_id_t{5});
}
TEST_CASE("BMS conditional continuation requires the immediately preceding valid arm", "[bms-program]") {
    auto nodes = copy_nodes(nested_program());
    SECTION("wrong predecessor") { nodes[4].value = bms_else_if_scope_t{branch_id_t{4}, 2}; }
    SECTION("missing predecessor") { nodes[4].value = bms_else_scope_t{branch_id_t{99}}; }
    const auto program = bms_program_t::from(std::move(nodes));
    REQUIRE_FALSE(program);
    CHECK(program.error().code == failure_code_t::invalid_data);
}
