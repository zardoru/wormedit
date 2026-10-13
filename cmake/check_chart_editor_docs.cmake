if(NOT DEFINED WORMEDIT_SOURCE_ROOT)
    message(FATAL_ERROR "WORMEDIT_SOURCE_ROOT is required")
endif()

set(plan_index "${WORMEDIT_SOURCE_ROOT}/docs/chart-editor-plan.md")
set(status_index "${WORMEDIT_SOURCE_ROOT}/memory/chart-editor-status.md")
set(plan_slices
    docs/chart-editor-plan/overview.md
    docs/chart-editor-plan/document-model.md
    docs/chart-editor-plan/editing.md
    docs/chart-editor-plan/editing/layout.md
    docs/chart-editor-plan/editing/interactions.md
    docs/chart-editor-plan/editing/metadata-interop.md
    docs/chart-editor-plan/playback-audio.md
    docs/chart-editor-plan/playback/player-preview.md
    docs/chart-editor-plan/formats-persistence.md
    docs/chart-editor-plan/testing.md
    docs/chart-editor-plan/stages.md
    docs/chart-editor-plan/stages/foundation.md
    docs/chart-editor-plan/stages/authoring-export.md
    docs/chart-editor-plan/stages/later.md
    docs/chart-editor-plan/tasks.md
    docs/chart-editor-plan/tasks/compose-split.md
    docs/chart-editor-plan/tasks/compose-features.md
    docs/chart-editor-plan/tasks/interop-evidence.md
    docs/chart-editor-plan/tasks/frontend-shell.md
    docs/chart-editor-plan/tasks/editing-commands.md
    docs/chart-editor-plan/tasks/measures-timing.md
    docs/chart-editor-plan/tasks/bms-branches.md
    docs/chart-editor-plan/tasks/resources-audio.md
    docs/chart-editor-plan/tasks/export.md
    docs/chart-editor-plan/tasks/persistence-metadata.md
    docs/chart-editor-plan/tasks/editing-depth.md
    docs/chart-editor-plan/tasks/scripting-storyboard.md
    docs/chart-editor-plan/tasks/late-compat.md
    docs/chart-editor-plan/tasks/osbx.md
    docs/chart-editor-plan/tasks/compose-ux.md
    docs/chart-editor-plan/tasks/notifications.md
    docs/chart-editor-plan/tasks/panels-menus.md
    docs/chart-editor-plan/tasks/rendering.md
    docs/chart-editor-plan/tasks/options-charts.md
    docs/chart-editor-plan/tasks/live-transport.md
    docs/chart-editor-plan/tasks/markers.md
    docs/chart-editor-plan/tasks/semantic-layers.md
    docs/chart-editor-plan/tasks/compose-followups.md
    docs/chart-editor-plan/tasks/state-ownership.md
    docs/chart-editor-plan/decisions.md
    docs/chart-editor-plan/decisions/open-questions.md
    docs/chart-editor-plan/decisions/code-style.md
    docs/references/osu-mania-version-profiles.md
    docs/references/stepmania-warps-and-conversion.md
)
set(status_slices
    memory/chart-editor-status/current-milestone.md
    memory/chart-editor-status/implemented.md
    memory/chart-editor-status/implemented/foundation.md
    memory/chart-editor-status/implemented/visual-preview.md
    memory/chart-editor-status/implemented/formats-persistence.md
    memory/chart-editor-status/implemented/audio-timing.md
    memory/chart-editor-status/implemented/editing-frontend.md
    memory/chart-editor-status/settled-decisions.md
    memory/chart-editor-status/settled-decisions/document-formats.md
    memory/chart-editor-status/settled-decisions/editor-audio-preview.md
    memory/chart-editor-status/settled-decisions/interaction-testing.md
    memory/chart-editor-status/settled-decisions/future-performance-compatibility.md
    memory/chart-editor-status/settled-decisions/architecture-persistence.md
    memory/chart-editor-status/open-decisions.md
    memory/chart-editor-status/resume.md
    memory/chart-editor-status/code-constraints.md
)

foreach(index IN ITEMS "${plan_index}" "${status_index}")
    if(NOT EXISTS "${index}")
        message(FATAL_ERROR "Missing chart-editor documentation index: ${index}")
    endif()
endforeach()

