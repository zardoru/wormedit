#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bms_branch_projection.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
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
constexpr std::string_view empty_branch_source = "#PLAYER 1\n"
                                                "#TITLE Empty block test\n"
                                                "#BPM 120\n"
                                                "#RANDOM 2\n"
                                                "#IF 1\n"
                                                "#ELSEIF 2\n"
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

auto make_empty_branch_history() -> document_history_t {
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto parsed = parse_bms_bytes(
        std::as_bytes(std::span{empty_branch_source.data(), empty_branch_source.size()}),
        std::move(options));
    if (!parsed.song || parsed.has_errors()) {
        throw std::runtime_error("empty BMS block fixture did not parse");
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

auto tree_ids_are_unique(const bms_control_node_t& root,
                         std::unordered_set<std::uint64_t>& ids) -> bool {
    if (!ids.insert(root.id.value).second) return false;
    return std::ranges::all_of(root.children, [&](const auto& child) {
        return tree_ids_are_unique(child, ids);
    });
}

auto chart_tree(const document_history_t& history) -> const bms_control_node_t&;
auto created_block_shape_is_valid(const bms_control_node_t& block) -> bool;
auto first_note_resource_name(const song_document_t& song) -> std::string_view;

void check_created_block_nodes(const document_history_t& history, branch_id_t statement_id,
                               std::size_t original_order) {
    REQUIRE(history.document.charts.front().bms_control_tree.has_value());
    const auto* block = find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 3");
    REQUIRE(block != nullptr);
    CHECK(created_block_shape_is_valid(*block));
    const auto* moved = find_node_by_id(chart_tree(history), statement_id);
    REQUIRE(moved != nullptr);
    CHECK(moved->original_text == "#WAV03 replacement.wav");
    CHECK(moved->source_order == original_order + 2);
}

void check_created_block_projection(const document_history_t& history) {
    REQUIRE(history.document.charts.front().active_branch_selection.size() == 2);
    CHECK(history.document.charts.front().active_branch_selection.front().selected_value == 1);
    CHECK(first_note_resource_name(history.document) == "active.wav");
}

void check_created_block_serialization(const document_history_t& history) {
    const auto written = write_bms(history.document, {.chart = history.document.charts.front().id,
                                                       .preserve_unchanged_source = false});
    REQUIRE(written.bytes.has_value());
    CHECK(written.bytes->find("#RANDOM 3\n#IF 1\n#WAV03 replacement.wav") !=
          std::string::npos);
}

void check_created_block_undo_redo(document_history_t& history) {
    REQUIRE(undo_document_command(history));
    CHECK(find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 3") == nullptr);
    REQUIRE(redo_document_command(history));
    CHECK(find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 3") != nullptr);
}

void edit_bms_note_and_bpm(document_history_t& history) {
    auto edited = history.document;
    REQUIRE_FALSE(edited.charts.front().measures.front().notes.empty());
    edited.charts.front().measures.front().notes.front().position.row = 480;
    REQUIRE_FALSE(edited.charts.front().bpms.empty());
    edited.charts.front().bpms.front().beats_per_minute = {150, 1};
    REQUIRE(commit_document_command(history, std::move(edited), "Edit active BMS objects").applied);
}

void check_serialized_note_and_bpm(const document_history_t& history) {
    CHECK(history.document.charts.front().measures.front().notes.front().position.row == 480);
    CHECK((history.document.charts.front().bpms.front().beats_per_minute ==
           signed_rational_t{150, 1}));
    CHECK(first_note_resource_name(history.document) == "active.wav");
}

auto add_root_note(document_history_t& history) -> document_command_result_t {
    auto edited = history.document;
    const auto& existing = edited.charts.front().measures.front().notes.front();
    const auto lane = edited.charts.front().lane_layouts.front().layout.lanes.front().id;
    edited.charts.front().measures.front().notes.push_back(
        {.id = object_id_t{900001},
         .position = {edited.charts.front().measures.front().id, 480},
         .lane = lane,
         .kind = note_kind_t::tap,
         .sound = existing.sound});
    return commit_document_command(history, std::move(edited), "Add root BMS note");
}

void check_root_note_survived(const document_history_t& history) {
    const auto& notes = history.document.charts.front().measures.front().notes;
    REQUIRE(notes.size() == 2);
    CHECK(std::ranges::any_of(notes, [](const auto& note) { return note.position.row == 480; }));
}

void check_normalized_tree_identity(const document_history_t& history, branch_id_t statement_id) {
    REQUIRE(history.document.charts.front().bms_control_tree.has_value());
    std::unordered_set<std::uint64_t> ids;
    CHECK(tree_ids_are_unique(*history.document.charts.front().bms_control_tree, ids));
    CHECK(history.document.charts.front().active_branch_selection.front().selected_value == 1);
    CHECK(find_node_by_id(chart_tree(history), statement_id) != nullptr);
}

void add_unreferenced_catalog_entry(document_history_t& history) {
    auto asset = history.document.audio_assets.front();
    asset.id = asset_id_t{900001};
    asset.source_path = "resolved/unused.wav";
    history.document.audio_assets.push_back(asset);
    auto resource = history.document.resources.front();
    resource.id = resource_id_t{900002};
    resource.name = "unused.wav";
    resource.asset = asset.id;
    resource.target_identities = {{format_family_t::bms, "ZZ", "WAV"}};
    history.document.resources.push_back(resource);
}

void check_unreferenced_resource_rejected(const document_command_result_t& result,
                                          const document_history_t& history) {
    CHECK_FALSE(result.applied);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(history.document.resources.size() == 4);
    CHECK(history.document.audio_assets.size() == 4);
}

void check_unreferenced_resource_preserved(const document_history_t& history) {
    const auto found = std::ranges::find(history.document.resources, resource_id_t{900002},
                                         &resource_entry_t::id);
    REQUIRE(found != history.document.resources.end());
    CHECK(found->name == "unused.wav");
    const auto loaded = std::ranges::find(history.document.audio_assets, asset_id_t{900001},
                                          &audio_asset_t::id);
    REQUIRE(loaded != history.document.audio_assets.end());
    CHECK(loaded->source_path == "resolved/unused.wav");
}

void check_empty_block_removal_history(document_history_t& history) {
    CHECK(find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 2") == nullptr);
    REQUIRE(undo_document_command(history));
    CHECK(find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 2") != nullptr);
    REQUIRE(redo_document_command(history));
    CHECK(find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 2") == nullptr);
}

auto chart_tree(const document_history_t& history) -> const bms_control_node_t&;

auto create_block_request(const document_history_t& history, std::string_view first_text,
                          std::string_view last_text, std::size_t leaves)
    -> bms_branch_block_create_command_t {
    const auto& tree = chart_tree(history);
    const auto* first = find_node(tree, bms_control_kind_t::raw_statement, first_text);
    const auto* last = find_node(tree, bms_control_kind_t::raw_statement, last_text);
    if (first == nullptr || last == nullptr) {
        throw std::runtime_error("BMS root statements not found for block command");
    }
    return {
        .chart = history.document.charts.front().id,
        .first_statement = first->id,
        .first_source_order = first->source_order,
        .first_source_span = first->source_span,
        .first_original_text = first->original_text,
        .last_statement = last->id,
        .last_source_order = last->source_order,
        .last_source_span = last->source_span,
        .last_original_text = last->original_text,
        .leaf_count = leaves,
    };
}

auto remove_block_request(const document_history_t& history, const bms_control_node_t& block)
    -> bms_branch_block_remove_command_t {
    return {
        .chart = history.document.charts.front().id,
        .random_block = block.id,
        .expected_source_order = block.source_order,
        .expected_source_span = block.source_span,
        .expected_original_text = block.original_text,
    };
}

auto created_block_shape_is_valid(const bms_control_node_t& block) -> bool {
    if (block.kind != bms_control_kind_t::random || block.value != 3 ||
        block.children.size() != 1) return false;
    const auto& branches = block.children.front().children;
    return block.children.front().kind == bms_control_kind_t::if_group && branches.size() == 3 &&
           branches.front().children.size() == 1 && branches[1].children.empty() &&
           branches[2].children.empty();
}

auto chart_tree(const document_history_t& history) -> const bms_control_node_t& {
    return *history.document.charts.front().bms_control_tree;
}

auto selected_branch(document_history_t& history, std::int64_t value)
    -> bms_branch_selection_request_t {
    auto proposed = history.document.charts.front().active_branch_selection;
    proposed.front().selected_value = value;
    return {
        .chart = history.document.charts.front().id,
        .proposed = std::move(proposed),
    };
}

auto first_note_resource_name(const song_document_t& song) -> std::string_view {
    const auto& note = song.charts.front().measures.front().notes.front();
    if (!note.sound.has_value()) {
        return {};
    }
    const auto resource = std::ranges::find(song.resources, *note.sound, &resource_entry_t::id);
    return resource == song.resources.end() ? std::string_view{} : resource->name;
}

auto replace_request(const document_history_t& history, std::string_view original,
                     std::string replacement)
    -> bms_branch_statement_edit_t {
    const auto* target =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, original);
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
    const auto* anchor =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200");
    if (anchor == nullptr) {
        throw std::runtime_error("inactive branch anchor not found in test fixture");
    }
    const auto* leaf = find_node(chart_tree(history), bms_control_kind_t::else_branch, "#ELSE");
    if (leaf == nullptr) {
        throw std::runtime_error("inactive branch leaf not found in test fixture");
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

auto insert_request_after(const document_history_t& history, std::string_view anchor_text,
                          std::string statement_text) -> bms_branch_statement_insert_t {
    const auto* leaf = find_node(chart_tree(history), bms_control_kind_t::else_branch, "#ELSE");
    const auto* anchor = find_node(chart_tree(history), bms_control_kind_t::raw_statement,
                                   anchor_text);
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
        .statement_text = std::move(statement_text),
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

auto delete_request_for(const document_history_t& history, std::string_view statement_text)
    -> bms_branch_statement_delete_t {
    const auto* leaf = find_node(chart_tree(history), bms_control_kind_t::else_branch, "#ELSE");
    const auto* target = find_node(chart_tree(history), bms_control_kind_t::raw_statement,
                                   statement_text);
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

auto retained_bms_bytes(const song_document_t& song) -> std::string {
    for (const auto& group : song.charts.front().metadata.format_groups) {
        if (group.family == format_family_t::bms && group.encoding.original_bytes) {
            const auto& bytes = *group.encoding.original_bytes;
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }
    }
    throw std::runtime_error("BMS source bytes were not retained");
}

auto has_only_crlf_line_endings(std::string_view text) -> bool {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r')) {
            return false;
        }
        if (text[index] == '\r' && (index + 1 == text.size() || text[index + 1] != '\n')) {
            return false;
        }
    }
    return true;
}

auto native_roundtrip(document_history_t history) -> document_history_t {
    const auto written = write_native_project(history.document);
    if (!written.bytes) {
        throw std::runtime_error("edited BMS fixture did not serialize to native project");
    }
    const auto parsed = parse_native_project(*written.bytes);
    if (!parsed.song) {
        throw std::runtime_error("edited BMS fixture did not reload from native project");
    }
    return make_document_history(*parsed.song);
}

auto parse_branch_fixture(std::string_view text, text_encoding_t encoding = text_encoding_t::utf8)
    -> document_history_t {
    constexpr std::string_view random_line = "#RANDOM 2";
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.encoding_override = encoding;
    options.branch_choices = {{source_line_order(text, random_line), 1}};
    const auto parsed = parse_bms_bytes(
        std::as_bytes(std::span{text.data(), text.size()}), std::move(options));
    if (!parsed.song || parsed.has_errors()) {
        throw std::runtime_error("BMS branch edit fixture did not parse");
    }
    return make_document_history(*parsed.song);
}

auto edit_or_throw(document_history_t& history, std::string_view before, std::string after)
    -> bms_branch_statement_command_result_t {
    auto result = apply_bms_branch_statement_edit_command(
        history, history.document.revision, replace_request(history, before, std::move(after)));
    if (!result) {
        throw std::runtime_error("BMS statement edit fixture command failed");
    }
    return result;
}

auto insert_or_throw(document_history_t& history, std::string_view anchor,
                     std::string statement) -> bms_branch_statement_command_result_t {
    auto result = apply_bms_branch_statement_insert_command(
        history, history.document.revision,
        insert_request_after(history, anchor, std::move(statement)));
    if (!result) {
        throw std::runtime_error("BMS statement insert fixture command failed");
    }
    return result;
}

auto delete_or_throw(document_history_t& history, std::string_view statement)
    -> bms_branch_statement_command_result_t {
    auto result = apply_bms_branch_statement_delete_command(
        history, history.document.revision, delete_request_for(history, statement));
    if (!result) {
        throw std::runtime_error("BMS statement delete fixture command failed");
    }
    return result;
}

auto prepare_edited_native_history(document_history_t history, branch_id_t statement_id)
    -> document_history_t {
    edit_or_throw(history, "#00011:0200", "#00011:000003");
    if (retained_bms_bytes(history.document).find("#00011:000003\n") == std::string::npos ||
        find_node_by_id(chart_tree(history), statement_id)->original_text != "#00011:000003") {
        throw std::runtime_error("grown BMS statement was not retained atomically");
    }
    auto loaded = native_roundtrip(std::move(history));
    if (retained_bms_bytes(loaded.document).find("#00011:000003\n") == std::string::npos ||
        find_node_by_id(chart_tree(loaded), statement_id) == nullptr) {
        throw std::runtime_error("native BMS roundtrip lost edited source identity");
    }
    return loaded;
}

auto verify_insert_delete_after_edit(document_history_t& history, branch_id_t statement_id) -> void {
    insert_or_throw(history, "#00011:000003", "#00012:01");
    if (retained_bms_bytes(history.document).find("#00011:000003\n#00012:01\n") ==
        std::string::npos) {
        throw std::runtime_error("insert did not follow edited source bytes");
    }
    delete_or_throw(history, "#00012:01");
    if (find_node_by_id(chart_tree(history), statement_id)->original_text != "#00011:000003") {
        throw std::runtime_error("insert/delete changed the edited statement identity");
    }
}

auto verify_shrink_delete_after_edit(document_history_t& history, branch_id_t statement_id) -> void {
    edit_or_throw(history, "#00011:000003", "#00011:03");
    if (retained_bms_bytes(history.document).find("#00011:03\n") == std::string::npos ||
        find_node_by_id(chart_tree(history), statement_id)->original_text != "#00011:03") {
        throw std::runtime_error("shrunk BMS statement was not retained");
    }
    delete_or_throw(history, "#00011:03");
    if (find_node_by_id(chart_tree(history), statement_id) != nullptr) {
        throw std::runtime_error("delete did not remove the edited statement");
    }
}

auto make_edited_cp932_history() -> document_history_t {
    constexpr std::string_view text = "#PLAYER 1\r\n"
                                      "#RANDOM 2\r\n"
                                      "#IF 1\r\n"
                                      "#COMMENT active\r\n"
                                      "#ELSE\r\n"
                                      "#COMMENT old\r\n"
                                      "#ENDIF\r\n"
                                      "#ENDRANDOM\r\n";
    auto history = parse_branch_fixture(text, text_encoding_t::cp932);
    edit_or_throw(history, "#COMMENT old", "#COMMENT 文字列-long");
    const auto bytes = retained_bms_bytes(history.document);
    if (!has_only_crlf_line_endings(bytes) ||
        find_node(chart_tree(history), bms_control_kind_t::raw_statement,
                  "#COMMENT 文字列-long") == nullptr) {
        throw std::runtime_error("CP932 replacement did not preserve text and CRLF endings");
    }
    return history;
}

auto reject_unrepresentable_cp932(document_history_t& history) -> bool {
    const auto original_bytes = retained_bms_bytes(history.document);
    const auto revision = history.document.revision;
    const auto result = apply_bms_branch_statement_edit_command(
        history, revision,
        replace_request(history, "#COMMENT 文字列-long", "#COMMENT 😀"));
    return !result && history.document.revision == revision &&
           retained_bms_bytes(history.document) == original_bytes &&
           find_node(chart_tree(history), bms_control_kind_t::raw_statement,
                     "#COMMENT 文字列-long") != nullptr;
}

auto edit_span_provenance_is_stable(const document_history_t& history, branch_id_t statement_id,
                                    std::size_t old_byte_end, std::size_t old_source_order,
                                    branch_id_t if_group_id, std::size_t old_close_begin) -> bool {
    const auto* edited = find_node_by_id(chart_tree(history), statement_id);
    const auto* if_group = find_node_by_id(chart_tree(history), if_group_id);
    return edited != nullptr && if_group != nullptr && if_group->close_source_span.has_value() &&
           edited->source_order == old_source_order && edited->source_span.byte_end == old_byte_end + 2 &&
           if_group->close_source_span->byte_begin == old_close_begin + 2;
}

struct edit_identity_t {
    branch_id_t statement_id;
    std::size_t byte_end{};
    std::size_t source_order{};
    branch_id_t if_group_id;
    std::size_t close_begin{};
};

auto find_if_group_for_header(const bms_control_node_t& node) -> const bms_control_node_t*;

auto edit_identity(const document_history_t& history) -> edit_identity_t {
    const auto* statement =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200");
    const auto* if_group = find_if_group_for_header(chart_tree(history));
    if (statement == nullptr || if_group == nullptr || !if_group->close_source_span) {
        throw std::runtime_error("BMS fixture source identities were not found");
    }
    return {statement->id, statement->source_span.byte_end, statement->source_order, if_group->id,
            if_group->close_source_span->byte_begin};
}

auto find_if_group_for_header(const bms_control_node_t& node) -> const bms_control_node_t* {
    if (node.kind == bms_control_kind_t::if_group &&
        std::ranges::any_of(node.children, [](const auto& child) {
            return child.kind == bms_control_kind_t::if_branch &&
                   child.original_text == "#IF 1";
        })) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* match = find_if_group_for_header(child)) return match;
    }
    return nullptr;
}

} // namespace

TEST_CASE("inactive BMS statement replacement is one revision-checked undoable command",
          "[unit][core][bms][branch][command]") {
    auto history = make_history();
    const auto statement =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0200");
    REQUIRE(statement != nullptr);
    const auto statement_id = statement->id;
    const auto request = replace_request(history, "#00011:0200", "#00011:0300");

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

TEST_CASE("edited BMS source remains editable after native roundtrip and insert delete shrink",
          "[unit][core][bms][branch][edit][roundtrip]") {
    auto history = make_history();
    const auto identity = edit_identity(history);
    const auto active_notes = history.document.charts.front().measures.front().notes.size();
    const auto active_sound = std::string(first_note_resource_name(history.document));

    history = prepare_edited_native_history(std::move(history), identity.statement_id);
    CHECK(edit_span_provenance_is_stable(history, identity.statement_id, identity.byte_end,
                                          identity.source_order, identity.if_group_id,
                                          identity.close_begin));
    CHECK(history.document.charts.front().active_branch_selection.front().selected_value == 1);
    verify_insert_delete_after_edit(history, identity.statement_id);
    verify_shrink_delete_after_edit(history, identity.statement_id);
    CHECK(history.document.charts.front().measures.front().notes.size() == active_notes);
    CHECK(std::string(first_note_resource_name(history.document)) == active_sound);
    REQUIRE(undo_document_command(history));
    CHECK(find_node_by_id(chart_tree(history), identity.statement_id)->original_text == "#00011:03");
}

TEST_CASE("BMS statement replacement preserves CRLF bytes and rejects unrepresentable source text",
          "[unit][core][bms][branch][edit][encoding]") {
    auto history = make_edited_cp932_history();
    CHECK(std::ranges::count(retained_bms_bytes(history.document), '\n') == 8);
    CHECK(reject_unrepresentable_cp932(history));
}

TEST_CASE("stale BMS branch actions reject without changing history or losing typed diagnostics",
          "[unit][core][bms][branch][command][negative]") {
    auto history = make_history();
    const auto original_revision = history.document.revision;
    auto request = replace_request(history, "#00011:0200", "#00011:0300");

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

TEST_CASE("active BMS statements reject edits with a typed diagnostic",
          "[unit][core][bms][branch][command][negative]") {
    auto history = make_history();
    const auto* statement =
        find_node(chart_tree(history), bms_control_kind_t::raw_statement, "#00011:0100");
    REQUIRE(statement != nullptr);
    const auto revision = history.document.revision;
    const auto applied = apply_bms_branch_statement_edit_command(
        history, revision,
        {.chart = history.document.charts.front().id,
         .statement = statement->id,
         .expected_source_order = statement->source_order,
         .expected_source_span = statement->source_span,
         .expected_original_text = statement->original_text,
         .replacement_text = "#00011:0300"});
    CHECK_FALSE(applied);
    REQUIRE(applied.format_diagnostics.size() == 1);
    CHECK(applied.format_diagnostics.front().code ==
          bms_branch_statement_edit_diagnostic_code_t::active_statement);
    CHECK(history.document.revision == revision);
}

TEST_CASE("inactive BMS statement edits reject ambiguous multi-chart source identity",
          "[unit][core][bms][branch][command][negative]") {
    auto history = make_history();
    auto second_chart = history.document.charts.front();
    second_chart.id = chart_id_t{second_chart.id.value + 1};
    history.document.charts.push_back(std::move(second_chart));
    const auto revision = history.document.revision;
    const auto applied = apply_bms_branch_statement_insert_command(
        history, revision, insert_request(history));
    CHECK_FALSE(applied);
    REQUIRE(applied.format_diagnostics.size() == 1);
    CHECK(applied.format_diagnostics.front().code ==
          bms_branch_statement_edit_diagnostic_code_t::unsupported_song_shape);
    CHECK(history.document.revision == revision);
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

TEST_CASE("inactive BMS insertion leaves the active chart projection unchanged",
          "[unit][core][bms][branch][command]") {
    auto history = make_history();
    const auto active_sound_before = std::string(first_note_resource_name(history.document));
    const auto active_note_count_before = history.document.charts.front().measures.front().notes.size();
    const auto applied = apply_bms_branch_statement_insert_command(
        history, history.document.revision, insert_request(history));
    REQUIRE(applied);
    CHECK(std::string(first_note_resource_name(history.document)) == active_sound_before);
    CHECK(history.document.charts.front().measures.front().notes.size() == active_note_count_before);
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

TEST_CASE("BMS branch selection rematerializes semantics as one undoable command",
          "[unit][core][bms][branch][selection][command]") {
    auto history = make_history();
    const auto song_id = history.document.id;
    const auto chart_id = history.document.charts.front().id;
    history.document.output_groups.push_back({
        .name = "BMS output",
        .family = format_family_t::bms,
        .charts = {chart_id},
        .relative_output_path = "chart.bms",
    });
    REQUIRE_FALSE(history.document.audio_assets.empty());
    history.document.audio_assets.front().stream = {
        .sample_rate = 48'000,
        .channels = 2,
        .frame_count = 123'456,
        .seekable = true,
    };
    history.document.audio_assets.front().source_path = "resolved/active.wav";
    history.document.audio_assets.front().waveform_cache_path = "active.waveform";
    history.document.charts.front().metadata.name = "Editor difficulty name";
    history.document.charts.front().measures.front().definition.meter = meter_t{8, 8};
    CHECK(first_note_resource_name(history.document) == "active.wav");

    const auto changed = apply_bms_branch_selection_command(history, history.document.revision,
                                                            selected_branch(history, 2));
    REQUIRE(changed);
    CHECK(changed.command.diagnostics.empty());
    CHECK(changed.format_diagnostics.empty());
    REQUIRE(changed.preview);
    CHECK(changed.preview.entering_statement_ids.size() == 1);
    CHECK(changed.preview.leaving_statement_ids.size() == 1);
    CHECK(history.document.id == song_id);
    CHECK(history.document.charts.front().id == chart_id);
    REQUIRE(history.document.output_groups.size() == 1);
    CHECK(history.document.output_groups.front().charts == std::vector{chart_id});
    REQUIRE_FALSE(history.document.audio_assets.empty());
    CHECK(history.document.audio_assets.front().source_path == "resolved/active.wav");
    CHECK(history.document.audio_assets.front().stream.sample_rate == 48'000);
    CHECK(history.document.audio_assets.front().waveform_cache_path == "active.waveform");
    CHECK(history.document.charts.front().metadata.name == "Editor difficulty name");
    CHECK(history.document.charts.front().measures.front().definition.meter == meter_t{8, 8});
    REQUIRE(history.document.charts.front().active_branch_selection.size() == 1);
    CHECK(history.document.charts.front().active_branch_selection.front().selected_value == 2);
    CHECK(first_note_resource_name(history.document) == "inactive.wav");
    CHECK(history.undo.size() == 1);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().active_branch_selection.front().selected_value == 1);
    CHECK(first_note_resource_name(history.document) == "active.wav");
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().active_branch_selection.front().selected_value == 2);
    CHECK(first_note_resource_name(history.document) == "inactive.wav");
}

TEST_CASE("BMS branch selection retains serialized active edits when switching away and back",
          "[unit][core][bms][branch][selection][roundtrip]") {
    auto history = make_history();
    auto edited = history.document;
    REQUIRE(edited.charts.front().measures.front().notes.size() == 1);
    edited.charts.front().measures.front().notes.front().position.row = 480;
    const auto edit = commit_document_command(history, std::move(edited), "Move branch note");
    REQUIRE(edit);

    const auto switched_away = apply_bms_branch_selection_command(
        history, history.document.revision, selected_branch(history, 2));
    REQUIRE(switched_away);
    REQUIRE(history.document.charts.front().measures.front().notes.size() == 1);
    CHECK(history.document.charts.front().measures.front().notes.front().position.row == 0);

    const auto switched_back = apply_bms_branch_selection_command(
        history, history.document.revision, selected_branch(history, 1));
    REQUIRE(switched_back);
    REQUIRE(history.document.charts.front().measures.front().notes.size() == 1);
    CHECK(history.document.charts.front().measures.front().notes.front().position.row == 480);
    CHECK(first_note_resource_name(history.document) == "active.wav");
}

TEST_CASE("scalar BMS branch choice command derives set and clear proposals from current state",
          "[unit][core][bms][branch][selection][scalar]") {
    auto history = make_history();
    const auto chart = history.document.charts.front().id;
    const auto branch = history.document.charts.front().active_branch_selection.front().branch;
    const auto selected =
        apply_bms_branch_choice_command(history, history.document.revision,
                                        {.chart = chart, .branch = branch, .selected_value = 2});
    REQUIRE(selected);
    CHECK(first_note_resource_name(history.document) == "inactive.wav");

    const auto selected_branch_id =
        history.document.charts.front().active_branch_selection.front().branch;
    const auto cleared = apply_bms_branch_choice_command(
        history, history.document.revision,
        {.chart = chart, .branch = selected_branch_id, .selected_value = std::nullopt});
    REQUIRE(cleared);
    CHECK(history.document.charts.front().active_branch_selection.empty());
    CHECK(history.document.charts.front().measures.front().notes.empty());
    CHECK(history.undo.size() == 2);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().active_branch_selection.front().selected_value == 2);
    CHECK(first_note_resource_name(history.document) == "inactive.wav");
}

TEST_CASE("BMS branch block creation preserves selected source statements and history",
          "[unit][core][bms][branch][block]") {
    auto history = make_history();
    const auto request = create_block_request(history, "#WAV03 replacement.wav",
                                              "#WAV03 replacement.wav", 3);
    const auto statement_id = request.first_statement;
    const auto original_order = request.first_source_order;
    const auto created = apply_bms_branch_block_create_command(
        history, history.document.revision, request);
    REQUIRE(created.applied);
    check_created_block_nodes(history, statement_id, original_order);
    check_created_block_projection(history);
    check_created_block_serialization(history);
    check_created_block_undo_redo(history);
}

TEST_CASE("BMS branch block creation serializes current note and timing edits",
          "[unit][core][bms][branch][block][roundtrip]") {
    auto history = make_history();
    edit_bms_note_and_bpm(history);
    const auto request = create_block_request(history, "#WAV03 replacement.wav",
                                              "#WAV03 replacement.wav", 2);
    const auto created = apply_bms_branch_block_create_command(
        history, history.document.revision, request);
    REQUIRE(created.applied);
    check_serialized_note_and_bpm(history);
}

TEST_CASE("BMS branch block creation keeps IDs unique after serializing a new root object",
          "[unit][core][bms][branch][block][roundtrip]") {
    auto history = make_history();
    const auto add_note = add_root_note(history);
    REQUIRE(add_note.applied);

    const auto request = create_block_request(history, "#WAV03 replacement.wav",
                                              "#WAV03 replacement.wav", 2);
    const auto created = apply_bms_branch_block_create_command(
        history, history.document.revision, request);

    REQUIRE(created.applied);
    check_root_note_survived(history);
    check_normalized_tree_identity(history, request.first_statement);
}

TEST_CASE("BMS branch block creation rejects an unroundtrippable unused resource atomically",
          "[unit][core][bms][branch][block][resources]") {
    auto history = make_history();
    add_unreferenced_catalog_entry(history);

    const auto request = create_block_request(history, "#WAV03 replacement.wav",
                                              "#WAV03 replacement.wav", 2);
    const auto created = apply_bms_branch_block_create_command(
        history, history.document.revision, request);
    check_unreferenced_resource_rejected(created, history);
    check_unreferenced_resource_preserved(history);
}

TEST_CASE("BMS branch block creation rejects a custom lane map it cannot reconcile",
          "[unit][core][bms][branch][block][negative]") {
    auto history = make_history();
    history.document.charts.front().lane_layouts.front().layout.profile_id = "custom-layout";
    const auto request = create_block_request(history, "#WAV03 replacement.wav",
                                              "#WAV03 replacement.wav", 2);
    const auto revision = history.document.revision;
    const auto created = apply_bms_branch_block_create_command(history, revision, request);
    CHECK_FALSE(created.applied);
    CHECK_FALSE(created.diagnostics.empty());
    CHECK(history.document.revision == revision);
    CHECK(first_note_resource_name(history.document) == "active.wav");
}

TEST_CASE("BMS branch block creation rejects a range crossing existing control flow",
          "[unit][core][bms][branch][block][negative]") {
    auto history = make_history();
    const auto request = create_block_request(history, "#WAV03 replacement.wav", "#00011:0200", 2);
    const auto revision = history.document.revision;
    const auto created = apply_bms_branch_block_create_command(history, revision, request);
    CHECK_FALSE(created.applied);
    CHECK_FALSE(created.diagnostics.empty());
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
    CHECK(find_node(chart_tree(history), bms_control_kind_t::raw_statement,
                    "#WAV03 replacement.wav") != nullptr);
}

TEST_CASE("BMS branch block removal accepts only an exact empty block",
          "[unit][core][bms][branch][block]") {
    auto history = make_empty_branch_history();
    const auto* block = find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 2");
    REQUIRE(block != nullptr);
    const auto removed = apply_bms_branch_block_remove_command(
        history, history.document.revision, remove_block_request(history, *block));
    REQUIRE(removed.applied);
    check_empty_block_removal_history(history);
}

TEST_CASE("BMS branch block removal rejects a nonempty selected leaf atomically",
          "[unit][core][bms][branch][block][negative]") {
    auto history = make_history();
    const auto create = create_block_request(history, "#WAV03 replacement.wav",
                                             "#WAV03 replacement.wav", 2);
    REQUIRE(apply_bms_branch_block_create_command(history, history.document.revision, create)
                .applied);
    const auto* block = find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 2");
    REQUIRE(block != nullptr);
    const auto revision = history.document.revision;
    const auto removed = apply_bms_branch_block_remove_command(
        history, revision, remove_block_request(history, *block));
    CHECK_FALSE(removed.applied);
    CHECK_FALSE(removed.diagnostics.empty());
    CHECK(history.document.revision == revision);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("BMS branch selection maps nested controls across normalized source orders",
          "[unit][core][bms][branch][selection][nested]") {
    constexpr std::string_view nested_source = "#PLAYER 1\n"
                                               "#BPM 120\n"
                                               "#WAV01 first.wav\n"
                                               "#WAV02 second.wav\n"
                                               "#RANDOM 2\n"
                                               "#IF 2\n"
                                               "#SWITCH 2\n"
                                               "#CASE 1\n"
                                               "#00011:0100\n"
                                               "#SKIP\n"
                                               "#CASE 2\n"
                                               "#00012:0200\n"
                                               "#SKIP\n"
                                               "#ENDSW\n"
                                               "#ENDIF\n"
                                               "#ENDRANDOM\n";
    bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    options.branch_choices = {
        {source_line_order(nested_source, "#RANDOM 2"), 2},
        {source_line_order(nested_source, "#SWITCH 2"), 2},
    };
    const auto parsed = parse_bms_bytes(
        std::as_bytes(std::span{nested_source.data(), nested_source.size()}), std::move(options));
    REQUIRE(parsed.song.has_value());
    REQUIRE_FALSE(parsed.has_errors());
    auto history = make_document_history(*parsed.song);
    const auto* random = find_node(chart_tree(history), bms_control_kind_t::random, "#RANDOM 2");
    const auto* switch_block =
        find_node(chart_tree(history), bms_control_kind_t::switch_block, "#SWITCH 2");
    REQUIRE(random != nullptr);
    REQUIRE(switch_block != nullptr);
    CHECK(first_note_resource_name(history.document) == "second.wav");

    bms_branch_selection_request_t request{
        .chart = history.document.charts.front().id,
        .proposed = {{random->id, 2}, {switch_block->id, 1}},
    };
    const auto changed =
        apply_bms_branch_selection_command(history, history.document.revision, std::move(request));
    REQUIRE(changed);
    REQUIRE(history.document.charts.front().active_branch_selection.size() == 2);
    CHECK(first_note_resource_name(history.document) == "first.wav");
}

TEST_CASE("BMS branch selection rejects stale invalid and lossy proposals atomically",
          "[unit][core][bms][branch][selection][command][negative]") {
    auto history = make_history();
    const auto revision = history.document.revision;

    const auto stale =
        apply_bms_branch_selection_command(history, revision + 1, selected_branch(history, 2));
    CHECK_FALSE(stale);
    REQUIRE(stale.command.diagnostics.size() == 1);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());

    auto invalid_request = selected_branch(history, 3);
    const auto invalid =
        apply_bms_branch_selection_command(history, revision, std::move(invalid_request));
    CHECK_FALSE(invalid);
    REQUIRE(invalid.format_diagnostics.size() == 1);
    CHECK(invalid.format_diagnostics.front().code ==
          bms_branch_selection_diagnostic_code_t::invalid_selection);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());

    auto parse_limited_request = selected_branch(history, 2);
    parse_limited_request.max_line_bytes = 1;
    const auto parse_limited =
        apply_bms_branch_selection_command(history, revision, std::move(parse_limited_request));
    CHECK_FALSE(parse_limited);
    REQUIRE(parse_limited.format_diagnostics.size() == 1);
    CHECK(parse_limited.format_diagnostics.front().code ==
          bms_branch_selection_diagnostic_code_t::parse_failed);
    CHECK_FALSE(parse_limited.parse_diagnostics.empty());
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());

    history.document.charts.front().bookmarks.push_back({
        .id = object_id_t{999'999},
        .label = "preserve me",
        .position = local_position_t{history.document.charts.front().measures.front().id, 0},
    });
    const auto lossy =
        apply_bms_branch_selection_command(history, revision, selected_branch(history, 2));
    CHECK_FALSE(lossy);
    REQUIRE(lossy.format_diagnostics.size() == 1);
    CHECK(lossy.format_diagnostics.front().code ==
          bms_branch_selection_diagnostic_code_t::unsupported_editor_state);
    CHECK(history.document.charts.front().bookmarks.size() == 1);
    CHECK(history.document.charts.front().active_branch_selection.front().selected_value == 1);
    CHECK(history.undo.empty());

    auto unrepresentable = make_history();
    unrepresentable.document.charts.front().measures.front().notes.front().kind = note_kind_t::roll;
    const auto write_failed = apply_bms_branch_selection_command(
        unrepresentable, unrepresentable.document.revision, selected_branch(unrepresentable, 2));
    CHECK_FALSE(write_failed);
    REQUIRE(write_failed.format_diagnostics.size() == 1);
    CHECK(write_failed.format_diagnostics.front().code ==
          bms_branch_selection_diagnostic_code_t::write_failed);
    CHECK_FALSE(write_failed.write_diagnostics.empty());
    CHECK(unrepresentable.document.charts.front().active_branch_selection.front().selected_value ==
          1);
    CHECK(unrepresentable.undo.empty());
}
