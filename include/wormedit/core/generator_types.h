#pragma once
#include <cstddef>
namespace wormedit {
inline constexpr std::size_t maximum_generator_source_bytes = 64 * 1024;
inline constexpr std::size_t maximum_generator_parameters = 256;
inline constexpr std::size_t maximum_generator_notes = 16384;
inline constexpr std::size_t maximum_generator_layers = 256;
inline constexpr std::size_t maximum_generator_recipes = 256;
struct generator_diff_t { std::size_t added{}, changed{}, removed{}, unchanged{}; };
}
