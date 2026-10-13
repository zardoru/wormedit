#include <catch2/generators/catch_generators.hpp>
#include <wormedit/core/command_registry.h>

#include <catch2/catch_test_macros.hpp>

#include <wormedit/core/shortcut_profile_storage.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace wormedit;

namespace {

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(
        diagnostics, [code](const diagnostic_t& diagnostic) { return diagnostic.code == code; });
}

auto make_invoker(std::shared_ptr<std::vector<command_id_t>> calls) -> command_invoker_t {
    return [calls = std::move(calls)](const command_invocation_context_t& context) {
        calls->push_back(context.command);
        return command_invocation_result_t{.accepted = true};
    };
}

auto registration(std::string id, command_focus_scope_t scope, shortcut_chord_t shortcut,
                  command_invoker_t invoke, bool mutation = false) -> command_registration_t {
    return {.definition = {.id = {id},
                           .display_name = id,
                           .localization_key = "command." + id,
                           .category = command_category_t::edit,
                           .focus_scope = scope,
                           .default_shortcuts = {shortcut},
                           .enabled = true,
                           .mutation = mutation,
                           .invoke = std::move(invoke)}};
}

auto focus_for_compose() -> command_focus_t {
    return {
        .workspace_active = true,
        .active_tab = workspace_tab_t::compose,
        .visible_tabs = {workspace_tab_t::setup, workspace_tab_t::timing, workspace_tab_t::compose},
    };
}

auto invokes_command_in_focus(const command_registry_t& registry, command_focus_t focus,
                              const std::shared_ptr<std::vector<command_id_t>>& calls,
                              std::string_view id, std::size_t call_count) -> bool {
    const command_id_t command_id{std::string(id)};
    const auto result = invoke_command(registry, command_id, focus);
    return result.status == command_dispatch_status_t::invoked && result.command == command_id &&
           calls->size() == call_count && calls->back() == command_id;
}

auto has_command(const command_registry_t& registry, std::string_view id) -> bool {
    return find_command(registry, command_id_t{std::string(id)}) != nullptr;
}

} // namespace

namespace {
void check_chart_clipboard_chord(const command_registry_t& registry, shortcut_key_t key,
                                std::string_view expected, shortcut_primary_modifier_t primary,
                                shortcut_modifier_t modifier) {
    const shortcut_event_t event{.chord = {.key = key,
        .modifiers = static_cast<shortcut_modifiers_t>(modifier)}};
    auto focus = focus_for_compose();
    CHECK(dispatch_shortcut(registry, event, focus, primary).command ==
          command_id_t{std::string(expected)});
    focus.text_entry_active = true;
    CHECK(dispatch_shortcut(registry, event, focus, primary).status ==
          command_dispatch_status_t::focus_consumed);
    focus.text_entry_active = false;
    focus.ime_composition_active = true;
    CHECK(dispatch_shortcut(registry, event, focus, primary).status ==
          command_dispatch_status_t::focus_consumed);
}
}

TEST_CASE("Chart clipboard chords use platform Primary and defer to text and IME",
          "[unit][core][command_registry][clipboard]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    check_chart_clipboard_chord(registry, shortcut_key_t::c, "edit.copy",
        shortcut_primary_modifier_t::control, shortcut_modifier_t::control);
    check_chart_clipboard_chord(registry, shortcut_key_t::x, "edit.cut",
        shortcut_primary_modifier_t::meta, shortcut_modifier_t::meta);
    CHECK(calls->size() == 2);
}

TEST_CASE("registry stores stable metadata and shares typed invocation for menus and shortcuts",
          "[unit][core][command_registry]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    command_registry_t registry;
    auto add =
        registration("edit.sample", command_focus_scope_t::global,
                     {.key = shortcut_key_t::a,
                      .modifiers = shortcut_modifier_t::primary | shortcut_modifier_t::shift},
                     make_invoker(calls), true);
    add.definition.checked = false;
    REQUIRE(register_command(registry, std::move(add)).applied);

    const auto* command = find_command(registry, command_id_t{"edit.sample"});
    REQUIRE(command);
    CHECK(command->definition.display_name == "edit.sample");
    CHECK(command->definition.localization_key == "command.edit.sample");
    CHECK(command->definition.category == command_category_t::edit);
    CHECK(command->current_shortcuts == command->definition.default_shortcuts);
    CHECK(command->definition.checked == false);

    auto shortcut = dispatch_shortcut(
        registry,
        {.chord = {.key = shortcut_key_t::a,
                   .modifiers = shortcut_modifier_t::control | shortcut_modifier_t::shift}},
        {}, shortcut_primary_modifier_t::control);
    REQUIRE(shortcut.status == command_dispatch_status_t::invoked);
    CHECK(shortcut.command == command_id_t{"edit.sample"});

    const auto menu = invoke_command(registry, command_id_t{"edit.sample"});
    REQUIRE(menu.status == command_dispatch_status_t::invoked);
    REQUIRE(calls->size() == 2);
    CHECK((*calls)[0] == command_id_t{"edit.sample"});
    CHECK((*calls)[1] == command_id_t{"edit.sample"});

    REQUIRE(update_command_state(registry, command_id_t{"edit.sample"}, true, true).applied);
    CHECK(find_command(registry, command_id_t{"edit.sample"})->definition.checked == true);
}