set(stages_index "${WORMEDIT_SOURCE_ROOT}/docs/chart-editor-plan/stages.md")
file(READ "${stages_index}" stages_index_text)
set(stage_slices
    docs/chart-editor-plan/stages/foundation.md
    docs/chart-editor-plan/stages/authoring-export.md
    docs/chart-editor-plan/stages/later.md
)
foreach(slice IN LISTS stage_slices)
    string(REGEX REPLACE "^docs/chart-editor-plan/" "" index_target "${slice}")
    string(FIND "${stages_index_text}" "](${index_target})" link_offset)
    if(link_offset EQUAL -1)
        message(FATAL_ERROR "Implementation-stage index does not link to ${slice}")
    endif()
endforeach()

file(READ "${plan_index}" plan_index_text)
set(plan_index_slices
    docs/chart-editor-plan/overview.md
    docs/chart-editor-plan/document-model.md
    docs/chart-editor-plan/editing.md
    docs/chart-editor-plan/playback-audio.md
    docs/chart-editor-plan/formats-persistence.md
    docs/chart-editor-plan/testing.md
    docs/chart-editor-plan/stages.md
    docs/chart-editor-plan/tasks.md
    docs/chart-editor-plan/decisions.md
)
foreach(slice IN LISTS plan_index_slices)
    string(REGEX REPLACE "^docs/" "" index_target "${slice}")
    string(FIND "${plan_index_text}" "](${index_target})" link_offset)
    if(link_offset EQUAL -1)
        message(FATAL_ERROR "Plan index does not link to ${slice}")
    endif()
endforeach()

set(settled_decisions_index
    "${WORMEDIT_SOURCE_ROOT}/memory/chart-editor-status/settled-decisions.md")
file(READ "${settled_decisions_index}" settled_decisions_index_text)
set(settled_decision_slices
    memory/chart-editor-status/settled-decisions/document-formats.md
    memory/chart-editor-status/settled-decisions/editor-audio-preview.md
    memory/chart-editor-status/settled-decisions/interaction-testing.md
    memory/chart-editor-status/settled-decisions/future-performance-compatibility.md
    memory/chart-editor-status/settled-decisions/architecture-persistence.md
)
foreach(slice IN LISTS settled_decision_slices)
    string(REGEX REPLACE "^memory/chart-editor-status/" "" index_target "${slice}")
    string(FIND "${settled_decisions_index_text}" "](${index_target})" link_offset)
    if(link_offset EQUAL -1)
        message(FATAL_ERROR "Settled-decision index does not link to ${slice}")
    endif()
endforeach()

set(editing_index "${WORMEDIT_SOURCE_ROOT}/docs/chart-editor-plan/editing.md")
file(READ "${editing_index}" editing_index_text)
set(editing_slices
    docs/chart-editor-plan/editing/layout.md
    docs/chart-editor-plan/editing/interactions.md
    docs/chart-editor-plan/editing/metadata-interop.md
)
foreach(slice IN LISTS editing_slices)
    string(REGEX REPLACE "^docs/chart-editor-plan/" "" index_target "${slice}")
    string(FIND "${editing_index_text}" "](${index_target})" link_offset)
    if(link_offset EQUAL -1)
        message(FATAL_ERROR "Editing-plan index does not link to ${slice}")
    endif()
endforeach()

set(decisions_index "${WORMEDIT_SOURCE_ROOT}/docs/chart-editor-plan/decisions.md")
file(READ "${decisions_index}" decisions_index_text)
string(FIND "${decisions_index_text}" "](decisions/open-questions.md)" questions_link_offset)
if(questions_link_offset EQUAL -1)
    message(FATAL_ERROR "Technical-decisions index does not link to open questions")
endif()

file(READ "${status_index}" status_index_text)
set(status_index_slices
    memory/chart-editor-status/current-milestone.md
    memory/chart-editor-status/implemented.md
    memory/chart-editor-status/settled-decisions.md
    memory/chart-editor-status/open-decisions.md
    memory/chart-editor-status/resume.md
    memory/chart-editor-status/code-constraints.md
)
foreach(slice IN LISTS status_index_slices)
    string(REGEX REPLACE "^memory/" "" index_target "${slice}")
    string(FIND "${status_index_text}" "](${index_target})" link_offset)
    if(link_offset EQUAL -1)
        message(FATAL_ERROR "Status index does not link to ${slice}")
    endif()
