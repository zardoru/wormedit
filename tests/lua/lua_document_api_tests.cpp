#include "../../src/scripting/lua_document_api.h"
#include <wormedit/core/document.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>

using namespace wormedit;
using namespace wormedit::scripting;
namespace {
auto chart_fixture() -> chart_document_t {
    chart_document_t chart{.id=chart_id_t{UINT64_MAX},.measures={
        {.id=measure_id_t{11},.definition={{3,2},meter_t{6,4}}},{.id=measure_id_t{12}}}};
    chart.semantic_layers.push_back({.id=layer_id_t{55},.name="melody"});
    chart.measures[0].notes.push_back({.id=object_id_t{100},.position={measure_id_t{11},123},.lane=lane_id_t{7},
        .semantic_layers={layer_id_t{55}},.label="original"});
    lane_layout_t layout{.profile_id="keys",.display_name="Keys",.lanes={
        {.id=lane_id_t{7},.name="Left",.midi_destination=midi_destination_t{2,60}}}};
    chart.lane_layouts.push_back({.id=object_id_t{8},.position={measure_id_t{11},0},.layout=std::move(layout)});
    chart.bpms.push_back({.id=object_id_t{20},.position={measure_id_t{11},0},.beats_per_minute={241,2}});
    chart.scrolls.push_back({.id=object_id_t{21},.position={measure_id_t{11},120},.multiplier={-3,2}});
    chart.speeds.push_back({.id=object_id_t{22},.position={measure_id_t{11},240},.multiplier={5,4},.transition_beats={1,3},.transition_is_seconds=true});
    chart.pauses.push_back({.id=object_id_t{23},.position={measure_id_t{11},360},.kind=pause_kind_t::delay,.duration={1,7}});
    chart.warps.push_back({.id=object_id_t{24},.span={{measure_id_t{11},480},{measure_id_t{11},600}}});
    chart.kiai.push_back({.id=object_id_t{25},.position={measure_id_t{11},720},.enabled=true});
    chart.timing_anchors.push_back({.id=object_id_t{26},.position={measure_id_t{11},0},.source_sample_frame=UINT64_MAX,.locked=true});
    return chart;
}
auto midi_event(std::uint64_t tick,std::initializer_list<unsigned> bytes) -> midi_event_t {
    midi_event_t event{.tick=tick,.channel=2};
    for (auto byte:bytes) event.data.push_back(static_cast<std::byte>(byte));
    return event;
}
auto song_fixture() -> song_document_t {
    song_document_t song{.id=song_id_t{UINT64_MAX},.revision=UINT64_MAX};
    song.midi_assets.push_back({.id=asset_id_t{44},.ticks_per_quarter=480,.events={
        midi_event(0,{0x92,60,100}),midi_event(240,{0x82,60,0}),midi_event(10,{0xb2,7,90}),
        midi_event(0,{0xff,0x51,3,7,0xa1,0x20}),midi_event(0,{0xff,0x58,4,4,2,24,8}),
        midi_event(0,{0xff,0x03,1,'A'}),midi_event(120,{0xff,0x06,1,'x'}),
        midi_event(0,{0xe2,0,64}),midi_event(0,{0xd2,1}),midi_event(0,{0xf0,1,2,0xf7}),midi_event(0,{0xff,0x05,1,'x'})}});
    return song;
}
auto snapshot(const song_document_t& song,const chart_document_t& chart) -> lua_document_snapshot_t {
    const std::array selection{editor_selection_object_t{.id=object_id_t{100}}};
    auto result=lua_document_snapshot_t::from(song,chart,selection);
    REQUIRE(result);
    return std::move(*result);
}
auto evaluate(const lua_document_snapshot_t& snapshot,std::string_view source)
    -> result_t<lua_sandbox_value_t,wormedit::error_t<failure_code_t>> {
    auto sandbox=lua_sandbox_t::from(); REQUIRE(sandbox);
    REQUIRE(snapshot.install(*sandbox));
    return sandbox->evaluate(source);
}
void check_script(const lua_document_snapshot_t& snapshot,std::string_view source) {
    const auto result=evaluate(snapshot,source);
    INFO((result ? "success" : result.error().message));
    REQUIRE(result); CHECK(*result==lua_sandbox_value_t{true});
}
}