TEST_CASE("dispatcher honors global, workspace, active-tab, then lane precedence",
          "[unit][core][command_registry]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto invoke = make_invoker(calls);
    const shortcut_chord_t key{.key = shortcut_key_t::q};
    command_registry_t registry;
    REQUIRE(register_command(registry,
                             registration("a.global", command_focus_scope_t::global, key, invoke))
                .applied);
    auto workspace = registration("b.workspace", command_focus_scope_t::workspace, key, invoke);
    workspace.definition.tab = workspace_tab_t::compose;
    REQUIRE(register_command(registry, std::move(workspace)).applied);
    auto tab = registration("c.tab", command_focus_scope_t::tab, key, invoke);
    tab.definition.tab = workspace_tab_t::compose;
    REQUIRE(register_command(registry, std::move(tab)).applied);
    auto lane = registration("d.lane", command_focus_scope_t::lane, key, invoke);
    lane.definition.lane = lane_id_t{7};
    lane.definition.tab = workspace_tab_t::compose;
    REQUIRE(register_command(registry, std::move(lane)).applied);

    auto focus = focus_for_compose();
    focus.focused_lane = lane_id_t{7};
    auto result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    REQUIRE(result.status == command_dispatch_status_t::invoked);
    CHECK(result.command == command_id_t{"a.global"});

    REQUIRE(remap_command(registry, command_id_t{"a.global"}, {}).applied);
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    REQUIRE(result.status == command_dispatch_status_t::invoked);
    CHECK(result.command == command_id_t{"b.workspace"});

    REQUIRE(remap_command(registry, command_id_t{"b.workspace"}, {}).applied);
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    REQUIRE(result.status == command_dispatch_status_t::invoked);
    CHECK(result.command == command_id_t{"c.tab"});

    REQUIRE(remap_command(registry, command_id_t{"c.tab"}, {}).applied);
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    REQUIRE(result.status == command_dispatch_status_t::invoked);
    CHECK(result.command == command_id_t{"d.lane"});
}

TEST_CASE("modal text and IME focus consume unrelated shortcuts and hidden tabs stay inactive",
          "[unit][core][command_registry][negative]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto key = shortcut_chord_t{.key = shortcut_key_t::x};
    command_registry_t registry;
    REQUIRE(register_command(registry, registration("edit.global", command_focus_scope_t::global,
                                                    key, make_invoker(calls)))
                .applied);
    auto text =
        registration("text.complete", command_focus_scope_t::text_entry, key, make_invoker(calls));
    text.definition.focus_context = "metadata.title";
    REQUIRE(register_command(registry, std::move(text)).applied);
    auto modal =
        registration("modal.accept", command_focus_scope_t::modal, key, make_invoker(calls));
    modal.definition.focus_context = "confirm.delete";
    REQUIRE(register_command(registry, std::move(modal)).applied);
    auto ime = registration("ime.commit", command_focus_scope_t::ime, key, make_invoker(calls));
    REQUIRE(register_command(registry, std::move(ime)).applied);

    auto focus = focus_for_compose();
    focus.text_entry_active = true;
    focus.text_entry_context = "other.field";
    auto result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    CHECK(result.status == command_dispatch_status_t::focus_consumed);
    CHECK(calls->empty());
    focus.text_entry_context = "metadata.title";
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    CHECK(result.command == command_id_t{"text.complete"});
    REQUIRE(calls->size() == 1);

    focus.modal_active = true;
    focus.modal_context = "confirm.delete";
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    CHECK(result.command == command_id_t{"modal.accept"});
    REQUIRE(calls->size() == 2);

    focus.ime_composition_active = true;
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    CHECK(result.command == command_id_t{"ime.commit"});
    REQUIRE(calls->size() == 3);

    auto hidden = registration("tab.hidden", command_focus_scope_t::tab, key, make_invoker(calls));
    hidden.definition.tab = workspace_tab_t::timing;
    REQUIRE(register_command(registry, std::move(hidden)).applied);
    focus = focus_for_compose();
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    CHECK(result.command == command_id_t{"edit.global"});
    REQUIRE(update_command_state(registry, command_id_t{"edit.global"}, false, std::nullopt));
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    CHECK(result.status == command_dispatch_status_t::disabled);
    CHECK(result.command == command_id_t{"edit.global"});

    auto open_timing = registration("workspace.open_timing", command_focus_scope_t::workspace, key,
                                    make_invoker(calls));
    open_timing.definition.tab = workspace_tab_t::timing;
    REQUIRE(register_command(registry, std::move(open_timing)).applied);
    focus.visible_tabs = {workspace_tab_t::compose};
    REQUIRE(update_command_state(registry, command_id_t{"edit.global"}, true, std::nullopt));
    REQUIRE(remap_command(registry, command_id_t{"edit.global"}, {}).applied);
    result =
        dispatch_shortcut(registry, {.chord = key}, focus, shortcut_primary_modifier_t::control);
    CHECK(result.status == command_dispatch_status_t::no_match);
}

TEST_CASE("equal-priority conflicts are diagnosed and do not invoke an arbitrary mutation",
          "[unit][core][command_registry][negative]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    auto invoke = make_invoker(calls);
    const shortcut_chord_t key{.key = shortcut_key_t::k};
    command_registry_t registry;
    auto first = registration("edit.first", command_focus_scope_t::tab, key, invoke, true);
    first.definition.tab = workspace_tab_t::compose;
    auto second = registration("edit.second", command_focus_scope_t::tab, key, invoke, true);
    second.definition.tab = workspace_tab_t::compose;
    REQUIRE(register_command(registry, std::move(first)).applied);
    REQUIRE(register_command(registry, std::move(second)).applied);

    const auto diagnostics = diagnose_shortcut_conflicts(registry);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().severity == diagnostic_severity_t::warning);
    CHECK(has_code(diagnostics, diagnostic_code_t::duplicate_binding));
    const auto result = dispatch_shortcut(registry, {.chord = key}, focus_for_compose(),
                                          shortcut_primary_modifier_t::control);
    CHECK(result.status == command_dispatch_status_t::conflict);
    CHECK(calls->empty());
}

