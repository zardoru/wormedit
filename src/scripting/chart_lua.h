#pragma once
#include "lua_generator.h"
namespace wormedit::scripting {
// Trusted declaration prelude; scripts still execute in the ordinary bounded sandbox.
[[nodiscard]] auto chart_lua_prelude() -> std::string_view;
[[nodiscard]] auto decode_lua_charts(const lua_sandbox_context_t&) -> generator_output_t;
} // namespace wormedit::scripting