TEST_CASE("Lua document API version identities and selection are exact", "[lua-document-api]") {
    const auto view=snapshot(song_fixture(),chart_fixture());
    check_script(view,R"(
        assert(wormedit.api_version==1)
        assert(wormedit.project_id=='18446744073709551615')
        assert(wormedit.chart_id=='18446744073709551615')
        assert(wormedit.revision=='18446744073709551615')
        assert(#wormedit.selection==1 and wormedit.selection[1].id=='100')
        assert(wormedit.divisions_per_standard_measure==960)
        return true
    )");
}

TEST_CASE("Lua document API measure grid retains exact fractions and meter", "[lua-document-api]") {
    check_script(snapshot(song_fixture(),chart_fixture()),R"(
        local m=wormedit.measures[1]
        assert(m.id=='11' and m.length.num==3 and m.length.den==2)
        assert(m.meter.num==6 and m.meter.den==4 and m.row_count==1440)
        assert(wormedit.measures[2].start_row==1440)
        local n=0; for _,item in ipairs(wormedit.measures) do n=n+1 end
        assert(n==2)
        return true
    )");
}

TEST_CASE("Lua document API exposes every represented timing family without floating conversion", "[lua-document-api]") {
    check_script(snapshot(song_fixture(),chart_fixture()),R"(
        local t=wormedit.timing
        assert(t.bpms[1].value.num==241 and t.bpms[1].value.den==2)
        assert(t.scrolls[1].value.num==-3 and t.scrolls[1].position.row==120)
        assert(t.speeds[1].transition.den==3 and t.speeds[1].transition_is_seconds)
        assert(t.pauses[1].kind=='delay' and t.pauses[1].duration.den==7)
        assert(t.warps[1]['end'].row==600)
        assert(t.kiai[1].enabled and t.anchors[1].locked)
        assert(t.anchors[1].source_sample_frame=='18446744073709551615')
        assert(t.beat_zero_audio_offset_seconds.num==0)
        return true
    )");
}

TEST_CASE("Lua document API exposes lane profiles notes and layers", "[lua-document-api]") {
    check_script(snapshot(song_fixture(),chart_fixture()),R"(
        local p=wormedit.lane_profile[1]
        assert(p.profile_id=='keys' and p.lanes[1].id=='7')
        assert(p.lanes[1].midi_destination.channel==2 and p.lanes[1].midi_destination.note==60)
        assert(wormedit.notes[1].position.measure=='11' and wormedit.notes[1].position.row==123)
        assert(wormedit.notes[1].lane=='7' and wormedit.notes[1].layers[1]=='55')
        assert(wormedit.layers[1].name=='melody')
        return true
    )");
}

TEST_CASE("Lua document API exposes only baseline MIDI messages and never source paths", "[lua-document-api]") {
    check_script(snapshot(song_fixture(),chart_fixture()),R"(
        local m=wormedit.midi_assets[1]
        assert(m.id=='44' and m.ticks_per_quarter==480 and #m.events==7)
        assert(m.events[1].kind=='note_on' and m.events[1].pitch==60 and m.events[1].velocity==100)
        assert(m.events[2].kind=='note_off' and m.events[2].tick==240)
        assert(m.events[3].kind=='controller' and m.events[3].controller==7 and m.events[3].value==90)
        assert(m.events[4].kind=='tempo' and m.events[5].kind=='time_signature')
        assert(m.events[6].kind=='track_name' and m.events[7].kind=='marker')
        assert(m.discarded_event_count==4 and m.source_path==nil)
        return true
    )");
}

TEST_CASE("Lua document snapshots own their data after the source changes", "[lua-document-api]") {
    auto document=chart_fixture();
    const auto view=snapshot(song_fixture(),document);
    document.measures[0].notes[0].label="changed";
    document.measures.clear();
    check_script(view,"return wormedit.notes[1].label=='original' and #wormedit.measures==2");
}

TEST_CASE("Lua document snapshots reject deep writes and invalid numeric lookups", "[lua-document-api]") {
    const auto source=GENERATE("wormedit.measures[1].length.num=99", "rawset(wormedit.notes[1],'lane','9')",
        "local x=wormedit.measures[4]", "local x=wormedit.measures[0]", "local x=wormedit.notes[1.5]");
    const auto result=evaluate(snapshot(song_fixture(),chart_fixture()),source);
    REQUIRE_FALSE(result); CHECK(result.error().code==failure_code_t::task_failed);
}

TEST_CASE("Lua document snapshot construction has small explicit node and byte limits", "[lua-document-api]") {
    const auto bytes=GENERATE(std::size_t{1},std::size_t{128});
    const auto result=lua_document_snapshot_t::from(song_fixture(),chart_fixture(),{}, {.maximum_bytes=bytes});
    REQUIRE_FALSE(result); CHECK(result.error().code==failure_code_t::capacity_exceeded);
}

TEST_CASE("Lua document snapshot construction observes cancellation", "[lua-document-api]") {
    std::stop_source cancellation; cancellation.request_stop();
    const auto result=lua_document_snapshot_t::from(song_fixture(),chart_fixture(),{}, {.cancellation=cancellation.get_token()});
    REQUIRE_FALSE(result); CHECK(result.error().code==failure_code_t::cancelled);
}

TEST_CASE("Lua document snapshots can explicitly restrict the visible MIDI inputs", "[lua-document-api]") {
    auto song=song_fixture();
    song.midi_assets.push_back({.id=asset_id_t{99},.ticks_per_quarter=960});
    const std::array declared{asset_id_t{44}};
    auto result=lua_document_snapshot_t::from(song,chart_fixture(),{}, {}, std::span<const asset_id_t>{declared});
    REQUIRE(result);
    check_script(*result,"return #wormedit.midi_assets==1 and wormedit.midi_assets[1].id=='44'");
}