TEST_CASE("mutation repeat is suppressed while repeatable navigation remains dispatchable",
          "[unit][core][command_registry]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    command_registry_t registry;
    REQUIRE(register_command(registry,
                             registration("edit.mutate", command_focus_scope_t::global,
                                          {.key = shortcut_key_t::m}, make_invoker(calls), true))
                .applied);
    REQUIRE(register_command(
                registry, registration("navigation.move", command_focus_scope_t::global,
                                       {.key = shortcut_key_t::right}, make_invoker(calls), false))
                .applied);

    auto result =
        dispatch_shortcut(registry, {.chord = {.key = shortcut_key_t::m}, .auto_repeat = true}, {},
                          shortcut_primary_modifier_t::control);
    CHECK(result.status == command_dispatch_status_t::repeat_suppressed);
    CHECK(calls->empty());
    result =
        dispatch_shortcut(registry, {.chord = {.key = shortcut_key_t::right}, .auto_repeat = true},
                          {}, shortcut_primary_modifier_t::control);
    CHECK(result.status == command_dispatch_status_t::invoked);
    REQUIRE(calls->size() == 1);
}

TEST_CASE("shortcut profiles round trip independently and apply atomically",
          "[unit][core][command_registry]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    command_registry_t registry;
    REQUIRE(
        register_command(registry, registration("edit.one", command_focus_scope_t::global,
                                                {.key = shortcut_key_t::a}, make_invoker(calls)))
            .applied);
    REQUIRE(
        register_command(registry, registration("edit.two", command_focus_scope_t::global,
                                                {.key = shortcut_key_t::b}, make_invoker(calls)))
            .applied);
    REQUIRE(remap_command(registry, command_id_t{"edit.one"},
                          {{.key = shortcut_key_t::c,
                            .modifiers = shortcut_modifier_t::primary | shortcut_modifier_t::alt}})
                .applied);
    REQUIRE(remap_command(registry, command_id_t{"edit.two"}, {}).applied);

    const auto profile = shortcut_profile_t::from(registry, "custom-layout");
    REQUIRE(profile.bindings.size() == 2);
    const auto serialized = serialize_shortcut_profile(profile);
    REQUIRE_FALSE(serialized.empty());
    const auto parsed = parse_shortcut_profile(serialized);
    REQUIRE(parsed);
    CHECK(*parsed.profile == profile);

    REQUIRE(remap_command(registry, command_id_t{"edit.one"}, {}).applied);
    REQUIRE(
        remap_command(registry, command_id_t{"edit.two"}, {{.key = shortcut_key_t::d}}).applied);
    REQUIRE(apply_shortcut_profile(registry, *parsed.profile).applied);
    CHECK(find_command(registry, command_id_t{"edit.one"})->current_shortcuts ==
          std::vector<shortcut_chord_t>{
              {.key = shortcut_key_t::c,
               .modifiers = shortcut_modifier_t::primary | shortcut_modifier_t::alt}});
    CHECK(find_command(registry, command_id_t{"edit.two"})->current_shortcuts.empty());

    const auto before = shortcut_profile_t::from(registry, "before");
    auto invalid = *parsed.profile;
    invalid.bindings.push_back({.command = {"missing.command"}});
    const auto rejected = apply_shortcut_profile(registry, invalid);
    CHECK_FALSE(rejected.applied);
    CHECK(has_code(rejected.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(shortcut_profile_t::from(registry, "before") == before);
}

TEST_CASE("malformed profiles, unknown chords, and all configured bounds are rejected",
          "[unit][core][command_registry][negative]") {
    const auto invalid_json = parse_shortcut_profile("{bad json");
    CHECK_FALSE(invalid_json);
    CHECK(has_code(invalid_json.diagnostics, diagnostic_code_t::invalid_binding));
    const auto oversized_json =
        parse_shortcut_profile(std::string(maximum_shortcut_profile_bytes + 1, ' '));
    CHECK_FALSE(oversized_json);
    CHECK(has_code(oversized_json.diagnostics, diagnostic_code_t::invalid_resolution));
    const auto bad_usage = parse_shortcut_profile(
        R"({"format":"wormedit-shortcuts","version":1,"id":"p","bindings":[{"command":"x.y","shortcuts":[{"key":232,"modifiers":0}]}]})");
    CHECK_FALSE(bad_usage);
    CHECK(has_code(bad_usage.diagnostics, diagnostic_code_t::invalid_binding));

    command_registry_t registry;
    auto invalid_id =
        registration("Bad ID", command_focus_scope_t::global, {.key = shortcut_key_t::a}, {});
    CHECK_FALSE(register_command(registry, std::move(invalid_id)).applied);
    auto too_many =
        registration("too.many", command_focus_scope_t::global, {.key = shortcut_key_t::a}, {});
    too_many.definition.default_shortcuts.assign(maximum_shortcuts_per_command + 1,
                                                 {.key = shortcut_key_t::a});
    CHECK_FALSE(register_command(registry, std::move(too_many)).applied);

    auto duplicate = registration("duplicate.chord", command_focus_scope_t::global,
                                  {.key = shortcut_key_t::a}, {});
    duplicate.definition.default_shortcuts.push_back({.key = shortcut_key_t::a});
    CHECK_FALSE(register_command(registry, std::move(duplicate)).applied);

    for (std::size_t i = 0; i < maximum_registered_commands; ++i) {
        auto item = registration("cmd." + std::to_string(i), command_focus_scope_t::global,
                                 {.key = shortcut_key_t::a}, {});
        REQUIRE(register_command(registry, std::move(item)).applied);
    }
    CHECK(registry.commands.size() == maximum_registered_commands);
    const auto capped =
        register_command(registry, registration("cmd.extra", command_focus_scope_t::global,
                                                {.key = shortcut_key_t::b}, {}));
    CHECK_FALSE(capped.applied);
    CHECK(has_code(capped.diagnostics, diagnostic_code_t::invalid_resolution));

    command_registry_t enum_registry;
    auto invalid_enum =
        registration("invalid.enum", command_focus_scope_t::global, {.key = shortcut_key_t::a}, {});
    invalid_enum.definition.repeat_policy = static_cast<command_repeat_policy_t>(255);
    const auto invalid_registration = register_command(enum_registry, std::move(invalid_enum));
    CHECK_FALSE(invalid_registration.applied);
    CHECK(has_code(invalid_registration.diagnostics, diagnostic_code_t::invalid_binding));
}

TEST_CASE("default registry seeds discoverable editor commands and visible tab routing",
          "[unit][core][command_registry]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    CHECK(has_command(registry, "document.new_project"));
    CHECK(has_command(registry, "document.new_chart"));
    CHECK(has_command(registry, "navigation.previous_marker"));
    CHECK(has_command(registry, "navigation.next_marker"));
    CHECK(has_command(registry, "navigation.previous_bookmark"));
    CHECK(has_command(registry, "navigation.next_bookmark"));
    CHECK(has_command(registry, "markers.add_bookmark"));
    CHECK(has_command(registry, "navigation.previous_snap"));
    CHECK(has_command(registry, "navigation.next_snap"));
    CHECK(has_command(registry, "history.undo"));
    CHECK(has_command(registry, "history.redo"));
    CHECK(has_command(registry, "edit.delete"));
    CHECK(has_command(registry, "selection.clear"));
    CHECK(has_command(registry, "transport.play_pause"));
    CHECK(has_command(registry, "timing.insert_bpm"));
    CHECK(has_command(registry, "timing.insert_sv"));
    CHECK(has_command(registry, "audio.paint_hitsound"));

    const auto* new_chart = find_command(registry, command_id_t{"document.new_chart"});
    REQUIRE(new_chart);
    REQUIRE(new_chart->definition.default_shortcuts.size() == 1);
    CHECK(new_chart->definition.default_shortcuts.front() ==
          shortcut_chord_t{.key = shortcut_key_t::n,
                           .modifiers = shortcut_modifier_t::primary | shortcut_modifier_t::shift});

    auto focus = focus_for_compose();
    const auto switch_tab = dispatch_shortcut(registry, {.chord = {.key = shortcut_key_t::f3}},
                                              focus, shortcut_primary_modifier_t::control);
    CHECK(switch_tab.status == command_dispatch_status_t::invoked);
    CHECK(switch_tab.command == command_id_t{"workspace.open_timing"});
    CHECK(calls->back() == command_id_t{"workspace.open_timing"});

    focus.visible_tabs = {workspace_tab_t::compose};
    const auto hidden = dispatch_shortcut(registry, {.chord = {.key = shortcut_key_t::f3}}, focus,
                                          shortcut_primary_modifier_t::control);
    CHECK(hidden.status == command_dispatch_status_t::no_match);
}

TEST_CASE("Go To Bookmark is bound to Ctrl Shift G without changing Ctrl G",
          "[unit][core][command_registry][bookmark-chooser]") {
    const auto registry = command_registry_t::from(make_invoker(
        std::make_shared<std::vector<command_id_t>>()));
    const auto* command = find_command(registry, command_id_t{"navigation.go_to_bookmark"});
    REQUIRE(command);
    REQUIRE(command->definition.default_shortcuts.size() == 1);
    CHECK(command->definition.default_shortcuts.front() ==
          shortcut_chord_t{.key = shortcut_key_t::g,
                           .modifiers = shortcut_modifier_t::primary | shortcut_modifier_t::shift});
    const auto focus = focus_for_compose();
    const auto chord = dispatch_shortcut(registry,
        {.chord = {.key = shortcut_key_t::g,
                   .modifiers = shortcut_modifier_t::control | shortcut_modifier_t::shift}},
        focus, shortcut_primary_modifier_t::control);
    CHECK(chord.status == command_dispatch_status_t::invoked);
    CHECK(chord.command == command_id_t{"navigation.go_to_bookmark"});
    const auto old = dispatch_shortcut(registry,
        {.chord = {.key = shortcut_key_t::g,
                   .modifiers = static_cast<shortcut_modifiers_t>(
                       shortcut_modifier_t::control)}},
        focus, shortcut_primary_modifier_t::control);
    CHECK(old.command != command_id_t{"navigation.go_to_bookmark"});
}

auto save_binding_dispatches_for_primary(const command_registry_t& registry,
                                         shortcut_primary_modifier_t primary) -> bool {
    const auto modifier = primary == shortcut_primary_modifier_t::control
                              ? shortcut_modifier_t::control : shortcut_modifier_t::meta;
    const auto chord = shortcut_chord_t{
        .key = shortcut_key_t::s,
        .modifiers = static_cast<shortcut_modifiers_t>(modifier),
    };
    const auto saved = dispatch_shortcut(registry, {.chord = chord}, focus_for_compose(), primary);
    const auto repeated = dispatch_shortcut(registry,
        {.chord = chord, .auto_repeat = true}, focus_for_compose(), primary);
    return saved.status == command_dispatch_status_t::invoked &&
           saved.command == command_id_t{"document.save_project"} &&
           repeated.status == command_dispatch_status_t::repeat_suppressed;
}

auto save_command_has_primary_binding(const command_registry_t& registry) -> bool {
    const auto* command = find_command(registry, command_id_t{"document.save_project"});
    return command && command->definition.mutation &&
           command->definition.default_shortcuts.size() == 1 &&
           command->definition.default_shortcuts.front() ==
               shortcut_chord_t{
                   .key = shortcut_key_t::s,
                   .modifiers = static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary),
               };
}

TEST_CASE("Save command uses platform Primary and suppresses held-key repeats",
          "[unit][core][command_registry][save-shortcut]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    CHECK(save_command_has_primary_binding(registry));
    CHECK(save_binding_dispatches_for_primary(registry, shortcut_primary_modifier_t::control));
    CHECK(save_binding_dispatches_for_primary(registry, shortcut_primary_modifier_t::meta));
    CHECK(calls->size() == 2);
}

TEST_CASE("timing insertion commands route through Compose focus",
          "[unit][core][command_registry][timing]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    CHECK(invokes_command_in_focus(registry, focus_for_compose(), calls, "timing.insert_bpm", 1));
    CHECK(invokes_command_in_focus(registry, focus_for_compose(), calls, "timing.insert_sv", 2));
    CHECK(invoke_command(registry, command_id_t{"timing.insert_bpm"},
                         {.workspace_active = true, .active_tab = workspace_tab_t::setup,
                          .visible_tabs = {workspace_tab_t::setup, workspace_tab_t::compose}})
              .status == command_dispatch_status_t::focus_consumed);
}

TEST_CASE("transport shortcuts leave primary Space available for canvas gestures",
          "[unit][core][command_registry][transport]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    const auto focus = focus_for_compose();
    const auto invoke = [&](shortcut_modifiers_t modifiers,
                            shortcut_primary_modifier_t primary_modifier) {
        return dispatch_shortcut(registry,
                                 {.chord = {.key = shortcut_key_t::space,
                                            .modifiers = modifiers}},
                                 focus, primary_modifier).command;
    };
    const std::array commands{
        invoke(static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control),
               shortcut_primary_modifier_t::control),
        invoke(shortcut_modifier_t::control | shortcut_modifier_t::shift,
               shortcut_primary_modifier_t::control),
        invoke(static_cast<shortcut_modifiers_t>(shortcut_modifier_t::meta),
               shortcut_primary_modifier_t::meta),
        invoke(shortcut_modifier_t::meta | shortcut_modifier_t::shift,
               shortcut_primary_modifier_t::meta),
    };
    CHECK(commands == std::array<std::optional<command_id_t>, 4>{
                          std::nullopt,
                          command_id_t{"transport.play_from_start"},
                          std::nullopt,
                          command_id_t{"transport.play_from_start"}});
}

