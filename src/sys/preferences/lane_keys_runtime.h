#pragma once
#include "sys/preferences/preferences_runtime.h"
#include "sys/commands/shortcut_rows.h"
#include <wormedit/core/lane_keys.h>

namespace wormedit::sys {
class lane_keys_runtime_t {
  public:
    [[nodiscard]] static auto can_open(const chart_document_t* chart) -> bool {
        return chart && !chart->lane_layouts.empty() && !chart->lane_layouts.front().layout.profile_id.empty();
    }
    [[nodiscard]] auto view() const -> const ui::lane_keys_view_t& { return view_; }
    [[nodiscard]] auto open(const song_document_t&, chart_id_t, local_position_t,
                            const program_preferences_t&) -> bool;
    // A changed layout closes a captured dialog before a draft can reach preferences.
    void refresh(const song_document_t&);
    [[nodiscard]] auto dispatch(const ui::lane_keys_action_t&, program_preferences_runtime_t&) -> bool;
    void capture(std::span<const wormedit::ui::shortcut_event_t>);
    [[nodiscard]] auto status() const -> const std::string& { return status_; }
  private:
    ui::lane_keys_view_t view_;
    chart_id_t chart_;
    song_id_t project_;
    lane_layout_t layout_;
    input_profile_t fallback_;
    lane_key_bindings_t draft_;
    std::vector<lane_definition_t> lanes_;
    bool reset_{};
    std::string status_;
    void rebuild();
    void bind(std::uint16_t, bool unbind = false);
    [[nodiscard]] auto apply(program_preferences_runtime_t&) -> bool;
};
}
