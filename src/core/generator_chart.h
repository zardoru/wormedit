#pragma once
#include <wormedit/core/generator.h>
namespace wormedit {
[[nodiscard]] auto materialize_generator_sample_references(document_command_transaction_t&, generator_recipe_t&)
    -> diagnostic_list_t;
[[nodiscard]] auto count_new_chart_declarations(std::span<const generator_chart_binding_t>,
                                                const generator_recipe_t&) -> std::size_t;
[[nodiscard]] auto validate_generator_charts(const generator_recipe_t&) -> diagnostic_list_t;
} // namespace wormedit