TEST_CASE("Playback rate shortcuts have no registry conflicts", "[unit][commands]") {
    const auto registry = command_registry_t::from(
        [](const command_invocation_context_t&) { return command_invocation_result_t{}; });
    CHECK(diagnose_shortcut_conflicts(registry, shortcut_primary_modifier_t::control).empty());
    CHECK(diagnose_shortcut_conflicts(registry, shortcut_primary_modifier_t::meta).empty());
    const auto* slower = find_command(registry, command_id_t{"transport.rate_slower"});
    const auto* faster = find_command(registry, command_id_t{"transport.rate_faster"});
    REQUIRE(slower);
    REQUIRE(faster);
    CHECK(slower->definition.default_shortcuts.front().modifiers ==
          static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary));
    CHECK(faster->definition.default_shortcuts.front().modifiers ==
          static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary));
    CHECK(slower->definition.default_shortcuts.front().key == shortcut_key_t::f11);
    CHECK(faster->definition.default_shortcuts.front().key == shortcut_key_t::f12);
}

TEST_CASE("Tap timing registry metadata is nonmutating and initially disabled", "[unit][commands]") {
    auto registry = command_registry_t::from([](const command_invocation_context_t&) {
        return command_invocation_result_t{.accepted = true};
    });
    const auto* tap = find_command(registry, {"timing.tap"});
    const auto* assistant = find_command(registry, {"timing.tap_assistant"});
    REQUIRE(tap);
    REQUIRE(assistant);
    CHECK_FALSE(
        (tap->definition.mutation || assistant->definition.mutation || tap->definition.enabled));
    REQUIRE(tap->definition.default_shortcuts.size() == 1);
    CHECK(tap->definition.default_shortcuts.front().key == shortcut_key_t::t);
}

