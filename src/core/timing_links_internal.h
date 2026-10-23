#pragma once
#include <wormedit/core/document.h>
#include <type_traits>
#include <utility>

namespace wormedit::timing_link_detail {
struct timing_event_filter_t {
    std::optional<pause_kind_t> pause;
    template<class Event> auto operator()(const Event& event) const -> bool {
        if constexpr (std::is_same_v<Event, pause_event_t>) return !pause || event.kind == *pause;
        else return true;
    }
};
// One authority for the independently linked collections; stops/delays share storage.
template<class Source, class Target, class Visit>
auto visit_timing_events(Source& source, Target& target, timing_category_t category, Visit&& visit)
    -> std::invoke_result_t<Visit, decltype((source.bpms)), decltype((target.bpms)), timing_event_filter_t> {
    const auto all = timing_event_filter_t{};
    switch (category) {
    case timing_category_t::bpm: return visit(source.bpms, target.bpms, all);
    case timing_category_t::scroll: return visit(source.scrolls, target.scrolls, all);
    case timing_category_t::speed: return visit(source.speeds, target.speeds, all);
    case timing_category_t::kiai: return visit(source.kiai, target.kiai, all);
    case timing_category_t::stop: return visit(source.pauses, target.pauses, timing_event_filter_t{.pause = pause_kind_t::stop});
    case timing_category_t::delay: return visit(source.pauses, target.pauses, timing_event_filter_t{.pause = pause_kind_t::delay});
    case timing_category_t::warp: return visit(source.warps, target.warps, all);
    case timing_category_t::anchor: return visit(source.timing_anchors, target.timing_anchors, all);
    case timing_category_t::offset:
    case timing_category_t::measures:
    case timing_category_t::count: return {};
    }
    return {};
}
} // namespace wormedit::timing_link_detail
