#include <wormedit/core/document.h>
#include <wormedit/core/sound_import.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

using namespace wormedit;

#if defined(_WIN32)
#define TEST_ROOT "C:/tmp"
#else
#define TEST_ROOT "/tmp"
#endif

namespace {

auto bmson_song() -> song_document_t {
    std::ifstream stream(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" /
                             "published-vox-slicing.bmson",
                         std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream),
                            std::istreambuf_iterator<char>()};
    const auto parsed =
        parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song);
    return *parsed.song;
}

auto entry(const std::string& name, sound_kind_t kind) -> bulk_sound_import_entry_t {
    return {.asset = {.source_path = std::filesystem::path(TEST_ROOT) / "stems" / (name + ".ogg"),
                      .name = name,
                      .audio_stream = audio_stream_info_t{.sample_rate = 48'000,
                                                          .channels = 2,
                                                          .frame_count = 480'000,
                                                          .seekable = true}},
            .kind = kind};
}

// Mirrors the caller's copy step: consecutive reserved asset IDs and project-relative copies.
auto reserved(bulk_sound_import_action_t action, const song_document_t& document)
    -> bulk_sound_import_action_t {
    auto next = next_resource_asset_id(document)->value;
    for (auto& item : action.entries) {
        item.asset.expected_new_asset_id = asset_id_t{next};
        item.asset.copied_project_relative_path =
            std::filesystem::path("assets") /
            (std::to_string(next) + "-" + item.asset.source_path.filename().string());
        ++next;
    }
    return action;
}

auto bgm_marker_count(const chart_document_t& chart) -> std::size_t {
    std::size_t count = 0;
    for (const auto& measure : chart.measures) count += measure.bgm_events.size();
    return count;
}

auto same_resource_group_links(const std::vector<resource_entry_t>& left,
                               const std::vector<resource_entry_t>& right) -> bool {
    return std::ranges::equal(left, right, [](const auto& first, const auto& second) {
        return first.id == second.id && first.sound_group == second.sound_group;
    });
}

} // namespace