TEST_CASE("Tap timing dispatch accepts workspace taps and rejects text entry", "[unit][commands]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    auto registry = command_registry_t::from(make_invoker(calls));
    REQUIRE(update_command_state(registry, {"timing.tap"}, true, std::nullopt).applied);
    auto focus = focus_for_compose();
    CHECK(dispatch_shortcut(registry, {.chord = {.key = shortcut_key_t::t}}, focus,
        shortcut_primary_modifier_t::control).status == command_dispatch_status_t::invoked);
    REQUIRE(calls->size() == 1);
    CHECK(calls->front().value == "timing.tap");
    focus.text_entry_active = true;
    CHECK(dispatch_shortcut(registry, {.chord = {.key = shortcut_key_t::t}}, focus,
        shortcut_primary_modifier_t::control).status != command_dispatch_status_t::invoked);
    CHECK(calls->size() == 1);
}

namespace {
void check_timing_lock_chord(const command_registry_t& registry,
                             shortcut_primary_modifier_t primary,
                             shortcut_modifier_t modifier) {
    CHECK(diagnose_shortcut_conflicts(registry, primary).empty());
    const auto focus = focus_for_compose();
    const shortcut_event_t lock{.chord = {.key = shortcut_key_t::t,
        .modifiers = modifier | shortcut_modifier_t::shift}};
    const auto dispatched = dispatch_shortcut(registry, lock, focus, primary);
    CHECK(dispatched.status == command_dispatch_status_t::invoked);
    CHECK(dispatched.command == command_id_t{"timing.toggle_lock"});
    auto old = lock;
    old.chord.key = shortcut_key_t::l;
    CHECK(dispatch_shortcut(registry, old, focus, primary).command == command_id_t{"semantic_layer.create"});
    CHECK(dispatch_shortcut(registry, {.chord = {.key = shortcut_key_t::t}},
                            focus, primary).command == command_id_t{"timing.tap"});
    auto text_focus = focus;
    text_focus.text_entry_active = true;
    CHECK(dispatch_shortcut(registry, lock, text_focus, primary).status ==
          command_dispatch_status_t::focus_consumed);
}
}

TEST_CASE("Timing Lock uses Primary Shift T in Compose without shortcut conflicts",
          "[unit][commands][command_registry]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    auto registry = command_registry_t::from(make_invoker(calls));
    REQUIRE(update_command_state(registry, {"timing.tap"}, true, std::nullopt).applied);
    check_timing_lock_chord(registry, shortcut_primary_modifier_t::control,
                            shortcut_modifier_t::control);
    check_timing_lock_chord(registry, shortcut_primary_modifier_t::meta,
                            shortcut_modifier_t::meta);
}