endforeach()

set(implemented_index "${WORMEDIT_SOURCE_ROOT}/memory/chart-editor-status/implemented.md")
file(READ "${implemented_index}" implemented_index_text)
set(implemented_slices
    memory/chart-editor-status/implemented/foundation.md
    memory/chart-editor-status/implemented/visual-preview.md
    memory/chart-editor-status/implemented/formats-persistence.md
    memory/chart-editor-status/implemented/audio-timing.md
    memory/chart-editor-status/implemented/editing-frontend.md
)
foreach(slice IN LISTS implemented_slices)
    string(REGEX REPLACE "^memory/chart-editor-status/" "" index_target "${slice}")
    string(FIND "${implemented_index_text}" "](${index_target})" link_offset)
    if(link_offset EQUAL -1)
        message(FATAL_ERROR "Implemented-capability index does not link to ${slice}")
    endif()
endforeach()

set(chart_editor_docs
    docs/chart-editor-plan.md
    memory/chart-editor-status.md
    ${plan_slices}
    ${status_slices}
)
foreach(relative_file IN LISTS chart_editor_docs)
    set(document "${WORMEDIT_SOURCE_ROOT}/${relative_file}")
    if(NOT EXISTS "${document}")
        message(FATAL_ERROR "Missing chart-editor documentation slice: ${relative_file}")
    endif()
    file(READ "${document}" contents)
    if(NOT contents MATCHES "^# [^\n]+")
        message(FATAL_ERROR "${relative_file} must begin with one H1 heading")
    endif()
    string(REGEX MATCHALL "\n# [^\n]+" additional_h1_headings "\n${contents}")
    list(LENGTH additional_h1_headings h1_count)
    if(NOT h1_count EQUAL 1)
        message(FATAL_ERROR "${relative_file} must contain exactly one H1 heading")
    endif()

    cmake_path(GET document PARENT_PATH document_directory)
    set(remaining_contents "${contents}")
    while(remaining_contents MATCHES "\\]\\(([^#\n\r\)]*\\.md)(#[^\n\r\)]*)?\\)")
        set(markdown_link "${CMAKE_MATCH_0}")
        set(target "${CMAKE_MATCH_1}")
        if(target MATCHES "^[a-zA-Z][a-zA-Z0-9+.-]*:")
            string(FIND "${remaining_contents}" "${markdown_link}" link_offset)
            string(LENGTH "${markdown_link}" link_length)
            math(EXPR next_offset "${link_offset} + ${link_length}")
            string(SUBSTRING "${remaining_contents}" "${next_offset}" -1 remaining_contents)
            continue()
        endif()
        cmake_path(ABSOLUTE_PATH target BASE_DIRECTORY "${document_directory}" NORMALIZE
                   OUTPUT_VARIABLE resolved_target)
        if(NOT EXISTS "${resolved_target}")
            message(FATAL_ERROR
                "Broken local Markdown link in ${relative_file}: ${target} -> ${resolved_target}")
        endif()
        string(FIND "${remaining_contents}" "${markdown_link}" link_offset)
        string(LENGTH "${markdown_link}" link_length)
        math(EXPR next_offset "${link_offset} + ${link_length}")
        string(SUBSTRING "${remaining_contents}" "${next_offset}" -1 remaining_contents)
    endwhile()
endforeach()