TEST_CASE("bulk import adds stems as BMSON channels visible as BGM markers in one undo step",
          "[unit][core][sounds][import]") {
    auto history = make_document_history(bmson_song());
    const auto chart_id = history.document.charts.front().id;
    const auto revision = history.document.revision;
    const auto resources = history.document.resources.size();
    const auto channels = history.document.charts.front().sound_channels.size();
    const auto markers = bgm_marker_count(history.document.charts.front());
    const auto lanes = history.document.charts.front().bgm_source_lanes.size();

    const auto result = apply_bulk_sound_import(
        history, revision,
        reserved({.chart = chart_id,
                  .entries = {entry("drums", sound_kind_t::stem), entry("bass", sound_kind_t::stem),
                              entry("click", sound_kind_t::sample)}},
                 history.document));
    REQUIRE(result);
    const auto& chart = history.document.charts.front();
    CHECK(history.document.resources.size() == resources + 3);
    CHECK(chart.sound_channels.size() == channels + 2);
    CHECK(chart.bgm_source_lanes.size() == lanes + 2);
    CHECK(bgm_marker_count(chart) == markers + 2);
    CHECK(validate_document(history.document).empty());

    const auto& drums = history.document.resources[resources];
    CHECK(drums.name == "drums");
    CHECK(drums.sound_kind == sound_kind_t::stem);
    CHECK(std::ranges::find(drums.roles, resource_role_t::bgm) != drums.roles.end());
    CHECK(history.document.resources.back().sound_kind == sound_kind_t::sample);

    const auto& stem_channel = chart.sound_channels[channels];
    REQUIRE(stem_channel.events.size() == 1);
    CHECK_FALSE(stem_channel.events.front().continuation);
    CHECK_FALSE(stem_channel.events.front().playable);
    const auto& stem_marker = chart.measures.front().bgm_events.back();
    CHECK(stem_marker.sound_channel_event == chart.sound_channels.back().events.front().id);

    const auto written = write_bmson(history.document);
    std::string messages;
    for (const auto& d : written.diagnostics) messages += d.message + "; ";
    INFO(messages);
    REQUIRE(written.json);
    const auto reparsed = parse_bmson_bytes(std::as_bytes(
        std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song);
    CHECK(reparsed.song->charts.front().sound_channels.size() == channels + 2);

    REQUIRE(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.resources.size() == resources);
    CHECK(history.document.charts.front().sound_channels.size() == channels);
    CHECK(history.document.revision == revision);
}

TEST_CASE("bulk import keeps list order and tags, and skips channels without a BMSON chart",
          "[unit][core][sounds][import]") {
    auto history = make_document_history(bmson_song());
    const auto resources = history.document.resources.size();
    const auto channels = history.document.charts.front().sound_channels.size();
    auto first = entry("a", sound_kind_t::stem);
    first.tags = {"loop", "Loop", "drums"};
    const auto result = apply_bulk_sound_import(
        history, history.document.revision,
        reserved({.entries = {first, entry("b", sound_kind_t::keysound)}}, history.document));
    REQUIRE(result);
    CHECK(history.document.resources[resources].name == "a");
    CHECK(history.document.resources[resources].tags == std::vector<std::string>{"loop", "drums"});
    CHECK(history.document.resources[resources + 1].name == "b");
    CHECK(history.document.charts.front().sound_channels.size() == channels);
}

TEST_CASE("BMS bulk import allocates one contiguous run in entry order",
          "[unit][core][sounds][import][bms-identifiers]") {
    auto history = make_document_history(bmson_song());
    history.document.resources.front().target_identities = {
        {format_family_t::bms, "01", "WAV"},
        {format_family_t::bms, "03", "WAV"},
    };
    auto first = entry("snd2", sound_kind_t::keysound);
    auto second = entry("snd10", sound_kind_t::keysound);
    first.asset.allocate_bms_identifier = true;
    second.asset.allocate_bms_identifier = true;
    const auto action = reserved({.entries = {first, second}}, history.document);
    REQUIRE(apply_bulk_sound_import(history, history.document.revision, action));
    const auto start = history.document.resources.size() - 2;
    CHECK(history.document.resources[start].target_identities.back().identifier == "04");
    CHECK(history.document.resources[start + 1].target_identities.back().identifier == "05");
    CHECK(history.undo.size() == 1);
}

TEST_CASE("BMS bulk import rejects beyond namespace capacity before changing the song",
          "[unit][core][sounds][import][bms-identifiers]") {
    auto history = make_document_history(bmson_song());
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    auto& occupied = history.document.resources.front().target_identities;
    for (std::size_t value = 1; value < 36 * 36; ++value)
        occupied.push_back({format_family_t::bms,
            std::string{digits[value / 36], digits[value % 36]}, "WAV"});
    const auto before_resources = history.document.resources.size();
    const auto before_revision = history.document.revision;
    auto item = entry("over-capacity", sound_kind_t::keysound);
    item.asset.allocate_bms_identifier = true;
    const auto result = apply_bulk_sound_import(
        history, before_revision, reserved({.entries = {item}}, history.document));
    CHECK_FALSE(result);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("0 IDs left") != std::string::npos);
    CHECK(history.document.resources.size() == before_resources);
    CHECK(history.document.revision == before_revision);
    CHECK(history.undo.empty());
}