namespace {
void check_notification_chord(const command_registry_t& registry,
                              shortcut_primary_modifier_t primary_mode,
                              shortcut_modifier_t primary_key) {
    CHECK(diagnose_shortcut_conflicts(registry, primary_mode).empty());
    const auto dispatched = dispatch_shortcut(registry,
        {.chord = {.key = shortcut_key_t::m,
                   .modifiers = primary_key | shortcut_modifier_t::shift}},
        {.workspace_active = true}, primary_mode);
    CHECK(dispatched.command == command_id_t{"view.notifications"});
    CHECK(dispatched.status == command_dispatch_status_t::invoked);
}
}

TEST_CASE("notification history default chord opens without a chart and has no conflicts",
          "[unit][core][command_registry][notifications]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    check_notification_chord(registry, shortcut_primary_modifier_t::control,
                             shortcut_modifier_t::control);
    check_notification_chord(registry, shortcut_primary_modifier_t::meta,
                             shortcut_modifier_t::meta);
}

TEST_CASE("notification history shortcut respects text and IME guards",
          "[unit][core][command_registry][notifications]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    const shortcut_chord_t chord{shortcut_key_t::m,
        shortcut_modifier_t::control | shortcut_modifier_t::shift};
    const auto text = dispatch_shortcut(registry, {.chord = chord},
        {.text_entry_active = true}, shortcut_primary_modifier_t::control);
    const auto ime = dispatch_shortcut(registry, {.chord = chord},
        {.ime_composition_active = true}, shortcut_primary_modifier_t::control);
    CHECK(text.status == command_dispatch_status_t::focus_consumed);
    CHECK(ime.status == command_dispatch_status_t::focus_consumed);
    CHECK(calls->empty());
}

TEST_CASE("Plain Space plays or stops once and respects input owners", "[unit][commands]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    auto focus = focus_for_compose();
    const shortcut_event_t event{.chord = {.key = shortcut_key_t::space}};
    const auto result = dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control);
    CHECK(result.command == command_id_t{"transport.play_stop"});
    auto repeat = event;
    repeat.auto_repeat = true;
    const auto repeated = dispatch_shortcut(registry, repeat, focus, shortcut_primary_modifier_t::control);
    CHECK(repeated.status == command_dispatch_status_t::repeat_suppressed);
    CHECK(calls->size() == 1);
    focus.ime_composition_active = true;
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).status ==
          command_dispatch_status_t::focus_consumed);
    CHECK(calls->size() == 1);
}

TEST_CASE("Primary V starts paste only for the Compose input owner", "[unit][commands][clipboard]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    auto focus = focus_for_compose();
    const shortcut_event_t event{.chord = {.key = shortcut_key_t::v,
                                          .modifiers = static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control)}};
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).command ==
          command_id_t{"edit.paste"});
    focus.text_entry_active = true;
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).status ==
          command_dispatch_status_t::focus_consumed);
    CHECK(calls->size() == 1);
}

TEST_CASE("Compose palette Alt digits select exactly one tool", "[core][command-registry]") {
    using namespace wormedit;
    std::string invoked;
    const auto registry = command_registry_t::from([&](const auto& context) {
        invoked = context.command.value;
        return command_invocation_result_t{.accepted = true};
    });
    const std::array ids{"edit.tool_select", "edit.tool_draw", "edit.tool_layer",
        "edit.note_kind_mine", "edit.note_kind_invisible", "edit.note_kind_lift",
        "edit.note_kind_fake", "edit.note_kind_roll"};
    for (unsigned digit = 0; digit < ids.size(); ++digit) {
        invoked.clear();
        const auto result = dispatch_shortcut(registry,
            {.chord = {static_cast<shortcut_key_t>(static_cast<unsigned>(shortcut_key_t::digit_1) + digit),
                       static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt)}},
            focus_for_compose(),
            shortcut_primary_modifier_t::control);
        CHECK(invoked == ids[digit]);
    }
}

namespace {

auto remap_test_registry(const std::shared_ptr<std::vector<command_id_t>>& calls) -> command_registry_t {
    command_registry_t registry;
    REQUIRE(register_command(registry, registration("test.remap", command_focus_scope_t::global,
        shortcut_chord_t{.key = shortcut_key_t::a, .modifiers = 0}, make_invoker(calls))).applied);
    return registry;
}

const shortcut_chord_t remap_test_chord{.key = shortcut_key_t::k,
    .modifiers = static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control)};

} // namespace

void check_save_and_reload(const command_registry_t& registry, const std::shared_ptr<std::vector<command_id_t>>& calls,
                           const std::filesystem::path& path) {
    REQUIRE(save_shortcut_profile_atomic(path, shortcut_profile_t::from(registry, "user")));
    auto reloaded = remap_test_registry(calls);
    const auto loaded = load_shortcut_profile(path);
    REQUIRE(loaded);
    REQUIRE(apply_shortcut_profile(reloaded, *loaded.profile).applied);
    const auto* command = find_command(reloaded, {"test.remap"});
    REQUIRE(command != nullptr);
    CHECK(command->current_shortcuts == std::vector<shortcut_chord_t>{remap_test_chord});
}

TEST_CASE("remapped shortcut survives atomic save and reload", "[core][command-registry][shortcut]") {
    const auto calls = std::make_shared<std::vector<command_id_t>>();
    auto registry = remap_test_registry(calls);
    REQUIRE(remap_command(registry, {"test.remap"}, {remap_test_chord}).applied);
    CHECK_FALSE(remap_command(registry, {"test.unknown"}, {remap_test_chord}).applied);

    const auto directory = std::filesystem::temp_directory_path() / "wormedit-remap-persistence-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    check_save_and_reload(registry, calls, directory / "shortcuts.json");
    std::filesystem::remove_all(directory);
}

