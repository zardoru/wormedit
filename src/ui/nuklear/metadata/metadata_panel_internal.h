#pragma once

#include "ui/nuklear/metadata/metadata_state.h"

#include <cstddef>
#include <initializer_list>

struct nk_context;
struct nk_style_button;

struct metadata_tree_identity_t {
    const char* hash;
    int seed;
};

struct MetadataPanel {
    nk_context* context;
    const wormedit::ui::metadata_view_t* metadata;
    wormedit::ui::workspace_action_t* action;
    bool window_focused;
};

struct metadata_rating_value_t {
    bool has_rating{};
    std::int64_t numerator{};
    std::uint64_t denominator{1};
};

extern worm_ui_nuklear_metadata_state_t& state;

metadata_tree_identity_t metadata_tree_identity(int original_line);
int metadata_tree_push(nk_context* context, int tree_type, const char* title, int initial_state,
                      int original_line);
void muted_label(nk_context* context, const char* text);
int copy_metadata_text(char* destination, std::size_t capacity, const char* source,
                       std::size_t source_bytes);
// Form rows share a fixed label column, so a label stays beside its field at any width.
inline constexpr float metadata_label_width = 176.0f;
inline constexpr float metadata_row_button_width = 120.0f;
inline constexpr float metadata_unit_width = 24.0f;
// Label column, one dynamic field, then fixed-width trailing controls.
void metadata_form_row(nk_context* context, float height, std::initializer_list<float> trailing = {});
void metadata_input_row(nk_context* context, const char* label, char* buffer, std::size_t capacity,
                        int enabled);
void style_metadata_footer_button(nk_style_button& style, bool enabled, bool accented);
void render_footer_buttons(nk_context* context, bool revert_enabled, bool apply_enabled,
                           const char* revert_label, const char* apply_label, bool& revert,
                           bool& apply);
