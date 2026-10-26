#pragma once
#include <wormedit/core/id.h>
#include <wormedit/core/generator_types.h>
#include <string>
#include <string_view>
namespace wormedit { struct generator_recipe_t; struct generator_preview_t; struct generator_chart_content_t; }
#include <array>
#include <vector>
namespace wormedit::ui {
struct generator_parameter_form_t {
    char name[129]{};
    char value[16385]{"0"};
    auto operator==(const generator_parameter_form_t&) const -> bool = default;
};
struct generator_input_form_t {
    char asset[32]{};
    auto operator==(const generator_input_form_t&) const -> bool = default;
};
struct generator_form_t {
    recipe_id_t recipe;
    char path[4097]{};
    char seed[32]{"0"};
    std::vector<generator_parameter_form_t> parameters;
    std::vector<generator_input_form_t> inputs;
    bool keep_recipe{true};
    bool use_saved_source{};
    auto operator==(const generator_form_t&) const -> bool = default;
    static auto from(const generator_recipe_t&) -> generator_form_t;
    void refill(const generator_recipe_t&);
};
struct generator_entry_view_t { recipe_id_t id; std::string label; };
struct generator_chart_preview_t {
    std::string name;
    std::size_t measures{}, notes{}, bgm{}, timing{};
    void refill(const generator_chart_content_t&);
    auto operator==(const generator_chart_preview_t&) const -> bool = default;
};
struct compose_generator_view_t {
    std::vector<generator_entry_view_t> recipes;
    generator_form_t selected_form;
    bool has_selection{};
    bool busy{}, can_apply{}, requires_confirmation{};
    generator_diff_t notes, layers, charts, sv;
    std::vector<generator_chart_preview_t> chart_previews;
    std::vector<std::string> rationalizations;
    std::string_view status;
    void refill_preview(const generator_preview_t&);
};
}