TEST_CASE("fragmented BMS capacity splits an import across the largest gaps in order",
          "[unit][core][sounds][import][bms-identifiers]") {
    auto history = make_document_history(bmson_song());
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    auto& identities = history.document.resources.front().target_identities;
    for (std::size_t value = 1; value < 36 * 36; ++value) {
        if (value == 1 || (value >= 3 && value <= 5) || (value >= 8 && value <= 9)) continue;
        identities.push_back({format_family_t::bms,
            std::string{digits[value / 36], digits[value % 36]}, "WAV"});
    }
    std::vector<bulk_sound_import_entry_t> entries;
    for (const auto name : {"snd2", "snd3", "snd4", "snd5", "snd10"}) {
        auto item = entry(name, sound_kind_t::keysound);
        item.asset.allocate_bms_identifier = true;
        entries.push_back(std::move(item));
    }
    const auto result = apply_bulk_sound_import(
        history, history.document.revision,
        reserved({.entries = std::move(entries)}, history.document));
    REQUIRE(result);
    const auto start = history.document.resources.size() - 5;
    const std::vector<std::string> expected{"03", "04", "05", "08", "09"};
    for (std::size_t index = 0; index < expected.size(); ++index)
        CHECK(history.document.resources[start + index].target_identities.back().identifier == expected[index]);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("grouped bulk import creates and joins groups with independent contiguous ID runs",
          "[unit][core][sounds][import][sound-groups]") {
    auto song = bmson_song();
    const auto existing_group = sound_group_id_t{55};
    song.sound_groups.push_back({.id = existing_group, .name = "Loops"});
    song.resources.front().sound_group = existing_group;
    auto history = make_document_history(song);
    const auto before = history.document;
    auto first = entry("kick_01", sound_kind_t::keysound);
    auto second = entry("kick_02", sound_kind_t::keysound);
    auto joining = entry("loops_03", sound_kind_t::keysound);
    first.asset.allocate_bms_identifier = true;
    second.asset.allocate_bms_identifier = true;
    joining.asset.allocate_bms_identifier = true;
    auto action = bulk_sound_import_action_t{
        .entries = {first, second, joining},
        .groups = {{.name = "Kicks", .entry_indexes = {0, 1}},
                   {.name = "Loops", .existing_group = existing_group,
                    .entry_indexes = {2}}}};

    REQUIRE(apply_bulk_sound_import(history, history.document.revision,
                                    reserved(std::move(action), history.document)));
    const auto start = before.resources.size();
    REQUIRE(history.document.sound_groups.size() == 2);
    CHECK(history.document.sound_groups.back().name == "Kicks");
    CHECK(history.document.resources[start].sound_group == history.document.sound_groups.back().id);
    CHECK(history.document.resources[start + 1].sound_group == history.document.sound_groups.back().id);
    CHECK(history.document.resources[start + 2].sound_group == existing_group);
    CHECK(history.document.resources[start].target_identities.back().identifier == "01");
    CHECK(history.document.resources[start + 1].target_identities.back().identifier == "02");
    CHECK(history.document.resources[start + 2].target_identities.back().identifier == "03");
    CHECK(history.document.resources.front().sound_group == existing_group);
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.sound_groups == before.sound_groups);
    CHECK(same_resource_group_links(history.document.resources, before.resources));
}

TEST_CASE("grouped import rejects name collisions and fragmented group ID runs atomically",
          "[unit][core][sounds][import][sound-groups]") {
    auto history = make_document_history(bmson_song());
    auto first = entry("a", sound_kind_t::keysound);
    auto second = entry("b", sound_kind_t::keysound);
    first.asset.allocate_bms_identifier = true;
    second.asset.allocate_bms_identifier = true;
    auto action = bulk_sound_import_action_t{
        .entries = {first, second},
        .groups = {{.name = "Same", .entry_indexes = {0}},
                   {.name = "same", .entry_indexes = {1}}}};
    const auto before = history.document;
    auto result = apply_bulk_sound_import(history, before.revision,
                                          reserved(action, history.document));
    CHECK_FALSE(result);
    CHECK(same_resource_group_links(history.document.resources, before.resources));
    CHECK(history.document.sound_groups == before.sound_groups);
    CHECK(history.undo.empty());

    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    auto& identities = history.document.resources.front().target_identities;
    for (std::size_t value = 1; value < 36 * 36; ++value) {
        if (value == 1 || value == 3) continue;
        identities.push_back({format_family_t::bms,
            std::string{digits[value / 36], digits[value % 36]}, "WAV"});
    }
    const auto fragmented_before = history.document;
    action.groups = {{.name = "Two", .entry_indexes = {0, 1}}};
    result = apply_bulk_sound_import(history, fragmented_before.revision,
                                     reserved(action, history.document));
    CHECK_FALSE(result);
    CHECK_FALSE(result.diagnostics.empty());
    CHECK(same_resource_group_links(history.document.resources, fragmented_before.resources));
    CHECK(history.document.sound_groups == fragmented_before.sound_groups);
    CHECK(history.undo.empty());
}

TEST_CASE("group ID runs pack larger groups first while preserving entry order",
          "[unit][core][sounds][import][sound-groups][bms-identifiers]") {
    auto history = make_document_history(bmson_song());
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    auto& identities = history.document.resources.front().target_identities;
    identities.clear();
    for (std::size_t value = 1; value < 36 * 36; ++value) {
        if (value == 1 || value == 2 || (value >= 4 && value <= 7)) continue;
        identities.push_back({format_family_t::bms,
            std::string{digits[value / 36], digits[value % 36]}, "WAV"});
    }
    auto entries = std::vector<bulk_sound_import_entry_t>{};
    for (const auto name : {"small1", "small2", "large1", "large2", "large3", "large4"}) {
        auto item = entry(name, sound_kind_t::keysound);
        item.asset.allocate_bms_identifier = true;
        entries.push_back(std::move(item));
    }
    auto action = bulk_sound_import_action_t{
        .entries = std::move(entries),
        .groups = {{.name = "Small", .entry_indexes = {0, 1}},
                   {.name = "Large", .entry_indexes = {2, 3, 4, 5}}}};
    const auto before = history.document;

    REQUIRE(apply_bulk_sound_import(history, before.revision,
                                    reserved(std::move(action), before)));
    const auto start = before.resources.size();
    CHECK(history.document.resources[start].target_identities.back().identifier == "01");
    CHECK(history.document.resources[start + 1].target_identities.back().identifier == "02");
    CHECK(history.document.resources[start + 2].target_identities.back().identifier == "04");
    CHECK(history.document.resources[start + 3].target_identities.back().identifier == "05");
    CHECK(history.document.resources[start + 4].target_identities.back().identifier == "06");
    CHECK(history.document.resources[start + 5].target_identities.back().identifier == "07");
    CHECK(history.undo.size() == 1);
}

