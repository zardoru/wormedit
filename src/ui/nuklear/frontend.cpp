#define NK_IMPLEMENTATION
#define NK_SDL_GL3_IMPLEMENTATION
#define GL_SILENCE_DEPRECATION

#include "ui/frontend.h"

#include "wormedit/nuklear_config.h"

#include "nuklear_sdl_gl3_sdl3.h"

#include <stddef.h>
#include <string.h>

#define WORMEDIT_MAX_VERTEX_MEMORY (512 * 1024)
#define WORMEDIT_MAX_ELEMENT_MEMORY (128 * 1024)

static SDL_Window* g_window;
static struct nk_context* g_context;
static char g_recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];

static int recovery_action_offered(const WormUiRecoveryCandidateView* candidate, size_t action) {
    return candidate && (candidate->action_mask & action) != 0;
}

static void set_recovery_action(WormUiWorkspaceAction* action,
                                const WormUiRecoveryCandidateView* candidate,
                                WormUiWorkspaceActionKind kind) {
    if (!action || !candidate) {
        return;
    }
    action->kind = kind;
    action->recovery_scan_generation = candidate->scan_generation;
    action->recovery_row_index = candidate->row_index;
    action->recovery_candidate_index = candidate->candidate_index;
    if (kind == WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS) {
        memcpy(action->recovery_save_as_destination, g_recovery_save_as_destination,
               sizeof(action->recovery_save_as_destination));
    }
}

bool worm_ui_frontend_init(SDL_Window* window) {
    if (!window || g_context) {
        return false;
    }

    struct nk_context* context = nk_sdl_init(window);
    if (!context) {
        return false;
    }
    struct nk_font_atlas* atlas = NULL;
    nk_sdl_font_stash_begin(&atlas);
    (void)atlas;
    nk_sdl_font_stash_end();

    if (!SDL_StartTextInput(window)) {
        nk_sdl_shutdown();
        return false;
    }
    g_window = window;
    g_context = context;
    return true;
}

void worm_ui_frontend_shutdown(void) {
    if (!g_context) {
        return;
    }
    if (g_window) {
        SDL_StopTextInput(g_window);
    }
    nk_sdl_shutdown();
    g_window = NULL;
    g_context = NULL;
}

void worm_ui_frontend_begin_input(void) {
    if (g_context) {
        nk_input_begin(g_context);
    }
}

bool worm_ui_frontend_handle_event(const SDL_Event* event) {
    if (!g_context || !event) {
        return false;
    }
    SDL_Event mutable_event = *event;
    return nk_sdl_handle_event(&mutable_event) != 0;
}

void worm_ui_frontend_end_input(void) {
    if (g_context) {
        nk_input_end(g_context);
        nk_sdl_handle_grab();
    }
}

WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view) {
    WormUiWorkspaceAction action = {
        WORM_UI_WORKSPACE_ACTION_NONE, WORM_UI_WORKSPACE_TAB_SETUP, 0, 0, 0, {0}};
    if (!g_context || !g_window || !view || !view->project_title || !view->artist ||
        !view->selected_difficulty || !view->status_text || !view->git_hash || !view->tab_heading ||
        view->summary_line_count > WORM_UI_WORKSPACE_SUMMARY_CAPACITY ||
        view->recovery_candidate_count > WORM_UI_RECOVERY_ROW_CAPACITY ||
        view->recovery_diagnostic_count > WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY ||
        view->active_tab < WORM_UI_WORKSPACE_TAB_SETUP ||
        view->active_tab > WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE) {
        return action;
    }

    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSize(g_window, &width, &height) || width <= 0 || height <= 0) {
        return action;
    }

    if (nk_begin(g_context, "wormedit-workspace", nk_rect(0, 0, (float)width, (float)height),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        nk_layout_row_begin(g_context, NK_DYNAMIC, 30, 4);
        nk_layout_row_push(g_context, 0.24f);
        nk_labelf(g_context, NK_TEXT_LEFT, "%s%s", view->project_title, view->dirty ? " *" : "");
        nk_layout_row_push(g_context, 0.18f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Artist: %s", view->artist);
        nk_layout_row_push(g_context, 0.22f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Difficulty: %s", view->selected_difficulty);
        nk_layout_row_push(g_context, 0.36f);
        nk_label(g_context, view->status_text, NK_TEXT_RIGHT);
        nk_layout_row_end(g_context);

        nk_layout_row_dynamic(g_context, 34, WORM_UI_WORKSPACE_TAB_COUNT);
        for (int tab = 0; tab < WORM_UI_WORKSPACE_TAB_COUNT; ++tab) {
            const WormUiWorkspaceTabItem* item = &view->tabs[tab];
            if (!item->label) {
                continue;
            }
            if (!item->enabled) {
                nk_widget_disable_begin(g_context);
            }
            if (nk_button_label(g_context, item->label) && item->enabled &&
                tab != (int)view->active_tab) {
                action.kind = WORM_UI_WORKSPACE_ACTION_SWITCH_TAB;
                action.tab = (WormUiWorkspaceTab)tab;
            }
            if (!item->enabled) {
                nk_widget_disable_end(g_context);
            }
        }

        nk_layout_row_dynamic(g_context, 28, 1);
        nk_label(g_context, view->tab_heading, NK_TEXT_LEFT);

        float recovery_height = 0.0f;
        if (view->recovery_panel_visible) {
            recovery_height = 40.0f +
                              (view->recovery_notice && view->recovery_notice[0] ? 22.0f : 0.0f) +
                              (float)view->recovery_candidate_count * 48.0f +
                              (float)view->recovery_diagnostic_count * 18.0f;
            for (size_t row = 0; row < view->recovery_candidate_count; ++row) {
                const WormUiRecoveryCandidateView* candidate = &view->recovery_candidates[row];
                if (recovery_action_offered(candidate, WORM_UI_RECOVERY_ACTION_SAVE_AS)) {
                    recovery_height += 25.0f;
                    if (g_recovery_save_as_destination[0] == '\0') {
                        recovery_height += 18.0f;
                    }
                }
                if (candidate->disabled_reason && candidate->disabled_reason[0]) {
                    recovery_height += 20.0f;
                }
            }
            nk_layout_row_dynamic(g_context, 24, 1);
            nk_label(g_context, view->recovery_heading ? view->recovery_heading : "Recovery",
                     NK_TEXT_LEFT);
            if (view->recovery_notice && view->recovery_notice[0]) {
                nk_layout_row_dynamic(g_context, 22, 1);
                nk_label(g_context, view->recovery_notice, NK_TEXT_LEFT);
            }
            for (size_t row = 0; row < view->recovery_candidate_count; ++row) {
                const WormUiRecoveryCandidateView* candidate = &view->recovery_candidates[row];
                nk_layout_row_dynamic(g_context, 20, 1);
                nk_labelf(g_context, NK_TEXT_LEFT, "%s  %s",
                          candidate->path_label ? candidate->path_label : "(unknown recovery path)",
                          candidate->summary ? candidate->summary : "");

                nk_layout_row_dynamic(g_context, 25, 5);
                const struct {
                    size_t bit;
                    const char* label;
                    WormUiWorkspaceActionKind kind;
                } buttons[] = {
                    {WORM_UI_RECOVERY_ACTION_INSPECT, "Inspect",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT},
                    {WORM_UI_RECOVERY_ACTION_RECOVER, "Recover",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER},
                    {WORM_UI_RECOVERY_ACTION_SAVE_AS, "Save as",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS},
                    {WORM_UI_RECOVERY_ACTION_DISCARD, "Discard",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD},
                    {WORM_UI_RECOVERY_ACTION_QUARANTINE, "Quarantine",
                     WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE},
                };
                for (size_t button = 0; button < sizeof(buttons) / sizeof(buttons[0]); ++button) {
                    const int offered = recovery_action_offered(candidate, buttons[button].bit);
                    if (!offered) {
                        nk_widget_disable_begin(g_context);
                    }
                    const int clicked = nk_button_label(g_context, buttons[button].label);
                    if (!offered) {
                        nk_widget_disable_end(g_context);
                    }
                    int save_as_ready = 1;
                    if (buttons[button].kind == WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS) {
                        save_as_ready = g_recovery_save_as_destination[0] != '\0';
                    }
                    if (offered && save_as_ready && clicked &&
                        action.kind == WORM_UI_WORKSPACE_ACTION_NONE) {
                        set_recovery_action(&action, candidate, buttons[button].kind);
                    }
                }
                if (recovery_action_offered(candidate, WORM_UI_RECOVERY_ACTION_SAVE_AS)) {
                    nk_layout_row_begin(g_context, NK_DYNAMIC, 25, 2);
                    nk_layout_row_push(g_context, 0.18f);
                    nk_label(g_context, "New project path", NK_TEXT_LEFT);
                    nk_layout_row_push(g_context, 0.82f);
                    (void)nk_edit_string_zero_terminated(
                        g_context, NK_EDIT_FIELD, g_recovery_save_as_destination,
                        sizeof(g_recovery_save_as_destination), nk_filter_default);
                    nk_layout_row_end(g_context);
                    if (g_recovery_save_as_destination[0] == '\0') {
                        nk_layout_row_dynamic(g_context, 18, 1);
                        nk_label(g_context,
                                 "Save as is disabled until a new destination is entered.",
                                 NK_TEXT_LEFT);
                    }
                }
                if (candidate->disabled_reason && candidate->disabled_reason[0]) {
                    nk_layout_row_dynamic(g_context, 20, 1);
                    nk_label(g_context, candidate->disabled_reason, NK_TEXT_LEFT);
                }
            }
            if (view->recovery_rows_truncated) {
                nk_layout_row_dynamic(g_context, 18, 1);
                nk_label(g_context, "Additional recovery candidates are not shown.", NK_TEXT_LEFT);
            }
            for (size_t diagnostic = 0; diagnostic < view->recovery_diagnostic_count;
                 ++diagnostic) {
                if (view->recovery_diagnostics[diagnostic]) {
                    nk_layout_row_dynamic(g_context, 18, 1);
                    nk_label(g_context, view->recovery_diagnostics[diagnostic], NK_TEXT_LEFT);
                }
            }
        }

        float content_height = height > 300 ? (float)height - 165.0f - recovery_height : 80.0f;
        if (content_height < 80.0f) {
            content_height = 80.0f;
        }
        nk_layout_row_dynamic(g_context, content_height, 1);
        if (nk_group_begin(g_context, "workspace-content", NK_WINDOW_BORDER)) {
            nk_layout_row_dynamic(g_context, 24, 1);
            if (view->summary_line_count == 0) {
                nk_label(g_context, "No items in this view.", NK_TEXT_LEFT);
            }
            for (size_t line = 0; line < view->summary_line_count; ++line) {
                if (view->summary_lines[line]) {
                    nk_label(g_context, view->summary_lines[line], NK_TEXT_LEFT);
                }
            }
            nk_group_end(g_context);
        }

        nk_layout_row_begin(g_context, NK_DYNAMIC, 28, 5);
        nk_layout_row_push(g_context, 0.16f);
        nk_labelf(g_context, NK_TEXT_LEFT, "History: %s",
                  view->history_available ? "available" : "unavailable");
        nk_layout_row_push(g_context, 0.10f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Undo: %zu", view->undo_depth);
        nk_layout_row_push(g_context, 0.10f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Redo: %zu", view->redo_depth);
        nk_layout_row_push(g_context, 0.52f);
        nk_labelf(g_context, NK_TEXT_LEFT, "Revision build: %s", view->git_hash);
        nk_layout_row_push(g_context, 0.12f);
        if (nk_button_label(g_context, "Quit")) {
            action.kind = WORM_UI_WORKSPACE_ACTION_QUIT;
        }
        nk_layout_row_end(g_context);
    }
    nk_end(g_context);
    return action;
}

void worm_ui_frontend_render(int window_width, int window_height) {
    (void)window_width;
    (void)window_height;
    if (g_context) {
        nk_sdl_render(NK_ANTI_ALIASING_ON, WORMEDIT_MAX_VERTEX_MEMORY, WORMEDIT_MAX_ELEMENT_MEMORY);
    }
}
