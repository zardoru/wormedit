#pragma once
#include <wormedit/core/generator.h>
#include <wormedit/core/interaction_types.h>
#include <wormedit/scripting/lua_sandbox.h>
#include <span>
namespace wormedit::scripting {
// Owned snapshots expose only declared MIDI inputs. A fresh independent replay
// must produce the same typed batch in array order. Preparation, both VMs,
// snapshot materialization and result extraction share one instruction budget;
// simultaneous VM allocations share one memory cap. No live document references
// enter Lua. Ordinary runs read no source files; an explicit reader is reserved
// for project-bundle preparation under the same VM limits.
[[nodiscard]] auto run_lua_generator(const song_document_t&, const chart_document_t&,
    const generator_recipe_t&, std::span<const editor_selection_object_t> selection = {},
    lua_sandbox_limits_t limits = {}, lua_project_reader_t reader = {}) -> diagnosed_t<generator_output_t>;
}