set(task_card_index "${WORMEDIT_SOURCE_ROOT}/docs/chart-editor-plan/tasks.md")
file(READ "${task_card_index}" task_card_index_text)
set(task_card_files
    docs/chart-editor-plan/tasks/feedback-cjk.md
    docs/chart-editor-plan/tasks/feedback-skins.md
    docs/chart-editor-plan/tasks/feedback-navigation.md
    docs/chart-editor-plan/tasks/feedback-library.md
    docs/chart-editor-plan/tasks/menu-input-capture.md
    docs/chart-editor-plan/tasks/scratch-presentation.md
    docs/chart-editor-plan/tasks/options-dialog.md
    docs/chart-editor-plan/tasks/recent-projects.md
    docs/chart-editor-plan/tasks/dropdown-surfaces.md
    docs/chart-editor-plan/tasks/chart-table-reorder.md
    docs/chart-editor-plan/tasks/bundled-skin-lanes.md
    docs/chart-editor-plan/tasks/options-dialog-drag.md
    docs/chart-editor-plan/tasks/semantic-skins.md
    docs/chart-editor-plan/tasks/layout-skin-preferences.md
    docs/chart-editor-plan/tasks/semantic-layout-identity.md
    docs/chart-editor-plan/tasks/measure-local-snapping.md
    docs/chart-editor-plan/tasks/compose-split.md
    docs/chart-editor-plan/tasks/compose-features.md
    docs/chart-editor-plan/tasks/interop-evidence.md
    docs/chart-editor-plan/tasks/frontend-shell.md
    docs/chart-editor-plan/tasks/editing-commands.md
    docs/chart-editor-plan/tasks/measures-timing.md
    docs/chart-editor-plan/tasks/bms-branches.md
    docs/chart-editor-plan/tasks/resources-audio.md
    docs/chart-editor-plan/tasks/export.md
    docs/chart-editor-plan/tasks/persistence-metadata.md
    docs/chart-editor-plan/tasks/editing-depth.md
    docs/chart-editor-plan/tasks/scripting-storyboard.md
    docs/chart-editor-plan/tasks/late-compat.md
    docs/chart-editor-plan/tasks/osbx.md
    docs/chart-editor-plan/tasks/compose-ux.md
    docs/chart-editor-plan/tasks/notifications.md
    docs/chart-editor-plan/tasks/panels-menus.md
    docs/chart-editor-plan/tasks/rendering.md
    docs/chart-editor-plan/tasks/options-charts.md
    docs/chart-editor-plan/tasks/live-transport.md
    docs/chart-editor-plan/tasks/markers.md
    docs/chart-editor-plan/tasks/semantic-layers.md
    docs/chart-editor-plan/tasks/compose-followups.md
    docs/chart-editor-plan/tasks/compose-cleanup.md
    docs/chart-editor-plan/tasks/compose-docks.md
    docs/chart-editor-plan/tasks/dialogs.md
    docs/chart-editor-plan/tasks/draw-tool.md
    docs/chart-editor-plan/tasks/project-charts.md
    docs/chart-editor-plan/tasks/editing-feedback.md
    docs/chart-editor-plan/tasks/bms-editing.md
    docs/chart-editor-plan/tasks/color-schemes.md
    docs/chart-editor-plan/tasks/manual-tutorial.md
    docs/chart-editor-plan/tasks/stems-performance.md
    docs/chart-editor-plan/tasks/bmson-bga-editing.md
    docs/chart-editor-plan/tasks/bmson-hybrid-lanes.md
    docs/chart-editor-plan/tasks/bmson-conversion.md
    docs/chart-editor-plan/tasks/bookmark-rename-harness.md
    docs/chart-editor-plan/tasks/state-ownership.md
)
set(task_card_fields Status Outcome "Read first" "Files owned" Steps Invariants
    "Reject/failure behavior" Verify "Out of scope")
foreach(card_file IN LISTS task_card_files)
    string(REGEX REPLACE "^docs/chart-editor-plan/" "" index_target "${card_file}")
    string(FIND "${task_card_index_text}" "](${index_target})" link_offset)
    if(link_offset EQUAL -1)
        message(FATAL_ERROR "Task-card index does not link to ${card_file}")
    endif()
    file(READ "${WORMEDIT_SOURCE_ROOT}/${card_file}" card_text)
    string(REPLACE ";" "@SEMI@" card_escaped "${card_text}")
    string(REGEX REPLACE "\n## T-" "\n@CARD@## T-" card_marked "${card_escaped}")
    string(REPLACE "@CARD@" ";" card_marked "${card_marked}")
    string(REPLACE "[" "(" card_marked "${card_marked}")
    string(REPLACE "]" ")" card_marked "${card_marked}")
    list(POP_FRONT card_marked)
    foreach(card IN LISTS card_marked)
        string(REGEX MATCH "^## (T-[0-9]+)" card_id "${card}")
        foreach(field IN LISTS task_card_fields)
            string(FIND "${card}" "- **${field}:**" field_offset)
            if(field_offset EQUAL -1)
                message(FATAL_ERROR "${card_file} ${CMAKE_MATCH_1} lacks field: ${field}")
            endif()
        endforeach()
    endforeach()
endforeach()
