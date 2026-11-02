#pragma once

#include <wormedit/core/generator.h>
#include <wormedit/scripting/lua_sandbox.h>

namespace wormedit::scripting {
// Trusted descriptor adapter. Caller locations are supplied privately by the sandbox.
[[nodiscard]] auto osbx_lua_prelude() -> std::string_view;
[[nodiscard]] auto decode_lua_osbx(const lua_sandbox_context_t&) -> osbx_document_t;
[[nodiscard]] auto decode_lua_storyboards(const lua_sandbox_context_t&) -> std::vector<generator_storyboard_t>;
// Evaluates detached declarations twice with shared budgets, then validates before publication.
[[nodiscard]] auto run_osbx_lua(std::string_view source, std::string_view chunk_name = "storyboard",
                                lua_sandbox_limits_t limits = {},
                                const lua_sandbox_context_t& context = {})
    -> diagnosed_t<osbx_document_t>;
} // namespace wormedit::scripting