TEST_CASE("bulk import handles fifty files in one step", "[unit][core][sounds][import]") {
    auto history = make_document_history(bmson_song());
    const auto chart_id = history.document.charts.front().id;
    const auto resources = history.document.resources.size();
    bulk_sound_import_action_t action{.chart = chart_id};
    for (int index = 0; index < 50; ++index)
        action.entries.push_back(entry("stem" + std::to_string(index), sound_kind_t::stem));
    REQUIRE(apply_bulk_sound_import(history, history.document.revision,
                                    reserved(action, history.document)));
    CHECK(history.document.resources.size() == resources + 50);
    CHECK(validate_document(history.document).empty());
    CHECK(history.undo.size() == 1);
}

TEST_CASE("an invalid entry rejects the whole bulk import", "[unit][core][sounds][import]") {
    auto history = make_document_history(bmson_song());
    const auto before = history.document;
    const auto rejects = [&](bulk_sound_import_action_t action, std::uint64_t revision) {
        const auto result = apply_bulk_sound_import(history, revision, action);
        CHECK_FALSE(result);
        CHECK_FALSE(result.diagnostics.empty());
        CHECK(history.document.revision == before.revision);
        CHECK(history.document.resources.size() == before.resources.size());
        CHECK(history.undo.empty());
    };
    const auto revision = history.document.revision;
    rejects({}, revision);
    rejects({.entries = {entry("a", sound_kind_t::stem)}}, revision + 1);
    rejects({.entries = {entry("a", sound_kind_t::stem), entry("a", sound_kind_t::stem)}}, revision);
    auto relative = entry("bad", sound_kind_t::stem);
    relative.asset.source_path = "relative/bad.ogg";
    rejects({.entries = {entry("ok", sound_kind_t::stem), relative}}, revision);
    auto bad_tag = entry("tagged", sound_kind_t::stem);
    bad_tag.tags = {"bad\ntag"};
    rejects({.entries = {bad_tag}}, revision);
    rejects({.chart = chart_id_t{999'999}, .entries = {entry("a", sound_kind_t::stem)}}, revision);
    bulk_sound_import_action_t too_many;
    for (std::size_t index = 0; index <= maximum_bulk_sound_import; ++index)
        too_many.entries.push_back(entry("n" + std::to_string(index), sound_kind_t::sample));
    rejects(too_many, revision);
}

TEST_CASE("group import takes the first fitting BMS ID gap",
          "[unit][core][sounds][import][sound-groups][bms-identifiers]") {
    auto history = make_document_history(bmson_song());
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    auto& identities = history.document.resources.front().target_identities;
    identities.clear();
    for (std::size_t value = 1; value < 36 * 36; ++value) {
        if ((value >= 1 && value <= 4) || value == 6 || value == 7) continue;
        identities.push_back({format_family_t::bms,
            std::string{digits[value / 36], digits[value % 36]}, "WAV"});
    }
    auto first = entry("first", sound_kind_t::keysound);
    auto second = entry("second", sound_kind_t::keysound);
    first.asset.allocate_bms_identifier = true;
    second.asset.allocate_bms_identifier = true;
    auto action = bulk_sound_import_action_t{
        .entries = {first, second},
        .groups = {{.name = "Drums", .entry_indexes = {0, 1}}}};
    const auto start = history.document.resources.size();
    REQUIRE(apply_bulk_sound_import(history, history.document.revision,
                                   reserved(std::move(action), history.document)));
    CHECK(history.document.resources[start].target_identities.back().identifier == "01");
    CHECK(history.document.resources[start + 1].target_identities.back().identifier == "02");
}
