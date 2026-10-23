#pragma once
#include <wormedit/core/timing_links_types.h>
#include <string>
namespace wormedit::ui {
struct timing_link_category_view_t {
    timing_category_t category{};
    std::string label;
    std::string status;
    chart_id_t source;
    bool is_local_override{};
    bool requires_confirmation{};
    auto operator==(const timing_link_category_view_t&) const -> bool = default;
};
struct timing_link_context_t {
    song_id_t project;
    chart_id_t chart;
    std::uint64_t revision{}, session_generation{};
    auto operator==(const timing_link_context_t&) const -> bool = default;
};
struct timing_link_action_t {
    bool requested{};
    timing_link_context_t context;
    timing_link_request_t request;
    auto operator==(const timing_link_action_t&) const -> bool = default;
};
}
