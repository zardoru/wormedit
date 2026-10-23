#pragma once
#include <wormedit/core/bms_native_content.h>

namespace wormedit {
// One collection mapping is shared by normalization, projection and edit adapters.
template<class Value> struct bms_native_collection_t;
template<> struct bms_native_collection_t<bga_event_t> {
    static constexpr auto member = &chart_document_t::bga_events;
};
template<> struct bms_native_collection_t<bpm_event_t> {
    static constexpr auto member = &chart_document_t::bpms;
};
template<> struct bms_native_collection_t<pause_event_t> {
    static constexpr auto member = &chart_document_t::pauses;
};
template<> struct bms_native_collection_t<warp_event_t> {
    static constexpr auto member = &chart_document_t::warps;
};
template<> struct bms_native_collection_t<scroll_event_t> {
    static constexpr auto member = &chart_document_t::scrolls;
};
template<> struct bms_native_collection_t<speed_event_t> {
    static constexpr auto member = &chart_document_t::speeds;
};
template<> struct bms_native_collection_t<kiai_event_t> {
    static constexpr auto member = &chart_document_t::kiai;
};
template<> struct bms_native_collection_t<timing_anchor_t> {
    static constexpr auto member = &chart_document_t::timing_anchors;
};
template<> struct bms_native_collection_t<bookmark_t> {
    static constexpr auto member = &chart_document_t::bookmarks;
};
template<> struct bms_native_collection_t<section_t> {
    static constexpr auto member = &chart_document_t::sections;
};

template<> struct bms_native_collection_t<sv_recipe_t> {
    static constexpr auto member = &chart_document_t::sv_recipes;
};
template<> struct bms_native_collection_t<sound_channel_t> {
    static constexpr auto member = &chart_document_t::sound_channels;
};

template<class Value> auto& native_events(chart_document_t& chart) {
    if constexpr (std::same_as<Value, note_t>) return chart.measures.front().notes;
    else if constexpr (std::same_as<Value, bgm_event_t>) return chart.measures.front().bgm_events;
    else return chart.*bms_native_collection_t<Value>::member;
}
} // namespace wormedit
