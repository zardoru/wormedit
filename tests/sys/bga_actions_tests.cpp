#include "sys/compose_bga_actions.h"
#include <wormedit/core/resource_assignment.h>
#include <catch2/catch_test_macros.hpp>
#include <source_location>
#include <chrono>
#include <filesystem>
#include <fstream>
using namespace wormedit;
using namespace wormedit::sys;
namespace {
void check_bga(bool value, std::source_location where = std::source_location::current()) {
    INFO("BGA check at line " << where.line()); CHECK(value);
}
void require_bga(bool value, std::source_location where = std::source_location::current()) {
    INFO("BGA check at line " << where.line()); REQUIRE(value);
}

struct Fixture {
    std::filesystem::path directory;
    document_history_t history;
    workspace_controller_t workspace;
    Fixture() {
        directory = std::filesystem::temp_directory_path() / ("wormedit-bga-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        song_document_t song{.id = song_id_t{1}, .revision = 1,
            .charts = {{.id = chart_id_t{1}, .source_family = format_family_t::bms,
                .measures = {{.id = measure_id_t{1}}},
                .lane_layouts = {{.id = object_id_t{10}, .position = {measure_id_t{1}, 0},
                    .layout = {.profile_id = "test", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
                .bpms = {{.id = object_id_t{11}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}}}}};
        require_bga(static_cast<bool>(validate_document(song).empty()));
        history = document_history_t::from(std::move(song));
        const auto binding = workspace_controller_t::from(history);
        require_bga(static_cast<bool>(binding.value));
        workspace = *binding.value;
    }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(directory, error); }
    auto image(const char* name) -> std::string {
        const auto path = directory / name;
        std::ofstream file{path, std::ios::binary};
        file << "P6\n1 1\n255\n";
        file.put('x'); file.put('y'); file.put('z');
        return path.string();
    }
};
}
TEST_CASE("BGA batch skips unreadable images and imports readable images in one undo step", "[bga][import]") {
    notification_center_t center;
    notification_sink_t sink{center};
    Fixture fixture;
    const std::array paths{fixture.image("a.ppm"), fixture.image("b.ppm"), (fixture.directory / "bad.png").string()};
    const auto status = import_bga_files(fixture.history, fixture.workspace, chart_id_t{1}, paths, sink);
    INFO(status);
    center.drain_inbox(notification_time_t{0});
    const auto entries = center.snapshot(notification_capacity);
    require_bga(entries.size() == 1);
    check_bga(entries[0].severity == notification_severity_t::warning);
    check_bga(entries[0].source == notification_source_t::import_);
    check_bga(entries[0].message == status);
    require_bga(static_cast<bool>(fixture.history.document.resources.size() == 2));
    check_bga(fixture.history.undo.size() == 1);
    check_bga(status.find("skipped 1") != std::string::npos);
    const auto first_id = fixture.history.document.resources.front().target_identities.front().identifier;
    check_bga(first_id == "01");
    const std::array more{fixture.image("c.ppm")};
    (void)import_bga_files(fixture.history, fixture.workspace, chart_id_t{1}, more);
    check_bga(fixture.history.document.resources.front().target_identities.front().identifier == first_id);
    require_bga(static_cast<bool>(undo_document_command(fixture.history)));
    check_bga(fixture.history.document.resources.size() == 2);
    require_bga(static_cast<bool>(undo_document_command(fixture.history)));
    check_bga(fixture.history.document.resources.empty());
}
TEST_CASE("BGA remove unused keeps referenced resources and rejects stale actions", "[bga][remove]") {
    notification_center_t center;
    notification_sink_t sink{center};
    Fixture fixture;
    const std::array paths{fixture.image("a.ppm"), fixture.image("b.ppm")};
    (void)import_bga_files(fixture.history, fixture.workspace, chart_id_t{1}, paths);
    require_bga(static_cast<bool>(fixture.history.document.resources.size() == 2));
    const auto kept = fixture.history.document.resources.front().id;
    fixture.history.document.charts.front().bga_events.push_back(
        {.id = object_id_t{12}, .position = {measure_id_t{1}, 0}, .resource = kept});
    wormedit::ui::workspace_action_t action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA;
    action.compose_input_document_revision = 0;
    require_bga(static_cast<bool>(dispatch_compose_bga_action(action, fixture.history, fixture.workspace, chart_id_t{1}, sink)));
    check_bga(fixture.history.document.resources.size() == 2);
    center.drain_inbox(notification_time_t{0});
    auto entries = center.snapshot(notification_capacity);
    require_bga(entries.size() == 1);
    check_bga(entries[0].severity == notification_severity_t::warning);
    check_bga(entries[0].source == notification_source_t::edit);
    action.compose_input_document_revision = fixture.history.document.revision;
    const auto before = fixture.history.undo.size();
    require_bga(static_cast<bool>(dispatch_compose_bga_action(action, fixture.history, fixture.workspace, chart_id_t{1}, sink)));
    require_bga(static_cast<bool>(fixture.history.document.resources.size() == 1));
    check_bga(fixture.history.document.resources.front().id == kept);
    check_bga(fixture.history.undo.size() == before + 1);
    center.drain_inbox(notification_time_t{0});
    entries = center.snapshot(notification_capacity);
    require_bga(entries.size() == 2);
    check_bga(entries[0].severity == notification_severity_t::success);
    require_bga(static_cast<bool>(undo_document_command(fixture.history)));
    check_bga(fixture.history.document.resources.size() == 2);
}