TEST_CASE("failed remap save keeps the in-memory shortcut", "[core][command-registry][shortcut]") {
    const auto calls = std::make_shared<std::vector<command_id_t>>();
    auto registry = remap_test_registry(calls);
    REQUIRE(remap_command(registry, {"test.remap"}, {remap_test_chord}).applied);
    const auto directory = std::filesystem::temp_directory_path() / "wormedit-remap-failed-save-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    // A save into a missing directory fails, and the in-memory mapping stays in place.
    CHECK_FALSE(save_shortcut_profile_atomic(directory / "missing" / "shortcuts.json",
                                             shortcut_profile_t::from(registry, "user")));
    CHECK(find_command(registry, {"test.remap"})->current_shortcuts ==
          std::vector<shortcut_chord_t>{remap_test_chord});
    std::filesystem::remove_all(directory);
}

void check_conflict_pairs_match_diagnostic(const command_registry_t& registry) {
    const auto pairs = find_shortcut_conflicts(registry);
    REQUIRE(pairs.size() == 1);
    CHECK(pairs.front() == shortcut_conflict_t{.first = {"test.first"}, .second = {"test.second"}});
}

TEST_CASE("structured shortcut conflicts name the same pairs as the diagnostic", "[core][command-registry][shortcut]") {
    const auto calls = std::make_shared<std::vector<command_id_t>>();
    command_registry_t registry;
    const shortcut_chord_t shared{.key = shortcut_key_t::q, .modifiers = 0};
    REQUIRE(register_command(registry, registration("test.first", command_focus_scope_t::global, shared,
        make_invoker(calls))).applied);
    REQUIRE(register_command(registry, registration("test.second", command_focus_scope_t::global, shared,
        make_invoker(calls))).applied);
    REQUIRE(register_command(registry, registration("test.third", command_focus_scope_t::global,
        shortcut_chord_t{.key = shortcut_key_t::w, .modifiers = 0}, make_invoker(calls))).applied);

    check_conflict_pairs_match_diagnostic(registry);
    const auto diagnostics = diagnose_shortcut_conflicts(registry);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "shortcut conflict between 'test.first' and 'test.second'");
}

TEST_CASE("BGM pseudo-lane shortcuts are remappable conflict-checked Compose commands",
          "[unit][core][command_registry][t424]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    auto registry = command_registry_t::from(make_invoker(calls));
    CHECK(diagnose_shortcut_conflicts(registry).empty());
    const auto result = dispatch_shortcut(registry, {.chord = {.key = shortcut_key_t::backquote}},
                                          focus_for_compose(), shortcut_primary_modifier_t::control);
    CHECK(result.command == command_id_t{"edit.move_to_bgm"});
    CHECK(result.status == command_dispatch_status_t::invoked);
}

TEST_CASE("Shift Space loops the Compose selection and retains transport focus gates", "[unit][commands][loop-selection]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    auto focus = focus_for_compose();
    shortcut_event_t event{.chord = {.key = shortcut_key_t::space,
        .modifiers = static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift)}};
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).command ==
          command_id_t{"transport.loop_selection"});
    event.auto_repeat = true;
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).status ==
          command_dispatch_status_t::repeat_suppressed);
    event.auto_repeat = false;
    focus.text_entry_active = true;
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).status ==
          command_dispatch_status_t::focus_consumed);
    focus.text_entry_active = false;
    focus.active_tab = workspace_tab_t::metadata;
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).status ==
          command_dispatch_status_t::no_match);
    CHECK(calls->size() == 1);
}

TEST_CASE("Primary M splits a measure without shortcut conflicts", "[commands][measure-split]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    const auto focus = focus_for_compose();
    const auto event = shortcut_event_t{.chord={shortcut_key_t::m,static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control)}};
    CHECK(dispatch_shortcut(registry,event,focus,shortcut_primary_modifier_t::control).command == command_id_t{"measures.split_at_cursor"});
    CHECK(diagnose_shortcut_conflicts(registry,shortcut_primary_modifier_t::control).empty());
    CHECK(diagnose_shortcut_conflicts(registry,shortcut_primary_modifier_t::meta).empty());
}

TEST_CASE("Go to sound uses the platform primary K chord in Compose", "[core][command_registry][sound-picker]") {
    const auto registry = command_registry_t::from(make_invoker(
        std::make_shared<std::vector<command_id_t>>()));
    for (const auto primary : {shortcut_primary_modifier_t::control, shortcut_primary_modifier_t::meta}) {
        const auto modifier = primary == shortcut_primary_modifier_t::control
            ? shortcut_modifier_t::control : shortcut_modifier_t::meta;
        const auto dispatched = dispatch_shortcut(registry,
            {.chord = {.key = shortcut_key_t::k, .modifiers = static_cast<shortcut_modifiers_t>(modifier)}},
            focus_for_compose(), primary);
        CHECK(dispatched.status == command_dispatch_status_t::invoked);
        CHECK(dispatched.command == command_id_t{"navigation.go_to_sound"});
    }
}

