#pragma once

#include "ui/frontend/storyboard.h"
#include "ui/frontend/storyboard_hierarchy.h"
#include <wormedit/core/document.h>
#include <wormedit/core/osbx_osb_export.h>
#include <span>
#include <tuple>
#include <unordered_map>
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/command.h>

namespace wormedit::sys {
class storyboard_scene_presenter_t;
// Refills the persistent frontend hierarchy. Export reports and resolved-image
// failures come from their existing owners; this projection never runs Lua or IO.
void build_storyboard_hierarchy_view(ui::storyboard_view_t&, const song_document_t&,
                                   std::span<const osbx_osb_report_t> reports = {});
// Topology follows the document binding; selection/visibility and presenter
// diagnostics are frame overlays, not reasons to rebuild the authored tree.
class storyboard_hierarchy_projection_t {
  public:
    void refill(ui::storyboard_view_t&, const song_document_t&,
                const storyboard_scene_presenter_t* scenes = nullptr);
    [[nodiscard]] auto build_count() const -> std::size_t { return topology_.build_count(); }
  private:
    keyed_cache_t<std::tuple<const song_document_t*, document_stamp_t, storyboard_id_t,
        std::uint64_t, std::uint64_t, const osbx_document_t*>, bool> topology_;
    std::unordered_map<std::string, std::string> warnings_;
};
} // namespace wormedit::sys
