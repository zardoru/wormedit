#include "sys/sound_list_view.h"
#include "sys/sound_picker_view.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

void require_sound_projection(bool value) { REQUIRE(value); }
void check_sound_projection(bool value) { CHECK(value); }

auto catalog() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.audio_assets = {
        {.id = asset_id_t{1}, .source_path = "assets/1-drums.ogg",
         .stream = {.sample_rate = 48'000, .channels = 2, .frame_count = 480'000, .seekable = true}},
        {.id = asset_id_t{2}, .source_path = "assets/2-kick.wav",
         .stream = {.sample_rate = 44'100, .channels = 1, .frame_count = 4'410, .seekable = true}},
        {.id = asset_id_t{3}, .source_path = "assets/3-song.mp3",
         .stream = {.sample_rate = 44'100, .channels = 2, .frame_count = std::nullopt}}};
    song.visual_assets = {{.id = asset_id_t{9}, .source_path = "bg.png"}};
    song.resources = {
        {.id = resource_id_t{1}, .name = "Drums", .asset = asset_id_t{1},
         .roles = {resource_role_t::sample, resource_role_t::bgm},
         .sound_kind = sound_kind_t::stem, .tags = {"Loop", "drums"}},
        {.id = resource_id_t{2}, .name = "Kick", .asset = asset_id_t{2},
         .roles = {resource_role_t::sample}, .tags = {"drums"}},
        {.id = resource_id_t{3}, .name = "Song", .asset = asset_id_t{3},
         .roles = {resource_role_t::music, resource_role_t::bgm}, .missing = true},
        {.id = resource_id_t{4}, .name = "Background", .asset = asset_id_t{9},
         .roles = {resource_role_t::bga}}};
    chart_document_t chart;
    chart.id = chart_id_t{2};
    measure_chunk_t measure;
    measure.id = measure_id_t{3};
    measure.notes = {{.id = object_id_t{10}, .sound = resource_id_t{2}},
                     {.id = object_id_t{11}, .sound = resource_id_t{2}}};
    measure.bgm_events = {{.id = object_id_t{12}, .sound = resource_id_t{1}}};
    chart.measures = {measure};
    song.charts = {chart};
    return song;
}

auto large_projection_catalog() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{100};
    song.revision = 1;
    song.sound_groups = {{.id = sound_group_id_t{1}, .name = "Drums"},
                         {.id = sound_group_id_t{2}, .name = "Keys"},
                         {.id = sound_group_id_t{3}, .name = "Other"}};
    chart_document_t chart;
    chart.id = chart_id_t{200};
    chart.measures.push_back({.id = measure_id_t{300}});
    chart.semantic_layers = {{.id = layer_id_t{1}, .name = "Red"},
                             {.id = layer_id_t{2}, .name = "Blue"}};
    for (std::uint64_t index = 1; index <= 600; ++index) {
        song.audio_assets.push_back({.id = asset_id_t{index},
            .source_path = "audio/sound" + std::to_string(index) + ".ogg",
            .stream = {.sample_rate = 48'000, .channels = 2,
                       .frame_count = 96'000 + index, .seekable = true}});
        song.resources.push_back({.id = resource_id_t{index},
            .name = "sound" + std::to_string(index), .asset = asset_id_t{index},
            .roles = {resource_role_t::sample},
            .sound_kind = index % 10 == 0 ? sound_kind_t::stem : sound_kind_t::keysound,
            .tags = {index % 2 == 0 ? "Even" : "Odd"},
            .sound_group = sound_group_id_t{index % 3 + 1}});
        chart.measures.front().notes.push_back({.id = object_id_t{10'000 + index},
            .position = {measure_id_t{300}, index}, .sound = resource_id_t{index},
            .semantic_layers = index % 2 == 0 ? std::vector{layer_id_t{1}}
                                               : std::vector{layer_id_t{2}}});
        if (index % 2 == 0)
            chart.sound_layer_defaults.push_back({.resource = resource_id_t{index},
                .layers = {index % 4 == 0 ? layer_id_t{1} : layer_id_t{2}}});
    }
    for (std::uint64_t index = 1; index <= 70; ++index)
        chart.bpms.push_back({.id = object_id_t{20'000 + index},
            .position = {measure_id_t{300}, index}, .beats_per_minute = {120 + static_cast<std::int64_t>(index), 1}});
    song.charts.push_back(std::move(chart));
    return song;
}

auto full_keysound_request(std::span<const std::uint64_t> selected,
                           std::span<const std::uint64_t> groups,
                           std::span<const std::uint64_t> collapsed,
                           std::string_view search = "sound") -> keysound_list_request_t {
    return {.search = search, .selected_resource_ids = selected,
        .include_selected_metadata = true, .group_ids = groups,
        .group_filter_active = true, .include_ungrouped = false,
        .layer_filter = keysound_layer_filter_kind_t::any, .layer_id = 2,
        .collapsed_group_ids = collapsed, .ungrouped_collapsed = false,
        .first_row = 3, .row_limit = 17, .primary_resource_id = 55,
        .filter_layer_first = 0, .filter_layer_limit = 64,
        .sort = dock_sort_t::natural_name, .ids_available = true};
}

auto cached_legacy_matches_reference() -> bool {
    auto song = large_projection_catalog();
    const auto& chart = song.charts.front();
    keysound_list_cache_t cache;
    const auto& cached = keysound_list_view_t::from(cache, song, &chart, 32,
                                                       dock_sort_t::bms_id, false);
    const auto reference = keysound_list_view_t::from(song, &chart, 32, dock_sort_t::bms_id, false);
    if (!(cached == reference) || cache.projection_build_count() != 1 ||
        cache.asset_index_build_count() != 1) return false;
    const auto& hit = keysound_list_view_t::from(cache, song, &chart, 32,
                                                    dock_sort_t::bms_id, false);
    if (&cached != &hit || cache.projection_build_count() != 1 ||
        cache.asset_index_build_count() != 1) return false;
    song.audio_assets.push_back({.id = asset_id_t{900}, .source_path = "audio/new.wav"});
    song.resources.push_back({.id = resource_id_t{900}, .name = "New",
        .asset = asset_id_t{900}, .roles = {resource_role_t::sample}});
    ++song.revision;
    const auto& refreshed = keysound_list_view_t::from(cache, song, &chart, 32,
                                                          dock_sort_t::bms_id, false);
    return refreshed == keysound_list_view_t::from(song, &chart, 32, dock_sort_t::bms_id, false) &&
        cache.projection_build_count() == 2 && cache.asset_index_build_count() == 2;
}

auto cached_request_owns_inputs_and_matches_reference() -> bool {
    auto song = large_projection_catalog();
    const auto& chart = song.charts.front();
    keysound_list_cache_t cache;
    keysound_list_view_t reference;
    const keysound_list_view_t* first{};
    {
        const std::array selected{std::uint64_t{7}, std::uint64_t{19}, std::uint64_t{599}};
        const std::array groups{std::uint64_t{1}, std::uint64_t{2}};
        const std::array collapsed{std::uint64_t{2}};
        const std::string search{"sound"};
        const auto request = full_keysound_request(selected, groups, collapsed, search);
        first = &keysound_list_view_t::from(cache, song, &chart, request);
        reference = keysound_list_view_t::from(song, &chart, request);
    }
    if (!first || !(*first == reference) || cache.projection_build_count() != 1 ||
        cache.asset_index_build_count() != 1) return false;
    const std::array selected{std::uint64_t{7}, std::uint64_t{19}, std::uint64_t{599}};
    const std::array groups{std::uint64_t{1}, std::uint64_t{2}};
    const std::array collapsed{std::uint64_t{2}};
    const std::string search{"sound"};
    const auto repeated = full_keysound_request(selected, groups, collapsed, search);
    const auto& hit = keysound_list_view_t::from(cache, song, &chart, repeated);
    if (&hit != first || !(hit == reference) || cache.projection_build_count() != 1) return false;
    auto changed = repeated;
    changed.primary_resource_id = 19;
    const auto& rebuilt = keysound_list_view_t::from(cache, song, &chart, changed);
    if (!(rebuilt == keysound_list_view_t::from(song, &chart, changed)) ||
        cache.projection_build_count() != 2 || cache.asset_index_build_count() != 1) return false;
    const auto& legacy = keysound_list_view_t::from(cache, song, &chart, 24);
    return legacy == keysound_list_view_t::from(song, &chart, 24) &&
        cache.projection_build_count() == 3 && cache.asset_index_build_count() == 1;
}

auto cache_keys_include_project_chart_and_revision() -> bool {
    auto song = large_projection_catalog();
    auto& chart = song.charts.front();
    keysound_list_cache_t cache;
    const std::array groups{std::uint64_t{1}, std::uint64_t{2}, std::uint64_t{3}};
    const std::array<std::uint64_t, 0> none{};
    const auto request = full_keysound_request(none, groups, none);
    const auto first = keysound_list_view_t::from(cache, song, &chart, request);

    auto alternate_chart = chart;
    alternate_chart.semantic_layers.push_back({.id = layer_id_t{3}, .name = "Green"});
    const auto alternate = keysound_list_view_t::from(cache, song, &alternate_chart, request);
    if (alternate.layers.size() != first.layers.size() + 1 ||
        cache.asset_index_build_count() != 1) return false;

    auto copy = song;
    std::erase_if(copy.resources, [](const auto& resource) { return resource.id == resource_id_t{599}; });
    auto& copied_chart = copy.charts.front();
    const auto copied = keysound_list_view_t::from(cache, copy, &copied_chart, request);
    if (copied.total == first.total || cache.asset_index_build_count() != 2) return false;

    const auto back_to_song = keysound_list_view_t::from(cache, song, &chart, request);
    if (back_to_song != first || cache.asset_index_build_count() != 3) return false;

    song.audio_assets.push_back({.id = asset_id_t{900}, .source_path = "audio/new.ogg"});
    song.resources.push_back({.id = resource_id_t{900}, .name = "sound900",
        .asset = asset_id_t{900}, .roles = {resource_role_t::sample},
        .sound_group = sound_group_id_t{3}});
    ++song.revision;
    const auto after_revision = keysound_list_view_t::from(cache, song, &chart, request);
    return after_revision.total == first.total + 1 && cache.asset_index_build_count() == 4 &&
        after_revision == keysound_list_view_t::from(song, &chart, request);
}

auto every_full_request_field_invalidates() -> bool {
    using change_t = std::function<void(keysound_list_request_t&)>;
    const std::array selected{std::uint64_t{7}, std::uint64_t{19}};
    const std::array groups{std::uint64_t{1}, std::uint64_t{2}};
    const std::array collapsed{std::uint64_t{2}};
    const std::array changed_ids{std::uint64_t{8}};
    const std::array changed_groups{std::uint64_t{3}};
    const std::array changed_collapsed{std::uint64_t{1}};
    const std::array<change_t, 17> changes{
        [](auto& request) { request.search = "sound1"; },
        [&](auto& request) { request.selected_resource_ids = changed_ids; },
        [](auto& request) { request.include_selected_metadata = false; },
        [&](auto& request) { request.group_ids = changed_groups; },
        [](auto& request) { request.group_filter_active = false; },
        [](auto& request) { request.include_ungrouped = true; },
        [](auto& request) { request.layer_filter = keysound_layer_filter_kind_t::no_layer; },
        [](auto& request) { request.layer_id = 1; },
        [&](auto& request) { request.collapsed_group_ids = changed_collapsed; },
        [](auto& request) { request.ungrouped_collapsed = true; },
        [](auto& request) { request.first_row = 4; },
        [](auto& request) { request.row_limit = 18; },
        [](auto& request) { request.primary_resource_id = 19; },
        [](auto& request) { request.filter_layer_first = 1; },
        [](auto& request) { request.filter_layer_limit = 63; },
        [](auto& request) { request.sort = dock_sort_t::bms_id; },
        [](auto& request) { request.ids_available = false; }};
    for (const auto& change : changes) {
        auto song = large_projection_catalog();
        const auto& chart = song.charts.front();
        keysound_list_cache_t cache;
        const auto base = full_keysound_request(selected, groups, collapsed);
        static_cast<void>(keysound_list_view_t::from(cache, song, &chart, base));
        auto changed = base;
        change(changed);
        const auto& rebuilt = keysound_list_view_t::from(cache, song, &chart, changed);
        if (cache.projection_build_count() != 2 || cache.asset_index_build_count() != 1 ||
            !(rebuilt == keysound_list_view_t::from(song, &chart, changed))) return false;
    }
    return true;
}

auto legacy_key_fields_invalidate() -> bool {
    auto song = large_projection_catalog();
    const auto& chart = song.charts.front();
    keysound_list_cache_t cache;
    static_cast<void>(keysound_list_view_t::from(cache, song, &chart, 32,
                                                    dock_sort_t::natural_name, true));
    const auto& row_change = keysound_list_view_t::from(cache, song, &chart, 31,
                                                           dock_sort_t::natural_name, true);
    if (!(row_change == keysound_list_view_t::from(song, &chart, 31,
                                               dock_sort_t::natural_name, true))) return false;
    const auto& sort_change = keysound_list_view_t::from(cache, song, &chart, 31,
                                                            dock_sort_t::bms_id, true);
    if (!(sort_change == keysound_list_view_t::from(song, &chart, 31, dock_sort_t::bms_id, true)))
        return false;
    const auto& id_change = keysound_list_view_t::from(cache, song, &chart, 31,
                                                          dock_sort_t::bms_id, false);
    return id_change == keysound_list_view_t::from(song, &chart, 31, dock_sort_t::bms_id, false) &&
        cache.projection_build_count() == 4 && cache.asset_index_build_count() == 1;
}

} // namespace

TEST_CASE("cached legacy keysound projection matches reference and invalidates by revision",
          "[unit][sys][sounds][cache]") {
    check_sound_projection(cached_legacy_matches_reference());
}

TEST_CASE("cached full keysound projection owns request inputs and shares asset membership",
          "[unit][sys][sounds][cache]") {
    check_sound_projection(cached_request_owns_inputs_and_matches_reference());
}

TEST_CASE("keysound cache keys include project identity, chart identity and revision",
          "[unit][sys][sounds][cache]") {
    check_sound_projection(cache_keys_include_project_chart_and_revision());
}

TEST_CASE("every full keysound request field invalidates its cached projection",
          "[unit][sys][sounds][cache]") {
    check_sound_projection(every_full_request_field_invalidates());
}

TEST_CASE("legacy keysound cache includes each scalar request field",
          "[unit][sys][sounds][cache]") {
    check_sound_projection(legacy_key_fields_invalidate());
}

TEST_CASE("sound list shows audio resources only, with counts and inferred kinds",
          "[unit][sys][sounds]") {
    const auto list = sound_list_view_t::from(catalog(), {});
    REQUIRE(list.rows.size() == 3);
    CHECK(list.total_matching == 3);
    CHECK(list.rows[0].name == "Drums");
    CHECK(list.rows[0].kind == sound_kind_t::stem);
    CHECK(list.rows[0].use_count == 1);
    CHECK(list.rows[0].tags_text == "Loop, drums");
    CHECK(list.rows[0].duration_frames == 480'000);
    CHECK(list.rows[1].kind == sound_kind_t::keysound);
    CHECK(list.rows[1].use_count == 2);
    CHECK(list.rows[2].kind == sound_kind_t::stem);
    CHECK(list.rows[2].missing);
    CHECK(list.rows[2].duration_frames == 0);
}

TEST_CASE("sound list filters by name, tag and exact tag ignoring case", "[unit][sys][sounds]") {
    auto list = sound_list_view_t::from(catalog(), {.search = "KI"});
    REQUIRE(list.rows.size() == 1);
    CHECK(list.rows[0].name == "Kick");
    list = sound_list_view_t::from(catalog(), {.search = "loop"});
    REQUIRE(list.rows.size() == 1);
    CHECK(list.rows[0].name == "Drums");
    list = sound_list_view_t::from(catalog(), {.tag = "DRUMS"});
    CHECK(list.total_matching == 2);
    list = sound_list_view_t::from(catalog(), {.tag = "drum"});
    CHECK(list.total_matching == 0);
    CHECK(list.all_tags == std::vector<std::string>{"drums", "Loop"});
}

TEST_CASE("sound list windows its rows and clamps the start", "[unit][sys][sounds]") {
    auto song = catalog();
    for (std::uint64_t id = 10; id < 40; ++id) {
        song.audio_assets.push_back({.id = asset_id_t{id}, .source_path = "x.wav"});
        song.resources.push_back({.id = resource_id_t{id}, .name = "s" + std::to_string(id),
                                  .asset = asset_id_t{id}, .roles = {resource_role_t::sample}});
    }
    const auto list = sound_list_view_t::from(song, {.first_row = 30, .row_limit = 5});
    CHECK(list.total_matching == 33);
    REQUIRE(list.rows.size() == 3);
    CHECK(list.rows.front().name == "s37");
    const auto past = sound_list_view_t::from(song, {.first_row = 500, .row_limit = 5});
    CHECK(past.rows.empty());
    CHECK(past.first_row == 33);
}

TEST_CASE("sound properties describe one audio resource", "[unit][sys][sounds]") {
    const auto properties = sound_properties_view_t::from(catalog(), resource_id_t{1});
    REQUIRE(properties.valid);
    CHECK(properties.source_path == "assets/1-drums.ogg");
    CHECK(properties.channels == 2);
    CHECK(properties.sample_rate == 48'000);
    CHECK(properties.tags == std::vector<std::string>{"Loop", "drums"});
    CHECK(properties.use_count == 1);
    CHECK_FALSE(sound_properties_view_t::from(catalog(), resource_id_t{4}).valid);
    CHECK_FALSE(sound_properties_view_t::from(catalog(), resource_id_t{99}).valid);
    CHECK_FALSE(sound_properties_view_t::from(catalog(), resource_id_t{}).valid);
}

TEST_CASE("keysound list holds placeable samples in name order with their layers",
          "[unit][sys][sounds]") {
    auto song = catalog();
    song.resources[1].target_identities = {
        {.family = format_family_t::bms, .identifier = "0A", .namespace_name = "WAV"}};
    auto& chart = song.charts.front();
    chart.semantic_layers = {{.id = layer_id_t{7}, .name = "Vocal", .color = {1, 2, 3, 255}},
                             {.id = layer_id_t{8}, .name = "Drums", .color = {4, 5, 6, 255}}};
    chart.sound_layer_defaults = {{.resource = resource_id_t{2},
                                   .layers = {layer_id_t{8}, layer_id_t{99}, layer_id_t{7}}}};

    const auto list = keysound_list_view_t::from(song, &chart, 100);
    // The stem (1), the music/BGM file (3) and the BGA image (4) are not keysounds.
    REQUIRE(list.rows.size() == 1);
    CHECK(list.total == 1);
    CHECK_FALSE(list.truncated);
    REQUIRE(list.layers.size() == 2);
    CHECK(list.layers[1].name == "Drums");
    CHECK(list.layers[1].color == color_rgba_t{4, 5, 6, 255});
    const auto& row = list.rows.front();
    CHECK(row.resource == resource_id_t{2});
    CHECK(row.slot == "0A");
    CHECK(row.name == "Kick");
    CHECK_FALSE(row.missing);
    // The dangling layer 99 is ignored; both real layers are set.
    CHECK(row.layer_mask == 0b11);

    const auto without_chart = keysound_list_view_t::from(song, nullptr, 100);
    REQUIRE(without_chart.rows.size() == 1);
    CHECK(without_chart.rows.front().layer_mask == 0);
    CHECK(without_chart.layers.empty());
}

TEST_CASE("keysound dock natural and ID sorting happen before the row limit", "[sys][sounds][dock]") {
    song_document_t song;
    const std::array names{"snd10.wav", "snd2.wav", "snd1.wav"};
    const std::array slots{"0Z", "0a", "10"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto id = static_cast<std::uint64_t>(index + 1);
        song.audio_assets.push_back({.id = asset_id_t{id}, .source_path = names[index]});
        song.resources.push_back({.id = resource_id_t{id}, .name = names[index],
            .asset = asset_id_t{id}, .roles = {resource_role_t::sample},
            .target_identities = {{format_family_t::bms, slots[index], "WAV"}}});
    }
    const auto natural = keysound_list_view_t::from(song, nullptr, 2);
    require_sound_projection(natural.rows.size() == 2);
    check_sound_projection(natural.rows[0].name == "snd1.wav");
    check_sound_projection(natural.rows[1].name == "snd2.wav");
    const auto by_id = keysound_list_view_t::from(song, nullptr, 2, dock_sort_t::bms_id);
    require_sound_projection(by_id.rows.size() == 2);
    check_sound_projection(by_id.rows[0].slot == "0a");
    check_sound_projection(by_id.rows[1].slot == "0Z");
    check_sound_projection(by_id.bms_capacity.used == 3);
    check_sound_projection(by_id.bms_capacity.total == 1295);
    const auto without_ids = keysound_list_view_t::from(song, nullptr, 2, dock_sort_t::bms_id, false);
    require_sound_projection(without_ids.rows.size() == 2);
    check_sound_projection(without_ids.rows[0].name == "snd1.wav");
    check_sound_projection(without_ids.rows[1].name == "snd2.wav");
}

TEST_CASE("keysound list is bounded and reports the full count",
          "[unit][sys][sounds]") {
    song_document_t song;
    for (std::uint64_t index = 1; index <= 40; ++index) {
        song.audio_assets.push_back({.id = asset_id_t{index}, .source_path = "k.wav"});
        song.resources.push_back({.id = resource_id_t{index}, .asset = asset_id_t{index},
                                  .roles = {resource_role_t::sample}});
    }
    const auto list = keysound_list_view_t::from(song, nullptr, 16);
    CHECK(list.rows.size() == 16);
    CHECK(list.total == 40);
    CHECK(list.truncated);
    CHECK(list.rows.front().name == "Resource 1");
    CHECK(list.rows.front().slot.empty());
}

TEST_CASE("keysound section projection filters before windowing and advances across collapse",
          "[unit][sys][sounds][sound-groups]") {
    song_document_t song;
    song.sound_groups = {{.id = sound_group_id_t{10}, .name = "Piano"},
                         {.id = sound_group_id_t{20}, .name = "Percussion"}};
    chart_document_t chart;
    chart.semantic_layers = {{.id = layer_id_t{5}, .name = "Blue"}};
    const std::array names{"piano.wav", "kick.wav", "snare.wav", "loose.wav"};
    const std::array<std::uint64_t, 4> ids{1, 2, 3, 4};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        song.audio_assets.push_back({.id = asset_id_t{ids[index]}, .source_path = names[index]});
        song.resources.push_back({.id = resource_id_t{ids[index]}, .name = names[index],
            .asset = asset_id_t{ids[index]}, .roles = {resource_role_t::sample},
            .sound_group = index == 0 ? std::optional{sound_group_id_t{10}}
                : index < 3 ? std::optional{sound_group_id_t{20}}
                            : std::optional<sound_group_id_t>{}});
    }
    chart.sound_layer_defaults = {{.resource = resource_id_t{2}, .layers = {layer_id_t{5}}}};
    chart.id = chart_id_t{7};
    std::array<std::uint64_t, 1> collapsed_group_ids{10};
    const auto page = keysound_list_view_t::from(song, &chart, keysound_list_request_t{
        .search = "wav", .collapsed_group_ids = collapsed_group_ids,
        .first_row = 0, .row_limit = 3});
    check_sound_projection(page.display_total == 6);
    require_sound_projection(page.display_rows.size() == 3);
    check_sound_projection(page.display_rows[0].group_name == "Percussion");
    check_sound_projection(page.display_rows[1].resource == resource_id_t{2});
    check_sound_projection(page.display_rows[2].resource == resource_id_t{3});
    check_sound_projection(page.rows.size() == 2);
    check_sound_projection((page.listed_resources == std::vector<resource_id_t>{
        resource_id_t{2}, resource_id_t{3}, resource_id_t{4}}));
    check_sound_projection(page.advance_rows.size() == 4);
    check_sound_projection(page.display_first_row == 0);

    const auto filtered = keysound_list_view_t::from(song, &chart, keysound_list_request_t{
        .search = "KI", .group_ids = std::array<std::uint64_t, 1>{20},
        .group_filter_active = true, .include_ungrouped = false,
        .layer_filter = keysound_layer_filter_kind_t::specific, .layer_id = 5});
    require_sound_projection(filtered.rows.size() == 1);
    check_sound_projection(filtered.rows[0].resource == resource_id_t{2});
    check_sound_projection(filtered.advance_rows.size() == 1);
    const auto located = keysound_list_view_t::from(song, &chart, keysound_list_request_t{
        .row_limit = 1, .primary_resource_id = 4});
    check_sound_projection(located.primary_display_index == std::optional<std::size_t>{6});

    const std::array collapsed_groups{std::uint64_t{10}, std::uint64_t{20}};
    const auto collapsed = keysound_list_view_t::from(song, &chart, keysound_list_request_t{
        .collapsed_group_ids = collapsed_groups, .ungrouped_collapsed = true});
    check_sound_projection(collapsed.display_total == 3);
    check_sound_projection(collapsed.rows.empty());
    check_sound_projection(collapsed.listed_resources.empty());
    check_sound_projection(collapsed.advance_rows.size() == 4);
    const auto no_matches = keysound_list_view_t::from(song, &chart,
        keysound_list_request_t{.search = "not present"});
    check_sound_projection(no_matches.display_total == 0);
    check_sound_projection(no_matches.display_rows.empty());
    check_sound_projection(no_matches.listed_resources.empty());
    check_sound_projection(no_matches.advance_rows.empty());
}

TEST_CASE("keysound projection keeps row payload bounded for large selections and catalogs",
          "[unit][sys][sounds][sound-groups]") {
    song_document_t song;
    std::vector<std::uint64_t> selected;
    for (std::uint64_t id = 1; id <= 1200; ++id) {
        song.audio_assets.push_back({.id = asset_id_t{id}, .source_path = "sound.wav"});
        song.resources.push_back({.id = resource_id_t{id}, .name = "sound" + std::to_string(id),
            .asset = asset_id_t{id}, .roles = {resource_role_t::sample}});
        if (id > 1000) selected.push_back(id);
    }
    const auto list = keysound_list_view_t::from(song, nullptr, keysound_list_request_t{
        .selected_resource_ids = selected, .first_row = 0, .row_limit = 12});
    check_sound_projection(list.total == 1200);
    check_sound_projection(list.rows.size() == 11);
    check_sound_projection(list.display_rows.size() == 12);
    check_sound_projection(list.selection_rows.size() == 200);
    check_sound_projection(list.selected_rows.empty());
    check_sound_projection(list.listed_resources.size() == 1200);
    check_sound_projection(list.advance_rows.size() == 1200);
}

TEST_CASE("keysound default layer filters use stable IDs beyond the visible mask width",
          "[unit][sys][sounds][sound-groups]") {
    song_document_t song;
    song.audio_assets = {{.id = asset_id_t{1}, .source_path = "sample.wav"}};
    song.resources = {{.id = resource_id_t{1}, .name = "sample", .asset = asset_id_t{1},
                       .roles = {resource_role_t::sample}}};
    chart_document_t chart;
    for (std::uint64_t id = 1; id <= 65; ++id)
        chart.semantic_layers.push_back({.id = layer_id_t{id}, .name = "Layer " +
            std::to_string(id)});
    chart.sound_layer_defaults = {{.resource = resource_id_t{1}, .layers = {layer_id_t{65}}}};
    const auto list = keysound_list_view_t::from(song, &chart, keysound_list_request_t{
        .layer_filter = keysound_layer_filter_kind_t::specific, .layer_id = 65,
        .filter_layer_first = 64, .filter_layer_limit = 64});
    require_sound_projection(list.rows.size() == 1);
    check_sound_projection(list.rows[0].resource == resource_id_t{1});
    check_sound_projection(list.rows[0].layer_mask == 0);
    check_sound_projection(list.filter_layer_total == 65);
    check_sound_projection(list.filter_layer_first == 64);
    check_sound_projection(list.filter_layers.size() == 1);
    check_sound_projection(list.filter_layers[0].id == layer_id_t{65});
    check_sound_projection(list.selected_filter_layer_valid);
    const auto stale = keysound_list_view_t::from(song, &chart, keysound_list_request_t{
        .layer_filter = keysound_layer_filter_kind_t::specific, .layer_id = 999});
    check_sound_projection(!stale.selected_filter_layer_valid);
}

TEST_CASE("sound layer conflicts distinguish matching layers from unlayered notes",
          "[unit][sys][sounds][sound-note-layers]") {
    auto song = catalog();
    auto& chart = song.charts.front();
    chart.semantic_layers = {{.id = layer_id_t{7}}, {.id = layer_id_t{8}}};
    CHECK(keysound_list_view_t::from(song, &chart, 100).rows.front().note_layer_conflict_mask == 0);
    chart.measures.front().notes.front().semantic_layers = {layer_id_t{7}};
    CHECK(keysound_list_view_t::from(song, &chart, 100).rows.front().note_layer_conflict_mask == 0b10);
    chart.sound_layer_defaults = {{.resource = resource_id_t{2}, .layers = {layer_id_t{7}}}};
    CHECK(keysound_list_view_t::from(song, &chart, 100).rows.front().note_layer_conflict_mask == 0b10);
    chart.measures.front().notes.front().semantic_layers.push_back(layer_id_t{8});
    CHECK(keysound_list_view_t::from(song, &chart, 100).rows.front().note_layer_conflict_mask == 0b01);
}

TEST_CASE("BMS slots project empty and occupied IDs in a bounded window",
          "[unit][sys][sounds][sound-slots]") {
    auto song = catalog();
    song.resources[1].target_identities = {{format_family_t::bms, "0a", "wav"}};
    song.resources[2].target_identities = {{format_family_t::bms, "ZZ", "WAV"}};
    auto list = sound_slots_view_t::from(song, {.first_row = 8, .row_limit = 3});
    require_sound_projection(list.rows.size() == 3);
    check_sound_projection(list.total_matching == 1295);
    check_sound_projection(list.first_row == 8);
    check_sound_projection(list.rows[0].identifier == "09");
    check_sound_projection(!list.rows[0].resource);
    check_sound_projection(list.rows[0].name == "(empty)");
    check_sound_projection(list.rows[1].identifier == "0A");
    check_sound_projection(list.rows[1].resource == resource_id_t{2});
    check_sound_projection(list.rows[1].use_count == 2);
    check_sound_projection(list.rows[1].placeable);
    check_sound_projection(!list.rows[1].missing);
    list = sound_slots_view_t::from(song, {.search = "zz", .selected_slot = 1295});
    check_sound_projection(list.selected_row == 0);
    require_sound_projection(list.rows.size() == 1);
    check_sound_projection(list.rows[0].number == 1295);
    check_sound_projection(list.rows[0].missing);
    check_sound_projection(!list.rows[0].placeable);
}

TEST_CASE("BMS slot window clamps limits and filters names without changing assignments",
          "[unit][sys][sounds][sound-slots]") {
    auto song = catalog();
    song.resources[1].target_identities = {{format_family_t::bms, "01", "WAV"}};
    const auto identity = song.resources[1].target_identities.front().identifier;
    auto list = sound_slots_view_t::from(song, {.row_limit = 2000});
    check_sound_projection(list.rows.size() == 128);
    check_sound_projection(list.rows.front().number == 1);
    list = sound_slots_view_t::from(song, {.search = "kIcK", .selected_slot = 1});
    check_sound_projection(list.selected_row == 0);
    require_sound_projection(list.rows.size() == 1);
    check_sound_projection(list.rows.front().identifier == "01");
    check_sound_projection(list.total_matching == 1);
    list = sound_slots_view_t::from(song, {.first_row = 1294, .row_limit = 64});
    require_sound_projection(list.rows.size() == 1);
    check_sound_projection(list.rows.front().identifier == "ZZ");
    list = sound_slots_view_t::from(song, {.first_row = SIZE_MAX, .row_limit = SIZE_MAX});
    check_sound_projection(list.rows.empty());
    check_sound_projection(list.first_row == 1295);
    check_sound_projection(song.resources[1].target_identities.front().identifier == identity);
}

TEST_CASE("BMSON automatic Sounds lists stem rows without making stems slot placeable",
          "[sys][compose][bmson][stem][sound-list]") {
    auto song = catalog();
    chart_document_t chart;
    chart.source_family = format_family_t::bmson;
    const auto legacy = keysound_list_view_t::from(song, &chart, 100);
    require_sound_projection(legacy.rows.size() == 3);
    keysound_list_request_t request;
    request.row_limit = 100;
    const auto automatic = keysound_list_view_t::from(song, &chart, request);
    check_sound_projection(automatic.total == 3);
    check_sound_projection(std::ranges::any_of(automatic.rows, [](const auto& row) {
        return row.resource == resource_id_t{1};
    }));
    song.resources.front().target_identities = {{format_family_t::bms, "WAV", "01"}};
    const auto slots = sound_slots_view_t::from(song, {.selected_slot = 1});
    require_sound_projection(!slots.rows.empty());
    check_sound_projection(!slots.rows.front().placeable);
}

TEST_CASE("Non BMSON Sounds list and missing asset stem eligibility remain unchanged",
          "[sys][compose][bmson][stem][sound-list]") {
    auto song = catalog();
    chart_document_t chart;
    chart.source_family = format_family_t::bms;
    check_sound_projection(keysound_list_view_t::from(song, &chart, 100).rows.size() == 1);
    chart.source_family = format_family_t::bmson;
    song.audio_assets.erase(song.audio_assets.begin());
    const auto list = keysound_list_view_t::from(song, &chart, 100);
    check_sound_projection(std::ranges::none_of(list.rows, [](const auto& row) {
        return row.resource == resource_id_t{1};
    }));
}

TEST_CASE("Go to sound fuzzy search covers the full catalog and caches idle frames", "[sound-picker][projection]") {
    auto song = large_projection_catalog();
    song.resources[song.resources.size() - 2].name = "Super Snare Roll";
    const auto wanted = song.resources[song.resources.size() - 2].id;
    sound_picker_cache_t cache;
    const auto& first = sound_picker_view_result_t::from(cache, song, song.charts.front(), "SSR");
    require_sound_projection(first.rows.size() == 1);
    check_sound_projection(first.rows.front().resource == wanted);
    check_sound_projection(cache.catalog_builds == 1);
    check_sound_projection(cache.query_builds == 1);
    (void)sound_picker_view_result_t::from(cache, song, song.charts.front(), "ssr");
    check_sound_projection(cache.catalog_builds == 1);
    check_sound_projection(cache.query_builds == 1);
    const auto& all = sound_picker_view_result_t::from(cache, song, song.charts.front(), "");
    check_sound_projection(all.total == 540);
    check_sound_projection(all.rows.size() == 20);
    check_sound_projection(cache.catalog_builds == 1);
    song.resources[song.resources.size() - 2].missing = true;
    ++song.revision;
    const auto& missing = sound_picker_view_result_t::from(cache, song, song.charts.front(), "ssr");
    require_sound_projection(missing.rows.size() == 1);
    check_sound_projection(missing.rows.front().missing);
    check_sound_projection(cache.catalog_builds == 2);
}

TEST_CASE("Go to sound ranks exact prefix substring and fuzzy matches deterministically", "[sound-picker][projection]") {
    auto song = catalog();
    song.resources.clear();
    for (const auto& name : {"Kinetic Click", "Big kick", "Kick tail", "Kick", "Kick"}) {
        const auto id = static_cast<std::uint64_t>(song.resources.size() + 10);
        song.resources.push_back({.id = resource_id_t{id}, .name = name,
            .asset = asset_id_t{2}, .roles = {resource_role_t::sample}});
    }
    sound_picker_cache_t cache;
    const auto& found = sound_picker_view_result_t::from(cache, song, song.charts.front(), "kick");
    require_sound_projection(found.rows.size() == 5);
    check_sound_projection(found.rows[0].resource == resource_id_t{13});
    check_sound_projection(found.rows[1].resource == resource_id_t{14});
    check_sound_projection(found.rows[2].name == "Kick tail");
    check_sound_projection(found.rows[3].name == "Big kick");
    check_sound_projection(found.rows[4].name == "Kinetic Click");
}

TEST_CASE("Go to sound follows dock eligibility and searches WAV IDs", "[sound-picker][projection]") {
    auto song = catalog();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bms;
    song.resources[1].target_identities.push_back({.family = format_family_t::bms,
        .identifier = "A7", .namespace_name = "WAV"});
    sound_picker_cache_t cache;
    const auto& slot = sound_picker_view_result_t::from(cache, song, chart, "a7");
    require_sound_projection(slot.rows.size() == 1);
    check_sound_projection(slot.rows.front().resource == resource_id_t{2});
    const auto& bms = sound_picker_view_result_t::from(cache, song, chart, "");
    check_sound_projection(bms.total == 1);
    chart.source_family = format_family_t::bmson;
    ++song.revision;
    const auto& bmson = sound_picker_view_result_t::from(cache, song, chart, "");
    check_sound_projection(bmson.total == 3);
    check_sound_projection(sound_picker_view_result_t::from(cache, song, chart, "a7").rows.empty());
}

#include <filesystem>
#include <fstream>

namespace {
struct fallback_project_t {
    std::filesystem::path base = std::filesystem::temp_directory_path() / "wormedit-fallback-projection";
    fallback_project_t() {
        std::error_code error;
        std::filesystem::remove_all(base, error);
        std::filesystem::create_directories(base);
        for (const char* name : {"exact.wav", "single.ogg", "pair.ogg", "pair.flac"})
            std::ofstream(base / name) << "x";
    }
    ~fallback_project_t() {
        std::error_code error;
        std::filesystem::remove_all(base, error);
    }
};
} // namespace

TEST_CASE("fallback projection lists only missing sounds with one or several candidates",
          "[unit][sys][sound-list][audio-fallback]") {
    const fallback_project_t project;
    song_document_t song;
    song.id = song_id_t{9};
    song.revision = 3;
    const char* paths[]{"exact.wav", "single.wav", "pair.wav", "none.wav"};
    for (std::uint64_t index = 0; index < 4; ++index) {
        song.audio_assets.push_back({.id = asset_id_t{index + 1}, .source_path = paths[index]});
        song.resources.push_back({.id = resource_id_t{index + 10}, .name = paths[index],
                                  .asset = asset_id_t{index + 1}, .missing = index != 0});
    }
    const auto found = build_keysound_fallbacks_view(song, project.base, 32);
    require_sound_projection(found.size() == 2);
    check_sound_projection(found[0].resource == resource_id_t{11});
    check_sound_projection(found[0].result.outcome == audio_fallback_outcome_t::fallback);
    check_sound_projection(found[0].result.chosen == std::filesystem::path{"single.ogg"});
    check_sound_projection(found[1].resource == resource_id_t{12});
    check_sound_projection(found[1].result.outcome == audio_fallback_outcome_t::ambiguous);
    check_sound_projection(found[1].result.candidates.size() == 2);
    check_sound_projection(song.audio_assets[1].source_path == std::filesystem::path{"single.wav"});
    check_sound_projection(build_keysound_fallbacks_view(song, project.base, 1).size() == 1);
    song.resources[1].missing = false;
    const auto refreshed = build_keysound_fallbacks_view(song, project.base, 32);
    require_sound_projection(refreshed.size() == 1);
    check_sound_projection(refreshed.front().resource == resource_id_t{12});
}

TEST_CASE("Sounds use and path filters precede virtual windows and retain hidden selections",
          "[sys][sounds][sound-workflow]") {
    auto song = catalog();
    song.resources.push_back({.id = resource_id_t{5}, .name = "Alias kick", .asset = asset_id_t{2},
                              .roles = {resource_role_t::sample}, .missing = true});
    song.resources.push_back({.id = resource_id_t{6}, .name = "Unused", .asset = asset_id_t{1},
                              .roles = {resource_role_t::sample}});
    song.charts.push_back({.sound_channels = {{.source_clip = {.source = asset_id_t{2}},
                                             .events = {{}}}}});
    const std::array<std::uint64_t, 1> selected{6};
    keysound_list_cache_t cache;
    keysound_list_request_t request{.search = "ASSETS/2-KICK", .use_filter = resource_use_filter_t::used,
        .selected_resource_ids = selected, .include_selected_metadata = true,
        .first_row = 2, .row_limit = 1};
    const auto& used = keysound_list_view_t::from(cache, song, &song.charts[0], request);
    require_sound_projection(used.rows.size() == 1);
    check_sound_projection(used.rows[0].resource == resource_id_t{2});
    check_sound_projection(used.rows[0].use_count == 3);
    check_sound_projection(used.rows[0].source_path == "assets/2-kick.wav");
    require_sound_projection(used.selected_rows.size() == 1);
    check_sound_projection(used.selected_rows[0].resource == resource_id_t{6});
    check_sound_projection(used.all_resources.size() == 3);
    request.search = {};
    request.first_row = 1;
    request.use_filter = resource_use_filter_t::unused;
    const auto& unused = keysound_list_view_t::from(cache, song, &song.charts[0], request);
    require_sound_projection(unused.rows.size() == 1);
    check_sound_projection(unused.rows[0].resource == resource_id_t{6});
    check_sound_projection(cache.projection_build_count() == 2);
    request.use_filter = resource_use_filter_t::missing;
    const auto& missing = keysound_list_view_t::from(cache, song, &song.charts[0], request);
    require_sound_projection(missing.rows.size() == 1);
    check_sound_projection(missing.rows[0].resource == resource_id_t{5});
    check_sound_projection(missing.rows[0].use_count == 1);
    check_sound_projection(cache.projection_build_count() == 3);
}

TEST_CASE("BMS slots filter exact retained uses and full path before the row window",
          "[sys][sounds][sound-workflow]") {
    auto song = catalog();
    song.resources[1].target_identities = {{format_family_t::bms, "ZZ", "WAV"}};
    song.charts[0].sound_channels = {{.source_clip = {.source = asset_id_t{2}}, .events = {{}, {}}}};
    const auto used = sound_slots_view_t::from(song, {.search = "ASSETS/2-KICK", .selected_slot = 1295,
        .row_limit = 1, .use_filter = resource_use_filter_t::used});
    require_sound_projection(used.rows.size() == 1);
    check_sound_projection(used.total_matching == 1);
    check_sound_projection(used.rows[0].number == 1295);
    check_sound_projection(used.rows[0].use_count == 4);
    check_sound_projection(used.rows[0].source_path == "assets/2-kick.wav");
    check_sound_projection(used.selected_row == 0);
    const auto unused = sound_slots_view_t::from(song, {.selected_slot = 1295,
        .use_filter = resource_use_filter_t::unused});
    check_sound_projection(unused.total_matching == 1294);
    check_sound_projection(unused.selected_row == SIZE_MAX);
    check_sound_projection(bms_identifier_capacity(song, "WAV").used == 1);
}