namespace {
void check_dock_registry(bool value) { CHECK(value); }
void check_dock_shortcut(const command_registry_t& registry, shortcut_key_t key,
                         shortcut_modifiers_t modifiers, const char* id,
                         shortcut_primary_modifier_t primary) {
    shortcut_event_t event{.chord = {key, modifiers}};
    auto focus = focus_for_compose();
    const auto dispatched = dispatch_shortcut(registry, event, focus, primary);
    check_dock_registry(dispatched.status == command_dispatch_status_t::invoked);
    check_dock_registry(dispatched.command == command_id_t{id});
    event.auto_repeat = true;
    check_dock_registry(dispatch_shortcut(registry, event, focus, primary).status ==
                        command_dispatch_status_t::repeat_suppressed || std::string_view{id} == "navigation.go_to_sound");
    event.auto_repeat = false;
    focus.text_entry_active = true;
    check_dock_registry(dispatch_shortcut(registry, event, focus, primary).status ==
                        command_dispatch_status_t::focus_consumed);
    focus.text_entry_active = false;
    focus.ime_composition_active = true;
    check_dock_registry(dispatch_shortcut(registry, event, focus, primary).status ==
                        command_dispatch_status_t::focus_consumed);
    focus.ime_composition_active = false;
    focus.modal_active = true;
    check_dock_registry(dispatch_shortcut(registry, event, focus, primary).status ==
                        command_dispatch_status_t::focus_consumed);
    focus.modal_active = false;
    focus.active_tab = workspace_tab_t::metadata;
    check_dock_registry(dispatch_shortcut(registry, event, focus, primary).status ==
                        command_dispatch_status_t::no_match);
}
}
TEST_CASE("Dock shortcuts distinguish Layers creation Sounds toggle and Go to sound",
          "[core][command_registry][dock-shortcuts]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    auto registry = command_registry_t::from(make_invoker(calls));
    for (const auto primary : {shortcut_primary_modifier_t::control, shortcut_primary_modifier_t::meta}) {
        const auto modifier = primary == shortcut_primary_modifier_t::control
            ? shortcut_modifier_t::control : shortcut_modifier_t::meta;
        check_dock_registry(diagnose_shortcut_conflicts(registry, primary).empty());
        check_dock_shortcut(registry, shortcut_key_t::l, static_cast<shortcut_modifiers_t>(modifier),
                            "view.open_layers", primary);
        check_dock_shortcut(registry, shortcut_key_t::l, modifier | shortcut_modifier_t::shift,
                            "semantic_layer.create", primary);
        check_dock_shortcut(registry, shortcut_key_t::k, modifier | shortcut_modifier_t::shift,
                            "view.toggle_sounds", primary);
        check_dock_shortcut(registry, shortcut_key_t::k, static_cast<shortcut_modifiers_t>(modifier),
                            "navigation.go_to_sound", primary);
        for (const auto key : {shortcut_key_t::l, shortcut_key_t::k})
            check_dock_registry(dispatch_shortcut(registry, {.chord = {key,
                modifier | shortcut_modifier_t::shift}, .auto_repeat = true}, focus_for_compose(),
                primary).status == command_dispatch_status_t::repeat_suppressed);
    }
    const shortcut_chord_t remapped{shortcut_key_t::f8};
    check_dock_registry(remap_command(registry, {"view.toggle_sounds"}, {remapped}).applied);
    check_dock_registry(dispatch_shortcut(registry, {.chord = remapped}, focus_for_compose(), shortcut_primary_modifier_t::control).command ==
                        command_id_t{"view.toggle_sounds"});
    check_dock_registry(!dispatch_shortcut(registry, {.chord = {shortcut_key_t::k,
        shortcut_modifier_t::control | shortcut_modifier_t::shift}}, focus_for_compose(), shortcut_primary_modifier_t::control).command);
}

TEST_CASE("Command menu defaults to primary Shift P and respects text focus",
          "[unit][core][command-registry][command-menu]") {
    std::vector<std::string> invoked;
    auto registry = command_registry_t::from([&](const command_invocation_context_t& context) {
        invoked.push_back(context.command.value);
        return command_invocation_result_t{.accepted = true};
    });
    const auto* command = find_command(registry, {"workspace.command_menu"});
    REQUIRE(command);
    REQUIRE(command->definition.default_shortcuts.size() == 1);
    CHECK(command->definition.default_shortcuts[0] ==
          shortcut_chord_t{shortcut_key_t::p,
                           shortcut_modifier_t::primary | shortcut_modifier_t::shift});
    CHECK(invoke_command(registry, {"workspace.command_menu"}, {}).status == command_dispatch_status_t::invoked);
    CHECK(invoke_command(registry, {"workspace.command_menu"}, {.text_entry_active = true}).status ==
          command_dispatch_status_t::focus_consumed);
}

TEST_CASE("Quantize commands are searchable and preview shortcut respects text focus", "[core][command_registry][quantize-intent]") {
    const auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    CHECK(has_command(registry, "edit.toggle_quantize_preview"));
    CHECK(has_command(registry, "edit.apply_quantize"));
    const shortcut_event_t event{.chord = {.key = shortcut_key_t::q,
        .modifiers = shortcut_modifier_t::control | shortcut_modifier_t::alt}};
    auto focus = focus_for_compose();
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).command ==
          command_id_t{"edit.toggle_quantize_preview"});
    focus.text_entry_active = true;
    CHECK(dispatch_shortcut(registry, event, focus, shortcut_primary_modifier_t::control).status ==
          command_dispatch_status_t::focus_consumed);
}

TEST_CASE("Select All Objects uses Primary Alt A independently from note selection",
          "[unit][core][command_registry][selection-consolidation]") {
    auto calls = std::make_shared<std::vector<command_id_t>>();
    const auto registry = command_registry_t::from(make_invoker(calls));
    const auto primary = GENERATE(shortcut_primary_modifier_t::control, shortcut_primary_modifier_t::meta);
    const auto actual = primary == shortcut_primary_modifier_t::control
        ? shortcut_modifier_t::control : shortcut_modifier_t::meta;
    const auto modifiers = static_cast<shortcut_modifiers_t>(
        static_cast<shortcut_modifiers_t>(actual) | static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt));
    const shortcut_event_t event{.chord = {.key = shortcut_key_t::a, .modifiers = modifiers}};
    auto focus = focus_for_compose();
    CHECK(dispatch_shortcut(registry, event, focus, primary).command == command_id_t{"selection.select_all_objects"});
    focus.text_entry_active = true;
    CHECK(dispatch_shortcut(registry, event, focus, primary).status == command_dispatch_status_t::focus_consumed);
    focus.text_entry_active = false;
    focus.ime_composition_active = true;
    CHECK(dispatch_shortcut(registry, event, focus, primary).status == command_dispatch_status_t::focus_consumed);
}
